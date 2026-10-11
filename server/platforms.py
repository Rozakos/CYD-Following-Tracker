"""Upstream fetchers. Each platform turns a handle into (display_name, followers).

Adding a platform = add a class here and list it in REGISTRY. The device learns
about it from GET /platforms; no firmware change is needed.
"""
from __future__ import annotations

import hashlib
import math
import os
import re
import time
from dataclasses import dataclass

import httpx


class NotFound(Exception):
    """The handle does not exist (or hides its count) on the platform."""


class Upstream(Exception):
    """The platform API failed (network, quota, auth). Callers may serve stale data."""


@dataclass
class Profile:
    display_name: str
    followers: int


class Platform:
    id = ""
    name = ""
    color = "#888888"
    handle_hint = ""

    def enabled(self) -> bool:
        return True

    def meta(self) -> dict:
        return {"id": self.id, "name": self.name, "color": self.color,
                "icon_url": f"/icon/{self.id}", "handle_hint": self.handle_hint}

    async def fetch(self, client: httpx.AsyncClient, handle: str) -> Profile:
        raise NotImplementedError


class YouTube(Platform):
    """YouTube Data API v3 (`channels.list`, 1 quota unit per call; 10k/day free).

    Handle can be `@handle`, `handle`, or a channel id `UC...`. Note YouTube rounds
    public subscriber counts to 3 significant figures, so small daily changes on
    big channels show up as steps.
    """
    id, name, color = "youtube", "YouTube", "#FF0000"
    handle_hint = "@handle or channel ID (UC…)"

    def __init__(self):
        self.key = os.environ.get("YOUTUBE_API_KEY", "").strip()

    def enabled(self) -> bool:
        return bool(self.key)

    async def fetch(self, client, handle):
        params = {"part": "snippet,statistics", "key": self.key}
        if re.fullmatch(r"UC[0-9A-Za-z_-]{22}", handle):
            params["id"] = handle
        else:
            params["forHandle"] = handle if handle.startswith("@") else "@" + handle
        try:
            r = await client.get("https://www.googleapis.com/youtube/v3/channels", params=params, timeout=10)
        except httpx.HTTPError as e:
            raise Upstream(f"youtube: {e}") from e
        if r.status_code != 200:
            raise Upstream(f"youtube HTTP {r.status_code}: {r.text[:200]}")
        items = r.json().get("items") or []
        if not items:
            raise NotFound(f"no YouTube channel {handle}")
        stats, snip = items[0].get("statistics", {}), items[0].get("snippet", {})
        if stats.get("hiddenSubscriberCount"):
            raise NotFound(f"{handle} hides its subscriber count")
        return Profile(snip.get("title") or handle, int(stats.get("subscriberCount", 0)))


class Twitch(Platform):
    """Twitch Helix with an app access token (client-credentials flow).

    `channels/followers` returns only the `total` with an app token, which is all
    we need. The token is cached until shortly before it expires.
    """
    id, name, color = "twitch", "Twitch", "#9146FF"
    handle_hint = "login name, e.g. pokimane"

    def __init__(self):
        self.client_id = os.environ.get("TWITCH_CLIENT_ID", "").strip()
        self.secret = os.environ.get("TWITCH_CLIENT_SECRET", "").strip()
        self._token, self._token_exp = "", 0.0
        self._ids: dict[str, tuple[str, str]] = {}   # login -> (user id, display name)

    def enabled(self) -> bool:
        return bool(self.client_id and self.secret)

    async def _app_token(self, client) -> str:
        if self._token and time.time() < self._token_exp - 60:
            return self._token
        try:
            r = await client.post("https://id.twitch.tv/oauth2/token", timeout=10, data={
                "client_id": self.client_id, "client_secret": self.secret,
                "grant_type": "client_credentials"})
        except httpx.HTTPError as e:
            raise Upstream(f"twitch token: {e}") from e
        if r.status_code != 200:
            raise Upstream(f"twitch token HTTP {r.status_code}: {r.text[:200]}")
        j = r.json()
        self._token, self._token_exp = j["access_token"], time.time() + int(j.get("expires_in", 3600))
        return self._token

    async def _get(self, client, url, params):
        for attempt in (1, 2):
            headers = {"Client-Id": self.client_id, "Authorization": f"Bearer {await self._app_token(client)}"}
            try:
                r = await client.get(url, params=params, headers=headers, timeout=10)
            except httpx.HTTPError as e:
                raise Upstream(f"twitch: {e}") from e
            if r.status_code == 401 and attempt == 1:
                self._token = ""          # revoked/expired early: get a fresh one once
                continue
            if r.status_code != 200:
                raise Upstream(f"twitch HTTP {r.status_code}: {r.text[:200]}")
            return r.json()

    async def fetch(self, client, handle):
        login = handle.lstrip("@").lower()
        if login not in self._ids:
            users = (await self._get(client, "https://api.twitch.tv/helix/users", {"login": login})).get("data") or []
            if not users:
                raise NotFound(f"no Twitch user {login}")
            self._ids[login] = (users[0]["id"], users[0].get("display_name") or login)
        uid, display = self._ids[login]
        j = await self._get(client, "https://api.twitch.tv/helix/channels/followers",
                            {"broadcaster_id": uid, "first": 1})
        return Profile(display, int(j.get("total", 0)))


class Demo(Platform):
    """Synthetic, deterministic accounts for trying the device without API keys.

    Any handle works. Enabled with SOCIAL_DEMO=1. The app backfills a year of fake
    daily history the first time a demo account is seen.
    """
    id, name, color = "demo", "Demo", "#2EC27E"
    handle_hint = "anything, e.g. rozakos"

    def enabled(self) -> bool:
        return os.environ.get("SOCIAL_DEMO", "").strip() in ("1", "true", "yes")

    @staticmethod
    def value_at(handle: str, ts: float) -> int:
        seed = int(hashlib.sha256(handle.encode()).hexdigest()[:8], 16)
        base = 1_000 + seed % 2_000_000
        days = ts / 86400.0
        growth = 1 + 0.0015 * (days - 20000)            # ~0.15 %/day trend
        wobble = 1 + 0.01 * math.sin(days / 9 + seed % 7) + 0.004 * math.sin(ts / 3600 / 5)
        return max(0, int(base * growth * wobble))

    async def fetch(self, client, handle):
        return Profile(handle.lstrip("@").title(), self.value_at(handle, time.time()))


REGISTRY: list[Platform] = []


def load() -> dict[str, Platform]:
    """Enabled platforms, in display order. Called at startup (after .env is read)."""
    REGISTRY[:] = [YouTube(), Twitch(), Demo()]
    return {p.id: p for p in REGISTRY if p.enabled()}
