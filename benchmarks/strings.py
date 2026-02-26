#!/usr/bin/env python3
# String operations benchmark
s = ""
i = 0
while i < 1000:
    s = s + "x"
    i = i + 1
print(f"Length: {len(s)}")
