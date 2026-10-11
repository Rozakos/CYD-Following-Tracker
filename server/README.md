# social-api

Companion API for the CYD Follower Tracker. The ESP32 only ever talks to this
one normalized JSON API; YouTube/Twitch keys, caching and history live here.
Same shape and deployment as `stock-api` (FastAPI + uvicorn behind nginx on
rozakos.eu, bearer token, non-empty User-Agent).

```
CYD ──HTTPS──▶ rozakos.eu/social/api/v1 ──nginx──▶ social-api :8002 ──▶ YouTube Data API
                                                        │            └─▶ Twitch Helix
                                                        └──▶ SQLite (follower history)
```

## Endpoints

All under `/social/api/v1`, all need `Authorization: Bearer <API_SECRET>`
(except `/health`).

| Endpoint | Returns |
| --- | --- |
| `GET /platforms` | `[{id, name, color, icon_url, handle_hint}]` for every platform with keys configured. The device builds its platform picker from this. |
| `GET /social/{platform}/{handle}?range=1w` | `{platform, handle, display_name, followers, delta_24h, range, history: [ints oldest→newest, ≤10], tracked_since, cached, stale}`. `history` spans `range`, so the device's list delta (first→last) matches the chosen List window. |
| `GET /history/{platform}/{handle}?range=1mo[&limit=N]` | `{interval, range, tracked_since, points: [{ts, followers}]}` for the detail chart. |
| `GET /icon/{platform}?size=48` | PNG badge (`icons/<platform>.png` if present, otherwise drawn in the brand colour). |
| `GET /health` | `{ok, platforms}` |

Ranges: `1w` (3-hourly points), `1mo`/`3mo` (daily), `1y` (weekly), `max`
(~120 points over everything recorded).

**History starts when tracking starts.** Neither YouTube nor Twitch publishes
past follower counts, so the server records them itself: every live fetch
stores a sample (at most one per 15 min), and a poller refreshes every account
a device asked about in the last 14 days once an hour, so charts keep filling
in while the device is off. Charts for a new account fill up over the first
days and weeks.

Behaviour: counts are cached 10 min; if YouTube/Twitch fail, the last good
value is served with `stale: true` instead of an error. YouTube rounds public
subscriber counts to 3 significant figures, so big channels move in steps.

### Platforms

- **youtube**: `@handle`, `handle` or channel id `UC…`. Needs `YOUTUBE_API_KEY`.
- **twitch**: login name. Needs `TWITCH_CLIENT_ID` + `TWITCH_CLIENT_SECRET`.
- **demo**: any handle, synthetic numbers with a year of fake history
  (`SOCIAL_DEMO=1`). For trying the device before you have keys.

Add a platform by writing a class in `platforms.py` and adding it to `load()`;
it shows up in the device's picker without a firmware change.

## Run locally

```sh
cd server
python3 -m venv .venv && .venv/bin/pip install -r requirements.txt
cp .env.example .env        # set API_SECRET, keys
.venv/bin/uvicorn app:app --port 8002
curl -H "Authorization: Bearer changeme" localhost:8002/social/api/v1/platforms
```

## Deploy on rozakos.eu (next to stock-api)

```sh
rsync -a server/ rozakos@rozakos.eu:social-api/ --exclude .venv --exclude data --exclude .env
ssh rozakos@rozakos.eu
cd social-api && python3 -m venv .venv && .venv/bin/pip install -r requirements.txt
cp .env.example .env && nano .env
sudo cp social-api.service /etc/systemd/system/ && sudo systemctl enable --now social-api
# add nginx.conf.snippet to the rozakos.eu server block, then:
sudo nginx -t && sudo systemctl reload nginx
```

Then on the device's web admin (`http://<device-ip>/settings`) set the API
token to `API_SECRET`. The firmware's `cfg::API_BASE` already points at
`https://rozakos.eu/social/api/v1`.

## Tests

```sh
.venv/bin/pip install -r requirements-dev.txt
.venv/bin/python -m pytest tests
```

Upstreams are mocked with `httpx.MockTransport`; no network or keys needed.
