#!/usr/bin/env python3
"""home-assist CLI — control the M5StickS3 hub (temp/humidity + Daikin AC).

Speaks MCP JSON-RPC over HTTP directly; no MCP client registration needed.
Python 3 stdlib only.

Endpoint resolution order:
  1. $HOME_ASSIST_URL
  2. url.txt next to this script   (for packaged/uploaded skill bundles)
  3. ~/.config/home-assist/url     (first line)
  4. http://m5stick.local/mcp      (home LAN fallback)

usage:
  ac.py status              # 温湿度 + 空调/插座状态 (JSON)
  ac.py on                  # 开空调: 制冷 24C + 扫风 (夏季默认)
  ac.py off                 # 关空调 (红外关机)
  ac.py temp 26             # 设定温度 16-30
  ac.py mode cool           # auto|cool|heat|dry|fan
  ac.py fan high            # auto|quiet|low|medium|high
  ac.py swing on|off        # 上下扫风
  ac.py plug on|off         # 智能插座 (独立设备, 不是空调!)
"""
import json
import os
import pathlib
import sys
import urllib.request


def endpoint() -> str:
    url = os.environ.get("HOME_ASSIST_URL", "").strip()
    if url:
        return url
    for cfg in (
        pathlib.Path(__file__).resolve().parent / "url.txt",
        pathlib.Path.home() / ".config" / "home-assist" / "url",
    ):
        if cfg.is_file():
            lines = cfg.read_text().strip().splitlines()
            if lines:
                return lines[0].strip()
    return "http://m5stick.local/mcp"


def rpc(method: str, params=None):
    body = {"jsonrpc": "2.0", "id": 1, "method": method}
    if params is not None:
        body["params"] = params
    req = urllib.request.Request(
        endpoint(),
        data=json.dumps(body).encode(),
        headers={"Content-Type": "application/json"},
    )
    with urllib.request.urlopen(req, timeout=15) as r:
        return json.load(r)


def call(tool: str, args: dict):
    res = rpc("tools/call", {"name": tool, "arguments": args})
    result = res.get("result", {})
    for c in result.get("content", []):
        if c.get("type") == "text":
            print(c["text"])
    if result.get("isError"):
        sys.exit(1)


def die(msg: str):
    print(msg, file=sys.stderr)
    sys.exit(2)


def parse_onoff(word: str, what: str) -> bool:
    w = word.lower()
    if w in ("on", "1", "true", "开"):
        return True
    if w in ("off", "0", "false", "关"):
        return False
    die(f"{what} expects on|off, got: {word}")


def main():
    a = sys.argv[1:]
    if not a or a[0] in ("-h", "--help", "help"):
        print(__doc__)
        return
    cmd = a[0]
    if cmd == "status":
        call("get_status", {})
    elif cmd == "on":
        call("set_ac", {"power": True, "mode": "cool", "temp": 24, "swing": True})
    elif cmd == "off":
        call("set_ac", {"power": False})
    elif cmd == "temp" and len(a) > 1:
        call("set_ac", {"temp": int(a[1])})
    elif cmd == "mode" and len(a) > 1:
        call("set_ac", {"power": True, "mode": a[1]})
    elif cmd == "fan" and len(a) > 1:
        call("set_ac", {"fan": a[1]})
    elif cmd == "swing" and len(a) > 1:
        call("set_ac", {"swing": parse_onoff(a[1], "swing")})
    elif cmd == "plug" and len(a) > 1:
        call("set_plug_power", {"on": parse_onoff(a[1], "plug")})
    else:
        die(f"unknown command: {' '.join(a)}\n\n{__doc__}")


if __name__ == "__main__":
    main()
