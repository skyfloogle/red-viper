#!/usr/bin/env python3

import sys
with open(sys.argv[1], "rb") as f:
    dat = f.read()
prof = sorted(enumerate(int.from_bytes(dat[i:i+4], byteorder='little') for i in range(0,len(dat),4)), key=lambda k: k[1], reverse=True)
for i in range(100):
    print(hex(prof[i][0]*2), prof[i][1])