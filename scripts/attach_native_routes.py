#!/usr/bin/env python3
"""Prefer existing physical highway ways as native hiking-route relation members.
Never invent highway=path. Only attach where exact coordinate-node edge coverage >=90%
and physical-way overrun <=10%; otherwise preserve original overlay member.
"""
import argparse,collections,json,pathlib,xml.etree.ElementTree as ET
from native_translate import line_mapping,tagdict,top_level_elements

def edges(refs):
 for a,b in zip(refs,refs[1:]):
  if a!=b:yield (a,b) if a<b else (b,a)

def match(src,dst,line_cfg,report,attach=True):
 candidates={c for c,r in line_mapping(line_cfg).items() if attach and r.get('route_mode')=='relation'}
 relation_ids=set();invalid=[]
 overlay_lengths={};lookup=collections.defaultdict(set)
 with open(src,'rb') as f:
  for e in top_level_elements(f):
   if e.tag=='way':
    t=tagdict(e)
    if t.get('garmin:type') in candidates and t.get('garmin:feature')=='extended_polyline':
     oid=int(e.get('id'))
     ns=[int(n.get('ref')) for n in e.findall('nd')]
     if len(ns)>=2:
      overlay_lengths[oid]=len(ns)-1
      for edge in edges(ns):lookup[edge].add(oid)
    e.clear()
   elif e.tag=='relation':
    rid=int(e.get('id'))
    if rid in relation_ids:raise ValueError('Duplicate relation ID: '+str(rid))
    relation_ids.add(rid)
    if rid<=0 or rid>=1<<27:invalid.append(rid)
    e.clear()
   elif e.tag=='node':e.clear()
 remap={};next_id=1
 for rid in invalid:
  while next_id in relation_ids:next_id+=1
  if next_id>=1<<27:raise ValueError('No available MapCreator relation IDs')
  remap[rid]=next_id;relation_ids.add(next_id);next_id+=1
 matches=collections.defaultdict(collections.Counter);physical_len={};written=0
 with open(src,'rb') as f,open(dst,'w',encoding='utf-8') as out:
  out.write('<?xml version="1.0" encoding="UTF-8"?>\n<osm version="0.6" generator="garmin2osm-v0.92" upload="never">\n')
  replaced=None; relations=0
  for e in top_level_elements(f):
   if e.tag=='way':
    t=tagdict(e)
    if attach and 'highway' in t:
     pid=int(e.get('id'));ns=[int(n.get('ref')) for n in e.findall('nd')]
     physical_len[pid]=max(0,len(ns)-1)
     for edge in edges(ns):
      for oid in lookup.get(edge,()):matches[oid][pid]+=1
   elif e.tag=='relation':
    if replaced is None:
     replaced={}
     for oid,counts in matches.items():
      nseg=overlay_lengths[oid]
      for pid,matched in counts.most_common():
       # Not allowed to extend a colored marking beyond its actual physical segment.
       if matched/nseg>=0.9 and physical_len.get(pid,10**9)<=nseg*1.1:
        replaced[oid]=pid;break
    rid=int(e.get('id'))
    if rid in remap:e.set('id',str(remap[rid]))
    for m in e.findall('member'):
     if m.get('type')=='relation' and int(m.get('ref')) in remap:m.set('ref',str(remap[int(m.get('ref'))]))
    tt=tagdict(e)
    if tt.get('route')=='hiking' and tt.get('bgm:source_feature')=='extended_polyline':relations+=1
    if attach and tt.get('route')=='hiking' and tt.get('bgm:source_feature')=='extended_polyline':
     seen=set()
     for m in list(e.findall('member')):
      val=int(m.get('ref'))
      ref=str(replaced.get(val,val) if m.get('type')=='way' else val)
      key=(m.get('type'),ref,m.get('role',''))
      if key in seen:e.remove(m)
      else:m.set('ref',ref);seen.add(key)
   out.write(ET.tostring(e,encoding='unicode')+'\n');written+=1;e.clear()
  out.write('</osm>\n')
  summary={'candidate_overlay_ways':len(overlay_lengths),'attached_to_existing_highway':len(replaced or {}),
   'unmatched_overlay_members':len(overlay_lengths)-len(replaced or {}),'hiking_relations':relations,'relation_ids_remapped':len(remap),'relation_id_changes':{str(k):v for k,v in remap.items()},
   'policy':'Attach only with >=90% shared physical segments and <=10% physical overrun. No invented routable ways.'}
 pathlib.Path(report).write_text(json.dumps(summary,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
 return summary

def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--normalize-only',action='store_true')
 p.add_argument('--osm',required=True);p.add_argument('--out',required=True);p.add_argument('--lines',required=True);p.add_argument('--report',required=True)
 a=p.parse_args();print(json.dumps(match(a.osm,a.out,a.lines,a.report,attach=not a.normalize_only),ensure_ascii=False,indent=2))
if __name__=='__main__':main()
