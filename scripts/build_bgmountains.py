#!/usr/bin/env python3
"""BGMountains v0.97.5 native OsmAnd pipeline: POI, line and generated-name configuration.

Never needs extracted Garmin icons or custom styles on Android.
"""
from __future__ import annotations
from contextlib import contextmanager
from functools import partial
import argparse,datetime,hashlib,json,os,pathlib,shutil,subprocess,sys,time,urllib.request,zipfile
ROOT=pathlib.Path(__file__).resolve().parents[1]
TRANSLATOR=ROOT/'scripts/native_translate.py'

@contextmanager
def timed(report, stage):
    start = time.monotonic()
    status = 'failed'
    try:
        yield
        status = 'ok'
    finally:
        elapsed = time.monotonic() - start
        record = {'stage': stage, 'seconds': round(elapsed, 3), 'status': status}
        with report.open('a', encoding='utf-8') as stream:
            print(json.dumps(record), file=stream)
        print(f'[{stage}] {elapsed:.2f}s ({status})', flush=True)

def timed_run(report, stage, *args, check_mapcreator=False, **kwargs):
    with timed(report, stage):
        run(*args, **kwargs)
        if check_mapcreator:
            from mapcreator_diagnostics import analyze
            diagnostics = report.parent / 'mapcreator-diagnostics.json'
            result = analyze(kwargs['log'], diagnostics)
            if result['status'] == 'FAIL':
                raise RuntimeError(f'MapCreator logged errors or did not finish. See {diagnostics}')

def run(*args,log=None,cwd=None,env=None):
    cmd=[str(x) for x in args];print('+',' '.join(cmd),flush=True)
    if log:
        with open(log,'w',encoding='utf-8') as f:
            p=subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,cwd=cwd,env=env)
        if p.returncode:
            print(''.join(pathlib.Path(log).read_text(errors='replace').splitlines(True)[-40:]),file=sys.stderr)
            raise RuntimeError('Command failed, see '+str(log))
    else:subprocess.run(cmd,check=True,cwd=cwd,env=env)

def native_tool(engine, stage):
    if engine == 'python':
        return None
    if not shutil.which('cmake'):
        if engine == 'native':
            raise RuntimeError('--engine native requires CMake and an Expat runtime')
        print('CMake unavailable, using Python processing', flush=True)
        return None
    build = ROOT/'build'/'native'
    stage('native-configure','cmake','-S',ROOT,'-B',build,
          '-DCMAKE_BUILD_TYPE=Release','-DGARMIN_POSTPROCESS_ONLY=ON')
    stage('native-build','cmake','--build',build,'-j','2','--target','native_postprocess')
    tool = build/'native_postprocess'
    if tool.is_file():
        return tool
    if engine == 'native':
        raise RuntimeError('Native acceleration unavailable. An Expat runtime is required.')
    print('Expat runtime unavailable, using Python processing', flush=True)
    return None

def creator_dir(x):
    if not x:return None
    p=pathlib.Path(x).expanduser().resolve();p=p.parent if p.name=='utilities.sh' else p
    if not (p/'utilities.sh').exists():raise FileNotFoundError('MapCreator utilities.sh missing: '+str(p))
    return p

def fetch_creator(target):
    extracted=target/'unpacked';c=list(extracted.rglob('utilities.sh')) if extracted.exists() else []
    if c:return c[0].parent
    target.mkdir(parents=True,exist_ok=True)
    url='https://download.osmand.net/latest-night-build/OsmAndMapCreator-main.zip'
    zip_path=target/'OsmAndMapCreator-main.zip'
    if not zip_path.exists():
        print('Fetching official MapCreator:',url)
        with urllib.request.urlopen(url,timeout=120) as src,open(zip_path,'wb') as dst:shutil.copyfileobj(src,dst)
    with zipfile.ZipFile(zip_path) as z:
        for entry in z.infolist():
            parts=pathlib.PurePosixPath(entry.filename)
            if parts.is_absolute() or '..' in parts.parts:raise ValueError('Unsafe creator ZIP entry')
        z.extractall(extracted)
    c=list(extracted.rglob('utilities.sh'))
    if not c:raise FileNotFoundError('Creator ZIP lacks utilities.sh')
    for f in extracted.rglob('*.sh'):f.chmod(f.stat().st_mode|0o111)
    return c[0].parent

