"""Generate a tiny unlocked classic Garmin IMG with one RGN vector feature.
Only for exercising the converter's control flow, NOT a real-world compatibility test.
"""
from pathlib import Path
import struct
import sys

def u24(n): return (n & 0xffffff).to_bytes(3,'little')
def coord(deg):return round(deg*(1<<24)/360)
def bitstream(values):
    bits = [1,0,1,0]  # x same-sign+, y same-sign+
    for x,y in values:
        for value in (x,y):
            bits += [(value >> i)&1 for i in range(11)]
    return bytes(sum(bits[i+j]<<j for j in range(min(8,len(bits)-i))) for i in range(0,len(bits),8))

def gen(dest):
    blocksize=512
    blob=bytearray(512*20)
    blob[0x10:0x17]=b'DSKIMG\0'
    blob[0x41:0x48]=b'GARMIN\0'
    blob[0x61]=9;blob[0x62]=0
    blob[0x40c:0x410]=(512*8).to_bytes(4,'little')
    for j,(ext,block,length) in enumerate([('TRE',8,0xe0),('RGN',9,0xd0)]):
        at=0x600+j*512
        blob[at]=1;blob[at+1:at+9]=b'TESTMAP '
        blob[at+9:at+12]=ext.encode()
        struct.pack_into('<IHH',blob,at+12,length,0,0)
        struct.pack_into('<H',blob,at+32,block)
        struct.pack_into('<H',blob,at+34,0xffff)
    tre=bytearray(0xe0)
    struct.pack_into('<H',tre,0,0xc0)
    tre[2:12]=b'GARMIN TRE'
    for at,value in [(0x15,42.35),(0x18,25.05),(0x1b,41.85),(0x1e,24.45)]:
        tre[at:at+3]=u24(coord(value))
    struct.pack_into('<IIII',tre,0x21,0xc0,4,0xc4,14)
    # level with bits=20 and 1 subdivision
    tre[0xc0:0xc4]=bytes([0,20,1,0])
    sub=bytearray(14)
    sub[0:3]=u24(0);sub[3]=0x40
    sub[4:7]=u24(coord(24.75))
    sub[7:10]=u24(coord(42.1))
    struct.pack_into('<HH',sub,10,1200,1200)
    tre[0xc4:0xd2]=sub
    blob[8*512:8*512+len(tre)]=tre
    rgn=bytearray(0xd0)
    struct.pack_into('<H',rgn,0,0x7d)
    rgn[2:12]=b'GARMIN RGN'
    path=bitstream([(2000,2000)]*4)
    geom=bytes([0x03,0,0,0])+struct.pack('<hh',0,0)+bytes([len(path),0x99])+path
    struct.pack_into('<II',rgn,0x15,0x7d,len(geom))
    rgn[0x7d:0x7d+len(geom)]=geom
    blob[9*512:9*512+len(rgn)]=rgn
    Path(dest).write_bytes(blob)

if __name__=='__main__':gen(sys.argv[1])
