#!/usr/bin/env python3
# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""Bus notifier backend: serves weather + the next buses at one NJ Transit
stop as the small JSON document the gadget's morning tiles poll
(esp32/main/bus_client.c):

    {"updated": 1763300000, "mock": false,
     "buses": [{"route": "158", "arrival": "8:23", "arrival_epoch": ...}],
     "weather": {"code": 3, "temp_c": 7.1, "high_c": 10.2, "low_c": 4.0,
                 "is_day": true}}

The NJ Transit credentials live only here, never on the device. Bus data
comes from the BUSDV2 API (getBusDV), weather from Open-Meteo (no key).
Standard library only, so it runs in a stock python image.

Configuration (environment):
    NJT_USERNAME, NJT_PASSWORD   BUSDV2 account (required)
    NJT_BASE      API base; default the test environment, which is where
                  a newly approved account works. Production is
                  https://pcsdata.njtransit.com/api/BUSDV2
    NJT_STOP      stop number, as on the stop sign (required)
    NJT_ROUTES    optional comma list to keep, e.g. "158,159"
    WX_LAT, WX_LON  weather location (required)
    PORT          HTTP port (default 8765)
    TZ            local zone for parsing NJT times (default America/New_York)

Endpoints: /bus.json (the gadget's), /raw (last NJT response, for
debugging), /healthz.
"""

import datetime as dt
import json
import logging
import os
import re
import threading
import time
import urllib.parse
import urllib.request
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from zoneinfo import ZoneInfo

NJT_BASE = os.environ.get("NJT_BASE", "https://testpcsdata.njtransit.com/api/BUSDV2").rstrip("/")
NJT_USER = os.environ.get("NJT_USERNAME", "")
NJT_PASS = os.environ.get("NJT_PASSWORD", "")
STOP = os.environ.get("NJT_STOP", "")
ROUTES = {r.strip().upper() for r in os.environ.get("NJT_ROUTES", "").split(",") if r.strip()}
WX_LAT = os.environ.get("WX_LAT", "")
WX_LON = os.environ.get("WX_LON", "")
PORT = int(os.environ.get("PORT", "8765"))
TZ = ZoneInfo(os.environ.get("TZ", "America/New_York"))

BUS_MAX = 2                 # the firmware keeps MUSE_BUS_MAX entries
BUS_REFRESH_S = 30          # the gadget polls every 60 s in the window
WX_REFRESH_S = 600
TOKEN_MAX_AGE_S = 6 * 3600

log = logging.getLogger("bus-notifier")


def _post_form(url, fields, timeout=15):
    """multipart/form-data POST, as the BUSDV2 API expects."""
    boundary = uuid.uuid4().hex
    body = bytearray()
    for k, v in fields.items():
        body += (f"--{boundary}\r\nContent-Disposition: form-data; "
                 f'name="{k}"\r\n\r\n{v}\r\n').encode()
    body += f"--{boundary}--\r\n".encode()
    req = urllib.request.Request(url, data=bytes(body), method="POST", headers={
        "Content-Type": f"multipart/form-data; boundary={boundary}"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.loads(r.read().decode("utf-8"))


class NJT:
    def __init__(self):
        self.token = None
        self.token_at = 0.0

    def _auth(self):
        r = _post_form(f"{NJT_BASE}/authenticateUser",
                       {"username": NJT_USER, "password": NJT_PASS})
        if str(r.get("Authenticated")).lower() != "true" or not r.get("UserToken"):
            raise RuntimeError(f"NJT login failed: {r}")
        self.token, self.token_at = r["UserToken"], time.time()
        log.info("NJT: new token (%s)", NJT_BASE)

    def departures(self):
        if not self.token or time.time() - self.token_at > TOKEN_MAX_AGE_S:
            self._auth()
        for attempt in (0, 1):
            r = _post_form(f"{NJT_BASE}/getBusDV", {
                "token": self.token, "stop": STOP, "direction": "", "route": "",
                "IP": "", "minutesRequested": "120", "maxVariants": "10"})
            msg = ((r.get("message") or {}).get("message") or "").lower()
            if r.get("DVTrip") is None and msg and attempt == 0:
                # "invalid token" / "unknown user": the token expired.
                log.info("NJT: %s, logging in again", msg)
                self._auth()
                continue
            if r.get("DVTrip") is None:
                raise RuntimeError(f"NJT getBusDV: {r}")
            return r
        raise RuntimeError("unreachable")


_MIN_RE = re.compile(r"(\d+)\s*min", re.I)
_CLOCK_FORMATS = ("%m/%d/%Y %I:%M:%S %p", "%m/%d/%Y %H:%M:%S",
                  "%Y-%m-%dT%H:%M:%S", "%m/%d/%Y %I:%M %p")


def _parse_time(s, now):
    """NJT time text -> aware datetime, or None. Handles "5 MIN",
    "in 12 mins", "8:23 AM", and full date-times."""
    s = (s or "").strip()
    if not s:
        return None
    if s.upper() in ("NOW", "DUE", "ARRIVING"):
        return now
    m = _MIN_RE.search(s)
    if m:
        return now + dt.timedelta(minutes=int(m.group(1)))
    for fmt in _CLOCK_FORMATS:
        try:
            return dt.datetime.strptime(s, fmt).replace(tzinfo=TZ)
        except ValueError:
            pass
    for fmt in ("%I:%M %p", "%I:%M%p", "%H:%M"):
        try:
            t = dt.datetime.strptime(s.upper(), fmt).time()
        except ValueError:
            continue
        d = dt.datetime.combine(now.date(), t, TZ)
        if d < now - dt.timedelta(hours=2):    # past midnight
            d += dt.timedelta(days=1)
        return d
    return None


def digest_trips(trips, now):
    out = []
    for t in trips or []:
        route = (t.get("public_route") or "").strip().upper()
        if not route or (ROUTES and route not in ROUTES):
            continue
        status = (t.get("departurestatus") or "").upper()
        if "CANCEL" in status:
            continue
        # Prefer the live prediction; fall back to the schedule.
        when = _parse_time(t.get("departuretime"), now) or _parse_time(t.get("sched_dep_time"), now)
        if not when or when < now - dt.timedelta(minutes=1):
            continue
        out.append({
            "route": route[:7],
            "arrival": f"{when.hour % 12 or 12}:{when.minute:02d}",
            "arrival_epoch": int(when.timestamp()),
        })
    out.sort(key=lambda b: b["arrival_epoch"])
    return out[:BUS_MAX]


def fetch_weather():
    q = urllib.parse.urlencode({
        "latitude": WX_LAT, "longitude": WX_LON,
        "current": "temperature_2m,weather_code,is_day",
        "daily": "temperature_2m_max,temperature_2m_min",
        "timezone": str(TZ), "forecast_days": 1,
    })
    with urllib.request.urlopen(f"https://api.open-meteo.com/v1/forecast?{q}", timeout=15) as r:
        d = json.loads(r.read().decode("utf-8"))
    cur, daily = d["current"], d["daily"]
    return {
        "code": int(cur["weather_code"]),
        "temp_c": round(float(cur["temperature_2m"]), 1),
        "high_c": round(float(daily["temperature_2m_max"][0]), 1),
        "low_c": round(float(daily["temperature_2m_min"][0]), 1),
        "is_day": bool(cur["is_day"]),
    }


class State:
    def __init__(self):
        self.lock = threading.Lock()
        self.buses = []
        self.raw = None
        self.weather = None
        self.bus_ok_at = 0.0

    def doc(self):
        with self.lock:
            d = {"updated": int(time.time()), "mock": False, "buses": list(self.buses)}
            if self.weather:
                d["weather"] = dict(self.weather)
            return d


STATE = State()


def poller():
    njt = NJT()
    wx_at = 0.0
    while True:
        now = dt.datetime.now(TZ)
        try:
            r = njt.departures()
            buses = digest_trips(r.get("DVTrip"), now)
            with STATE.lock:
                STATE.raw, STATE.buses, STATE.bus_ok_at = r, buses, time.time()
            log.info("buses: %s", ", ".join(f"{b['route']} {b['arrival']}" for b in buses) or "none")
        except Exception as e:  # keep serving the last good data
            log.warning("NJT fetch failed: %s", e)
            with STATE.lock:
                if time.time() - STATE.bus_ok_at > 300:
                    STATE.buses = []    # stale predictions are worse than none
        if time.time() - wx_at > WX_REFRESH_S:
            try:
                w = fetch_weather()
                with STATE.lock:
                    STATE.weather = w
                wx_at = time.time()
                log.info("weather: %s", w)
            except Exception as e:
                log.warning("weather fetch failed: %s", e)
        time.sleep(BUS_REFRESH_S)


class Handler(BaseHTTPRequestHandler):
    def _json(self, obj, code=200):
        body = json.dumps(obj, separators=(",", ":")).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        path = self.path.split("?", 1)[0]
        if path == "/bus.json":
            self._json(STATE.doc())
        elif path == "/raw":
            with STATE.lock:
                self._json(STATE.raw)
        elif path == "/healthz":
            self._json({"ok": True})
        else:
            self._json({"error": "not found"}, 404)

    def log_message(self, fmt, *args):
        log.debug("%s " + fmt, self.client_address[0], *args)


def main():
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    missing = [k for k, v in (("NJT_USERNAME", NJT_USER), ("NJT_PASSWORD", NJT_PASS),
                              ("NJT_STOP", STOP), ("WX_LAT", WX_LAT), ("WX_LON", WX_LON)) if not v]
    if missing:
        raise SystemExit(f"set {', '.join(missing)} (see njt.env.example)")
    log.info("stop %s via %s, port %d", STOP, NJT_BASE, PORT)
    threading.Thread(target=poller, daemon=True).start()
    ThreadingHTTPServer(("0.0.0.0", PORT), Handler).serve_forever()


if __name__ == "__main__":
    main()
