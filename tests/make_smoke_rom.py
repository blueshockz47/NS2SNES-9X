from pathlib import Path
import struct
d=Path("build-host/fixtures")
d.mkdir(exist_ok=True)
for name,region in [("ntsc",0),("pal",2),("msu",0)]:
    r=bytearray(32768)
    r[:4]=bytes([0x78,0x18,0x80,0xfe])
    r[0x7fc0:0x7fd5]=b"NS2SNES SMOKE TEST   "
    r[0x7fd5:0x7fdc]=bytes([0x20,0,5,0,region,0x33,0])
    struct.pack_into("<HH",r,0x7fdc,0xffff,0)
    struct.pack_into("<H",r,0x7ffc,0x8000)
    checksum=sum(r)&0xffff
    struct.pack_into("<HH",r,0x7fdc,checksum^0xffff,checksum)
    (d/(name+".sfc")).write_bytes(r)
(d/"msu.msu").write_bytes(b"NS2SNES synthetic MSU data")
