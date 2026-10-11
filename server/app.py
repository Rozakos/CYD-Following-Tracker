"""social-api: the companion API for the CYD Follower Tracker.

The ESP32 only ever talks to this normalized JSON API; per-platform auth,
caching and history live here. Contract (all under API_PREFIX, bearer auth):

    GET /platforms
    GET /social/{platform}/{handle}?range=1w|1mo|3mo|1y|max
    GET /history/{platform}/{handle}?range=1w|1mo|3mo|1y|max[&limit=N]
    GET /icon/{platform}?size=48
    GET /health                      (no auth)

Run:  uvicorn app:app --host 127.0.0.1 --port 8002
"""
from __future__ import annotations

import asyncio
import hmac
import io
import logging
import os
import re
import time
from contextlib import asynccontextmanager
from pathlib import Path

import httpx
from dotenv import load_dotenv
from fastapi import APIRouter, Depends, FastAPI, Header, HTTPException, Query, Response

HERE = Path(__file__).resolve().parent
load_dotenv(HERE / ".env")

import platforms as plat  # noqa: E402  (reads env at load())
from store import Store   # noqa: E402

log = logging.getLogger("social-api")
logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")

API_PREFIX = os.environ.get("API_PREFIX", "/social/api/v1")
API_SECRET = os.environ.get("API_SECRET", "")
CACHE_TTL = int(os.environ.get("CACHE_TTL_SECONDS", "600"))
POLL_SECONDS = int(os.environ.get("POLL_SECONDS", "3600"))
SAMPLE_MIN_GAP = int(os.environ.get("SAMPLE_MIN_GAP_SECONDS", "900"))
ACTIVE_DAYS = int(os.environ.get("ACTIVE_DAYS", "14"))
RETENTION_DAYS = int(os.environ.get("RETENTION_DAYS", "1825"))
SPARK_POINTS = int(os.environ.get("SPARKLINE_POINTS", "10"))
DB_PATH = os.environ.get("DB_PATH", str(HERE / "data" / "social.db"))
ICON_DIR = Path(os.environ.get("ICON_DIR", str(HERE / "icons")))

# range token -> (window seconds, bucket seconds, interval label)
RANGES = {
    "1w":  (7 * 86400,   3600 * 3, "hourly"),
    "1mo": (30 * 86400,  86400,    "daily"),
    "3mo": (91 * 86400,  86400,    "daily"),
    "1y":  (365 * 86400, 86400 * 7, "weekly"),
    "max": (None,        None,     "daily"),
}
HANDLE_RE = re.compile(r"^@?[A-Za-z0-9._-]{1,64}$")

state: dict = {}


# ── auth ──────────────────────────────────────────────────────────────────────
def require_token(authorization: str = Header(default="")):
    token = authorization[7:].strip() if authorization.lower().startswith("bearer ") else ""
    if not API_SECRET or not token or not hmac.compare_digest(token, API_SECRET):
        raise HTTPException(401, "missing or invalid bearer token")


# ── fetching with cache + stale fallback ─────────────────────────────────────
async def current(platform_id: str, handle: str) -> dict:
    """Live profile, cached CACHE_TTL. On upstream failure serve the last good value."""
    platforms = state["platforms"]
    if platform_id not in platforms:
        raise HTTPException(404, f"unknown platform '{platform_id}'")
    if not HANDLE_RE.match(handle):
        raise HTTPException(400, "bad handle")
    key = (platform_id, handle.lower())
    cache = state["cache"]
    hit = cache.get(key)
    if hit and time.time() - hit["at"] < CACHE_TTL:
        return {**hit, "cached": True, "stale": False}
    lock = state["locks"].setdefault(key, asyncio.Lock())
    async with lock:                      # one upstream call per account at a time
        hit = cache.get(key)
        if hit and time.time() - hit["at"] < CACHE_TTL:
            return {**hit, "cached": True, "stale": False}
        try:
            prof = await platforms[platform_id].fetch(state["http"], handle)
        except plat.NotFound as e:
            raise HTTPException(404, str(e))
        except plat.Upstream as e:
            log.warning("%s/%s upstream failed: %s", platform_id, handle, e)
            if hit:
                return {**hit, "cached": True, "stale": True}
            raise HTTPException(502, "upstream unavailable")
        entry = {"display_name": prof.display_name, "followers": prof.followers, "at": time.time()}
        cache[key] = entry
        store: Store = state["store"]
        if platform_id == "demo" and store.first_ts(*key) is None:
            backfill_demo(handle, key)
        store.add_sample(*key, prof.followers, min_gap=SAMPLE_MIN_GAP)
        return {**entry, "cached": False, "stale": False}


