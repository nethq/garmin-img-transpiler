// garmin2osm: original Garmin geometry -> OSM XML (not raster), GPL-3.0-or-later.
// Shares the tested IMG/TRE/RGN/TYP decoders with garmin2osmand.
#define GARMIN_LIBRARY_MODE
#include "main.cpp"
#include <chrono>
#include <unordered_set>
#include <cctype>

namespace vector_export {
using Tags=std::vector<std::pair<std::string,std::string>>;
std::string xmlEscape(const std::string&s){std::string v;v.reserve(s.size());for(unsigned char c:s){switch(c){case '&':v+="&amp;";break;case '<':v+="&lt;";break;case '>':v+="&gt;";break;case '"':v+="&quot;";break;case '\'':v+="&apos;";break;default:if(c>=32||c==9||c==10||c==13)v+=char(c);}}return v;}
std::string code(int t){char b[20];std::snprintf(b,sizeof(b),"0x%x",t);return b;}
std::string hexColor(U32 c){char b[16];std::snprintf(b,sizeof(b),"#%02x%02x%02x",(c>>24)&255,(c>>16)&255,(c>>8)&255);return b;}
void tag(Tags&ts,std::string k,std::string v){if(!v.empty())ts.emplace_back(std::move(k),std::move(v));}
std::string makeTags(const Tags&tags){std::string out;for(auto&[k,v]:tags)out+="<tag k=\""+xmlEscape(k)+"\" v=\""+xmlEscape(v)+"\"/>";return out;}
struct Options{std::string input,output,temp;BBox bbox;bool clip=false,inspect=false,contours=false,allLevels=false,keepTemp=false;int limitMaps=0;};
Options parse(int argc,char**argv){Options o;for(int i=1;i<argc;i++){std::string a=argv[i];auto next=[&](){if(i+1>=argc)fail("Missing value for "+a);return std::string(argv[++i]);};
 if(a=="--help"||a=="-h"){std::cout<<"garmin2osm v0.4 --input MAP.img --output map.osm [--bbox W,S,E,N] [--inspect] [--contours] [--keep-temp]\n";std::exit(0);}else if(a=="--input")o.input=next();else if(a=="--output")o.output=next();else if(a=="--temp")o.temp=next();else if(a=="--inspect")o.inspect=true;else if(a=="--contours")o.contours=true;else if(a=="--all-levels")o.allLevels=true;else if(a=="--keep-temp")o.keepTemp=true;else if(a=="--limit-maps")o.limitMaps=std::stoi(next());else if(a=="--bbox"){std::string s=next();std::replace(s.begin(),s.end(),',',' ');double w,ss,e,n;if(std::sscanf(s.c_str(),"%lf%lf%lf%lf",&w,&ss,&e,&n)!=4)fail("Bad bbox");o.bbox={w,ss,e,n};o.clip=true;}else fail("Unknown argument: "+a);}
 if(o.input.empty())fail("--input required");if(o.output.empty()&&!o.inspect)fail("--output required");if(o.clip&&(!o.bbox.valid()||o.bbox.w< -180||o.bbox.e>180||o.bbox.s< -90||o.bbox.n>90))fail("Invalid bbox");return o;}

Tags lineTags(int t,bool ext,const std::string&name){Tags v;tag(v,"garmin:type",code(t));tag(v,"garmin:feature",ext?"extended_polyline":"polyline");tag(v,"name",name);
 if(ext){ // Physical trail identity not proven: preserve Garmin overlay, mark it as a hiking path geometry.
   tag(v,"highway","path");tag(v,"foot","yes");tag(v,"trailblazed","yes");tag(v,"garmin:route_overlay","yes");
   const auto it=gStyles.lines.find(t);if(it!=gStyles.lines.end()&&it->second.color)tag(v,"colour",hexColor(it->second.color));return v;
 }
 if(t<=0x0c){const char*klass=nullptr;
  switch(t){case 0:klass="road";break;case 1:klass="motorway";break;case 2:klass="primary";break;case 3:klass="secondary";break;case 4:klass="tertiary";break;case 5:klass="unclassified";break;case 6:klass="residential";break;case 7:klass="service";break;case 8:case 9:case 11:klass="motorway_link";break;case 10:klass="track";break;case 12:klass="residential";break;}
  if(klass)tag(v,"highway",klass);if(t==10)tag(v,"surface","unpaved");if(t==12)tag(v,"junction","roundabout");return v;}
 switch(t){case 0x0f:case 0x13:case 0x16:tag(v,"highway","path");break;
 case 0x14:tag(v,"railway","rail");break;case 0x15:tag(v,"natural","coastline");break;
 case 0x18:case 0x26:tag(v,"waterway","stream");if(t==0x26)tag(v,"intermittent","yes");break;
 case 0x1f:tag(v,"waterway","river");break;case 0x1a:case 0x1b:tag(v,"route","ferry");break;
 case 0x1c:case 0x1d:case 0x1e:tag(v,"boundary","administrative");break;
 case 0x20:case 0x21:case 0x22:case 0x23:case 0x24:case 0x25:tag(v,"contour","elevation");break;
 case 0x27:tag(v,"aeroway","runway");break;case 0x29:tag(v,"power","line");break;
 default:break;}return v;
}
Tags areaTags(int t,const std::string&name){Tags v;tag(v,"garmin:type",code(t));tag(v,"name",name);
 switch(t){case 0x01:case 0x02:case 0x03:tag(v,"landuse","residential");break;
 case 0x05:tag(v,"amenity","parking");break;case 0x07:tag(v,"aeroway","aerodrome");break;
 case 0x0b:tag(v,"amenity","hospital");break;case 0x0c:tag(v,"landuse","industrial");break;
 case 0x13:tag(v,"building","yes");break;case 0x17:tag(v,"leisure","park");break;
 case 0x18:tag(v,"leisure","golf_course");break;case 0x09:tag(v,"leisure","marina");break;
 case 0x14:case 0x15:case 0x16:tag(v,"boundary","national_park");break;
 case 0x28:case 0x32:tag(v,"natural","water");tag(v,"water","sea");break;
 case 0x3c:case 0x3d:case 0x40:case 0x41:tag(v,"natural","water");break;
 case 0x4c:tag(v,"natural","water");tag(v,"intermittent","yes");break;
 case 0x4d:tag(v,"natural","glacier");break;case 0x4e:tag(v,"landuse","orchard");break;
 case 0x4f:tag(v,"natural","scrub");break;case 0x50:tag(v,"natural","wood");break;
 case 0x51:tag(v,"natural","wetland");break;case 0x52:tag(v,"natural","grassland");break;
 default:break;}return v;
}
Tags poiTags(int t,const std::string&name){Tags v;tag(v,"garmin:type",code(t));tag(v,"garmin:feature","poi");tag(v,"name",name);
 int category=(t>>8)&255,subtype=t&255;
 if(t==0x6616){tag(v,"natural","peak");}
 else if(t==0x6412||t==0x6503){tag(v,"tourism","information");tag(v,"information","guidepost");}
 else if(t==0x650b||t==0x660b){tag(v,"tourism","information");tag(v,"information","board");}
 else if(t==0x6511||t==0x6414){tag(v,"amenity","drinking_water");}
 else if(t==0x6617){tag(v,"natural","peak");}
 else if(t==0x6609||t==0x660a||t==0x6405){tag(v,"tourism",t==0x660a?"wilderness_hut":"alpine_hut");}
 else if(t==0x6408){tag(v,"emergency","mountain_rescue");}
 else if(t==0x661e||t==0x6415){tag(v,"amenity","shelter");}
 else if(t==0x660c){tag(v,"natural","saddle");}
 else if(t==0x6613){tag(v,"waterway","waterfall");}
 else if(t==0x6407){tag(v,"tourism","picnic_site");}
 else if(t==0x640d){tag(v,"aerialway","station");}
 else if(t==0x6404||t==0x640b||t==0x641a||t==0x2c0b){tag(v,"amenity","place_of_worship");}
 else if(t==0x6602||t==0x6608){tag(v,"tourism","viewpoint");}
 else if(t==0x2800){tag(v,"place","locality");}
 else if(t==0x6600){ // Geographic spot heights; don't misclassify elevations as villages.
   try{size_t parsed=0;int n=std::stoi(name,&parsed);if(parsed==name.size()&&n>=0&&n<=8848)tag(v,"ele",std::to_string(n));}catch(...){}
 }
 else if(t==0x6614){tag(v,"garmin:marker","distance_or_index");}
 else if(t==0x6500){tag(v,"man_made","water_works");}
 else if(t==0x6402&&name.find("Посетителски")!=std::string::npos){tag(v,"tourism","information");tag(v,"information","office");}
 else if(t==0x6411){tag(v,"man_made","tower");}
 else if(t==0x6414){tag(v,"man_made","water_well");}
 else if(category==0x2b){tag(v,"tourism",subtype==5?"camp_site":"hotel");}
 else if(category==0x2f&&subtype==0x05){tag(v,"amenity","post_office");}
 else if(category==0x2d&&subtype==0x09){tag(v,"leisure","swimming_pool");}
 else if(t==0x6501){tag(v,"amenity",name.find("Ресторант")!=std::string::npos?"restaurant":"cafe");}
 // Other type 0x65/0x66 map symbols are custom to this Bulgarian TYP.
 // Preserve their Garmin type without asserting a false OSM POI category.
 // Some Bulgarian peak labels contain elevation in metres: "Мальовица (2729)".
 if(t==0x6616||t==0x6617){
  const auto a=name.rfind('('),b=name.rfind(')');
  if(a!=std::string::npos&&b==name.size()-1&&b>a+1){
   auto digits=name.substr(a+1,b-a-1);
   if(digits.size()>=2&&digits.size()<=4&&std::all_of(digits.begin(),digits.end(),[](unsigned char c){return std::isdigit(c)!=0;}))tag(v,"ele",digits);
  }
 }
 // Unknown Garmin categories retain original code, not falsely assigned to an OSM POI class.
 return v;
}

struct Store{
 sqlite3*db=nullptr;sqlite3_stmt*insNode=nullptr,*getNode=nullptr,*insPoi=nullptr,*insWay=nullptr;int64_t nodes=0,ways=0,poi=0;
 explicit Store(const std::string&path){if(sqlite3_open(path.c_str(),&db)!=SQLITE_OK)fail("Unable to open staging database");
  sql("PRAGMA journal_mode=DELETE; PRAGMA synchronous=NORMAL; PRAGMA temp_store=FILE; PRAGMA cache_size=-16000;");
  sql("CREATE TABLE nodes(id INTEGER PRIMARY KEY,lon INTEGER NOT NULL,lat INTEGER NOT NULL,tags TEXT NOT NULL DEFAULT '',UNIQUE(lon,lat));");
  sql("CREATE TABLE ways(id INTEGER PRIMARY KEY,refs TEXT NOT NULL,tags TEXT NOT NULL);");
  sql("CREATE TABLE pois(id INTEGER PRIMARY KEY,lon INTEGER NOT NULL,lat INTEGER NOT NULL,tags TEXT NOT NULL);");
  auto prepare=[&](sqlite3_stmt**st,const char*q){if(sqlite3_prepare_v2(db,q,-1,st,nullptr)!=SQLITE_OK)fail(sqlite3_errmsg(db));};
  prepare(&insNode,"INSERT OR IGNORE INTO nodes(lon,lat) VALUES(?1,?2)");
  prepare(&getNode,"SELECT id FROM nodes WHERE lon=?1 AND lat=?2");
  prepare(&insPoi,"INSERT INTO pois(id,lon,lat,tags) VALUES(?1,?2,?3,?4)");
  prepare(&insWay,"INSERT INTO ways(refs,tags) VALUES(?1,?2)");sql("BEGIN");}
 ~Store(){if(db){sqlite3_finalize(insNode);sqlite3_finalize(getNode);sqlite3_finalize(insPoi);sqlite3_finalize(insWay);sqlite3_close(db);}}
 void sql(const char*q){char*err=nullptr;int res=sqlite3_exec(db,q,nullptr,nullptr,&err);if(res!=SQLITE_OK){std::string message=err?err:"SQLite error";sqlite3_free(err);fail(message);}}
 static int32_t quant(double v){return int32_t(std::llround(v*10000000.));}
 int64_t node(double lon,double lat){if(!std::isfinite(lon)||!std::isfinite(lat)||lon< -180||lon>180||lat< -90||lat>90)return 0;
  int32_t x=quant(lon),y=quant(lat);sqlite3_reset(insNode);sqlite3_clear_bindings(insNode);sqlite3_bind_int(insNode,1,x);sqlite3_bind_int(insNode,2,y);if(sqlite3_step(insNode)!=SQLITE_DONE)fail(sqlite3_errmsg(db));
  sqlite3_reset(getNode);sqlite3_clear_bindings(getNode);sqlite3_bind_int(getNode,1,x);sqlite3_bind_int(getNode,2,y);
  if(sqlite3_step(getNode)!=SQLITE_ROW)fail("Failed to fetch OSM node");int64_t id=sqlite3_column_int64(getNode,0);sqlite3_reset(getNode);return id;
 }
 void addPoi(double lon,double lat,const Tags&t){
  if(!std::isfinite(lon)||!std::isfinite(lat)||std::abs(lon)>180||std::abs(lat)>90)return;
  // POIs are separate OSM nodes, even when co-located with a road vertex or another POI.
  // This avoids merging distinct POI identities into an intersection node.
  const auto text=makeTags(t);sqlite3_reset(insPoi);sqlite3_clear_bindings(insPoi);
  sqlite3_bind_int64(insPoi,1,1000000000000LL+(++poi));
  sqlite3_bind_int(insPoi,2,quant(lon));sqlite3_bind_int(insPoi,3,quant(lat));
  sqlite3_bind_text(insPoi,4,text.c_str(),-1,SQLITE_TRANSIENT);
  if(sqlite3_step(insPoi)!=SQLITE_DONE)fail(sqlite3_errmsg(db));
 }
 void way(std::vector<std::pair<double,double>>&pts,bool closed,const Tags&tags){if(pts.size()<(closed?3u:2u)||tags.size()<2)return;
  // Suppress malformed geographic coordinates rather than silently connecting disjoint valid vertices.
  for(const auto&p:pts)if(!std::isfinite(p.first)||!std::isfinite(p.second)||std::abs(p.first)>180||std::abs(p.second)>90)return;
  // Ignore consecutive coordinate duplicates, but retain all true Garmin vertices.
  std::vector<int64_t>ids;ids.reserve(pts.size()+1);for(const auto&p:pts){auto id=node(p.first,p.second);if(id && (ids.empty()||ids.back()!=id))ids.push_back(id);}
  if(closed&&ids.size()>=3&&ids.front()!=ids.back())ids.push_back(ids.front());
  if(ids.size()<(closed?4u:2u))return;
  std::ostringstream ss;for(auto i:ids){ss<<"<nd ref=\""<<i<<"\"/>";}
  auto refs=ss.str(),txt=makeTags(tags);
  sqlite3_reset(insWay);sqlite3_clear_bindings(insWay);sqlite3_bind_text(insWay,1,refs.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(insWay,2,txt.c_str(),-1,SQLITE_TRANSIENT);
  if(sqlite3_step(insWay)!=SQLITE_DONE)fail(sqlite3_errmsg(db));ways++;
 }
 void commit(){sql("COMMIT");sql("BEGIN");}
 void output(const std::string&file){sql("COMMIT");std::ofstream out(file,std::ios::binary);if(!out)fail("Cannot write "+file);
  out<<"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<osm version=\"0.6\" generator=\"garmin2osm-cpp-0.4\" upload=\"never\">\n";
  sqlite3_stmt*st=nullptr;
  if(sqlite3_prepare_v2(db,"SELECT id,lat,lon,tags FROM nodes ORDER BY id",-1,&st,nullptr)!=SQLITE_OK)fail(sqlite3_errmsg(db));
  while(sqlite3_step(st)==SQLITE_ROW){auto id=sqlite3_column_int64(st,0);double lat=sqlite3_column_int(st,1)/10000000.,lon=sqlite3_column_int(st,2)/10000000.;const char*tags=(const char*)sqlite3_column_text(st,3);
   out<<"<node id=\""<<id<<"\" version=\"1\" lat=\""<<std::fixed<<std::setprecision(7)<<lat<<"\" lon=\""<<lon<<"\">"<<(tags?tags:"")<<"</node>\n";}
  sqlite3_finalize(st);st=nullptr;
  if(sqlite3_prepare_v2(db,"SELECT id,lat,lon,tags FROM pois ORDER BY id",-1,&st,nullptr)!=SQLITE_OK)fail(sqlite3_errmsg(db));
  while(sqlite3_step(st)==SQLITE_ROW){
   out<<"<node id=\""<<sqlite3_column_int64(st,0)<<"\" version=\"1\" lat=\""<<std::fixed<<std::setprecision(7)
      <<sqlite3_column_int(st,1)/10000000.<<"\" lon=\""<<sqlite3_column_int(st,2)/10000000.<<"\">"
      <<(const char*)sqlite3_column_text(st,3)<<"</node>\n";
  }
  sqlite3_finalize(st);st=nullptr;
  if(sqlite3_prepare_v2(db,"SELECT id,refs,tags FROM ways ORDER BY id",-1,&st,nullptr)!=SQLITE_OK)fail(sqlite3_errmsg(db));
  while(sqlite3_step(st)==SQLITE_ROW){out<<"<way id=\""<<sqlite3_column_int64(st,0)<<"\" version=\"1\">"<<(const char*)sqlite3_column_text(st,1)<<(const char*)sqlite3_column_text(st,2)<<"</way>\n";}
  sqlite3_finalize(st);out<<"</osm>\n";out.close();if(!out)fail("Output write failed");
 }
};

struct Stats{std::map<std::pair<int,int>,uint64_t> byType;uint64_t lines=0,areas=0,points=0,skipped=0;};
void processMap(const Map&m,Store*db,Stats&stats,const Options&o){
 const auto&r=m.sections.at("RGN");
 int finest=-1,routeBpc=-1;uint64_t largest=0;std::map<int,uint64_t> extBytes;
 for(const auto&lev:m.levels)finest=std::max(finest,lev.bits);
 for(const auto&s:m.subs)extBytes[s.bits]+=s.extLen[1];
 for(auto [b,n]:extBytes)if(n>largest){largest=n;routeBpc=b;}
 if(routeBpc<0)routeBpc=finest;
 std::cerr<<"  map "<<m.name<<" finest="<<finest<<" extended-route-bpc="<<routeBpc<<" ";
 for(auto [b,n]:extBytes)if(n)std::cerr<<b<<":"<<n/1024<<"KiB ";std::cerr<<"\n";
 for(size_t index=0;index<m.subs.size();++index){const auto&s=m.subs[index];if(o.clip&&!s.bounds.overlaps(o.bbox))continue;
  const bool classic=(o.allLevels||s.bits==finest),extended=(o.allLevels||s.bits==routeBpc);
  if(!classic&&!extended)continue;
  auto record=[&](bool line,bool ext, bool polygon, int ty, std::vector<std::pair<double,double>>&ps,const std::string&name){
   if(line&&!o.contours&&ty>=0x20&&ty<=0x25&&!ext)return;
   stats.byType[{ext?2:(polygon?1:0),ty}]++;
   if(polygon){stats.areas++;if(db)db->way(ps,true,areaTags(ty,name));}
   else{stats.lines++;if(db)db->way(ps,false,lineTags(ty,ext,name));}
  };
  auto parsePoly=[&](const std::vector<U8>&buf,size_t from,size_t to,bool line,bool ext){
   if(from>=to||to>buf.size())return;size_t off=from;int guard=0;
   while(off<to&&guard++<100000){int ty=0;size_t used=0;const U8*p=buf.data()+off;
    auto coords=decodePoly(p,to-off,s.cx,s.cy,s.shift,line,ext,ty,used);
    if(!used){stats.skipped++;break;}off+=used;
    if(coords.size()<(line?2u:3u))continue;
    std::string name;
    if(!ext&&used>=4){U32 lbl=u24(p+1);name=(lbl&0x800000)?m.netLabel(lbl&0x3fffff):m.label(lbl&0x3fffff);}
    if(ext){ // Garmin RGN2 V2 label resides at end of bitstream when subtype bit 0x20 is set.
      if((p[1]&0x20)&&used>=3){U32 lbl=u24(p+used-3);name=m.label(lbl&0x3fffff);}
    }
    record(line,ext,!line,ty,coords,name);
   }
  };
  if(classic&&s.to>=s.from&&s.to-s.from<=8*1024*1024){
    auto buf=r.get(s.from,s.to-s.from);int n=bool(s.flags&0x10)+bool(s.flags&0x20)+bool(s.flags&0x40)+bool(s.flags&0x80);
    if(n&&buf.size()>=size_t((n-1)*2)){
      std::array<size_t,4> start{};size_t p=0,last=std::max(0,(n-1)*2);
      for(int k=0;k<4;k++)if(s.flags&(0x10<<k)){size_t at=(p==0?last:u16(buf.data()+2*(p-1)));start[size_t(k)]=at;p++;}
      bool valid=true;size_t previous=0;for(int k=0;k<4;k++)if(s.flags&(0x10<<k)){if(start[size_t(k)]<previous||start[size_t(k)]>buf.size())valid=false;previous=start[size_t(k)];}
      if(valid){
       for(int k=0;k<2;k++)if(s.flags&(0x10<<k)){
        size_t begin=start[k],end=buf.size();for(int kk=k+1;kk<4;kk++)if(s.flags&(0x10<<kk)){end=start[kk];break;}
        for(size_t j=begin;j+8<=end;){const U8*q=buf.data()+j;U32 inf=u24(q+1);size_t len=(inf&0x800000)?9:8;if(j+len>end)break;
         int typ=(int(q[0])<<8)|((inf&0x800000)?q[8]:0);
         double lon=geo(s.cx+s16(q+4)*(1<<s.shift)),lat=geo(s.cy+s16(q+6)*(1<<s.shift));
         if(!o.clip || (lon>=o.bbox.w&&lon<=o.bbox.e&&lat>=o.bbox.s&&lat<=o.bbox.n)){
          std::string name=(inf&0x400000)?m.poiLabel(inf&0x3fffff):m.label(inf&0x3fffff);stats.byType[{3,typ}]++;stats.points++;
          if(db)db->addPoi(lon,lat,poiTags(typ,name));}
         j+=len;
        }
       }
       if(s.flags&0x40)parsePoly(buf,start[2],(s.flags&0x80)?start[3]:buf.size(),true,false);
       if(s.flags&0x80)parsePoly(buf,start[3],buf.size(),false,false);
      }
    }
  }
  if(extended){for(int k=0;k<3;k++){
    if(!s.extLen[k]||s.extLen[k]>8*1024*1024)continue;
    auto ext=r.get(s.extFrom[k],s.extLen[k]);
    if(k<2)parsePoly(ext,0,ext.size(),k==1,true);
    else for(size_t at=0;at+6<=ext.size();){const U8*p=ext.data()+at;int sub=p[1];size_t len=6+bool(sub&0x80)+3*bool(sub&0x20);if(at+len>ext.size())break;
      int typ=0x10000+(int(p[0])<<8)+(sub&31);
      double lon=geo(s.cx+s16(p+2)*(1<<s.shift)),lat=geo(s.cy+s16(p+4)*(1<<s.shift));
      if(!o.clip || (lon>=o.bbox.w&&lon<=o.bbox.e&&lat>=o.bbox.s&&lat<=o.bbox.n)){
       std::string name; if(sub&0x20){U32 inf=u24(p+len-3);name=(inf&0x400000)?m.poiLabel(inf&0x3fffff):m.label(inf&0x3fffff);}
       stats.byType[{3,typ}]++;stats.points++;if(db)db->addPoi(lon,lat,poiTags(typ,name));}
      at+=len;}
   }}
  if(db&&(index%60==0))db->commit();
 }
 if(db)db->commit();
}
int run(int argc,char**argv){auto o=parse(argc,argv);MapSet set(o.input);
 for(const auto&[n,parts]:set.parts){auto it=parts.find("TYP");if(it!=parts.end()){gStyles.load(it->second);break;}}
 std::cerr<<"Maps="<<set.maps.size()<<" TYP line="<<gStyles.parsed<<" patterns="<<gStyles.patterns<<" icons="<<gStyles.pointStyles<<"\n";
 std::string temp=o.temp.empty()?(o.output+".stage.sqlite"):o.temp;
 if(!o.inspect&&fs::exists(temp))fail("Staging database exists: remove old file, or choose --temp newfile");
 if(!o.inspect&&fs::exists(o.output))fail("Output already exists; refuse to overwrite");
 std::unique_ptr<Store>store;if(!o.inspect)store=std::make_unique<Store>(temp);
 Stats total;int k=0;for(const auto&m:set.maps){if(o.clip&&!m.bounds.overlaps(o.bbox))continue;if(o.limitMaps>0&&k>=o.limitMaps)break;
  processMap(m,store.get(),total,o);k++;if(k%10==0)std::cerr<<"Processed "<<k<<" maps, ways="<<total.lines+total.areas<<" points="<<total.points<<"\n";}
 std::cerr<<"Finished scan maps="<<k<<" lines="<<total.lines<<" polygons="<<total.areas<<" points="<<total.points<<" parseStops="<<total.skipped<<"\n";
 std::vector<std::pair<std::pair<int,int>,uint64_t>>hist(total.byType.begin(),total.byType.end());std::sort(hist.begin(),hist.end(),[](const auto&a,const auto&b){return a.second>b.second;});
 std::cerr<<"Top feature types (category 0=classic lines, 1=areas, 2=ext lines, 3=POIs):\n";
 for(size_t i=0;i<std::min<size_t>(60,hist.size());i++)std::cerr<<hist[i].first.first<<":"<<code(hist[i].first.second)<<"="<<hist[i].second<<" ";std::cerr<<"\n";
 std::cerr<<"Extended trail styles observed:\n";
 for(const auto&[key,cnt]:hist){if(key.first!=2)continue;auto it=gStyles.lines.find(key.second);
  std::cerr<<"  "<<code(key.second)<<" count="<<cnt;
  if(it!=gStyles.lines.end())std::cerr<<" colour="<<hexColor(it->second.color)<<" pattern="<<(it->second.hasPattern()?"yes":"no");
  std::cerr<<"\n";
 }
 if(store){store->output(o.output);std::cerr<<"XML done: nodes="<<sqlite3_last_insert_rowid(store->db)<<" ways="<<store->ways<<" POIs="<<store->poi<<" => "<<o.output<<"\n";store.reset();if(!o.keepTemp)fs::remove(temp);}
 return 0;
}
} // namespace vector_export
int main(int argc,char**argv){try{return vector_export::run(argc,argv);}catch(const std::exception&e){std::cerr<<"ERROR: "<<e.what()<<"\n";return 1;}}
