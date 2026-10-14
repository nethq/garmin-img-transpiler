// garmin2osm: original Garmin geometry -> OSM XML (not raster), GPL-3.0-or-later.
// Shares the tested IMG/TRE/RGN/TYP decoders with garmin2osmand.
#define GARMIN_LIBRARY_MODE
#include "main.cpp"
#include <chrono>
#include <unordered_set>
#include <cctype>
#include <atomic>
#include <thread>
#include <mutex>
#include <optional>

namespace vector_export {
using Tags=std::vector<std::pair<std::string,std::string>>;
std::string xmlEscape(const std::string&s){std::string v;v.reserve(s.size());for(unsigned char c:s){switch(c){case '&':v+="&amp;";break;case '<':v+="&lt;";break;case '>':v+="&gt;";break;case '"':v+="&quot;";break;case '\'':v+="&apos;";break;default:if(c>=32||c==9||c==10||c==13)v+=char(c);}}return v;}
std::string code(int t){char b[20];std::snprintf(b,sizeof(b),"0x%x",t);return b;}
std::string hexColor(U32 c){char b[16];std::snprintf(b,sizeof(b),"#%02x%02x%02x",(c>>24)&255,(c>>16)&255,(c>>8)&255);return b;}
void tag(Tags&ts,std::string k,std::string v){if(!v.empty())ts.emplace_back(std::move(k),std::move(v));}
std::string makeTags(const Tags&tags){std::string out;for(auto&[k,v]:tags)out+="<tag k=\""+xmlEscape(k)+"\" v=\""+xmlEscape(v)+"\"/>";return out;}
struct Options{std::string input,output,temp,poiOverrides,poiTypesOut,poiManifest,iconsDir,poiAdditions,poiNameFixes;BBox bbox;bool clip=false,inspect=false,contours=false,allLevels=false,keepTemp=false;int limitMaps=0,jobs=1,examples=5;};
Options parse(int argc,char**argv){Options o;for(int i=1;i<argc;i++){std::string a=argv[i];auto next=[&](){if(i+1>=argc)fail("Missing value for "+a);return std::string(argv[++i]);};
 if(a=="--help"||a=="-h"){std::cout<<"garmin2osm v0.6 --input MAP.img [--output map.osm] [--inspect] [--poi-overrides rules.csv] [--poi-name-fixes fixes.csv] [--poi-additions additions.csv] [--poi-types-out unique-poi-types.csv] [--poi-manifest source-pois.csv] [--poi-examples N] [--icons-dir path] [--jobs N] [--bbox W,S,E,N] [--contours] [--keep-temp]\n";std::exit(0);}else if(a=="--input")o.input=next();else if(a=="--output")o.output=next();else if(a=="--temp")o.temp=next();else if(a=="--inspect")o.inspect=true;else if(a=="--contours")o.contours=true;else if(a=="--all-levels")o.allLevels=true;else if(a=="--keep-temp")o.keepTemp=true;else if(a=="--limit-maps")o.limitMaps=std::stoi(next());else if(a=="--poi-overrides")o.poiOverrides=next();else if(a=="--poi-name-fixes")o.poiNameFixes=next();else if(a=="--poi-additions")o.poiAdditions=next();else if(a=="--poi-types-out")o.poiTypesOut=next();else if(a=="--poi-manifest")o.poiManifest=next();else if(a=="--icons-dir")o.iconsDir=next();else if(a=="--poi-examples")o.examples=std::stoi(next());else if(a=="--jobs")o.jobs=std::stoi(next());else if(a=="--bbox"){std::string s=next();std::replace(s.begin(),s.end(),',',' ');double w,ss,e,n;if(std::sscanf(s.c_str(),"%lf%lf%lf%lf",&w,&ss,&e,&n)!=4)fail("Bad bbox");o.bbox={w,ss,e,n};o.clip=true;}else fail("Unknown argument: "+a);}
 if(o.input.empty())fail("--input required");if(o.output.empty()&&!o.inspect&&o.poiTypesOut.empty()&&o.poiManifest.empty()&&o.iconsDir.empty())fail("--output, --inspect or an inventory output required");if(!o.poiAdditions.empty()&&o.output.empty())fail("--poi-additions requires --output");if(o.jobs<1||o.jobs>64)fail("--jobs must be 1..64");if(o.examples<1||o.examples>20)fail("--poi-examples must be 1..20");if(o.clip&&(!o.bbox.valid()||o.bbox.w< -180||o.bbox.e>180||o.bbox.s< -90||o.bbox.n>90))fail("Invalid bbox");return o;}

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
 // 0x6414 handled above as drinking_water: conflicting old water_well branch removed.
 // Do not infer well vs spring without TYP icon or field observation.
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

