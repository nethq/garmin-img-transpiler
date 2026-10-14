#!/usr/bin/env python3
"""One-command BGMountains IMG -> reviewed OSM -> custom indexed OsmAnd OBF.

Only Python 3 stdlib needed for orchestration; CMake/C++/OsmAndMapCreator are external.
"""
from __future__ import annotations
import argparse
import csv
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import urllib.request
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
UPSTREAM = {
 'poi': 'https://raw.githubusercontent.com/osmandapp/OsmAnd-resources/master/poi/poi_types.xml',
 'rendering': 'https://raw.githubusercontent.com/osmandapp/OsmAnd-resources/master/obf_creation/rendering_types.xml'
}

def call(argv, cwd=None, log=None, env=None):
    argv = [str(s) for s in argv]
    print('+', ' '.join(shlex_quote(s) for s in argv), flush=True)
    if log:
        with Path(log).open('w', encoding='utf-8') as output:
            r = subprocess.run(argv, cwd=cwd, env=env, stdout=output, stderr=subprocess.STDOUT)
        if r.returncode:
            print('Command failed; last log lines:', file=sys.stderr)
            print('\n'.join(Path(log).read_text(errors='replace').splitlines()[-35:]), file=sys.stderr)
            raise SystemExit(r.returncode)
    else:
        subprocess.run(argv, cwd=cwd, env=env, check=True)

def shlex_quote(s):
    import shlex
    return shlex.quote(s)

def base_xml(provided, creator, key, cache, offline):
    if provided:
        p = Path(provided).expanduser().resolve()
        if not p.is_file(): raise SystemExit('Upstream XML not found: '+str(p))
        return p
    names = ['poi_types.xml'] if key == 'poi' else ['rendering_types.xml']
    if creator:
        for prefix in [creator, creator/'resources', creator/'config', creator/'poi', creator/'obf_creation']:
            for name in names:
                candidate = prefix/name
                if candidate.is_file(): return candidate.resolve()
    target = cache/names[0]
    if target.is_file(): return target
    if offline:
        raise SystemExit('Missing upstream '+names[0]+': pass --'+key+'-base FILE or allow download')
    target.parent.mkdir(parents=True,exist_ok=True)
    print('Fetching official OsmAnd '+names[0]+' (first run only) ...', flush=True)
    try:
        with urllib.request.urlopen(UPSTREAM[key], timeout=50) as u, target.open('wb') as out:
            shutil.copyfileobj(u,out)
    except Exception as ex:
        target.unlink(missing_ok=True)
        raise SystemExit('Cannot fetch official OsmAnd '+names[0]+': '+str(ex)+'\nProvide --'+key+'-base FILE manually.')
    return target

def catalog_rows(p):
    with Path(p).open('r',encoding='utf-8-sig',newline='') as f:
        rows=list(csv.DictReader(f))
    if not rows or not {'code','count','has_typ_icon'}.issubset(rows[0]):
        raise SystemExit('Invalid POI catalog (expected code,count,has_typ_icon): '+str(p))
    out=[]
    seen=set()
    for r in rows:
        code='0x'+format(int(r['code'],0),'x')
        if code in seen:raise SystemExit('Duplicate code in catalog: '+code)
        seen.add(code)
        out.append(dict(r,code=code,count=int(r['count'])))
    return sorted(out,key=lambda r:(-r['count'],int(r['code'],0)))

def write_xml(tree,path):
    ET.indent(tree,space='  ')
    tree.write(path,encoding='utf-8',xml_declaration=True)
    ET.parse(path)

