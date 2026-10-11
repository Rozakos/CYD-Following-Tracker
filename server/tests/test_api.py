"""social-api tests: python -m pytest server/tests  (no network: upstreams are mocked)."""
import importlib, json, os, sys, time
from pathlib import Path

import httpx
import pytest
from fastapi.testclient import TestClient

SERVER = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SERVER))
AUTH = {"Authorization": "Bearer s3cret"}
YT_CHANNEL = {"items": [{"snippet": {"title": "Marques Brownlee"},
                         "statistics": {"subscriberCount": "19200000", "hiddenSubscriberCount": False}}]}


def upstream(request: httpx.Request) -> httpx.Response:
    u = str(request.url)
    if "googleapis.com/youtube/v3/channels" in u:
        q = request.url.params
        if q.get("forHandle") == "@mkbhd" or q.get("id") == "UCBJycsmduvYEL83R_U4JriQ":
            return httpx.Response(200, json=YT_CHANNEL)
        if q.get("forHandle") == "@quota":
            return httpx.Response(403, json={"error": {"message": "quotaExceeded"}})
        return httpx.Response(200, json={"items": []})
    if "id.twitch.tv/oauth2/token" in u:
        return httpx.Response(200, json={"access_token": "tok", "expires_in": 5000})
    if "helix/users" in u:
        if request.url.params.get("login") == "pokimane":
            return httpx.Response(200, json={"data": [{"id": "44445592", "display_name": "pokimane"}]})
        return httpx.Response(200, json={"data": []})
    if "helix/channels/followers" in u:
        assert request.headers["Authorization"] == "Bearer tok" and request.headers["Client-Id"] == "cid"
        return httpx.Response(200, json={"total": 9300000, "data": []})
    return httpx.Response(404)


@pytest.fixture()
def client(tmp_path, monkeypatch):
    env = {"API_SECRET": "s3cret", "YOUTUBE_API_KEY": "k", "TWITCH_CLIENT_ID": "cid",
           "TWITCH_CLIENT_SECRET": "cs", "SOCIAL_DEMO": "1", "DB_PATH": str(tmp_path / "t.db"),
           "POLL_SECONDS": "0", "CACHE_TTL_SECONDS": "600"}
    for k, v in env.items():
        monkeypatch.setenv(k, v)
    for mod in ("app", "platforms", "store"):
        sys.modules.pop(mod, None)
    app = importlib.import_module("app")
    app.state["http"] = httpx.AsyncClient(transport=httpx.MockTransport(upstream))
    with TestClient(app.app) as c:
        c.mod = app
        yield c


P = "/social/api/v1"

def test_auth_required(client):
    assert client.get(f"{P}/platforms").status_code == 401
    assert client.get(f"{P}/platforms", headers={"Authorization": "Bearer nope"}).status_code == 401
    assert client.get(f"{P}/health").status_code == 200

def test_platforms_contract(client):
    r = client.get(f"{P}/platforms", headers=AUTH).json()
    assert [p["id"] for p in r] == ["youtube", "twitch", "demo"]
    assert set(r[0]) == {"id", "name", "color", "icon_url", "handle_hint"}
    assert r[0]["color"] == "#FF0000" and r[0]["icon_url"] == "/icon/youtube"

def test_youtube_handle_and_channel_id(client):
    j = client.get(f"{P}/social/youtube/@mkbhd", headers=AUTH).json()
    assert j["display_name"] == "Marques Brownlee" and j["followers"] == 19200000
    assert j["history"] == [19200000] and j["delta_24h"] == 0 and j["cached"] is False
    assert client.get(f"{P}/social/youtube/UCBJycsmduvYEL83R_U4JriQ", headers=AUTH).json()["followers"] == 19200000
    assert client.get(f"{P}/social/youtube/@mkbhd", headers=AUTH).json()["cached"] is True

def test_unknown_handle_and_platform(client):
    assert client.get(f"{P}/social/youtube/@nobody", headers=AUTH).status_code == 404
    assert client.get(f"{P}/social/twitch/nobody", headers=AUTH).status_code == 404
    assert client.get(f"{P}/social/myspace/tom", headers=AUTH).status_code == 404
    assert client.get(f"{P}/social/youtube/bad%20handle", headers=AUTH).status_code == 400

def test_twitch(client):
    j = client.get(f"{P}/social/twitch/pokimane", headers=AUTH).json()
    assert j["followers"] == 9300000 and j["display_name"] == "pokimane"

def test_stale_on_upstream_failure(client):
    key = ("youtube", "@quota")
    client.mod.state["cache"][key] = {"display_name": "Q", "followers": 5, "at": time.time() - 9999}
    j = client.get(f"{P}/social/youtube/@quota", headers=AUTH).json()
    assert j["stale"] is True and j["followers"] == 5
    client.mod.state["cache"].clear()
    assert client.get(f"{P}/social/youtube/@quota", headers=AUTH).status_code == 502

def test_history_delta_and_range(client):
    st = client.mod.state["store"]
    now = time.time()
    st.add_samples("youtube", "@mkbhd", [(now - 40 * 86400 + d * 86400, 19_000_000 + d * 5000) for d in range(40)])
    j = client.get(f"{P}/social/youtube/@mkbhd?range=1mo", headers=AUTH).json()
    assert len(j["history"]) == 10 and j["history"][-1] == 19_200_000
    assert j["history"] == sorted(j["history"])
    assert j["delta_24h"] == 19_200_000 - (19_000_000 + 39 * 5000)
    w = client.get(f"{P}/social/youtube/@mkbhd?range=1w", headers=AUTH).json()["history"]
    assert w[0] > j["history"][0]                 # shorter window starts later
    h = client.get(f"{P}/history/youtube/@mkbhd?range=3mo", headers=AUTH).json()
    assert h["interval"] == "daily" and h["points"][-1]["followers"] == 19_200_000
    ts = [p["ts"] for p in h["points"]]
    assert ts == sorted(ts) and len(ts) == 41
    lim = client.get(f"{P}/history/youtube/@mkbhd?range=max&limit=5", headers=AUTH).json()["points"]
    assert len(lim) == 5 and lim[0]["ts"] == ts[0] and lim[-1]["followers"] == 19_200_000
    assert client.get(f"{P}/history/youtube/@mkbhd?range=2y", headers=AUTH).status_code == 422

def test_demo_backfills_history(client):
    h = client.get(f"{P}/history/demo/rozakos?range=1y", headers=AUTH).json()
    assert len(h["points"]) >= 50
    j = client.get(f"{P}/social/demo/rozakos", headers=AUTH).json()
    assert len(j["history"]) == 10 and j["display_name"] == "Rozakos"

def test_icon_png(client):
    for pid in ("youtube", "twitch", "demo"):
        r = client.get(f"{P}/icon/{pid}?size=48", headers=AUTH)
        assert r.status_code == 200 and r.content[:4] == b"\x89PNG" and len(r.content) < 65536
    from PIL import Image; import io
    assert Image.open(io.BytesIO(r.content)).size == (48, 48)

def test_requested_accounts_are_tracked_for_polling(client):
    client.get(f"{P}/social/twitch/pokimane", headers=AUTH)
    assert client.mod.state["store"].active_accounts(3600) == [("twitch", "pokimane")]