 // BGMountains v0.6: the published legend is authoritative for types with an
 // extracted TYP bitmap match. Unknown codes remain unclassified, never guessed.
 static const std::unordered_map<int,std::pair<std::string,std::string>> legendVerified = {
  {0x200,{"capital_city","place=city;capital=yes"}},
  {0x800,{"large_city","place=city"}},
  {0x900,{"town","place=town"}},
  {0x2c05,{"school","amenity=school"}},
  {0x2c0b,{"church","amenity=place_of_worship"}},
  {0x2d04,{"casino","amenity=casino"}},
  {0x2d08,{"ice_rink","leisure=ice_rink"}},
  {0x2e05,{"pharmacy","amenity=pharmacy"}},
  {0x2e07,{"clothes_shop","shop=clothes"}},
  {0x2f0a,{"synagogue","amenity=place_of_worship;religion=jewish"}},
  {0x2f0c,{"mosque","amenity=place_of_worship;religion=muslim"}},
  {0x2f0e,{"car_wash","amenity=car_wash"}},
  {0x3004,{"courthouse","amenity=courthouse"}},
  {0x3008,{"fire_station","amenity=fire_station"}},
  {0x4700,{"port","industrial=port"}},
  {0x6400,{"quarry","landuse=quarry"}},
  {0x6401,{"bridge","man_made=bridge"}},
  {0x6402,{"building","building=yes"}},
  {0x6403,{"graveyard_tombstone",""}},
  {0x6404,{"church","amenity=place_of_worship"}},
  {0x6405,{"hunting_lodge",""}},
  {0x6406,{"barrier_gate","barrier=gate"}},
  {0x6407,{"picnic_site","tourism=picnic_site"}},
  {0x6408,{"mountain_rescue","emergency=mountain_rescue"}},
  {0x6409,{"monument","historic=monument"}},
  {0x640a,{"danger",""}},
  {0x640b,{"chapel","amenity=place_of_worship"}},
  {0x640c,{"mine_shaft","man_made=mine_shaft"}},
  {0x640d,{"lift",""}},
  {0x640e,{"bus_stop_station","highway=bus_stop"}},
  {0x640f,{"graveyard_tombstone",""}},
  {0x6411,{"tower","man_made=tower"}},
  {0x6412,{"trailhead","highway=trailhead"}},
  {0x6413,{"tunnel",""}},
  {0x6414,{"drinking_water","amenity=drinking_water"}},
  {0x6415,{"camp_site","tourism=camp_site"}},
  {0x6416,{"ruins","historic=ruins"}},
  {0x6417,{"well","man_made=water_well"}},
  {0x641a,{"church","amenity=place_of_worship"}},
  {0x641b,{"church","amenity=place_of_worship"}},
  {0x641c,{"bus_stop_station","highway=bus_stop"}},
  {0x6500,{"reservoir",""}},
  {0x6501,{"restaurant","amenity=restaurant"}},
  {0x6502,{"river_passage",""}},
  {0x6503,{"crossroad",""}},
  {0x6505,{"shop","shop=yes"}},
  {0x6506,{"museum","tourism=museum"}},
  {0x650b,{"winter_marking",""}},
  {0x650c,{"dead_end","noexit=yes"}},
  {0x6511,{"spring","natural=spring"}},
  {0x6600,{"triangulation",""}},
  {0x6601,{"natural_phenomenon",""}},
  {0x6602,{"viewpoint","tourism=viewpoint"}},
  {0x6603,{"triangulation","man_made=survey_point"}},
  {0x6608,{"hotel","tourism=hotel"}},
  {0x6609,{"hut","tourism=alpine_hut"}},
  {0x660a,{"refuge","amenity=shelter;shelter_type=basic_hut"}},
  {0x660b,{"mark",""}},
  {0x6610,{"meadow",""}},
  {0x6613,{"waterfall","waterway=waterfall"}},
  {0x6614,{"rock",""}},
  {0x6615,{"cave","natural=cave_entrance"}},
  {0x6617,{"peak","natural=peak"}},
  {0x6618,{"tree","natural=tree"}},
  {0x6619,{"prehistoric_site","historic=archaeological_site"}},
  {0x661a,{"antiquity_site","historic=archaeological_site"}},
  {0x661b,{"medieval_site","historic=archaeological_site"}},
 };
 const auto hit=legendVerified.find(t);
 if(hit!=legendVerified.end()){
   Tags corrected;
   for(const auto&kv:v){
     if(kv.first=="garmin:type"||kv.first=="garmin:feature"||kv.first=="name" ||
        (t==0x6600 && kv.first=="ele"))corrected.push_back(kv);
   }
   tag(corrected,"garmin:legend",hit->second.first);
   std::stringstream rules(hit->second.second);std::string item;
   while(std::getline(rules,item,';')){
     auto eq=item.find('=');if(eq!=std::string::npos&&eq>0)
       tag(corrected,item.substr(0,eq),item.substr(eq+1));
   }
   return corrected;
 }
 return v;
}


