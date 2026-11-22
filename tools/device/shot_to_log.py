#!/usr/bin/env python3
"""Reduz uma captura de tela (PNG) e imprime em base64 no log, entre marcadores, para poder
ser lida sem baixar artefatos."""
import base64
import io
import sys

from PIL import Image

path, name = sys.argv[1], sys.argv[2]
im = Image.open(path).convert("RGB")
w = 480 if im.width < im.height else 800
im = im.resize((w, int(im.height * w / im.width)))
buf = io.BytesIO()
im.save(buf, "JPEG", quality=72)
print(f"=====SHOT {name} BEGIN=====")
s = base64.b64encode(buf.getvalue()).decode()
for i in range(0, len(s), 4000):
    print(s[i:i + 4000])
print(f"=====SHOT {name} END=====")
