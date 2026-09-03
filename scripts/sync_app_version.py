#!/usr/bin/env python3
"""Sync app/VERSION (Zephyr's version file) with a Commitizen semver string.

Zephyr parses app/VERSION at build time with fixed regexes for
VERSION_MAJOR/VERSION_MINOR/PATCHLEVEL/VERSION_TWEAK/EXTRAVERSION
(see zephyr/cmake/modules/version.cmake), so there is no built-in way for
Commitizen's `version_files` to keep it in sync -- it only does whole-string
substitution and app/VERSION splits the version across separate lines.

Usage: sync_app_version.py <semver, e.g. 1.2.3 or 1.2.3-rc0+4>
"""
import re
import sys
from pathlib import Path

VERSION_FILE = Path(__file__).resolve().parent.parent / "app" / "VERSION"

SEMVER_RE = re.compile(
    r"^(?P<major>\d+)\.(?P<minor>\d+)\.(?P<patch>\d+)"
    r"(?:-(?P<extra>[0-9A-Za-z.-]+))?(?:\+(?P<tweak>\d+))?$"
)


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: sync_app_version.py <semver>", file=sys.stderr)
        return 1

    match = SEMVER_RE.match(sys.argv[1])
    if not match:
        print(f"error: '{sys.argv[1]}' is not a valid semver string", file=sys.stderr)
        return 1

    fields = {
        "VERSION_MAJOR": match["major"],
        "VERSION_MINOR": match["minor"],
        "PATCHLEVEL": match["patch"],
        "VERSION_TWEAK": match["tweak"] or "0",
        "EXTRAVERSION": match["extra"] or "",
    }

    lines = VERSION_FILE.read_text().splitlines()
    updated = [
        f"{key} = {fields[key]}".rstrip()
        if (key := line.split(" ", 1)[0]) in fields
        else line
        for line in lines
    ]
    VERSION_FILE.write_text("\n".join(updated) + "\n")
    print(f"Updated {VERSION_FILE} for version {sys.argv[1]}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