def backfill_demo(handle, key):
    now = time.time()
    rows = [(now - d * 86400, plat.Demo.value_at(handle, now - d * 86400)) for d in range(400, 0, -1)]
    rows += [(now - h * 3600, plat.Demo.value_at(handle, now - h * 3600)) for h in range(24 * 7, 0, -1)]
    state["store"].add_samples(*key, rows)


# ── history helpers ───────────────────────────────────────────────────────────
def series(key, rng, live_followers, limit=None):
    """[(ts, followers)] over the range, one point per bucket (last value wins),
    ending with the live value. Gaps are simply skipped (no invented data)."""
    window, bucket, _ = RANGES[rng]
    store: Store = state["store"]
    now = int(time.time())
    first = store.first_ts(*key) or now
    since = first if window is None else max(first, now - window)
    if bucket is None:                    # max: aim for ~120 daily-or-coarser buckets
        bucket = max(86400, (now - since) // 120 or 86400)
    buckets: dict[int, tuple[int, int]] = {}
    for ts, f in store.samples(*key, since):
        buckets[ts // bucket] = (ts, f)
    pts = [buckets[b] for b in sorted(buckets)]
    if pts and pts[-1][0] // bucket == now // bucket:
        pts[-1] = (now, live_followers)
    else:
        pts.append((now, live_followers))
    if limit and len(pts) > limit >= 2:   # keep first + last, like the firmware does
        n = len(pts)
        pts = [pts[i * (n - 1) // (limit - 1)] for i in range(limit)]
    return pts


def delta_24h(key, live):
    then = state["store"].value_at(*key, time.time() - 86400)
    if then is None:                      # tracked < 24 h: change since first sample
        s = state["store"].samples(*key)
        then = s[0][1] if s else live
    return live - then


# ── app ───────────────────────────────────────────────────────────────────────
async def poller():
    """Refresh every account a device asked about recently, so history keeps
    growing even while the device is off."""
    while True:
        await asyncio.sleep(POLL_SECONDS)
        try:
            for p, h in state["store"].active_accounts(ACTIVE_DAYS * 86400):
                state["cache"].pop((p, h), None)
                try:
                    await current(p, h)
                except HTTPException as e:
                    log.info("poll %s/%s: %s", p, h, e.detail)
                await asyncio.sleep(1)
            state["store"].prune(RETENTION_DAYS)
        except Exception:
            log.exception("poller iteration failed")


@asynccontextmanager
async def lifespan(app: FastAPI):
    Path(DB_PATH).parent.mkdir(parents=True, exist_ok=True)
    state.update(platforms=plat.load(), store=Store(DB_PATH), cache={}, locks={},
                 http=state.get("http") or httpx.AsyncClient(headers={"User-Agent": "social-api/1.0"}))
    if not API_SECRET:
        log.error("API_SECRET is not set: every request will be rejected")
    log.info("platforms enabled: %s", ", ".join(state["platforms"]) or "none (set YOUTUBE_API_KEY / TWITCH_*)")
    task = asyncio.create_task(poller()) if POLL_SECONDS > 0 else None
    yield
    if task:
        task.cancel()
    await state["http"].aclose()


app = FastAPI(title="social-api", lifespan=lifespan, docs_url=f"{API_PREFIX}/docs",
              openapi_url=f"{API_PREFIX}/openapi.json")
api = APIRouter(prefix=API_PREFIX)


@api.get("/health")
def health():
    return {"ok": True, "platforms": list(state.get("platforms", {}))}


@api.get("/platforms", dependencies=[Depends(require_token)])
def get_platforms():
    return [p.meta() for p in state["platforms"].values()]


@api.get("/social/{platform}/{handle}", dependencies=[Depends(require_token)])
async def get_social(platform: str, handle: str, range: str = Query("1w", pattern="^(1w|1mo|3mo|1y|max)$")):
    cur = await current(platform, handle)
    key = (platform, handle.lower())
    state["store"].touch(*key, cur["display_name"])
    pts = series(key, range, cur["followers"], limit=SPARK_POINTS)
    return {"platform": platform, "handle": handle, "display_name": cur["display_name"],
            "followers": cur["followers"], "delta_24h": delta_24h(key, cur["followers"]),
            "range": range, "history": [f for _, f in pts],
            "tracked_since": state["store"].first_ts(*key),
            "cached": cur["cached"], "stale": cur["stale"]}


@api.get("/history/{platform}/{handle}", dependencies=[Depends(require_token)])
async def get_history(platform: str, handle: str,
                      range: str = Query("1mo", pattern="^(1w|1mo|3mo|1y|max)$"),
                      limit: int | None = Query(None, ge=2, le=1000)):
    cur = await current(platform, handle)
    key = (platform, handle.lower())
    state["store"].touch(*key, cur["display_name"])
    pts = series(key, range, cur["followers"], limit=limit)
    return {"platform": platform, "handle": handle, "range": range, "interval": RANGES[range][2],
            "tracked_since": state["store"].first_ts(*key),
            "points": [{"ts": ts, "followers": f} for ts, f in pts]}


@api.get("/icon/{platform}", dependencies=[Depends(require_token)])
def get_icon(platform: str, size: int = Query(48, ge=16, le=256)):
    p = state["platforms"].get(platform)
    if not p:
        raise HTTPException(404, "unknown platform")
    return Response(render_icon(p, size), media_type="image/png",
                    headers={"Cache-Control": "public, max-age=86400"})


def render_icon(p: plat.Platform, size: int) -> bytes:
    """icons/<id>.png resized if present, otherwise a drawn badge in the brand colour."""
    from PIL import Image, ImageDraw, ImageFont
    src = ICON_DIR / f"{p.id}.png"
    if src.exists():
        img = Image.open(src).convert("RGBA").resize((size, size), Image.LANCZOS)
    else:
        s = size * 4                      # draw large, downsample for smooth edges
        img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
        d = ImageDraw.Draw(img)
        d.rounded_rectangle([0, 0, s - 1, s - 1], radius=s // 5, fill=p.color)
        if p.id == "youtube":
            d.polygon([(s * .38, s * .28), (s * .38, s * .72), (s * .74, s * .5)], fill="white")
        elif p.id == "twitch":
            d.polygon([(s*.25, s*.18), (s*.80, s*.18), (s*.80, s*.55), (s*.62, s*.72), (s*.48, s*.72),
                       (s*.36, s*.84), (s*.36, s*.72), (s*.25, s*.72)], fill="white")
            d.rectangle([s*.47, s*.32, s*.53, s*.50], fill=p.color)
            d.rectangle([s*.62, s*.32, s*.68, s*.50], fill=p.color)
        else:
            try:
                font = ImageFont.truetype("DejaVuSans-Bold.ttf", int(s * .42))
            except OSError:
                font = ImageFont.load_default()
            d.text((s / 2, s / 2), p.name[:2].upper(), fill="white", font=font, anchor="mm")
        img = img.resize((size, size), Image.LANCZOS)
    buf = io.BytesIO()
    img.save(buf, "PNG", optimize=True)
    return buf.getvalue()


app.include_router(api)
