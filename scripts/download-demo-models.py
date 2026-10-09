#!/usr/bin/env python3
"""Download credited free demo models; validate before replacing destination files."""
import argparse
import hashlib
import json
from pathlib import Path
import urllib.request

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("destination", nargs="?", type=Path,
                    default=Path.home() / "Downloads" / "STL-Browser-showcase")
args = parser.parse_args()
args.destination.mkdir(parents=True, exist_ok=True)
manifest = json.loads(Path(__file__).with_name("demo-models.json").read_text())
for name, model in manifest.items():
    request = urllib.request.Request(model["url"], headers={
        "User-Agent": "STL-Browser-demo/0.2 (github.com/JoeMirai/stl-browser)"})
    with urllib.request.urlopen(request, timeout=60) as response:
        data = response.read()
    if hashlib.sha256(data).hexdigest() != model["sha256"]:
        raise SystemExit(f"Checksum mismatch for {name}; the upstream file may have changed.")
    if name == "Utah Teapot.stl":
        # The university STL is Y-up; rotate vectors 90 degrees about X for
        # this Z-up demo collection. Preserve the shape and triangle topology.
        lines = []
        for line in data.decode("ascii").splitlines():
            fields = line.split()
            start = 1 if fields[:1] == ["vertex"] else 2 if fields[:2] == ["facet", "normal"] else None
            if start is not None:
                x, y, z = map(float, fields[start:start+3])
                fields[start:start+3] = [format(x,".9g"),format(-z,".9g"),format(y,".9g")]
                line = " " * (len(line)-len(line.lstrip())) + " ".join(fields)
            lines.append(line)
        data = ("\n".join(lines) + "\n").encode("ascii")
    target = args.destination / name
    target.write_bytes(data)
    print(f"Downloaded {target}")
print("Credits and model licenses: https://github.com/JoeMirai/stl-browser/blob/main/docs/model-credits.md")