// User-supplied CSV mapping: Garmin type, optional name substring, mode, OSM tags, note.
// Every field follows RFC4180-style CSV quoting; mode=replace clears only semantic defaults,
// retaining type/name/mapset and an optional coordinate-derived elevation.
std::string csvQuote(const std::string&s){std::string r="\"";for(char c:s){if(c=='\"')r+="\"\"";else if(c!='\r'&&c!='\n')r+=c;else r+=' ';}return r+"\"";}
std::vector<std::string> csvParse(const std::string&line){
 std::vector<std::string>v;std::string cur;bool quoted=false;
 for(size_t j=0;j<line.size();j++){char c=line[j];if(c=='\"'){if(quoted&&j+1<line.size()&&line[j+1]=='\"'){cur+='\"';j++;}else quoted=!quoted;}
  else if(c==','&&!quoted){v.push_back(cur);cur.clear();}else cur+=c;}
 if(quoted)fail("Unterminated CSV quotation");v.push_back(cur);return v;
}
struct PoiOverride{int type=0;std::string nameContains,mode,note;Tags tags;};
std::unordered_map<int,std::vector<PoiOverride>> overrides;
int parseType(const std::string&s){size_t n=0;int t=std::stoi(s,&n,0);if(n!=s.size()||t<0||t>0x1ffff)fail("Invalid Garmin type: "+s);return t;}
void loadOverrides(const std::string&file){if(file.empty())return;std::ifstream in(file);if(!in)fail("Cannot open POI override file: "+file);
 std::string line;int n=0;while(std::getline(in,line)){n++;if(!line.empty()&&line.back()=='\r')line.pop_back();if(line.empty()||line[0]=='#')continue;
  auto c=csvParse(line);if(c[0]=="code")continue;if(c.size()!=5)fail("Expected 5 CSV columns at "+file+":"+std::to_string(n));
  PoiOverride rule;try{rule.type=parseType(c[0]);}catch(...){fail("Bad code at "+file+":"+std::to_string(n));}
  rule.nameContains=c[1];rule.mode=c[2];rule.note=c[4];
  if(rule.mode!="replace"&&rule.mode!="append"&&rule.mode!="keep_raw"&&rule.mode!="drop")fail("Bad mode at line "+std::to_string(n));
  std::stringstream kvs(c[3]);std::string item;std::set<std::string> keys;
  while(std::getline(kvs,item,';')){if(item.empty())continue;auto eq=item.find('=');if(eq==std::string::npos||eq==0)fail("Bad tag in override line "+std::to_string(n));auto k=item.substr(0,eq),v=item.substr(eq+1);if(keys.count(k))fail("Duplicate override tag: "+k);keys.insert(k);
   if(k=="garmin:type"||k=="garmin:feature"||k=="garmin:mapset"||k=="name")fail("Cannot override provenance field "+k);rule.tags.emplace_back(k,v);}
  overrides[rule.type].push_back(std::move(rule));}
 size_t nRules=0;for(auto&kv:overrides)nRules+=kv.second.size();std::cerr<<"Loaded "<<nRules<<" POI override rules from "<<file<<"\n";
}
// Coordinate-specific name fixes are deliberately opt-in and never inferred from
// similar-looking strings. A wrong-coordinate phantom POI must NOT be renamed.
using NameFixKey=std::tuple<std::string,int,int32_t,int32_t,std::string>;
std::map<NameFixKey,std::string> nameFixes;
void loadNameFixes(const std::string&file){if(file.empty())return;
 std::ifstream in(file);if(!in)fail("Cannot open name corrections file: "+file);
 std::string line;size_t n=0;while(std::getline(in,line)){
  ++n;if(!line.empty()&&line.back()=='\r')line.pop_back();
  if(line.empty()||line[0]=='#')continue;
  auto col=csvParse(line);if(col.size()!=7)fail("Name fixes: expected 7 columns at line "+std::to_string(n));
  if(col[0]=="mapset")continue;
  int t=parseType(col[1]);double lat=std::stod(col[2]),lon=std::stod(col[3]);
  if(!std::isfinite(lat)||!std::isfinite(lon)||std::abs(lat)>90||std::abs(lon)>180)
   fail("Bad name-fix coordinates at line "+std::to_string(n));
  if(col[0].empty()||col[5].empty())fail("Name fix needs mapset and replacement at line "+std::to_string(n));
  NameFixKey key{col[0],t,int32_t(std::llround(lat*1e7)),int32_t(std::llround(lon*1e7)),col[4]};
  if(!nameFixes.emplace(key,col[5]).second)fail("Duplicate name-fix row at line "+std::to_string(n));
 }
 std::cerr<<"Loaded "<<nameFixes.size()<<" coordinate-specific POI name fixes\n";
}
std::string resolveNameFix(const std::string&map,int type,double lat,double lon,const std::string&old){
 auto it=nameFixes.find({map,type,int32_t(std::llround(lat*1e7)),int32_t(std::llround(lon*1e7)),old});
 return it==nameFixes.end()?old:it->second;
}

