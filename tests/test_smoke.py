"""Synthetic native IMG regression tests; does not assert real Garmin compatibility."""
import sqlite3
import struct
import subprocess
import sys
import tempfile
from pathlib import Path
from make_fixture import gen,coord,u24

binary=Path(sys.argv[1]); work=Path(tempfile.mkdtemp(prefix='garmin2osmand-'))
try:
    classic=work/'classic.img';gen(classic)
    gmp=work/'gmp.img'
    original=classic.read_bytes()
    b=bytearray(len(original))
    b[0:0x600]=original[:0x600]
    # Garmin FAT record for one GMP file, backed by four 512-byte blocks.
    b[0x600]=1;b[0x601:0x609]=b'TESTMAP '
    b[0x609:0x60c]=b'GMP'
    struct.pack_into('<IH',b,0x60c,2048,0)
    for k in range(4):struct.pack_into('<H',b,0x620+2*k,8+k)
    struct.pack_into('<H',b,0x628,0xffff)
    g=bytearray(2048)
    struct.pack_into('<H',g,0,0x35);g[2:12]=b'GARMIN GMP'
    struct.pack_into('<II',g,0x19,0x100,0x400)
    tre=bytearray(original[8*512:9*512]);rgn=bytearray(original[9*512:10*512])
    struct.pack_into('<II',tre,0x21,0x1c0,4)
    struct.pack_into('<II',tre,0x29,0x1c4,14)
    struct.pack_into('<II',rgn,0x15,0x47d,struct.unpack_from('<I',rgn,0x19)[0])
    g[0x100:0x100+0xd2]=tre[:0xd2]
    g[0x400:0x400+0x8b]=rgn[:0x8b]
    # RGN1 data has to live at its new absolute GMP offset.
    g[0x47d:0x47d+20]=rgn[0x7d:0x7d+20]
    b[8*512:12*512]=g
    gmp.write_bytes(b)
    for kind,img in [('classic',classic),('gmp',gmp)]:
        info=subprocess.run([binary,'--input',img,'--info'],capture_output=True,text=True)
        assert info.returncode==0,(kind,info.stderr)
        out=work/f'{kind}.sqlitedb'
        cmd=[binary,'--input',img,'--output',out,'--bbox','24.5,41.85,25.05,42.35','--min-zoom','12','--max-zoom','12']
        p=subprocess.run(cmd,capture_output=True,text=True)
        assert p.returncode==0,(kind,p.stdout,p.stderr)
        db=sqlite3.connect(out)
        count=db.execute('select count(*) from tiles').fetchone()[0]
        assert count>0,kind
        assert db.execute('pragma integrity_check').fetchone()[0]=='ok'
        assert db.execute('select hex(substr(image,1,8)) from tiles limit 1').fetchone()[0]=='89504E470D0A1A0A'
        assert db.execute('select tilenumbering from info').fetchone()[0]=='simple'
        db.close()
        resumed=subprocess.run(cmd+['--resume'],capture_output=True,text=True)
        assert resumed.returncode==0,(kind,resumed.stderr)
        capped=subprocess.run(cmd[:-0]+['--max-tiles','1'] if False else
            [binary,'--input',img,'--output',work/f'{kind}-cap.sqlitedb','--max-tiles','1'],capture_output=True,text=True)
        assert capped.returncode!=0 and not (work/f'{kind}-cap.sqlitedb').exists()
        # Plan mode must not create a DB, and --whole-map bypasses 100k budget automatically.
        plan=subprocess.run([binary,'--input',img,'--whole-map','--min-zoom','12','--max-zoom','12','--plan'],capture_output=True,text=True)
        assert plan.returncode==0,(kind,plan.stdout,plan.stderr)
        assert 'Tile candidates:' in plan.stdout
        whole=work/f'{kind}-whole.sqlitedb'
        full_cmd=[binary,'--input',img,'--output',whole,'--whole-map','--min-zoom','12','--max-zoom','12']
        p=subprocess.run(full_cmd,capture_output=True,text=True)
        assert p.returncode==0,(kind,p.stdout,p.stderr)
        with sqlite3.connect(whole) as db:
            assert db.execute('pragma integrity_check').fetchone()[0]=='ok'
            assert db.execute('select count(*) from tiles').fetchone()[0]>0
            assert db.execute('select count(*) from progress').fetchone()[0]>0
            assert db.execute('select count(*) from converter_settings').fetchone()[0]==1
            original=db.execute('select count(*) from tiles').fetchone()[0]
        resumed=subprocess.run(full_cmd+['--resume'],capture_output=True,text=True)
        assert resumed.returncode==0 and 'candidate tiles visited=0' in resumed.stdout,(kind,resumed.stdout,resumed.stderr)
        with sqlite3.connect(whole) as db:
            assert db.execute('select count(*) from tiles').fetchone()[0]==original
        changed=subprocess.run(full_cmd+['--resume','--aa','1'],capture_output=True,text=True)
        assert changed.returncode!=0 and 'Resume settings do not match' in changed.stderr
        # Force a small byte budget to exercise persistent scanline checkpoints.
        limited=work/f'{kind}-limited.sqlitedb'
        lim_cmd=[binary,'--input',img,'--output',limited,'--whole-map','--min-zoom','12','--max-zoom','12']
        failrun=subprocess.run(lim_cmd+['--max-bytes','1'],capture_output=True,text=True)
        assert failrun.returncode!=0 and limited.is_file()
        recovery=subprocess.run(lim_cmd+['--max-bytes','100000000','--resume'],capture_output=True,text=True)
        assert recovery.returncode==0,(kind,recovery.stdout,recovery.stderr)
        with sqlite3.connect(limited) as db:
            assert db.execute('pragma integrity_check').fetchone()[0]=='ok'
            assert db.execute('select count(*) from tiles').fetchone()[0]==original
        print(kind, 'PASSED tiles',count,'plus whole-map, checkpoint, restart and settings validation')
finally:
    import shutil
    shutil.rmtree(work)