def make_configs(poi_base,render_base,catalog,folder,threshold,frequent_zoom=15,rare_zoom=17):
    # Keep entire upstream definitions! Replacing with a standalone tiny XML is
    # precisely what used to make ordinary semantic POIs disappear.
    poi=ET.parse(poi_base); proot=poi.getroot()
    if proot.tag!='poi_types':raise SystemExit('Not an OsmAnd poi_types.xml: '+str(poi_base))
    if any(c.get('name')=='bgmountains_raw' for c in proot.findall('poi_category')):
        raise SystemExit('Input poi_types.xml already has a bgmountains_raw category')
    cat=ET.SubElement(proot,'poi_category',name='bgmountains_raw', default_tag='bgm_poi', no_edit='true')
    for r in catalog:
        code=r['code']
        ET.SubElement(cat,'poi_type',name='bgm_'+code.replace('0x',''),tag='bgm_poi',value='type_'+code,no_edit='true')
    pa=folder/'poi_types.bgmountains.xml'
    write_xml(poi,pa)

    rendering=ET.parse(render_base); rroot=rendering.getroot()
    if rroot.tag != 'osmand_types':
        # OsmAnd resources use the tag 'rendering_types'; fail instead of dropping all built-in types.
        raise SystemExit('Unexpected OsmAnd rendering_types.xml root: '+rroot.tag)
    cats=[c for c in rroot.iter('category') if c.get('name')=='user_defined']
    target=cats[0] if cats else ET.SubElement(rroot,'category',name='user_defined')
    for r in catalog:
        code=r['code']
        ET.SubElement(target,'type',tag='bgm_poi',value='type_'+code,
                      minzoom=str(frequent_zoom) if r['count']>=threshold else str(rare_zoom))
    rp=folder/'rendering_types.bgmountains.xml'
    write_xml(rendering,rp)
    summary={
       'catalog_types':len(catalog),
       'poi_count_in_catalog':sum(r.get('sample_count',r['count']) for r in catalog),
       'types_at_or_above_threshold':sum(r['count']>=threshold for r in catalog),
       'frequent_index_minzoom':frequent_zoom, 'rare_index_minzoom':rare_zoom,
       'items_at_or_above_threshold':sum(r.get('sample_count',r['count']) for r in catalog if r['count']>=threshold),
       'types_with_extracted_icon':sum(r['has_typ_icon']=='yes' for r in catalog),
       'poi_base_sha256':hashlib.sha256(Path(poi_base).read_bytes()).hexdigest(),
       'rendering_base_sha256':hashlib.sha256(Path(render_base).read_bytes()).hexdigest(),
       'note':'All Garmin hex POI types indexed; high-frequency raw types inserted into OBF map index from selected frequent zoom; low-frequency from selected rare zoom. Rendering exact Garmin icon PNGs requires installing compatible icons/style in OsmAnd, not merely compiling an OBF.'
    }
    (folder/'type-index-summary.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    return pa,rp,summary

def make_style(icon_csv, folder, catalog):
    """Optional addon for otherwise unclassified types using preinstalled OsmAnd icons.

    This cannot install Garmin bitmap icons and must be enabled in OsmAnd.
    """
    known={r['code'] for r in catalog}
    with Path(icon_csv).open(encoding='utf-8-sig',newline='') as f:
        rows=list(csv.DictReader(f))
    visible=[]
    for r in rows:
        code='0x'+format(int(r['code'],0),'x')
        if code not in known:continue  # bbox builds are smaller than national config
        if r['show_in_custom_style'].strip().lower() not in ('yes','true','1'):continue
        name=r['osmand_builtin_icon'].strip()
        if not name or not name.replace('_','').replace('-','').isalnum():
            raise SystemExit('Invalid OsmAnd icon in '+str(icon_csv)+' for '+code)
        zoom=int(r['minzoom'])
        if not 1<=zoom<=21:raise SystemExit('Invalid icon zoom for '+code)
        visible.append((code,name,zoom))
    root=ET.Element('renderingStyle',name='BGMountains custom POIs',depends='default',
                    defaultColor='#fff8ee',version='1')
    order=ET.SubElement(root,'order')
    points=ET.SubElement(root,'point')
    for code,name,zoom in visible:
        ET.SubElement(order,'case',tag='bgm_poi',value='type_'+code,minzoom=str(zoom),
                      point='true',objectType='1',order='128')
        ET.SubElement(points,'case',tag='bgm_poi',value='type_'+code,minzoom=str(zoom),
                      icon=name,iconOrder='10',iconVisibleSize='12')
    path=folder/'BGMountains-custom.render.xml'
    write_xml(ET.ElementTree(root),path)
    return path,len(visible)

def obtain_creator(cache, offline=False):
    """Opt-in download from official OsmAnd nightly build (large file)."""
    import zipfile
    link='https://download.osmand.net/latest-night-build/OsmAndMapCreator-main.zip'
    target=cache/'OsmAndMapCreator-main.zip'
    extracted=cache/'unpacked'
    found=list(extracted.rglob('utilities.sh')) if extracted.is_dir() else []
    if found:return found[0].parent
    if offline:raise SystemExit('Cannot download MapCreator with --offline; pass --creator')
    cache.mkdir(parents=True,exist_ok=True)
    if not target.is_file():
        print('Downloading official OsmAndMapCreator (~150 MB) ...',flush=True)
        try:
            with urllib.request.urlopen(link,timeout=180) as src,target.open('wb') as dest:
                shutil.copyfileobj(src,dest,length=2**20)
        except Exception as ex:
            target.unlink(missing_ok=True)
            raise SystemExit('Cannot download OsmAndMapCreator: '+str(ex)+'\nDownload manually: '+link)
    print('Extracting MapCreator...',flush=True)
    extracted.mkdir(parents=True,exist_ok=True)
    with zipfile.ZipFile(target) as z:
        for m in z.infolist():
            rel=Path(m.filename)
            if rel.is_absolute() or '..' in rel.parts:
                raise SystemExit('Unsafe file in MapCreator archive: '+m.filename)
            z.extract(m,extracted)
    found=list(extracted.rglob('utilities.sh'))
    if not found:raise SystemExit('MapCreator ZIP extracted but utilities.sh not found')
    for name in ('utilities.sh','inspector.sh'):
        for f in extracted.rglob(name):f.chmod(f.stat().st_mode|0o111)
    return found[0].parent

def resolve_creator(path):
    name = path or os.environ.get('OSMAND_MAPCREATOR')
    if not name:return None
    creator=Path(name).expanduser().resolve()
    if creator.is_file() and creator.name=='utilities.sh':creator=creator.parent
    if not (creator/'utilities.sh').is_file():
        raise SystemExit('Cannot find utilities.sh under '+str(creator))
    return creator

def main():
    p=argparse.ArgumentParser(description=__doc__,formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    inp=p.add_mutually_exclusive_group(required=True)
    inp.add_argument('--img',help='Original Garmin IMG; converter is compiled and executed')
    inp.add_argument('--osm',help='Already converted OSM XML; must contain bgm_poi=type_0x... tags (v0.7+)')
    p.add_argument('--creator',help='OsmAndMapCreator directory containing utilities.sh (or OSMAND_MAPCREATOR env)')
    p.add_argument('--download-creator',action='store_true',help='Download official OsmAndMapCreator ZIP automatically (~150 MB) when no --creator is set')
    p.add_argument('--out',default='build/bgmountains-output',help='Output folder')
    p.add_argument('--jobs',type=int,default=1,help='C++ IMG reader threads (opt-in)')
    p.add_argument('--bbox',help='Optional W,S,E,N bounds for small-area test')
    p.add_argument('--threshold',type=int,default=100,help='Frequent-type occurrence threshold')
    p.add_argument('--frequent-zoom',type=int,default=15,help='Store frequent raw map icons at zoom N and above')
    p.add_argument('--rare-zoom',type=int,default=17,help='Store rare raw map icons at zoom N and above')
    p.add_argument('--icons-config',default=str(ROOT/'config/osmand-icons.csv'),help='OsmAnd built-in icon choices for custom addon style')
    p.add_argument('--poi-overrides',default=str(ROOT/'config/poi-overrides-all-types.csv'))
    p.add_argument('--poi-name-fixes',default=str(ROOT/'config/poi-name-fixes.csv'))
    p.add_argument('--poi-additions',default=str(ROOT/'config/poi-additions.csv'))
    p.add_argument('--poi-base',help='Official upstream poi_types.xml; if omitted use cache/download')
    p.add_argument('--rendering-base',help='Official upstream rendering_types.xml; if omitted use cache/download')
    p.add_argument('--offline',action='store_true',help='Never download upstream XML')
    p.add_argument('--prepare-only',action='store_true',help='Do not run OsmAndMapCreator, but generate OSM and XMLs')
    p.add_argument('--skip-qa',action='store_true',help='Skip OSM comparison for faster repeat runs')
    p.add_argument('--java-opts',default='-Xms512M -Xmx8192M',help='JAVA_OPTS for OsmAndMapCreator')
    p.add_argument('--skip-cpp-build',action='store_true')
    p.add_argument('--catalog',help='Preexisting POI catalog when supplying --osm (otherwise use packaged nationwide catalog)')
    p.add_argument('--frequency-catalog',default=str(ROOT/'data/BGMountains-unique-poi-types.csv'),help='Reference nationwide counts for map index zoom decisions even in --bbox builds')
    args=p.parse_args()
    if args.jobs<1 or args.jobs>64 or args.threshold<1 or not 1<=args.frequent_zoom<=21 or not 1<=args.rare_zoom<=21: p.error('--jobs 1..64; threshold >=1; zoom 1..21')
    dest=Path(args.out).expanduser().resolve();dest.mkdir(parents=True,exist_ok=True)
    creator=resolve_creator(args.creator)
    if not creator and args.download_creator and not args.prepare_only:
        creator=obtain_creator(ROOT/'build'/'mapcreator-download',args.offline)
    if not creator and not args.prepare_only:p.error('Supply --creator /path/to/OsmAndMapCreator (or OSMAND_MAPCREATOR env) or use --prepare-only')
    if args.img:
        if not Path(args.img).is_file():p.error('IMG file missing: '+args.img)
        binary=ROOT/'build'/'garmin2osm'
        if not args.skip_cpp_build or not binary.is_file():
            call(['cmake','-S',ROOT,'-B',ROOT/'build','-DCMAKE_BUILD_TYPE=Release'])
            call(['cmake','--build',ROOT/'build','--config','Release','-j','2'])
        osm=dest/'BGMountains.osm'
        cmd=[binary,'--input',Path(args.img).resolve(),'--output',osm,'--poi-overrides',Path(args.poi_overrides).resolve(),
             '--poi-name-fixes',Path(args.poi_name_fixes).resolve(),'--poi-additions',Path(args.poi_additions).resolve(),
             '--poi-manifest',dest/'source-pois.csv','--poi-types-out',dest/'poi-types.csv','--jobs',args.jobs]
        if args.bbox:cmd+=['--bbox',args.bbox]
        call(cmd,log=dest/'converter.log')
        catalog=dest/'poi-types.csv'
        if not args.skip_qa:
            call([sys.executable,ROOT/'tools/validate_pois.py','compare','--source',dest/'source-pois.csv','--osm',osm,
                 '--rules',Path(args.poi_overrides).resolve(),'--report',dest/'validation.json','--mismatches',dest/'mismatches.csv'],
                 log=dest/'validator.log')
    else:
        osm=Path(args.osm).expanduser().resolve()
        if not osm.is_file():p.error('OSM file missing: '+str(osm))
        catalog=Path(args.catalog).expanduser().resolve() if args.catalog else ROOT/'data'/'BGMountains-unique-poi-types.csv'
    rows=catalog_rows(catalog)
    nationwide={r['code']:r['count'] for r in catalog_rows(args.frequency_catalog)} if args.frequency_catalog else {}
    for row in rows:
        row['sample_count']=row['count']
        row['count']=nationwide.get(row['code'],row['count'])
    # A separately supplied legacy OSM might lack our new classifier.
    # For --img, the v0.7 converter above just generated it by construction.
    if args.osm:
        import gzip
        opener=gzip.open if osm.suffix=='.gz' else open
        with opener(osm,'rt',encoding='utf-8',errors='replace') as f:
            found=any('k="bgm_poi"' in line for line in f)
        if not found:
            raise SystemExit('Existing OSM lacks bgm_poi; re-extract with v0.7 C++ converter.')
    folder=dest/'osmand-config';folder.mkdir(exist_ok=True)
    poi_base=base_xml(args.poi_base,creator,'poi',dest/'upstream',args.offline)
    rendering_base=base_xml(args.rendering_base,creator,'rendering',dest/'upstream',args.offline)
    poi_cfg,render_cfg,summary=make_configs(poi_base,rendering_base,rows,folder,args.threshold,args.frequent_zoom,args.rare_zoom)
    style,nicons=make_style(args.icons_config,folder,rows)
    print('Indexed POI types:',summary['catalog_types'],'frequent:',summary['types_at_or_above_threshold'],'custom style icons:',nicons,flush=True)
    if args.prepare_only:
        print('PREPARED (no OBF generated). Files at:',dest)
        return 0
    before={str(i.resolve()) for i in dest.glob('*.obf')}
    # Save Java logs, and preserve OsmAndMapCreator temp data inside user-selected output dir.
    env=os.environ.copy();env['JAVA_OPTS']=args.java_opts
    program=creator/'utilities.sh'
    cmd=[str(program),'generate-obf-no-address',str(osm),
         '--poi-types='+str(poi_cfg),'--rendering-types='+str(render_cfg)]
    call(cmd,cwd=dest,log=dest/'mapcreator.log',env=env)
    after=[i for i in dest.glob('*.obf') if str(i.resolve()) not in before]
    if not after:
        # Creator sometimes writes next to the .osm source, even with cwd changed.
        alt=list(osm.parent.glob('*.obf'))
        after=[i for i in alt if i.is_file()]
    if not after:raise SystemExit('MapCreator returned success but no .obf was found; see '+str(dest/'mapcreator.log'))
    built=max(after,key=lambda p:p.stat().st_mtime)
    print('OBF created:',built,'bytes:',built.stat().st_size)
    if (creator/'inspector.sh').is_file():
        call([str(creator/'inspector.sh'),'-vpoi',str(built)],cwd=dest,log=dest/'obf-poi-index.txt',env=env)
    (dest/'build-manifest.json').write_text(json.dumps({
       'created_at_utc':dt.datetime.now(dt.timezone.utc).isoformat(),
       'obf':str(built),'obf_bytes':built.stat().st_size,
       'input_osm':str(osm),'poi_types':str(poi_cfg),'rendering_types':str(render_cfg),
       'custom_style':str(style), 'custom_style_icon_rules':nicons,
       'summary':summary
    },ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    return 0
if __name__=='__main__':
    sys.exit(main())
