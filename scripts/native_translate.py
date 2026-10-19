#!/usr/bin/env python3
"""v0.95: streaming IMG/OSM native-OsmAnd translation and honest QA.
Only user-editable controls: config/BGMountains-native-poi-mapping-v094.csv and config/line-mapping.csv.
"""
import argparse,base64,collections,csv,hashlib,json,pathlib,re,sqlite3,sys,xml.etree.ElementTree as ET
from xml.sax.saxutils import escape,quoteattr

def top_level_elements(source):
    """Bounded-memory XML pull iterator. ElementTree .clear() alone leaves one
    empty Element per processed object in the root; clearing the root prevents
    nationwide Garmin files from exhausting RAM during multi-pass conversion.
    Callers must fully process each element before requesting the next.
    """
    context=ET.iterparse(source,events=('start','end'))
    event, root=next(context)
    if event != 'start' or root.tag != 'osm':
        raise ValueError('Expected an OSM XML root element')
    depth=1
    for event,elem in context:
        if event=='start':depth+=1;continue
        if depth==2 and elem.tag in ('node','way','relation'):
            yield elem
            root.clear()
        depth-=1

def tagdict(elem):return {x.get('k'):x.get('v','') for x in elem.findall('tag')}
def set_tags(elem,d):
    for child in list(elem):
        if child.tag=='tag':elem.remove(child)
    for k,v in d.items():
        if v is not None and v!='':ET.SubElement(elem,'tag',k=k,v=str(v))
def read_csv(p):
    with open(p,encoding='utf-8-sig',newline='') as f:
        lines=(line for line in f if not line.startswith('#'))
        yield from csv.DictReader(lines)
def code(value):return hex(int(value,0)).lower()
def read_poi_configuration(path):
    """Read one canonical POI config: unique base rule per code and optional
    ordered name_contains rules (a v0.94 feature previously lost in Python).
    The v0.93/0.94 15-column files remain backward-compatible.
    """
    bases={}; conditional=[]
    for line_no,row in enumerate(read_csv(path),start=2):
        try:c=code(row['code'])
        except Exception as ex:raise ValueError(f'Invalid POI code at {path}:{line_no}: {row}') from ex
        row=dict(row);row['code']=c
        act=(row.get('action') or 'replace').strip().lower()
        if act not in ('replace','append','raw','drop'):
            raise ValueError(f'Invalid action at {path}:{line_no}: {act}')
        tags_from_string(row.get('osm_tags',''))
        if row.get('name_contains',''):
            conditional.append(row)
        else:
            if c in bases:raise ValueError(f'Duplicate POI base mapping {c} at {path}:{line_no}')
            bases[c]=row
    for row in conditional:
        if row['code'] not in bases:raise ValueError(f'Conditional rule has no base: {row["code"]}')
    return bases,conditional

def mapping(path):
    """Base POI map; conditional rules are applied by the C++ override stage."""
    return read_poi_configuration(path)[0]

def line_mapping(path):
    ret={}
    for row in read_csv(path):
        try:c=code(row['code'])
        except Exception as ex:raise ValueError('Invalid line code in '+str(path)+': '+str(row)) from ex
        if c in ret:raise ValueError(f'Duplicate line code {c} in {path}')
        ret[c]=row
    return ret

def tags_from_string(v):
    d={}
    for x in v.split(';'):
        if not x:continue
        if '=' not in x:raise ValueError(f'Invalid OSM tag {x!r}')
        k,val=x.split('=',1)
        if not k or not val:raise ValueError(f'Invalid OSM tag {x!r}')
        if k in ('name','garmin:type','garmin:feature','garmin:mapset','bgm:generated_name'):
            raise ValueError('Reserved mapping key: '+k)
        d[k]=val
    return d

