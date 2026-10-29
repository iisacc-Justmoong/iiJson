from pathlib import Path
import sys

prefix = Path(sys.argv[1])
share = prefix / "share" / "iiJson"
if not share.is_dir():
    raise SystemExit("Installed iiJson share directory is missing")
metadata = sorted(str(path.relative_to(share)) for path in share.rglob("*")
                  if path.name.endswith(".iisacc-preview")
                  or path.name in {".iisacc-temporary.lock", ".DS_Store"})
if metadata:
    raise SystemExit("Installed private provider metadata: " + ", ".join(metadata))
print("Installed documentation and examples contain no provider-private metadata")
