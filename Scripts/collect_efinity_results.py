#!/usr/bin/env python3
"""Collect Efinity artifacts without changing the vendor project."""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
from pathlib import Path


ARTIFACT_SUFFIXES = {".bit", ".hex", ".map", ".place", ".route", ".rpt", ".log", ".csv"}
REPORT_MARKERS = ("timing", "resource", "util", "map", "place", "route")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("project_dir", type=Path)
    parser.add_argument("result_dir", type=Path)
    args = parser.parse_args()
    project = args.project_dir.resolve()
    result = args.result_dir.resolve()
    reports = result / "reports"
    reports.mkdir(parents=True, exist_ok=True)
    records: list[dict[str, object]] = []
    for path in project.rglob("*"):
        if not path.is_file() or result in path.parents:
            continue
        lower = path.name.lower()
        is_named_report = path.suffix.lower() in {".rpt", ".out", ".xml"} and any(
            marker in lower for marker in REPORT_MARKERS
        )
        if path.suffix.lower() not in ARTIFACT_SUFFIXES and not is_named_report:
            continue
        relative = path.relative_to(project)
        destination = reports / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, destination)
        records.append({
            "source": relative.as_posix(),
            "bytes": path.stat().st_size,
            "sha256": sha256(path),
        })
    (result / "artifacts.json").write_text(
        json.dumps({"project": str(project), "artifacts": records}, indent=2), encoding="utf-8"
    )
    print(f"ARTIFACT_COUNT={len(records)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