# User-requested PRIVATE-map presentation fallback. A locality is a safe stock-map
# display tag, but this is synthetically applied to unknown source types and is
# therefore NOT suitable for public OSM imports. Elevation annotations are excluded.
SEMANTIC_KEYS = frozenset(('amenity','tourism','emergency','natural','man_made','place',
    'historic','shop','leisure','building','waterway','railway','power','aeroway',
    'highway','hazard','ford','water','water_source','landuse','barrier',
    'aerialway','craft','military','geological','public_transport','sport'))
GENERATED_SUFFIX = re.compile(r' - ([A-Z0-9]{7,16})$')

def stable_unknown_hash(elem,tags):
    name=tags.get('name','')
    m=GENERATED_SUFFIX.search(name) if tags.get('bgm:generated_name') == 'yes' else None
    if m:return m.group(1)  # preserve previously generated stable identity
    seed='|'.join((tags.get('garmin:mapset',''),tags.get('garmin:type',''),
                   elem.get('lat',''),elem.get('lon','')))
    return base64.b32encode(hashlib.blake2s(seed.encode('utf-8'),digest_size=7).digest()).decode().rstrip('=')[:11]

def add_native_unknown_fallback(elem,tags,rule):
    """Only unclassified POIs. Named POIs retain their genuine source names."""
    if any(k in tags for k in SEMANTIC_KEYS):return False
    if tags.get('garmin:type') == '0x6600' or 'ele' in tags:
        return False  # spot heights and elevation annotations are not unknown places
    if rule and rule.get('action','').strip().lower()=='drop':return False
    tags['place']='locality'
    tags['bgm:synthetic_fallback']='place_locality'
    tags['bgm:classification']='unverified'
    name=tags.get('name','').strip()
    if not name or tags.get('bgm:generated_name')=='yes' or name.startswith('Garmin обект '):
        tags['name']='Неуточнено - '+stable_unknown_hash(elem,tags)
        tags['bgm:generated_name']='yes'
    return True

def make_cpp_rules(pois,out,labels,conditional=()):
    """C++ v0.9 uses these internal stage files; they are NOT user control files."""
    with open(out,'w',encoding='utf-8',newline='') as f,open(labels,'w',encoding='utf-8',newline='') as g:
        w=csv.writer(f);w.writerow(['code','name_contains','mode','tags','note'])
        l=csv.writer(g);l.writerow(['code','label_bg'])
        for c,row in sorted(pois.items(),key=lambda t:int(t[0],0)):
            action=row.get('action','replace').strip().lower()
            if action not in ('replace','append','raw','drop'):raise ValueError('Invalid action for '+c)
            vals=tags_from_string(row.get('osm_tags',''))
            if action=='raw':action='replace';vals={}
            if action=='drop':vals={}
            w.writerow([c,'',action,';'.join(k+'='+v for k,v in vals.items()),'v0.92 compiled from poi-mapping.csv'])
            if row.get('label_bg'):l.writerow([c,row['label_bg']])
        # Order matters: base rule first, then the explicit name-specific rules
        # in CSV order. C++ evaluates these against ORIGINAL Garmin names.
        for row in conditional:
            c=row['code'];action=(row.get('action') or 'replace').strip().lower()
            vals=tags_from_string(row.get('osm_tags',''))
            if action=='raw':action='keep_raw';vals={}
            w.writerow([c,row['name_contains'],action,
                ';'.join(k+'='+v for k,v in vals.items()),
                'v0.95 name-specific rule: '+row.get('note','')])