def disk_preflight(out, source_bytes, extra_gb=0):
    """Conservative warning/blocking check before disk-heavy MapCreator stage.
    In experience 6 GiB OSM has required >25 GiB scratch. 5x + 1 GiB
    is intentionally approximate, not a guarantee of completion.
    """
    gib=1024**3
    free=shutil.disk_usage(out).free
    required=max(int(2*gib),int(source_bytes*5 + gib),int(extra_gb*gib))
    if free<required:
        raise RuntimeError(f'Insufficient free space for OBF build: free={free/gib:.2f} GiB; '
                           f'estimated minimum={required/gib:.2f} GiB. '
                           'Use --prepare-only or set --ignore-disk-check if scratch resides on another filesystem.')
    print(f'Disk preflight OK: free={free/gib:.1f} GiB, estimated minimum={required/gib:.1f} GiB',flush=True)

def main():
    p=argparse.ArgumentParser(description=__doc__,formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    p.add_argument('--img',help='Original Garmin IMG')
    p.add_argument('--osm',help='Already generated original Garmin-derived raw OSM')
    p.add_argument('--native-osm',help='Resume from already translated OSM; use a separate output directory')
    p.add_argument('--out',default='build/bgmountains-v097-05')
    p.add_argument('--poi-config',default=str(ROOT/'config/poi-mapping.csv'))
    p.add_argument('--line-config',default=str(ROOT/'config/line-mapping.csv'))
    p.add_argument('--hash-config',default=None,help='Generated POI suffix lengths by Garmin type')
    p.add_argument('--engine',choices=('auto','native','python'),default='auto',help='Use native streaming translation, route attachment and audit when available')
    p.add_argument('--jobs',type=int,default=1,help='IMG workers: default 1; explicitly set N to parallelize')
    p.add_argument('--bbox',help='W,S,E,N region for smoke tests')
    p.add_argument('--review-pois',action='store_true',help='Inventory the IMG, write editable poi-review.csv using --poi-config, and exit before conversion')
    p.add_argument('--template',action='store_true',help='Generate blank new-map mapping templates using this IMG (not BGMountains defaults)')
    p.add_argument('--attach-routes',action='store_true',help='Opt in to high-memory matching of trail overlays to physical paths (not recommended on full map)')
    p.add_argument('--no-route-attachment',action='store_true',help='Deprecated: attachment is disabled by default')
    p.add_argument('--prepare-only',action='store_true',help='Extract/translate/validate, do not compile OBF')
    p.add_argument('--creator',help='Existing OsmAndMapCreator directory containing utilities.sh')
    p.add_argument('--download-creator',action='store_true',help='Obtain official OsmAndMapCreator from download.osmand.net')
    p.add_argument('--java-opts',default='-Xms512M -Xmx8192M')
    p.add_argument('--keep-raw',action='store_true',help='Keep pre-translation OSM for debugging')
    p.add_argument('--ignore-disk-check',action='store_true',help='Override conservative OBF scratch space safety check')
    p.add_argument('--disk-headroom-gb',type=float,default=0,help='Minimum free GiB required before Java OBF build (0=auto estimate)')
    a=p.parse_args()
    if sum(bool(x) for x in (a.img,a.osm,a.native_osm)) != 1:p.error('Provide exactly one of --img, --osm or --native-osm')
    if a.native_osm and (a.template or a.bbox):p.error('--template and --bbox cannot be applied to translated OSM')
    if a.jobs<1 or a.jobs>64:p.error('--jobs must be 1..64')
    if a.review_pois and (not a.img or a.template):p.error('--review-pois requires --img and a source POI profile')
    if not a.template:
        for typ,pth in (('POI',a.poi_config),('line',a.line_config)):
            if not pathlib.Path(pth).is_file():p.error(f'{typ} mapping file not found: {pth}')
    out=pathlib.Path(a.out).resolve();out.mkdir(parents=True,exist_ok=True)
    native = pathlib.Path(a.native_osm).resolve() if a.native_osm else None
    if native and (not native.is_file() or native.parent == out):
        p.error('--native-osm requires an existing file and a separate --out directory')
    timings = out / ('timings-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f') + '.jsonl')
    stage = partial(timed_run, timings)
    print('Stage timings:', timings, flush=True)
    src=pathlib.Path(a.osm).resolve() if a.osm else None
    poi=pathlib.Path(a.poi_config).resolve();line=pathlib.Path(a.line_config).resolve()
    # Resolve MapCreator before running an expensive full-country extraction.
    # Missing download/network credentials must fail before consuming gigabytes.
    creator=None
    if not a.prepare_only and not a.template and not a.review_pois:
        creator=creator_dir(a.creator or os.environ.get('OSMAND_MAPCREATOR'))
        if not creator and a.download_creator:
            with timed(timings, 'fetch-creator'):
                creator=fetch_creator(ROOT/'build'/'mapcreator')
        if not creator:
            p.error('OBF requires --creator DIR or --download-creator; for testing use --prepare-only.')
    hash_args = ['--hash-config',a.hash_config] if a.hash_config else []
    index_args = []
    if creator:
        from hiking_profile import compile_indexes
        with timed(timings, 'prepare-indexes'):
            index_args = compile_indexes(creator,poi,out)
    tool = None if a.template or a.review_pois else native_tool(a.engine, stage)
    engine = 'native' if tool else 'python'
    print('OSM processing engine:',engine,flush=True)
    bin_path=ROOT/'build'/'garmin2osm'
    if a.img:
        stage('cmake-configure','cmake','-S',ROOT,'-B',ROOT/'build','-DCMAKE_BUILD_TYPE=Release')
        stage('cmake-build','cmake','--build',ROOT/'build','-j','2','--target','garmin2osm')
        if a.review_pois:
            from hiking_profile import review_inventory
            target = out/'poi-review.csv'
            if target.exists():raise FileExistsError(f'Preserving existing edits in {target}')
            cmd = [bin_path,'--input',a.img,'--poi-types-out',out/'discovered-pois.csv','--jobs',a.jobs]
            if a.bbox:cmd.extend(['--bbox',a.bbox])
            stage('inventory',*cmd,log=out/'inventory.log')
            with timed(timings, 'review-inventory'):
                review_inventory(out/'discovered-pois.csv',poi,target)
            print('Edit',target,'and resume with --poi-config',target)
            return 0
        src=out/'IMG-raw.osm'
        cmd=[str(bin_path),'--input',str(pathlib.Path(a.img).resolve()),'--output',str(src),
            '--poi-types-out',str(out/'discovered-pois.csv'),'--poi-manifest',str(out/'source-pois.csv'),
            '--jobs',str(a.jobs)]
        if a.bbox:cmd+=['--bbox',a.bbox]
        if not a.template:
            stage('prepare',sys.executable,TRANSLATOR,'prepare','--poi',poi,'--lines',line,*hash_args,'--out',out)
            cmd+=['--poi-overrides',str(out/'cpp-poi-override.csv'),'--poi-labels',str(out/'cpp-name-labels.csv')]
        stage('extract',*cmd,log=out/'extract.log')
    if a.template:
        if not a.img:p.error('--template requires --img')
        stage('template',sys.executable,TRANSLATOR,'template','--poi',out/'poi-mapping.TEMPLATE.csv',
            '--lines',out/'line-mapping.TEMPLATE.csv','--catalog',out/'discovered-pois.csv','--osm',src)
        print('Templates created. Review both files before using them for native OSM conversion.')
        return 0
    dst=out/'BGMountains-native.osm'
    attach = a.attach_routes and not a.no_route_attachment
    if not native:
        stage('transform',sys.executable,TRANSLATOR,'transform','--engine',engine,
              '--poi',poi,'--lines',line,*hash_args,'--osm',src,
              '--out',dst,'--report',out/'route-report.json',log=out/'translate.log')
    # Existing translated checkpoints can contain negative relation IDs. Repair
    # them on resume even when route attachment is disabled.
    if native or attach:
        if tool:
            from native_backend import postprocess
            from native_translate import read_poi_configuration, line_mapping
            pois, _ = read_poi_configuration(poi)
            with timed(timings, 'attach-audit-native' if attach else 'normalize-audit-native'):
                postprocess(tool,native or dst,dst,pois,line_mapping(line),
                            out/'route-attachment.json',out/'semantic-audit.json',attach=attach,
                            log=out/'route-attachment.log')
        else:
            attached=out/'BGMountains-native-attached.osm'
            stage('attach-routes' if attach else 'normalize-relations',sys.executable,
                  ROOT/'scripts/attach_native_routes.py','--osm',native or dst,'--out',attached,
                  '--lines',line,'--report',out/'route-attachment.json',
                  *([] if attach else ['--normalize-only']),log=out/'route-attachment.log')
            attached.replace(dst)
    if not tool:
        stage('audit',sys.executable,TRANSLATOR,'audit','--poi',poi,'--lines',line,'--osm',dst,
              '--out',out/'semantic-audit.json',log=out/'audit.log')
    # The audit consumes the complete XML stream and already counts hiking relations.
    audit = json.loads((out/'semantic-audit.json').read_text(encoding='utf-8'))
    n = sum(audit['route_relation_counts'].values())
    if not a.keep_raw and a.img:src.unlink(missing_ok=True)
    if a.prepare_only:
        print('PREPARED OSM:',dst,'route relations:',n,'No OBF compiled.')
        return 0
    if not a.ignore_disk_check:disk_preflight(out,dst.stat().st_size,a.disk_headroom_gb)
    env=os.environ.copy();env['JAVA_OPTS']=a.java_opts
    # utilities.sh versions differ in whether JAVA_OPTS is honored; this flag is
    # explicitly recognized by the JVM. It also reaches spawned Java processes.
    env['JAVA_TOOL_OPTIONS']=a.java_opts
    # Index settings travel in the OBF; the phone uses its existing rendering style.
    started=time.time()
    stage('build-obf',creator/'utilities.sh','generate-obf-no-address',dst,*index_args,log=out/'mapcreator.log',cwd=out,env=env,check_mapcreator=True)
    obfs=[f for f in out.glob('*.obf') if f.stat().st_mtime >= started-2 and f.stat().st_size>0]
    if not obfs:raise RuntimeError('Creator returned no OBF in '+str(out))
    obf=max(obfs,key=lambda x:x.stat().st_mtime)
    if (creator/'inspector.sh').exists():
        stage('inspect-obf',creator/'inspector.sh','-vpoi',obf,log=out/'obf-inspection.txt',cwd=out,env=env)
    (out/'build-manifest.json').write_text(json.dumps({
      'version':'v0.97.5','processing_engine':engine,'input':a.img or a.osm or a.native_osm,'poi_config':str(poi),'line_config':str(line),
      'hash_config':str(pathlib.Path(a.hash_config).resolve()) if a.hash_config else str(poi),
      'hash_config_sha256':hashlib.sha256(pathlib.Path(a.hash_config or poi).read_bytes()).hexdigest(),
      'poi_config_sha256':hashlib.sha256(poi.read_bytes()).hexdigest(),
      'line_config_sha256':hashlib.sha256(line.read_bytes()).hexdigest(),
      'output_obf':str(obf),'obf_bytes':obf.stat().st_size,
      'compiled_at':datetime.datetime.now(datetime.timezone.utc).isoformat(),
      'notice':'Native stock rendering. POI display zoom controlled by OsmAnd; no claim that all tags get icons.'},ensure_ascii=False,indent=2)+'\n')
    print('BUILT OBF:',obf)
    return 0
if __name__=='__main__':sys.exit(main())