std::optional<Tags> configuredPoi(int type,const std::string&name){
 Tags result=poiTags(type,name);
 auto hit=overrides.find(type);if(hit==overrides.end())return result;
 for(const auto&rule:hit->second){if(!rule.nameContains.empty()&&name.find(rule.nameContains)==std::string::npos)continue;
  if(rule.mode=="drop")return std::nullopt;
  if(rule.mode=="replace"||rule.mode=="keep_raw"){
   result.erase(std::remove_if(result.begin(),result.end(),[](const auto&kv){return kv.first!="garmin:type"&&kv.first!="garmin:feature"&&kv.first!="name";}),result.end());}
  for(const auto&[k,v]:rule.tags){result.erase(std::remove_if(result.begin(),result.end(),[&](const auto&kv){return kv.first==k;}),result.end());if(!v.empty())result.emplace_back(k,v);}
 }
 return result;
}
struct Sample{std::string map,name;double lat=0,lon=0;};
struct PoiTypeInfo{uint64_t count=0,named=0;std::vector<Sample>examples;};
// Deterministic nationwide samples, favoring named points; no "first five" local bias.
uint64_t sampleHash(const Sample&s){
 std::ostringstream os;os<<s.map<<"/"<<std::fixed<<std::setprecision(7)<<s.lat<<"/"<<s.lon<<"/"<<s.name;
 uint64_t h=14695981039346656037ULL;for(unsigned char c:os.str()){h^=c;h*=1099511628211ULL;}return h;
}
void addSample(PoiTypeInfo&info,const Sample&sample,int maxExamples){
 for(auto&prev:info.examples)if(std::abs(prev.lat-sample.lat)<0.00000005&&std::abs(prev.lon-sample.lon)<0.00000005&&prev.name==sample.name)return;
 info.examples.push_back(sample);
 std::sort(info.examples.begin(),info.examples.end(),[](const Sample&a,const Sample&b){
  if(a.name.empty()!=b.name.empty())return !a.name.empty();
  auto x=sampleHash(a),y=sampleHash(b);return x==y?a.map<b.map:x<y;
 });
 if((int)info.examples.size()>maxExamples)info.examples.resize(maxExamples);
}
std::string semanticTags(int type){const auto t=poiTags(type,"");std::string out;for(const auto&[k,v]:t)if(k!="garmin:type"&&k!="garmin:feature"&&k!="name"){if(!out.empty())out+=';';out+=k+"="+v;}return out;}
void writePoiTypes(const std::string&file,const std::map<int,PoiTypeInfo>& types,int n){
 if(file.empty())return;std::ofstream out(file);if(!out)fail("Cannot write "+file);
 out<<"code,kind,count,named,default_tags,has_typ_icon,configured_rules";
 for(int i=1;i<=n;i++)out<<",example"<<i<<"_lat,example"<<i<<"_lon,example"<<i<<"_name,example"<<i<<"_map";out<<"\n";
 for(auto&[type,entry]:types){int rules=0;auto it=overrides.find(type);if(it!=overrides.end())rules=(int)it->second.size();
  out<<csvQuote(code(type))<<","<<(type>=0x10000?"extended":"classic")<<","<<entry.count<<","<<entry.named<<","<<csvQuote(semanticTags(type))<<","<<(gStyles.icons.count(type)?"yes":"no")<<","<<rules;
  for(int i=0;i<n;i++){if(i<(int)entry.examples.size()){
    auto&s=entry.examples[i];out<<","<<std::fixed<<std::setprecision(7)<<std::llround(s.lat*10000000.)/10000000.<<","<<std::llround(s.lon*10000000.)/10000000.<<","<<csvQuote(s.name)<<","<<csvQuote(s.map);
   }else out<<",,,,";}out<<"\n";
 }
 if(!out)fail("Cannot complete type CSV");std::cerr<<"POI unique-type catalog: "<<types.size()<<" types -> "<<file<<"\n";
}
void saveIcons(const std::string&dir){if(dir.empty())return;fs::create_directories(dir);
 for(const auto&[type,ico]:gStyles.icons){std::string path=(fs::path(dir)/(code(type)+".png")).string();FILE*f=std::fopen(path.c_str(),"wb");if(!f)fail("Cannot open icon "+path);
  png_structp png=png_create_write_struct(PNG_LIBPNG_VER_STRING,nullptr,nullptr,nullptr);png_infop info=png_create_info_struct(png);
  if(!png||!info){std::fclose(f);fail("PNG alloc failed");}
  if(setjmp(png_jmpbuf(png))){png_destroy_write_struct(&png,&info);std::fclose(f);fail("PNG write failed: "+path);}
  png_init_io(png,f);png_set_IHDR(png,info,ico.w,ico.h,8,PNG_COLOR_TYPE_RGBA,PNG_INTERLACE_NONE,PNG_COMPRESSION_TYPE_DEFAULT,PNG_FILTER_TYPE_DEFAULT);
  png_write_info(png,info);std::vector<U8> img(size_t(ico.w)*ico.h*4);for(size_t i=0;i<ico.pixels.size();i++){U32 p=ico.pixels[i];img[i*4]=p>>24;img[i*4+1]=p>>16;img[i*4+2]=p>>8;img[i*4+3]=p;}
  std::vector<png_bytep> rows(ico.h);for(int y=0;y<ico.h;y++)rows[y]=img.data()+size_t(y)*ico.w*4;
  png_write_image(png,rows.data());png_write_end(png,nullptr);png_destroy_write_struct(&png,&info);std::fclose(f);
 }
 std::cerr<<"Exported "<<gStyles.icons.size()<<" TYP icons to "<<dir<<"\n";
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
  out<<"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<osm version=\"0.6\" generator=\"garmin2osm-cpp-0.6\" upload=\"never\">\n";
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

struct Stats{std::map<std::pair<int,int>,uint64_t> byType;uint64_t lines=0,areas=0,points=0,skipped=0,extendedOutsideBounds=0;std::map<int,PoiTypeInfo> poiTypes;};
void processMap(const Map&m,Store*db,Stats&stats,const Options&o,std::ostream*poiManifest=nullptr){
 const auto&r=m.sections.at("RGN");
 auto recordPoi=[&](int typ,double lon,double lat,const std::string&rawName,bool extended){
    const std::string name=resolveNameFix(m.name,typ,lat,lon,rawName);
    stats.byType[{3,typ}]++;stats.points++;
    auto&info=stats.poiTypes[typ];info.count++;if(!name.empty())info.named++;
    addSample(info,{m.name,name,lat,lon},o.examples);
    auto tags=configuredPoi(typ,name);
    if(poiManifest)(*poiManifest)<<csvQuote(m.name)<<","<<csvQuote(extended?"extended":"classic")<<","<<csvQuote(code(typ))<<","<<std::fixed<<std::setprecision(7)<<Store::quant(lat)/10000000.<<","<<Store::quant(lon)/10000000.<<","<<csvQuote(name)<<"\n";
    if(tags&&db){tag(*tags,"garmin:mapset",m.name);db->addPoi(lon,lat,*tags);}
 };
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
          std::string name=(inf&0x400000)?m.poiLabel(inf&0x3fffff):m.label(inf&0x3fffff);recordPoi(typ,lon,lat,name,false);}
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
      // Avoid malformed extended records that escape their actual TRE subdivision.
      // A small ~5-metre tolerance accommodates quantization at Garmin zoom levels.
      if(lon < s.bounds.w-0.00005 || lon > s.bounds.e+0.00005 ||
         lat < s.bounds.s-0.00005 || lat > s.bounds.n+0.00005){stats.extendedOutsideBounds++;at+=len;continue;}
      if(!o.clip || (lon>=o.bbox.w&&lon<=o.bbox.e&&lat>=o.bbox.s&&lat<=o.bbox.n)){
       std::string name; if(sub&0x20){U32 inf=u24(p+6);name=(inf&0x400000)?m.poiLabel(inf&0x3fffff):m.label(inf&0x3fffff);}
       recordPoi(typ,lon,lat,name,true);}
      at+=len;}
   }}
  if(db&&(index%60==0))db->commit();
 }
 if(db)db->commit();
}