def inspect_counts(osm,pois,lines,out,strict=False):
    seen=collections.Counter();unresolved=collections.Counter();annotations=collections.Counter();fallback=collections.Counter();bad=collections.Counter();routes=collections.Counter();examples={};category=collections.Counter();n=0
    with open(osm,'rb') as src:
        for el in top_level_elements(src):
            if el.tag=='node':
                t=tagdict(el)
                if t.get('garmin:feature')=='poi':
                    n+=1;c=t.get('garmin:type','');seen[c]+=1
                    if c not in pois:bad[c]+=1
                    if t.get('bgm:synthetic_fallback')=='place_locality':fallback[c]+=1
                    if c=='0x6600' and 'ele' in t:
                        annotations['spot_height']+=1
                    elif not any(k in t for k in ('amenity','tourism','emergency','natural','man_made','place','historic','shop','leisure','building','waterway','railway','power','aeroway','highway','ele','hazard','ford','water','water_source','landuse','barrier','aerialway','craft','geological')):
                        unresolved[c]+=1
                    if len(examples.get(c,[]))<5:examples.setdefault(c,[]).append([el.get('lat'),el.get('lon'),t.get('name','')])
                    for k in ('amenity','tourism','emergency','natural','man_made','building','historic','shop','leisure'):
                        if k in t:category[f'{k}={t[k]}']+=1
            elif el.tag=='relation':
                t=tagdict(el)
                if t.get('route')=='hiking':routes[t.get('garmin:type','unknown')]+=1
            el.clear()
    summary={'status':'FAIL' if strict and (bad or unresolved) else ('WARN' if bad or unresolved else 'PASS'),
        'total_pois':n,'mapped_types':len(pois),'observed_types':len(seen),
        'source_codes_not_in_config':dict(bad),'unclassified_counts':dict(unresolved),
        'unclassified_total':sum(unresolved.values()),'synthetic_locality_fallback_total':sum(fallback.values()),'synthetic_locality_counts':dict(fallback),'annotations_not_missing_pois':dict(annotations),'route_relation_counts':dict(routes),
        'native_semantic_categories':dict(category.most_common(100)),
        'sample_coordinates':examples,
        'warning':'OSM audit only; stock icons/search not guaranteed. Synthetic localities are private-map fallback, not valid public OSM classification. Elevation-only objects deliberately have no artificial POI icon.'}
    pathlib.Path(out).write_text(json.dumps(summary,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    return summary

def transform(source,dest,pois,lines,report,strict=False):
    """Streaming write; route metadata held in SQLite so nationwide builds do not need RAM for all nodes/ways.
    Native route relations are built only from selected Garmin colored overlay ways,
    NOT from invented highway=path edges.
    """
    dbpath=pathlib.Path(dest).with_suffix('.edges.sqlite')
    if dbpath.exists():raise FileExistsError(dbpath)
    db=sqlite3.connect(dbpath)
    db.execute('PRAGMA synchronous=OFF');db.execute('PRAGMA journal_mode=MEMORY')
    db.execute('CREATE TABLE edges (code TEXT, way INTEGER, a INTEGER, b INTEGER)')
    poi_count=0; ways=0; candidate_edges=0; emitted=0; synthetic=0; kinds=collections.Counter();seen=collections.Counter();gaps=collections.Counter()
    with open(source,'rb') as inp,open(dest,'w',encoding='utf-8') as out:
        out.write('<?xml version="1.0" encoding="UTF-8"?>\n<osm version="0.6" generator="garmin2osm-v0.95" upload="never">\n')
        for el in top_level_elements(inp):
            if el.tag=='node':
                tags=tagdict(el)
                if tags.get('garmin:feature')=='poi':
                    poi_count+=1;c=tags.get('garmin:type','')
                    if c in pois:
                        row=pois[c];tags['bgm:legend_bg']=row.get('legend_bg','') or row.get('label_bg','')
                        tags['bgm:priority']=row.get('priority','normal')
                        # only provenance; no false POI semantic category inserted here
                        tags.pop('bgm_poi',None);tags.pop('bgm:synthetic_icon',None)
                        # Unclassified Garmin POIs intentionally remain raw: stock OsmAnd
                        # cannot visually draw a unique icon for each without installed resources.
                    else:gaps[c]+=1;tags.pop('bgm_poi',None)
                    if add_native_unknown_fallback(el,tags,pois.get(c)):
                        synthetic+=1
                    set_tags(el,tags);seen[c]+=1
                out.write(ET.tostring(el,encoding='unicode')+'\n');emitted+=1
            elif el.tag=='way':
                ways+=1;tags=tagdict(el)
                if tags.get('garmin:feature')=='extended_polyline' or 'bgm_route' in tags:
                    c=tags.get('garmin:type','')
                    r=lines.get(c)
                    tags.pop('bgm_route',None)
                    # Raw geometry and colour may still be needed for diagnosis;
                    # no duplicate road geometry or misleading water/terrain categories.
                    if r:
                        tags['bgm:line_legend']=r.get('legend_bg','')
                        tags['bgm:translation_status']=r.get('route_confidence','unknown')
                        tags.update(tags_from_string(r.get('osm_way_tags','')))
                        if r.get('route_mode','').lower()=='relation':
                            nref=[int(x.attrib['ref']) for x in el.findall('nd')]
                            if len(nref)>=2:
                                db.execute('INSERT INTO edges VALUES (?,?,?,?)',(c,int(el.get('id')),nref[0],nref[-1]));candidate_edges+=1
                    else:gaps[c]+=1
                    set_tags(el,tags)
                out.write(ET.tostring(el,encoding='unicode')+'\n');emitted+=1
            elif el.tag=='relation':
                out.write(ET.tostring(el,encoding='unicode')+'\n');emitted+=1
            el.clear()
        db.commit()
        # Use topological unions on endpoint node refs to avoid one nationwide route per color.
        grouped=collections.defaultdict(list)
        for c,w,a,b in db.execute('SELECT code,way,a,b FROM edges ORDER BY code,way'):
            grouped[c].append((w,a,b))
        relations=0;components=0;largest=0
        for c,edges in sorted(grouped.items()):
            parent=list(range(len(edges)));rank=[0]*len(edges)
            def root(i):
                while parent[i]!=i:
                    parent[i]=parent[parent[i]];i=parent[i]
                return i
            def union(a,b):
                ra,rb=root(a),root(b)
                if ra==rb:return
                if rank[ra]<rank[rb]:ra,rb=rb,ra
                parent[rb]=ra
                if rank[ra]==rank[rb]:rank[ra]+=1
            endpoint={}
            for i,(w,a,b) in enumerate(edges):
                for n in (a,b):
                    if n in endpoint:union(i,endpoint[n])
                    else:endpoint[n]=i
            comps=collections.defaultdict(list)
            for i,(w,a,b) in enumerate(edges):comps[root(i)].append(w)
            for members in comps.values():
                components+=1;largest=max(largest,len(members));relations+=1
                # Deterministic, negative IDs because prior C++ IDs are positive
                rel=ET.Element('relation',id=str(-relations),version='1')
                for wid in sorted(members):ET.SubElement(rel,'member',type='way',ref=str(wid),role='')
                row=lines[c]
                tags={'type':'route','route':'hiking','network':row.get('network') or 'lwn',
                    'garmin:type':c,'bgm:source_feature':'extended_polyline',
                    'bgm:confidence':row.get('route_confidence','color_only')}
                if row.get('osmc_symbol'):tags['osmc:symbol']=row['osmc_symbol']
                if row.get('legacy_color'):tags['colour']=row['legacy_color']
                # Use a non-confusing internal identifier, NOT a fabricated official route name.
                tags['ref']='BG'+c.replace('0x','').upper()
                set_tags(rel,tags)
                out.write(ET.tostring(rel,encoding='unicode')+'\n')
        out.write('</osm>\n')
    db.close();dbpath.unlink(missing_ok=True)
    result={'pois':poi_count,'synthetic_locality_fallbacks':synthetic,'ways':ways,'route_candidate_ways':candidate_edges,
        'route_relations':relations,'route_components':components,'largest_component_ways':largest,
        'source_types_missing_mapping':dict(gaps),'notes':['Synthetic place=locality on unknown features is PRIVATE-map-only' ,'Native OSM route relations; no custom phone-side stylesheet','Display zoom remains controlled by stock OsmAnd, not config zoom_hint','Route symbol derived from Garmin color; color_only mappings require review']}
    pathlib.Path(report).write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    return result

def template(catalog,poi_file,line_file,osm=None):
    """Future map: source-driven, abstains from guessing semantic identities."""
    if pathlib.Path(poi_file).exists() or pathlib.Path(line_file).exists():raise FileExistsError('Refusing to overwrite mapping templates')
    pr=[]
    for row in read_csv(catalog):
        pr.append({'code':row['code'],'source_count':row['count'],'legend_bg':'','legend_en':'',
            'meaning_confidence':'unknown','osm_tags':'','label_bg':f'Garmin обект {row["code"]}',
            'priority':'normal','zoom_hint':'17','action':'raw',
            'note':f'SAMPLE {row.get("example1_lat","")}, {row.get("example1_lon","")}, {row.get("example1_name","")}'})
    lc=collections.Counter()
    if osm:
        with open(osm,'rb') as inp:
            for el in top_level_elements(inp):
                if el.tag=='way':
                    t=tagdict(el)
                    if t.get('garmin:feature')=='extended_polyline':lc[t.get('garmin:type','')]+=1
                el.clear()
    lr=[{'code':k,'source_count':v,'legend_bg':'','legacy_color':'', 'osm_way_tags':'',
        'route_mode':'preserve','osmc_symbol':'','network':'lwn','route_confidence':'unknown',
        'priority':'normal','note':'Unmapped type: check Garmin legend and geometry before enabling'} for k,v in sorted(lc.items())]
    with open(poi_file,'w',encoding='utf-8',newline='') as f:
        w=csv.DictWriter(f,fieldnames=list(pr[0]) if pr else ['code','source_count','legend_bg','legend_en','meaning_confidence','osm_tags','label_bg','priority','zoom_hint','action','note']);w.writeheader();w.writerows(pr)
    with open(line_file,'w',encoding='utf-8',newline='') as f:
        w=csv.DictWriter(f,fieldnames=list(lr[0]) if lr else ['code','source_count','legend_bg','legacy_color','osm_way_tags','route_mode','osmc_symbol','network','route_confidence','priority','note']);w.writeheader();w.writerows(lr)
    return {'poi_types':len(pr),'line_types':len(lr)}

def main():
    p=argparse.ArgumentParser(description=__doc__);sub=p.add_subparsers(dest='mode',required=True)
    for cmd in ['prepare','transform','audit','template']:
        s=sub.add_parser(cmd);s.add_argument('--poi',required=True);s.add_argument('--lines',required=True)
        if cmd=='prepare':s.add_argument('--out',required=True)
        elif cmd=='transform':s.add_argument('--osm',required=True);s.add_argument('--out',required=True);s.add_argument('--report',required=True)
        elif cmd=='audit':s.add_argument('--osm',required=True);s.add_argument('--out',required=True);s.add_argument('--strict',action='store_true')
        else:s.add_argument('--catalog',required=True);s.add_argument('--osm')
    a=p.parse_args()
    if a.mode=='template':res=template(a.catalog,a.poi,a.lines,a.osm)
    else:
        ps,conditional=read_poi_configuration(a.poi);ls=line_mapping(a.lines)
        if a.mode=='prepare':
            out=pathlib.Path(a.out);out.mkdir(parents=True,exist_ok=True)
            make_cpp_rules(ps,out/'cpp-poi-override.csv',out/'cpp-name-labels.csv',conditional);res={'poi_types':len(ps),'name_specific_rules':len(conditional),'line_types':len(ls)}
        elif a.mode=='transform':res=transform(a.osm,a.out,ps,ls,a.report)
        else:res=inspect_counts(a.osm,ps,ls,a.out,a.strict)
    print(json.dumps(res,ensure_ascii=False,indent=2));return 0
if __name__=='__main__':sys.exit(main())
