"""Gate the real portable source; report adapter separately without a gate."""
import json
import sys

with open(sys.argv[1], encoding="utf-8") as stream:
    report = json.load(stream)
files = {entry["filename"].rsplit("/src/", 1)[-1]: entry["summary"]
         for data in report["data"] for entry in data["files"]}
for name in ("policy.cpp", "wifi_manager.cpp"):
    if name not in files:
        raise SystemExit(f"Missing production coverage for {name}")
    summary = files[name]
    print(f"{name}: lines={summary['lines']['percent']:.2f}% "
          f"branches={summary['branches']['percent']:.2f}%")
core = files["policy.cpp"]
if core["lines"]["percent"] < 95 or core["branches"]["percent"] < 90:
    raise SystemExit("Core coverage gate failed: require 95% lines / 90% branches")