// Large parallel jobs write a separate SQLite stage per worker, then stream all OSM
// nodes before all OSM ways. Node IDs are offset by 10^10 per worker, including refs.
void outputStages(const std::string&outFile,const std::vector<std::string>&paths){
 std::ofstream out(outFile,std::ios::binary);if(!out)fail("Cannot open "+outFile);
 out<<"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<osm version=\"0.6\" generator=\"garmin2osm-cpp-0.6\" upload=\"never\">\n";
 auto walk=[&](int pass){for(size_t k=0;k<paths.size();k++){
   sqlite3*db=nullptr;if(sqlite3_open_v2(paths[k].c_str(),&db,SQLITE_OPEN_READONLY,nullptr)!=SQLITE_OK)fail("Cannot read parallel stage");
   const int64_t delta=int64_t(k)*10000000000LL;
   auto query=[&](const char*sql,auto callback){sqlite3_stmt*st=nullptr;
    if(sqlite3_prepare_v2(db,sql,-1,&st,nullptr)!=SQLITE_OK)fail(sqlite3_errmsg(db));
    while(sqlite3_step(st)==SQLITE_ROW)callback(st);sqlite3_finalize(st);};
   if(pass==0){
    query("SELECT id,lat,lon,tags FROM nodes ORDER BY id",[&](sqlite3_stmt*st){
     out<<"<node id=\""<<sqlite3_column_int64(st,0)+delta<<"\" version=\"1\" lat=\""<<std::fixed<<std::setprecision(7)<<sqlite3_column_int(st,1)/10000000.<<"\" lon=\""<<sqlite3_column_int(st,2)/10000000.<<"\">"<<(const char*)sqlite3_column_text(st,3)<<"</node>\n";});
    query("SELECT id,lat,lon,tags FROM pois ORDER BY id",[&](sqlite3_stmt*st){
     out<<"<node id=\""<<sqlite3_column_int64(st,0)+delta<<"\" version=\"1\" lat=\""<<std::fixed<<std::setprecision(7)<<sqlite3_column_int(st,1)/10000000.<<"\" lon=\""<<sqlite3_column_int(st,2)/10000000.<<"\">"<<(const char*)sqlite3_column_text(st,3)<<"</node>\n";});
   } else {
    query("SELECT id,refs,tags FROM ways ORDER BY id",[&](sqlite3_stmt*st){
     std::string refs=(const char*)sqlite3_column_text(st,1);
     if(delta){size_t from=0;while((from=refs.find("<nd ref=\"",from))!=std::string::npos){
      auto begin=from+9,end=refs.find('"',begin);if(end==std::string::npos)fail("Bad internal way reference");
      auto old=std::stoll(refs.substr(begin,end-begin));auto val=std::to_string(old+delta);
      refs.replace(begin,end-begin,val);from=begin+val.size();}}
     out<<"<way id=\""<<sqlite3_column_int64(st,0)+delta<<"\" version=\"1\">"<<refs<<(const char*)sqlite3_column_text(st,2)<<"</way>\n";});
   }
   sqlite3_close(db);
  }};
 walk(0);walk(1);out<<"</osm>\n";out.close();if(!out)fail("Failed writing merged OSM");
}

