#!/usr/bin/env python3
"""Acha um elemento num dump do uiautomator pelo texto ou pela descrição e imprime "x y" (centro)."""
import re
import sys
import xml.etree.ElementTree as ET

path, wanted = sys.argv[1], sys.argv[2]
root = ET.parse(path).getroot()
for n in root.iter("node"):
    if n.get("text") == wanted or n.get("content-desc") == wanted:
        a, b, c, d = map(int, re.findall(r"\d+", n.get("bounds")))
        print((a + c) // 2, (b + d) // 2)
        sys.exit(0)
sys.exit(1)
