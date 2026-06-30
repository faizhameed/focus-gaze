#!/usr/bin/env python3
"""Export Ultralytics YOLO11n to ONNX for focusGaze (imgsz=320).

Usage (from repo root, with venv):
  python3 -m venv .venv && .venv/bin/pip install ultralytics onnx
  .venv/bin/python scripts/export_yolo11n.py
"""
from __future__ import annotations

import shutil
from pathlib import Path

from ultralytics import YOLO

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "models" / "yolo11n.onnx"


def main() -> None:
    OUT.parent.mkdir(parents=True, exist_ok=True)
    model = YOLO("yolo11n.pt")
    exported = Path(model.export(format="onnx", imgsz=320, simplify=True, opset=12))
    shutil.copy(exported, OUT)
    print(f"Wrote {OUT} ({OUT.stat().st_size} bytes)")
    for i, name in model.names.items():
        if "phone" in name.lower() or "cell" in name.lower():
            print(f"COCO class {i} = {name}")


if __name__ == "__main__":
    main()