// Extra user-authored POIs are not claims about original IMG contents.
// Mark as manual_poi so source-vs-IMG validator can distinguish them.
void injectManualPois(const std::string&file,const std::string&dbPath){
 if(file.empty())return;std::ifstream input(file);if(!input)fail("Cannot read additions file: "+file);
 sqlite3*db=nullptr;if(sqlite3_open(dbPath.c_str(),&db)!=SQLITE_OK)fail("Cannot open stage for additions");
 auto sql=[&](const char*q){if(sqlite3_exec(db,q,nullptr,nullptr,nullptr)!=SQLITE_OK)fail(sqlite3_errmsg(db));};
 sqlite3_stmt*findId=nullptr;sqlite3_prepare_v2(db,"SELECT COALESCE(MAX(id),999999999999) FROM pois",-1,&findId,nullptr);
 int64_t seq=999999999999LL;if(sqlite3_step(findId)==SQLITE_ROW)seq=std::max<int64_t>(seq,sqlite3_column_int64(findId,0));sqlite3_finalize(findId);
 sqlite3_stmt*insert=nullptr;if(sqlite3_prepare_v2(db,"INSERT INTO pois(id,lon,lat,tags) VALUES(?1,?2,?3,?4)",-1,&insert,nullptr)!=SQLITE_OK)fail(sqlite3_errmsg(db));
 sql("BEGIN");std::string row;size_t count=0;int lineno=0;
 while(std::getline(input,row)){lineno++;if(!row.empty()&&row.back()=='\r')row.pop_back();if(row.empty()||row[0]=='#')continue;
  auto cols=csvParse(row);if(cols[0]=="lat")continue;if(cols.size()!=4)fail("Additions need lat,lon,name,tags at line "+std::to_string(lineno));
  size_t parsed=0;double lat=std::stod(cols[0],&parsed);if(parsed!=cols[0].size())fail("Bad latitude");double lon=std::stod(cols[1],&parsed);if(parsed!=cols[1].size()||!std::isfinite(lat)||!std::isfinite(lon)||std::abs(lat)>90||std::abs(lon)>180)fail("Bad manual POI coordinates");
  Tags tags;tag(tags,"garmin:feature","manual_poi");tag(tags,"source","user_manual");tag(tags,"name",cols[2]);
  std::stringstream vals(cols[3]);std::string kv;while(std::getline(vals,kv,';')){if(kv.empty())continue;auto eq=kv.find('=');if(eq==std::string::npos||eq==0)fail("Bad manual tag at line "+std::to_string(lineno));
    auto k=kv.substr(0,eq);if(k=="garmin:feature"||k=="source"||k=="name")fail("Cannot override manual provenance fields");tag(tags,k,kv.substr(eq+1));}
  auto txt=makeTags(tags);sqlite3_reset(insert);sqlite3_clear_bindings(insert);
  sqlite3_bind_int64(insert,1,++seq);sqlite3_bind_int(insert,2,Store::quant(lon));sqlite3_bind_int(insert,3,Store::quant(lat));sqlite3_bind_text(insert,4,txt.c_str(),-1,SQLITE_TRANSIENT);
  if(sqlite3_step(insert)!=SQLITE_DONE)fail("Manual POI insertion failed: "+std::string(sqlite3_errmsg(db)));count++;
 }
 sql("COMMIT");sqlite3_finalize(insert);sqlite3_close(db);std::cerr<<"Injected "<<count<<" manually supplied POIs (not IMG-derived)\n";
}

