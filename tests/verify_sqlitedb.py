"""Verify an OsmAnd simple XYZ SQLite raster map with Python stdlib only."""
import sqlite3
import struct
import sys

path = sys.argv[1]
db = sqlite3.connect(f'file:{path}?mode=ro', uri=True)
assert db.execute('PRAGMA integrity_check').fetchone()[0] == 'ok'
info = db.execute('SELECT tilenumbering,minzoom,maxzoom,tilesize FROM info').fetchone()
assert info[0] == 'simple' and info[3] == 256, info
zlo, zhi = int(info[1]), int(info[2])
counts = db.execute('SELECT z, COUNT(*) FROM tiles GROUP BY z ORDER BY z').fetchall()
assert counts and min(z for z,n in counts)>=zlo and max(z for z,n in counts)<=zhi
for x,y,z,img in db.execute('SELECT x,y,z,image FROM tiles'):
    assert 0<=x<1<<z and 0<=y<1<<z and zlo<=z<=zhi, (x,y,z)
    assert img[:8]==b'\x89PNG\r\n\x1a\n'
    assert struct.unpack('>II',img[16:24])==(256,256)
print('OK:',path, 'metadata=',info, 'tiles=',counts)
