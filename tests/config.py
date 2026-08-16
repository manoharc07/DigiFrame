"""Shared state and helpers for the test modules."""
from __future__ import annotations

import json
import urllib.error
import urllib.parse
import urllib.request

HOST = "digiframe.local"
VERBOSE = False

# The UA the firmware uses. ESPN 403s most others — see test_espn.py.
UA = "ESP32HTTPClient"
ESPN_SITE = "http://site.api.espn.com/apis/site/v2/sports"
ESPN_CORE = "http://sports.core.api.espn.com/v2/sports"


def http(url: str, ua: str | None = UA, timeout: float = 25.0,
         method: str = "GET", want_headers: bool = False):
    """Plain urllib so the suite has no third-party dependency.

    Returns (status, body_bytes) or (status, body_bytes, headers).
    Never raises on an HTTP error status — the status is the assertion.
    """
    req = urllib.request.Request(url, method=method)
    if ua is not None:
        req.add_header("User-Agent", ua)
    else:
        req.add_header("User-Agent", "")
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            body, status, hdrs = r.read(), r.status, dict(r.headers)
    except urllib.error.HTTPError as e:
        body, status, hdrs = e.read(), e.code, dict(e.headers)
    return (status, body, hdrs) if want_headers else (status, body)


def espn_json(url: str):
    status, body = http(url)
    if status != 200:
        raise AssertionError(f"{url} -> HTTP {status}")
    return json.loads(body)


# ---- the device ---------------------------------------------------------
def dev_url(path: str, **params) -> str:
    q = ("?" + urllib.parse.urlencode(params)) if params else ""
    return f"http://{HOST}{path}{q}"


def device_up(timeout: float = 4.0) -> bool:
    try:
        return http(dev_url("/"), timeout=timeout)[0] == 200
    except Exception:                                   # noqa: BLE001
        return False


def dev_get(path: str, **params):
    return http(dev_url(path, **params))


def dev_json(path: str, **params):
    status, body = dev_get(path, **params)
    if status != 200:
        raise AssertionError(f"{path} -> HTTP {status}")
    return json.loads(body)


def dev_post(path: str, **params):
    return http(dev_url(path, **params), method="POST", timeout=15)