int run(int argc,char**argv){
 auto o=parse(argc,argv);loadOverrides(o.poiOverrides);loadNameFixes(o.poiNameFixes);MapSet set(o.input);
 for(const auto&[n,parts]:set.parts){auto it=parts.find("TYP");if(it!=parts.end()){gStyles.load(it->second);break;}}
 std::cerr<<"Maps="<<set.maps.size()<<" TYP icons="<<gStyles.pointStyles<<" jobs="<<o.jobs<<"\n";
 saveIcons(o.iconsDir);
 std::vector<const Map*>selected;for(const auto&m:set.maps){if(o.clip&&!m.bounds.overlaps(o.bbox))continue;if(o.limitMaps>0&&(int)selected.size()>=o.limitMaps)break;selected.push_back(&m);}
 int jobs=std::min<int>(o.jobs,std::max<size_t>(1,selected.size()));
 const bool exporting=!o.output.empty();
 std::string temp=o.temp.empty()?(o.output.empty()?o.input+".stage.sqlite":o.output+".stage.sqlite"):o.temp;
 if(exporting&&fs::exists(o.output))fail("Output already exists; refuse overwrite: "+o.output);
 std::vector<std::string>dbPaths(jobs),manifestPaths(jobs);for(int i=0;i<jobs;i++){
  dbPaths[i]=temp+".worker"+std::to_string(i)+".sqlite";if(exporting&&fs::exists(dbPaths[i]))fail("Stage exists: "+dbPaths[i]);
  if(!o.poiManifest.empty()){manifestPaths[i]=o.poiManifest+".worker"+std::to_string(i)+".tmp";if(fs::exists(manifestPaths[i]))fail("Manifest temp exists: "+manifestPaths[i]);}
 }
 std::vector<Stats>stats(jobs);std::atomic<size_t>next{0};std::exception_ptr error;std::mutex errorLock;
 auto worker=[&](int id){try{
  std::unique_ptr<Store>db;if(exporting)db=std::make_unique<Store>(dbPaths[id]);
  std::ofstream manifest;if(!manifestPaths[id].empty()){manifest.open(manifestPaths[id]);if(!manifest)fail("Cannot write POI manifest");}
  for(;;){auto idx=next.fetch_add(1);if(idx>=selected.size())break;processMap(*selected[idx],db.get(),stats[id],o,manifest.is_open()?&manifest:nullptr);}
  if(db)db->sql("COMMIT");
 }catch(...){std::lock_guard<std::mutex>guard(errorLock);if(!error)error=std::current_exception();}};
 if(jobs==1)worker(0);else {std::vector<std::thread>threads;for(int k=0;k<jobs;k++)threads.emplace_back(worker,k);for(auto&t:threads)t.join();}
 if(error)std::rethrow_exception(error);
 Stats total;for(const auto&st:stats){total.lines+=st.lines;total.areas+=st.areas;total.points+=st.points;total.skipped+=st.skipped;total.extendedOutsideBounds+=st.extendedOutsideBounds;
  for(const auto&[k,v]:st.byType)total.byType[k]+=v;
  for(const auto&[k,v]:st.poiTypes){auto&dest=total.poiTypes[k];dest.count+=v.count;dest.named+=v.named;
   for(const auto&ex:v.examples)addSample(dest,ex,o.examples);}}
 writePoiTypes(o.poiTypesOut,total.poiTypes,o.examples);
 if(!o.poiManifest.empty()){
  if(fs::exists(o.poiManifest))fail("Manifest exists; refuse overwrite");std::ofstream out(o.poiManifest);if(!out)fail("Cannot write "+o.poiManifest);
  out<<"mapset,kind,code,lat,lon,name\n";
  for(const auto&p:manifestPaths){std::ifstream in(p);if(in.peek()!=std::char_traits<char>::eof())out<<in.rdbuf();in.close();fs::remove(p);}
 }
 std::cerr<<"Finished "<<selected.size()<<" maps; lines="<<total.lines<<" areas="<<total.areas<<" rawPOIs="<<total.points<<" uniquePOItypes="<<total.poiTypes.size()<<" parseStops="<<total.skipped<<" extendedOutsideBounds="<<total.extendedOutsideBounds<<"\n";
 if(exporting){injectManualPois(o.poiAdditions,dbPaths[0]);outputStages(o.output,dbPaths);if(!o.keepTemp)for(auto&p:dbPaths)fs::remove(p);std::cerr<<"Wrote "<<o.output<<"\n";}
 return 0;
}
} // namespace vector_export
int main(int argc,char**argv){try{return vector_export::run(argc,argv);}catch(const std::exception&e){std::cerr<<"ERROR: "<<e.what()<<"\n";return 1;}}
