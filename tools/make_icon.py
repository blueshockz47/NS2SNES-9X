#!/usr/bin/env python3
"""Export the canonical NS2xSNES9x Homebrew Menu JPEG as PNG."""
from pathlib import Path
from PIL import Image
import sys
root = Path(__file__).resolve().parents[1]
out = Path(sys.argv[1]) if len(sys.argv)>1 else root / "icon.png"
with Image.open(root / "icon.jpg") as im:
    im.convert("RGB").resize((256,256), Image.Resampling.LANCZOS).save(out)
print(out)
