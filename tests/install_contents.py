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
license_text = (share / "LICENSE").read_text(encoding="utf-8")
if "Apache License" not in license_text or "Version 2.0, January 2004" not in license_text:
    raise SystemExit("Installed LICENSE is not the Apache License, Version 2.0")
if "MIT License" in license_text or "Permission is hereby granted" in license_text:
    raise SystemExit("Installed LICENSE still carries the MIT license text")
notice_text = (share / "NOTICE").read_text(encoding="utf-8")
if "Copyright (c) 2026 iisacc" not in notice_text:
    raise SystemExit("Installed NOTICE is missing Copyright (c) 2026 iisacc")
print("Installed documentation and examples contain no provider-private metadata")
print("Installed LICENSE is Apache-2.0 and NOTICE carries the iisacc copyright")
