# YOLO11n model for focusGaze

## File

- `yolo11n.onnx` — Ultralytics YOLO11n, input **1×3×320×320** NCHW, output **1×84×2100** (xywh + 80 COCO class scores).
- COCO class **67** = `cell phone`.

## Re-export

```bash
python3 -m venv .venv
.venv/bin/pip install ultralytics onnx
.venv/bin/python scripts/export_yolo11n.py
```

## Runtime resolution order

1. `FOCUSGAZE_YOLO_MODEL` env path  
2. `$FOCUSGAZE_DATA_DIR/models/yolo11n.onnx` (or Application Support)  
3. `<repo>/models/yolo11n.onnx` (dev)  
4. Next to the `focusgaze` executable: `../models/yolo11n.onnx` / `models/yolo11n.onnx`

## License

Ultralytics YOLO weights/export may be subject to AGPL/commercial terms. Review before distributing binaries with the model bundled.
