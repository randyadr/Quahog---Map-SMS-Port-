"""Restore the v0.4.4 binary assets and DLL from the repository's install ZIP.

Run from any directory with Python 3.10+: python prepare_checkout.py
Existing source files are never extracted or replaced.
"""
from pathlib import Path
import hashlib
import zipfile

ROOT = Path(__file__).resolve().parent
PACKAGE = ROOT / "Quahog_Mod_v0.4.4.zip"
EXPECTED_SHA256 = "0d65d79413e04e30127a8cad2d9b9d084946cabe4c4971a0c1a3e1d8548b661f"
BINARY_FILES = (
    "quahog-native/assets/collision_stub.bmd",
    "quahog-native/assets/quahog.bmd",
    *(f"quahog-native/assets/quahog_{index:02d}.col" for index in range(10)),
    "quahog-native/bin/windows-x86_64/quahog_native.dll",
)


def main():
    if not PACKAGE.is_file():
        raise SystemExit(f"Missing {PACKAGE.name}. Download it from this repository into {ROOT}.")
    digest = hashlib.sha256()
    with PACKAGE.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    if digest.hexdigest() != EXPECTED_SHA256:
        raise SystemExit("Package checksum mismatch; no files were extracted.")
    with zipfile.ZipFile(PACKAGE) as archive:
        for name in BINARY_FILES:
            target = ROOT / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(archive.read(name))
    print(f"Restored {len(BINARY_FILES)} binary files. Source files were preserved.")


if __name__ == "__main__":
    main()
