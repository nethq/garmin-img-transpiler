// garmin2osmand - bounded-memory native Garmin IMG -> OsmAnd raster SQLiteDB.
// SPDX-License-Identifier: GPL-3.0-or-later
// NOTE: Experimental reader. Rejects encrypted/XOR files; see README coverage.
// Garmin IMG structures informed by public Garmin IMG documentation and QMapShack.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <iomanip>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>
#include <png.h>
#ifdef __linux__
#include <dlfcn.h>
#endif
#include <iconv.h>
#include <sqlite3.h>
namespace fs=std::filesystem;
using U8=uint8_t; using U32=uint32_t; using U64=uint64_t;
static constexpr double pi=3.14159265358979323846;
static int gAA=2;
static U32 u16(const U8*p){return U32(p[0])|(U32(p[1])<<8);}
static U32 u24(const U8*p){return u16(p)|(U32(p[2])<<16);}
static U32 u32(const U8*p){return u16(p)|(u16(p+2)<<16);}
static int s16(const U8*p){return int16_t(u16(p));}
static int s24(const U8*p){U32 v=u24(p);return int((v^0x800000u)-0x800000u);}
static double geo(int v){return double(v)*360.0/16777216.0;}
static std::string trim(const char*p,size_t n){std::string s(p,n);while(!s.empty()&&(s.back()==' '||s.back()=='\0'))s.pop_back();return s;}
static void fail(const std::string&s){throw std::runtime_error(s);}
struct BBox {double w=180,s=90,e=-180,n=-90; bool valid() const {return w<=e&&s<=n;} void add(const BBox&b){w=std::min(w,b.w);s=std::min(s,b.s);e=std::max(e,b.e);n=std::max(n,b.n);} bool overlaps(const BBox&b)const {return !(w>b.e || e<b.w || s>b.n || n<b.s);} };
struct File {
 std::ifstream in; U64 size=0;std::mutex ioMutex;
 explicit File(const std::string&path){in.open(path,std::ios::binary);if(!in)fail("Cannot open "+path);in.seekg(0,std::ios::end);size=U64(in.tellg());}
 void read(U64 pos,void*dest,size_t n){std::lock_guard<std::mutex> lk(ioMutex);if(pos>size||n>size-pos)fail("IMG truncated: out-of-range read");in.clear();in.seekg(std::streamoff(pos));in.read(static_cast<char*>(dest),std::streamsize(n));if(!in)fail("IMG read failed");}
 std::vector<U8> get(U64 pos,size_t n){std::vector<U8> b(n);read(pos,b.data(),n);return b;}
};
// A Garmin FAT entry may enumerate noncontiguous blocks. Read logical files via their block lists.
struct Part{
 File* file=nullptr; U64 size=0; U64 blockSize=0;std::vector<U32>blocks;
 void read(U64 pos,void*dst,size_t len) const{
  if(pos>size||len>size-pos||blockSize==0)fail("Invalid Garmin section read");
  auto*out=static_cast<U8*>(dst);
  while(len){U64 i=pos/blockSize,delta=pos%blockSize;
   if(i>=blocks.size())fail("FAT block chain shorter than section");
   size_t n=size_t(std::min<U64>(len,blockSize-delta));
   file->read(U64(blocks[size_t(i)])*blockSize+delta,out,n);
   pos+=n;out+=n;len-=n;
  }
 }
 std::vector<U8> get(U64 pos,size_t n)const{std::vector<U8>r(n);read(pos,r.data(),n);return r;}
};
// Virtual section: classic TRE/RGN has its own address zero; GMP embeds all sections,
// whose absolute offsets are relative to the start of the GMP logical file.
struct View {const Part* p=nullptr; U64 header=0,origin=0;
 std::vector<U8> get(U64 relative,size_t n) const {if(!p)fail("Missing section");return p->get(origin+relative,n);}
 std::vector<U8> hdr(size_t n)const{return p->get(header,n);}
};
struct Level{int level=0,bits=0,count=0;};
struct Sub{U32 from=0,to=0;int bits=0,level=0;int cx=0,cy=0,shift=0;U8 flags=0;BBox bounds;
 std::array<U32,3> extFrom{},extLen{};};
struct Map{
 std::string name; std::map<std::string,View>sections;
 std::vector<Level>levels;std::vector<Sub>subs; BBox bounds;
 int lblCoding=0,lblCodepage=0,lblShift=0;
 U32 lbl1Off=0,lbl1Len=0,lbl6Off=0,lbl6Len=0,net1Off=0,net1Len=0;int netShift=0;
 mutable std::unordered_map<U32,std::string> labelCache;
 std::string label(U32 ptr) const {
  if(!lbl1Len||ptr==0||lblCoding!=9)return {};
  U64 shifted=U64(ptr)<<lblShift;
  if(shifted>=lbl1Len)return {};
  auto found=labelCache.find(ptr);if(found!=labelCache.end())return found->second;
  const auto&lbl=sections.at("LBL");
  auto raw=lbl.get(lbl1Off+shifted,size_t(std::min<U64>(180,lbl1Len-shifted)));
  std::string text;
  for(unsigned char c:raw){if(c==0||text.size()>100)break;
   if(c>=0x1b&&c<=0x1f){if(!text.empty())text+=' ';continue;}
   if(c>=0x20)text.push_back(char(c));
  }
  // Convert the embedded Bulgarian Windows-1251 names to UTF-8.
  if(lblCodepage==1251&& !text.empty()){
    iconv_t cd=iconv_open("UTF-8","WINDOWS-1251");
    if(cd!=(iconv_t)-1){std::string out(text.size()*4+8,'\0');char*inPtr=text.data();size_t inSize=text.size();
     char*outPtr=out.data();size_t outSize=out.size();
     if(iconv(cd,&inPtr,&inSize,&outPtr,&outSize)!=(size_t)-1){out.resize(outPtr-out.data());text=std::move(out);}
     iconv_close(cd);
    }
  }
  if(labelCache.size()>10000)labelCache.clear();
  labelCache.emplace(ptr,text);return text;
 }
 std::string poiLabel(U32 ptr) const {
  // Garmin POI label pointers with flag 0x400000 refer to LBL6 records.
  const auto it=sections.find("LBL");
  if(!lbl6Len||it==sections.end()||ptr+4>lbl6Len)return {};
  const auto rec=it->second.get(lbl6Off+ptr,4);
  return label(u32(rec.data())&0x3fffff);
 }
 std::string netLabel(U32 ptr) const {
  // NET1 points to LBL1 after applying the NET address shift.
  const auto it=sections.find("NET");
  if(!net1Len||it==sections.end()||(U64(ptr)<<netShift)+4>net1Len)return {};
  const auto rec=it->second.get(net1Off+(U64(ptr)<<netShift),4);
  U32 offset=u32(rec.data());if(offset&0x400000)return {};
  return label(offset&0x3fffff);
 }

 std::unordered_map<int,std::vector<int>>byBits;
 // Fixed 1-degree geographic bins (approximate R-tree). Large extents live in broadByBits.
 std::unordered_map<int,std::unordered_map<int,std::vector<int>>> cellsByBits;
 std::unordered_map<int,std::vector<int>> broadByBits;
};
struct MapSet{
 File file; U64 blockSize=0; std::map<std::string,std::map<std::string,Part>>parts;std::vector<Map>maps;BBox bounds;
 explicit MapSet(const std::string&name):file(name){parse();}
 static bool looksValid(const std::vector<U8>&b,const char*s){return b.size()>=12&&std::memcmp(b.data()+2,s,10)==0;}
 void parse(){
  if(file.size<2048)fail("IMG too small");
  auto hdr=file.get(0,1536);
  if(hdr[0]!=0)fail("XOR-obfuscated IMG: unsupported (no unlocking/decryption)");
  if(std::memcmp(hdr.data()+0x10,"DSKIMG",6)||std::memcmp(hdr.data()+0x41,"GARMIN",6))fail("Not a Garmin DSKIMG container");
  int power=hdr[0x61]+hdr[0x62];if(power<9||power>22)fail("Invalid block size in Garmin header");blockSize=U64(1)<<power;
  const U64 fatEnd= u32(hdr.data()+0x40c)!=0 ? std::min<U64>(u32(hdr.data()+0x40c),file.size):std::min<U64>(file.size,16*1024*1024);
  // Entries start at 0x600. A zero flag is a placeholder; stop at data region.
  std::map<std::pair<std::string,std::string>,std::map<int,std::vector<U32>>>chains;
  std::map<std::pair<std::string,std::string>,U64>sizes;
  int consecutiveNonEntries=0;
  for(U64 off=0x600;off+512<=fatEnd;off+=512){
   auto f=file.get(off,512);if(f[0]!=0x01){if(++consecutiveNonEntries>24)break;continue;}
   consecutiveNonEntries=0;
   auto name=trim(reinterpret_cast<char*>(f.data()+1),8),ext=trim(reinterpret_cast<char*>(f.data()+9),3);
   if(name.empty()||ext.empty()||name[0]==' '||name=="MAPSOURC"||name=="SENDMAP2")continue;
   auto key=std::make_pair(name,ext);U32 sz=u32(f.data()+12);int index=int(u16(f.data()+16));
   if(sz&&(!sizes.count(key)||index==0))sizes[key]=sz;
   auto&chain=chains[key][index];
   for(int j=0;j<240;j++){U32 block=u16(f.data()+32+2*j);if(block==0xFFFF)break;
    if(U64(block)*blockSize>=file.size)continue;
    chain.push_back(block);
   }
  }
  for(const auto&[k,chunks]:chains){if(!sizes.count(k))continue;
   Part p;p.file=&file;p.blockSize=blockSize;p.size=sizes[k];
   for(const auto&[seq,blocks]:chunks){(void)seq;p.blocks.insert(p.blocks.end(),blocks.begin(),blocks.end());}
   if(p.blocks.empty()||p.size>p.blocks.size()*p.blockSize)continue;
   parts[k.first][k.second]=std::move(p);
  }
  if(parts.empty())fail("No usable Garmin FAT subfiles found");
  for(auto&[name,sections]:parts){
   Map m;m.name=name;
   if(sections.count("GMP")){
    const auto&p=sections.at("GMP");auto g=p.get(0,std::min<U64>(64,p.size));
    if(g.size()<0x31||!looksValid(g,"GARMIN GMP"))continue;
    for(const auto&[type,at]:std::vector<std::pair<std::string,int>>{{"TRE",0x19},{"RGN",0x1d},{"LBL",0x21},{"NET",0x25}}){
     U32 offset=u32(g.data()+at);if(!offset||offset+21>p.size)continue;
     m.sections[type]=View{&p,offset,0};
    }
   }else for(const auto&[type,p]:sections){m.sections[type]=View{&p,0,0};}
   if(!m.sections.count("TRE")||!m.sections.count("RGN"))continue;
   try{parseMap(m);if(!m.subs.empty()){bounds.add(m.bounds);maps.push_back(std::move(m));}}
   catch(const std::exception&e){std::cerr<<"Warning: skip map "<<name<<": "<<e.what()<<"\n";}
  }
  if(maps.empty())fail("No readable unlocked TRE/RGN maps found. May be unsupported NT variant, locked, or malformed IMG.");
 }
 void parseMap(Map&m){
  const auto&t=m.sections.at("TRE");const auto&r=m.sections.at("RGN");
  auto th=t.hdr(0xc0);auto rh=r.hdr(0x7d);
  if(!looksValid(th,"GARMIN TRE")||!looksValid(rh,"GARMIN RGN"))fail("invalid TRE/RGN header");
  if(th[0x0d]&0x80)fail("locked/encrypted TRE header");
  m.bounds=BBox{geo(s24(th.data()+0x1e)),geo(s24(th.data()+0x1b)),geo(s24(th.data()+0x18)),geo(s24(th.data()+0x15))};
  if(m.sections.count("LBL")){
   auto lh=m.sections.at("LBL").hdr(0xb0);
   if(looksValid(lh,"GARMIN LBL")){
    m.lbl1Off=u32(lh.data()+0x15);m.lbl1Len=u32(lh.data()+0x19);
    m.lblShift=lh[0x1d];m.lblCoding=lh[0x1e];
    m.lbl6Off=u32(lh.data()+0x57);m.lbl6Len=u32(lh.data()+0x5b);
    if(u16(lh.data())>0xaa)m.lblCodepage=u16(lh.data()+0xaa);
   }
  }

  if(m.sections.count("NET")){
   const auto nh=m.sections.at("NET").hdr(0x35);
   if(looksValid(nh,"GARMIN NET")){
    m.net1Off=u32(nh.data()+0x15);m.net1Len=u32(nh.data()+0x19);m.netShift=nh[0x1d];
   }
  }
  if(m.bounds.w>m.bounds.e)fail("cross-dateline maps are not supported yet");
  U32 lOff=u32(th.data()+0x21),lLen=u32(th.data()+0x25),sOff=u32(th.data()+0x29),sLen=u32(th.data()+0x2d);
  if(!lLen||lLen%4||lLen>4096||sLen>32*1024*1024)fail("unexpected TRE level/subdivision size");
  auto lb=t.get(lOff,lLen),sb=t.get(sOff,sLen);
  U32 count=0;
  for(size_t i=0;i<lb.size();i+=4){Level l{int(lb[i]&0x0f),int(lb[i+1]),int(u16(lb.data()+i+2))};
   if(l.bits>24||l.bits<0||l.count<0)fail("bad map level");
   m.levels.push_back(l);count+=U32(l.count);
  }
  if(count>300000||!count)fail("bad subdivision count");
  U32 classicBase=u32(rh.data()+0x15),classicLength=u32(rh.data()+0x19);
  size_t off=0;
  for(size_t li=0;li<m.levels.size();li++){
   const auto&lev=m.levels[li];size_t entrySize=(li+1==m.levels.size()?14:16);
   for(int j=0;j<lev.count;j++){
    if(off+entrySize>sb.size())fail("short TRE subdivision table");
    auto*b=sb.data()+off;off+=entrySize; Sub s;
    s.from=classicBase+u24(b);s.flags=b[3];s.cx=s24(b+4);s.cy=s24(b+7);s.bits=lev.bits;s.shift=24-lev.bits;s.level=lev.level;
    int width=int(u16(b+10)&0x7fff),height=int(u16(b+12));
    s.bounds=BBox{geo(s.cx-(width<<s.shift)),geo(s.cy-(height<<s.shift)),geo(s.cx+(width<<s.shift)+1),geo(s.cy+(height<<s.shift)+1)};
    m.subs.push_back(s);
   }
  }
  for(size_t i=0;i<m.subs.size();i++)m.subs[i].to=(i+1<m.subs.size()?m.subs[i+1].from:classicBase+classicLength);
  // NT-format extended segments are separate RGN2/3/4 sections.
  if(u16(th.data())>=0x9a&&u32(th.data()+0x80)&&u16(th.data()+0x84)>=12){
   U32 off2=u32(th.data()+0x7c),num=u32(th.data()+0x80),step=u16(th.data()+0x84);
   if(num>=m.subs.size()*U32(step)&&num<32*1024*1024){
    auto b=t.get(off2,num);
    const U32 basePoly=u32(rh.data()+0x1d),baseLine=u32(rh.data()+0x39),basePoint=u32(rh.data()+0x55);
    const U32 lenPoly=u32(rh.data()+0x21),lenLine=u32(rh.data()+0x3d),lenPoint=u32(rh.data()+0x59);
    for(size_t i=0;i<m.subs.size();i++){const U8*p=b.data()+i*step;
     m.subs[i].extFrom={basePoly+u32(p),baseLine+u32(p+4),basePoint+u32(p+8)};
    }
    for(size_t i=0;i<m.subs.size();i++)for(int k=0;k<3;k++){
     U32 end=(i+1<m.subs.size()?m.subs[i+1].extFrom[k]:std::array<U32,3>{basePoly+lenPoly,baseLine+lenLine,basePoint+lenPoint}[k]);
     auto start=m.subs[i].extFrom[k];m.subs[i].extLen[k]=(end>=start?end-start:0);
    }
   }
  }
  for(size_t i=0;i<m.subs.size();i++){
   const auto&sub=m.subs[i];m.byBits[sub.bits].push_back(int(i));
   int w=int(std::floor(sub.bounds.w)),e=int(std::floor(sub.bounds.e));
   int s=int(std::floor(sub.bounds.s)),n=int(std::floor(sub.bounds.n));
   if(e-w>60 || n-s>60 || (e-w+1)*(n-s+1)>300){m.broadByBits[sub.bits].push_back(int(i));continue;}
   for(int gy=s;gy<=n;gy++)for(int gx=w;gx<=e;gx++)
     m.cellsByBits[sub.bits][(gx+180)*256+(gy+90)].push_back(int(i));
  }
 }
};
struct XY{double x=0,y=0;};
static double mercY(double lat){lat=std::clamp(lat,-85.05112878,85.05112878)*pi/180;return (1-std::asinh(std::tan(lat))/pi)*0.5;}
static double mercX(double lon){return (lon+180)/360.;}
static double invMercY(double t){return std::atan(std::sinh(pi*(1-2*t)))*180/pi;}
struct Image{
 int w=256,h=256;std::vector<U8>rgb;bool painted=false;
 explicit Image(int n=256):w(n),h(n),rgb(size_t(n)*n*4,0){}
 void px(int x,int y,U32 color){if(x<0||y<0||x>=w||y>=h)return;
  U8*a=rgb.data()+4*(size_t(y)*w+x);U32 sa=color&255;if(!sa)return;
  U32 da=a[3],oa=sa+da*(255-sa)/255;
  if(oa){a[0]=U8(((color>>24&255)*sa+a[0]*da*(255-sa)/255)/oa);
         a[1]=U8(((color>>16&255)*sa+a[1]*da*(255-sa)/255)/oa);
         a[2]=U8(((color>>8&255)*sa+a[2]*da*(255-sa)/255)/oa);a[3]=U8(oa);painted=true;}
 }
 void disc(double xx,double yy,double r,U32 col){int minX=int(std::floor(xx-r)),maxX=int(std::ceil(xx+r));int minY=int(std::floor(yy-r)),maxY=int(std::ceil(yy+r));
  if(minX>w||minY>h||maxX<0||maxY<0)return;
  for(int y=std::max(0,minY);y<std::min(h,maxY+1);y++)for(int x=std::max(0,minX);x<std::min(w,maxX+1);x++){
   double d=(x+.5-xx)*(x+.5-xx)+(y+.5-yy)*(y+.5-yy);if(d<=r*r)px(x,y,col);
  }
 }
 void line(XY a,XY b,double radius,U32 col){
  // Bounding guard prevents huge work on offscreen geometry: clip parametric line to enlarged tile.
  double dx=b.x-a.x,dy=b.y-a.y,lo=0,hi=1,m=radius+2;
  auto clip=[&](double p,double q){if(std::abs(p)<1e-15)return q>=0;double t=q/p;if(p<0){if(t>hi)return false;lo=std::max(lo,t);}else{if(t<lo)return false;hi=std::min(hi,t);}return true;};
  if(!clip(-dx,a.x+m)||!clip(dx,w+m-a.x)||!clip(-dy,a.y+m)||!clip(dy,h+m-a.y)||lo>hi)return;
  XY p{a.x+lo*dx,a.y+lo*dy},q{a.x+hi*dx,a.y+hi*dy};
  double length=std::hypot(q.x-p.x,q.y-p.y);int n=std::max(1,int(std::ceil(length*1.7)));
  for(int i=0;i<=n;i++){double t=double(i)/n;disc(p.x+t*(q.x-p.x),p.y+t*(q.y-p.y),radius,col);}
 }
 void polygon(const std::vector<XY>&v,U32 color){if(v.size()<3)return;double minY=h,maxY=-1;
  for(auto&p:v){minY=std::min(minY,p.y);maxY=std::max(maxY,p.y);}
  for(int y=std::max(0,int(std::floor(minY)));y<std::min(h,int(std::ceil(maxY)));y++){
   std::vector<double>cuts;double scan=y+.5;
   for(size_t i=0,j=v.size()-1;i<v.size();j=i++){
    auto a=v[j],b=v[i];if((a.y<=scan&&scan<b.y)||(b.y<=scan&&scan<a.y))cuts.push_back(a.x+(scan-a.y)*(b.x-a.x)/(b.y-a.y));
   }
   std::sort(cuts.begin(),cuts.end());for(size_t i=1;i<cuts.size();i+=2){
    for(int x=std::max(0,int(std::ceil(cuts[i-1]-.5)));x<std::min(w,int(std::floor(cuts[i]-.5))+1);x++)px(x,y,color);
   }
  }
 }
 Image downsample(int factor) const {
  if(factor==1)return *this;
  if(w%factor||h%factor)fail("Invalid downsample factor");
  Image result(w/factor);result.painted=painted;
  for(int y=0;y<result.h;y++)for(int x=0;x<result.w;x++){
   U32 sumA=0,sumR=0,sumG=0,sumB=0;
   for(int fy=0;fy<factor;fy++)for(int fx=0;fx<factor;fx++){
    const U8*p=&rgb[4*(size_t(y*factor+fy)*w+x*factor+fx)];
    sumA+=p[3];sumR+=U32(p[0])*p[3];sumG+=U32(p[1])*p[3];sumB+=U32(p[2])*p[3];
   }
   U8*p=&result.rgb[4*(size_t(y)*result.w+x)];
   p[3]=U8(sumA/(factor*factor));
   if(sumA){p[0]=U8(sumR/sumA);p[1]=U8(sumG/sumA);p[2]=U8(sumB/sumA);}
  }
  return result;
 }
 std::vector<U8> png()const{
  struct Buf{std::vector<U8>v;};Buf b;png_structp p=png_create_write_struct(PNG_LIBPNG_VER_STRING,nullptr,nullptr,nullptr);
  if(!p)fail("libpng init failed");png_infop info=png_create_info_struct(p);if(!info){png_destroy_write_struct(&p,nullptr);fail("libpng info failed");}
  if(setjmp(png_jmpbuf(p))){png_destroy_write_struct(&p,&info);fail("libpng encoding failed");}
  png_set_write_fn(p,&b,[](png_structp p,png_bytep d,png_size_t n){auto*buf=(Buf*)png_get_io_ptr(p);buf->v.insert(buf->v.end(),d,d+n);},[](png_structp){});
  png_set_IHDR(p,info,w,h,8,PNG_COLOR_TYPE_RGBA,PNG_INTERLACE_NONE,PNG_COMPRESSION_TYPE_DEFAULT,PNG_FILTER_TYPE_DEFAULT);
  png_set_compression_level(p,6);png_write_info(p,info);
  std::vector<png_bytep>rows(static_cast<size_t>(h), nullptr);for(int y=0;y<h;y++)rows[size_t(y)]=const_cast<png_bytep>(rgb.data()+size_t(y)*w*4);
  png_write_image(p,rows.data());png_write_end(p,nullptr);png_destroy_write_struct(&p,&info);return b.v;
 }
};
static U32 rgba(int r,int g,int b,int a=255){return (U32(r)<<24)|(U32(g)<<16)|(U32(b)<<8)|U32(a);}
// Embedded Garmin TYP decoder: colors, patterned trail lines and polygon fills.
// The TYP table includes a full 1bpp bitmap for each patterned hiking-route stroke.
struct Style { U32 color=0; double width=1; U32 casing=0;double casingWidth=0;
 int rows=0;std::vector<U32>pattern;bool hasPattern()const{return rows>0 && pattern.size()==size_t(32*rows);} };
struct Icon {int w=0,h=0;std::vector<U32>pixels;};
struct Styles {
 std::unordered_map<int,Icon> icons;
 std::unordered_map<int,Style> lines;
 std::unordered_map<int,U32> areas;
 int parsed=0,patterns=0,polygonStyles=0,pointStyles=0;
 static U32 bgr(const U8*p){return rgba(p[2],p[1],p[0]);}
 static int typeKey(U32 code,bool point=false){
   U32 sw=(code>>5)|((code&31)<<11), hi=sw&(point?0x7ff:0x7f);
   return (code&0x2000)?int(0x10000|(hi<<8)|(code&31)):int(point?((hi<<8)|(code&31)):hi);
 }
 void load(const Part& part){
  if(part.size>8*1024*1024||part.size<91)return;
  auto b=part.get(0,size_t(part.size));
  if(std::memcmp(b.data()+2,"GARMIN TYP",10)!=0)return;
  auto table=[&](int tableOff,int dataOff, auto consume){
   U32 a=u32(b.data()+tableOff),step=u16(b.data()+tableOff+4),len=u32(b.data()+tableOff+6),data=u32(b.data()+dataOff);
   if(step<3||step>8||len>8*1024*1024||a> b.size()||len>b.size()-a||data>=b.size())return;
   for(U32 i=0;i<len/step;i++){
    const U8*ent=b.data()+a+size_t(i)*step;
    U32 off=0;for(U32 k=2;k<step;k++)off|=U32(ent[k])<<(8*(k-2));
    if(off>b.size()-data-1)continue;
    U32 raw=u16(ent);consume(typeKey(raw),b.data()+data+off,b.size()-data-off);
   }
  };
  // The point table has a different 11-bit type-mask. Read it independently.
  {
   const U32 a=u32(b.data()+0x33),step=u16(b.data()+0x37),len=u32(b.data()+0x39),data=u32(b.data()+0x17);
   if(step>=3&&step<=8&&len<1000000&&a+len<=b.size()&&data<b.size()){
    for(U32 i=0;i<len/step;i++){
     const U8*e=b.data()+a+size_t(i)*step;
     U32 rel=0;for(U32 j=2;j<step;j++)rel|=U32(e[j])<<(8*(j-2));
     if(rel>=b.size()-data)continue;
     const U8*d=b.data()+data+rel;size_t nbytes=b.size()-data-rel;
     if(nbytes<8)continue;
     int w=d[1],h=d[2],nc=d[3],mode=d[4];
     if(w<4||w>40||h<4||h>40||nc<1||nc>128)continue;
     int bits=0;
     if(mode==0){bits=(nc<3?nc:nc==3?2:nc<16?4:8);}
     else if(mode==0x10){bits=(nc<3?2:nc<15?4:8);}
     else if(mode==0x20){bits=(nc<3?nc:nc<4?2:nc<16?4:8);}
     else continue;
     if(bits<1||bits>8)continue;
     int stride=(w*bits+7)/8;size_t off=5;
     if(off+size_t(nc)*3+size_t(h*stride)>nbytes)continue;
     if(mode==0x20)continue; // alpha point palette not yet implemented
     std::vector<U32>palette(size_t(1)<<bits,0);
     for(int c=0;c<nc;c++)palette[size_t(c)]=bgr(d+off+size_t(c)*3);
     off+=size_t(nc)*3;
     Icon icon;icon.w=w;icon.h=h;icon.pixels.resize(size_t(w)*h,0);
     bool nonEmpty=false;
     for(int y=0;y<h;y++)for(int x=0;x<w;x++){
      size_t bit=size_t(y)*stride*8+size_t(x)*bits;
      int index=(d[off+bit/8]>>(bit%8))&((1<<bits)-1);
      U32 c=palette[size_t(index)];icon.pixels[size_t(y)*w+x]=c;
      nonEmpty|=(c&255)!=0;
     }
     int type=typeKey(u16(e),true);
     if(nonEmpty){icons[type]=std::move(icon);pointStyles++;}
    }
   }
  }
  table(0x3d,0x1f,[&](int key,const U8*d,size_t length){
   if(length<8)return;int mode=d[0]&7,rows=d[0]>>3;Style style;
   size_t q=2;
   auto take=[&]()->U32{if(q+3>length)return 0;auto c=bgr(d+q);q+=3;return c;};
   if(rows>0&&rows<=31){
    U32 foreground=take(),background=0;
    if(mode==0||mode==1||mode==5)background=take();
    if(mode==1){take();take();}
    if(mode==3){take();take();}else if(mode==5||mode==7){take();}
    if(q+size_t(rows)*4>length)return;
    style.rows=rows;style.width=rows;style.color=foreground;
    style.pattern.reserve(size_t(rows)*32);
    for(int y=0;y<rows;y++)for(int x=0;x<32;x++){
      int bit=(d[q+size_t(y)*4+x/8]>>(x%8))&1;
      style.pattern.push_back(bit?foreground:background);
    }
    patterns++;
   }else if(rows==0){
    U32 primary=take(),border=0;
    if(mode==0||mode==1||mode==5)border=take();
    if(mode==1){take();take();}
    if(mode==3){take();take();}
    if(mode==5||mode==7)take();
    if(mode==6||mode==0||mode==1||mode==3||mode==5||mode==7){
      style.color=primary;
      if(q<length)style.width=std::max(0.6,double(d[q++]));
      if((mode==0||mode==1)&&q<length){
       style.casingWidth=double(d[q]);
       if(style.casingWidth>style.width)style.casing=border;
      }
      style.width=std::min(style.width,15.0);style.casingWidth=std::min(style.casingWidth,17.0);
    }else return;
   } else return;
   lines[key]=std::move(style);parsed++;
  });
  table(0x47,0x27,[&](int key,const U8*d,size_t length){
   if(length<4)return;int kind=d[0]&15;
   // Simple solid fills (6/7) and the dominant tone in 2-colour textures.
   if(kind==6||kind==7||kind==1||kind==8||kind==9||kind==11||kind==13||kind==14||kind==15){
    areas[key]=bgr(d+1);polygonStyles++;
   }
  });
 }
 Style line(int type)const{
  auto found=lines.find(type);if(found!=lines.end())return found->second;
  // Fallback only when Garmin TYP omitted this type.
  if(type>=0x20&&type<=0x25)return {rgba(133,103,81,175),0.7};
  if(type==0x16||type==0x17||type==0x18||type==0x19||type==0x1f)return {rgba(39,128,185),1.4};
  if(type==1||type==2)return {rgba(238,174,119),2.2};
  return {rgba(107,96,88),1.15};
 }
 U32 area(int type)const{
  auto found=areas.find(type);if(found!=areas.end())return found->second;
  if(type==0x32||type==0x39||type==0x3b||type==0x3c||type==0x3d||type==0x3e||type==0x3f)return rgba(210,231,196);
  if(type==0x28||type==0x29||type==0x40||type==0x41)return rgba(161,203,222);
  return rgba(241,240,229,140);
 }
};
static Styles gStyles;
static void drawIcon(Image&img,const Icon&icon,XY pt){
 if(icon.w==0||icon.h==0)return;
 const int x0=int(std::round(pt.x))-icon.w*gAA/2,y0=int(std::round(pt.y))-icon.h*gAA/2;
 for(int y=0;y<icon.h;y++)for(int x=0;x<icon.w;x++){
  U32 c=icon.pixels[size_t(y)*icon.w+x];if(!(c&255))continue;
  for(int a=0;a<gAA;a++)for(int b=0;b<gAA;b++)img.px(x0+x*gAA+b,y0+y*gAA+a,c);
 }
}
static void patternedStroke(Image&img,const std::vector<XY>&poly,const Style&st){
 if(poly.size()<2)return;
 if(!st.hasPattern()&&st.casingWidth>st.width&&st.casing){
  Style underneath;underneath.color=st.casing;underneath.width=st.casingWidth;
  patternedStroke(img,poly,underneath);
 }
 double arc=0;
 for(size_t i=1;i<poly.size();i++){
  XY a=poly[i-1],b=poly[i];double dx=b.x-a.x,dy=b.y-a.y,len=std::hypot(dx,dy);
  if(len<1e-6)continue;
  // Pixel-center footprint and fixed phase along the FULL polyline, not per segment.
  const double half=gAA*(st.hasPattern()?std::max(0.5,st.rows*.5):std::max(.5,st.width*.5));
  int x0=std::max(0,int(std::floor(std::min(a.x,b.x)-half-1))),x1=std::min(img.w-1,int(std::ceil(std::max(a.x,b.x)+half+1)));
  int y0=std::max(0,int(std::floor(std::min(a.y,b.y)-half-1))),y1=std::min(img.h-1,int(std::ceil(std::max(a.y,b.y)+half+1)));
  if(x0<=x1&&y0<=y1)for(int y=y0;y<=y1;y++)for(int x=x0;x<=x1;x++){
   double t=((x+.5-a.x)*dx+(y+.5-a.y)*dy)/(len*len);
   if(t<0||t>1)continue;
   double perp=((x+.5-a.x)*(-dy)+(y+.5-a.y)*dx)/len;
   if(st.hasPattern()){
    int py=int(std::floor(perp/gAA+st.rows*.5));if(py<0||py>=st.rows)continue;
    int px=int(std::floor((arc+t*len)/gAA))%32;
    U32 c=st.pattern[size_t(py)*32+px];if(c&255)img.px(x,y,c);
   }else if(std::abs(perp)<=gAA*st.width*.5){img.px(x,y,st.color);}
  }
  // Round end caps improve multi-segment continuity where segments meet.
  if(!st.hasPattern())img.disc(b.x,b.y,gAA*st.width*.5,st.color);
  arc+=len;
 }
}
// Bitstream for classic Garmin polyline geometry (LSB first).
struct Bits{
 const U8*p;size_t bytes,pos=0;explicit Bits(const U8*b,size_t n):p(b),bytes(n){}
 U32 get(int count){if(count<0||count>31||pos+size_t(count)>bytes*8)fail("short Garmin geometry bitstream");
  U32 v=0;for(int i=0;i<count;i++){size_t at=pos++;v|=U32((p[at/8]>>(at%8))&1)<<i;}return v;}
 size_t available()const{return bytes*8-pos;}
};
static int coordBits(int base,bool signedVals){int n=2+(base<=9?base:2*base-9)+(signedVals?1:0);return n;}
static std::vector<std::pair<double,double>> decodePoly(const U8*p,size_t avail,int cx,int cy,int shift,bool isLine,bool extended,int&type,size_t&used){
 std::vector<std::pair<double,double>>pts; used=0;
 if(extended){
  if(avail<8)return pts;type=0x10000+(int(p[0])<<8)+(p[1]&0x1f);int dx=s16(p+2),dy=s16(p+4);
  size_t k=6;int blen=0;if((p[k]&1)==0){if(avail<k+2)return pts;blen=int(u16(p+k)>>2)-1;k+=2;}else {blen=int(p[k]>>1)-1;k++;}
  if(blen<0||blen>65536||avail<k+1+size_t(blen))return pts;
  U8 base=p[k++];const U8*payload=p+k;used=k+size_t(blen)+((p[1]&0x20)?3:0);if(used>avail)return {};
  int bx=0,by=0;bool xs=true,ys=true;int nx=1,ny=1;Bits b(payload,size_t(blen));
  if(b.available()<4)return pts;
  bool xSame=b.get(1);if(xSame){xs=false;nx=b.get(1)?-1:1;}bool ySame=b.get(1);if(ySame){ys=false;ny=b.get(1)?-1:1;}bool extra=b.get(1);
  bx=coordBits(base&15,xs)+(extra?1:0);by=coordBits(base>>4,ys)+(extra?1:0);
  if(bx>29||by>29)return pts;
  int x=cx+(dx*(1<<shift)),y=cy+(dy*(1<<shift));pts.emplace_back(geo(x),geo(y));
  for(int i=0;i<12000&&b.available()>=size_t(bx+by);i++){
   auto signedDelta=[&](int n,bool haveSign,int direction){
     int v=int(b.get(n));if(!haveSign)return v*direction;
     const int sign=1<<(n-1);
     int accum=0;
     while(v==sign && b.available()>=size_t(n)){accum+=v-1;v=int(b.get(n));}
     return v<sign?accum+v:v-2*sign-accum;
    };
   int vx=signedDelta(bx,xs,nx),vy=signedDelta(by,ys,ny);
   x+=vx*(1<<shift);y+=vy*(1<<shift);pts.emplace_back(geo(x),geo(y));
  }return pts;
 }
 if(avail<10)return pts;
 bool longlen=(p[0]&0x80)!=0;type=p[0]&(isLine?0x3f:0x7f);int dx=s16(p+4),dy=s16(p+6);
 size_t k=8;int blen=longlen?int(u16(p+k)):int(p[k]);k+=longlen?2:1;
 if(blen<0||blen>65536||avail<k+1+size_t(blen))return pts;
 U8 base=p[k++];const U8*payload=p+k;used=k+size_t(blen);
 if(!blen)return pts;
 Bits b(payload,size_t(blen));bool xs=true,ys=true;int nx=1,ny=1;
 if(!b.available())return pts;
 bool xSame=b.get(1);if(xSame){xs=false;nx=b.get(1)?-1:1;}bool ySame=b.get(1);if(ySame){ys=false;ny=b.get(1)?-1:1;}
 int bx=coordBits(base&15,xs),by=coordBits(base>>4,ys);
 if(bx>29||by>29)return pts;
 int x=cx+(dx*(1<<shift)),y=cy+(dy*(1<<shift));pts.emplace_back(geo(x),geo(y));
 bool extra=(u24(p+1)&0x400000)!=0;
 for(int i=0;i<12000&&b.available()>=size_t(bx+by+(extra?1:0));i++){
  if(extra)b.get(1);
  auto signedDelta=[&](int n,bool haveSign,int direction){
    int v=int(b.get(n));if(!haveSign)return v*direction;
    const int sign=1<<(n-1);
    int accum=0;
    while(v==sign && b.available()>=size_t(n)){accum+=v-1;v=int(b.get(n));}
    return v<sign?accum+v:v-2*sign-accum;
   };
  int vx=signedDelta(bx,xs,nx),vy=signedDelta(by,ys,ny);
  x+=vx*(1<<shift);y+=vy*(1<<shift);pts.emplace_back(geo(x),geo(y));
 }return pts;
}
struct Label {XY pt;std::string value;int priority=0;};
#ifdef __linux__
// Optional runtime integration with libcairo: no separate GIS or font package is shipped.
struct Cairo {
 void*module=nullptr;
 using Surface=void;using Context=void;
 Surface*(*createSurface)(int,int,int)=nullptr;
 Context*(*createContext)(Surface*)=nullptr;
 void(*destroyContext)(Context*)=nullptr;
 void(*destroySurface)(Surface*)=nullptr;
 void(*flush)(Surface*)=nullptr;
 unsigned char*(*pixels)(Surface*)=nullptr;
 int(*stride)(Surface*)=nullptr;
 void(*font)(Context*,const char*,int,int)=nullptr;
 void(*size)(Context*,double)=nullptr;
 void(*color)(Context*,double,double,double,double)=nullptr;
 void(*move)(Context*,double,double)=nullptr;
 void(*text)(Context*,const char*)=nullptr;
 bool available=false;
 Cairo(){
  module=dlopen("libcairo.so.2",RTLD_LAZY|RTLD_LOCAL);if(!module)return;
  auto bind=[&](auto&f,const char*name){void*p=dlsym(module,name);static_assert(sizeof(f)==sizeof(p));std::memcpy(&f,&p,sizeof(p));return p!=nullptr;};
  available=bind(createSurface,"cairo_image_surface_create")&&bind(createContext,"cairo_create")&&
   bind(destroyContext,"cairo_destroy")&&bind(destroySurface,"cairo_surface_destroy")&&
   bind(flush,"cairo_surface_flush")&&bind(pixels,"cairo_image_surface_get_data")&&
   bind(stride,"cairo_image_surface_get_stride")&&bind(font,"cairo_select_font_face")&&
   bind(size,"cairo_set_font_size")&&bind(color,"cairo_set_source_rgba")&&
   bind(move,"cairo_move_to")&&bind(text,"cairo_show_text");
 }
 ~Cairo(){if(module)dlclose(module);}
};
static Cairo cairo;
#endif
static void drawLabels(Image& img,std::vector<Label>&labels){
#ifdef __linux__
 if(!cairo.available||labels.empty())return;
 auto*surface=cairo.createSurface(0,img.w,img.h);if(!surface)return;
 auto*cr=cairo.createContext(surface);if(!cr){cairo.destroySurface(surface);return;}
 cairo.font(cr,"DejaVu Sans",0,0);cairo.size(cr,10*gAA);
 std::vector<std::array<double,4>> placed;
 std::stable_sort(labels.begin(),labels.end(),[](const Label&a,const Label&b){return a.priority>b.priority;});
 int counter=0;
 for(const auto&label:labels){
  if(label.value.empty()||counter>=85)continue;
  double px=label.pt.x,py=label.pt.y;
  if(px<-20*gAA||py<-20*gAA||px>=img.w+20*gAA||py>=img.h+20*gAA)continue;
  int nch=0;for(unsigned char c:label.value)if((c&0xc0)!=0x80)nch++;
  if(nch==0||nch>55)continue;
  double tw=std::max(15.0,5.7*nch)*gAA,th=11.0*gAA;
  double lx=px-tw*.5,ly=py-th*.5;
  bool crowded=false;
  for(const auto&rect:placed){if(lx<rect[2]+4*gAA&&lx+tw+4*gAA>rect[0]&&ly<rect[3]+4*gAA&&ly+th+4*gAA>rect[1]){crowded=true;break;}}
  if(crowded)continue;
  placed.push_back({lx,ly,lx+tw,ly+th});counter++;
  double x=lx,y=py+3*gAA;
  cairo.color(cr,1,1,1,.92);
  for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++){
   if(!dx&&!dy)continue;cairo.move(cr,x+dx*gAA,y+dy*gAA);cairo.text(cr,label.value.c_str());
  }
  cairo.color(cr,.10,.13,.13,.92);cairo.move(cr,x,y);cairo.text(cr,label.value.c_str());
 }
 cairo.flush(surface);
 unsigned char*data=cairo.pixels(surface);int stride=cairo.stride(surface);
 if(data&&stride>=img.w*4)for(int y=0;y<img.h;y++)for(int x=0;x<img.w;x++){
  U8*v=data+size_t(y)*stride+x*4;int alpha=v[3];if(!alpha)continue;
  // Cairo ARGB32 in little-endian memory: BGRA premultiplied.
  int r=std::min(255,int(v[2])*255/alpha),g=std::min(255,int(v[1])*255/alpha),b=std::min(255,int(v[0])*255/alpha);
  img.px(x,y,rgba(r,g,b,alpha));
 }
 cairo.destroyContext(cr);cairo.destroySurface(surface);
#else
 (void)img;(void)labels;
#endif
}
struct Options{std::string input,output;int minZoom=10,maxZoom=14;bool info=false,fill=false,overwrite=false,resume=false,verbose=false,wholeMap=false,maxTilesExplicit=false,plan=false;BBox bbox;bool customBBox=false;U64 maxTiles=100000,maxBytes=2ULL*1024*1024*1024;int aa=2;};
static void help(){std::cout<<"garmin2osmand 0.3 - C++ Garmin IMG to OsmAnd SQLite raster map\n"
 "Usage: garmin2osmand --input MAP.img --output map.sqlitedb [options]\n"
 "  --info                inspect IMG without rendering\n"
 "  --bbox W,S,E,N        optional geographic region; omit for all mapped areas\n"
 "  --whole-map           entire IMG coverage; unlimited candidate budget by default\n"
 "  --plan                calculate tile counts only (no database created)\n"
 "  --min-zoom N          lowest zoom (default 10)\n"
 "  --max-zoom N          highest zoom (default 14)\n"
 "  --max-tiles N         candidate tile limit (default 100000, 0 = unlimited)\n"
 "  --max-bytes N         output limit (default 2147483648, 0 = unlimited)\n"
 "  --resume              resume with completed-row checkpoints\n"
 "  --overwrite           replace existing output\n"
 "  --fill                create opaque map tiles (default transparent overlay)\n"
 "  --verbose             print detailed per-zoom progress\n"
 "  --help                show this message\n";}
static Options args(int argc,char**argv){Options o;
 for(int i=1;i<argc;i++){
  std::string a=argv[i];auto value=[&](){if(i+1>=argc)fail("Missing value for "+a);return std::string(argv[++i]);};
  if(a=="--help"||a=="-h"){help();std::exit(0);}else if(a=="--input")o.input=value();else if(a=="--output")o.output=value();
  else if(a=="--aa")o.aa=std::stoi(value());else if(a=="--min-zoom")o.minZoom=std::stoi(value());else if(a=="--max-zoom")o.maxZoom=std::stoi(value());
  else if(a=="--max-tiles"){o.maxTiles=std::stoull(value());o.maxTilesExplicit=true;}else if(a=="--max-bytes")o.maxBytes=std::stoull(value());
  else if(a=="--resume")o.resume=true;else if(a=="--overwrite")o.overwrite=true;
  else if(a=="--plan")o.plan=true;else if(a=="--whole-map")o.wholeMap=true;else if(a=="--info")o.info=true;else if(a=="--fill")o.fill=true;else if(a=="--verbose")o.verbose=true;
  else if(a=="--bbox"){
   auto v=value();std::replace(v.begin(),v.end(),',',' ');double w,s,e,n;
   if(sscanf(v.c_str(),"%lf %lf %lf %lf",&w,&s,&e,&n)!=4)fail("Bad --bbox");o.bbox={w,s,e,n};o.customBBox=true;
  }else fail("Unknown argument "+a);
 }
 if(o.input.empty())fail("--input MAP.img required");if(!o.info&&!o.plan&&o.output.empty())fail("--output required");
 if(o.minZoom<0||o.maxZoom>19||o.minZoom>o.maxZoom)fail("Zoom range must satisfy 0 <= min <= max <= 19");
 if(o.customBBox&&(!o.bbox.valid()||o.bbox.s< -85.05112878||o.bbox.n>85.05112878))fail("Invalid --bbox (must be normal bounds within Web Mercator)");
 if(o.aa!=1&&o.aa!=2)fail("--aa must be 1 or 2");gAA=o.aa;
 if(o.resume&&o.overwrite)fail("Use only one of --resume or --overwrite");
 if(o.wholeMap&&o.customBBox)fail("Use --whole-map without --bbox");
 if(o.wholeMap&&!o.maxTilesExplicit)o.maxTiles=0;
 return o;
}
struct Db{
 sqlite3*db=nullptr;sqlite3_stmt*insert=nullptr;sqlite3_stmt*exists=nullptr;sqlite3_stmt*checkpoint=nullptr;
 explicit Db(const Options&o,const BBox&box){int rc=sqlite3_open_v2(o.output.c_str(),&db,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE,nullptr);if(rc!=SQLITE_OK)fail("sqlite open: "+std::string(sqlite3_errmsg(db)));
  exec("PRAGMA journal_mode=TRUNCATE");exec("PRAGMA synchronous=NORMAL");exec("PRAGMA cache_size=-2048");
  exec("CREATE TABLE IF NOT EXISTS tiles (x integer,y integer,z integer,s integer,image blob,PRIMARY KEY(x,y,z,s))");
  exec("CREATE TABLE IF NOT EXISTS info (tilenumbering,minzoom,maxzoom,title,tilesize,ellipsoid,inverted_y,timecolumn)");
  exec("CREATE TABLE IF NOT EXISTS progress (z integer PRIMARY KEY, y integer NOT NULL)");
  exec("CREATE TABLE IF NOT EXISTS converter_settings (key text PRIMARY KEY, value text NOT NULL)");
  std::ostringstream ss;ss<<std::setprecision(16)<<fs::file_size(o.input)<<'|'<<box.w<<','<<box.s<<','<<box.e<<','<<box.n<<'|'<<o.minZoom<<'-'<<o.maxZoom<<'|'<<o.fill<<'|'<<o.aa;
  std::string signature=ss.str();
  sqlite3_stmt*set=nullptr;
  check(sqlite3_prepare_v2(db,"SELECT value FROM converter_settings WHERE key='config'",-1,&set,nullptr));
  const int result=sqlite3_step(set);
  if(result==SQLITE_ROW){std::string old=reinterpret_cast<const char*>(sqlite3_column_text(set,0));sqlite3_finalize(set);
   if(old!=signature)fail("Resume settings do not match the existing database (IMG size/bbox/zoom/fill/AA). Use the exact original options or --overwrite.");
  }else{sqlite3_finalize(set);
   if(o.resume)std::cerr<<"Warning: resuming a legacy database without settings fingerprint; existing tile checks remain active\n";
   sqlite3_stmt*put=nullptr;check(sqlite3_prepare_v2(db,"INSERT OR REPLACE INTO converter_settings VALUES('config',?)",-1,&put,nullptr));
   sqlite3_bind_text(put,1,signature.c_str(),-1,SQLITE_TRANSIENT);
   check(sqlite3_step(put)==SQLITE_DONE?SQLITE_OK:SQLITE_ERROR);sqlite3_finalize(put);
  }
  // OsmAnd uses standard XYZ numbering for tilenumbering='simple'.
  if(o.resume){
    sqlite3_stmt*st=nullptr;check(sqlite3_prepare_v2(db,"SELECT tilenumbering,minzoom,maxzoom FROM info LIMIT 1",-1,&st,nullptr));
    if(sqlite3_step(st)!=SQLITE_ROW || std::string(reinterpret_cast<const char*>(sqlite3_column_text(st,0)))!="simple"
       || sqlite3_column_int(st,1)!=o.minZoom || sqlite3_column_int(st,2)!=o.maxZoom){
      sqlite3_finalize(st);fail("Resume requires matching OsmAnd simple XYZ tile metadata and zoom range");
    }
    sqlite3_finalize(st);
  }
  if(!o.resume){exec("DELETE FROM info");sqlite3_stmt*st=nullptr;const char*q="INSERT INTO info VALUES ('simple',?,?,?,256,0,0,'no')";
   check(sqlite3_prepare_v2(db,q,-1,&st,nullptr));sqlite3_bind_int(st,1,o.minZoom);sqlite3_bind_int(st,2,o.maxZoom);
   sqlite3_bind_text(st,3,"Garmin IMG overlay",-1,SQLITE_STATIC);check(sqlite3_step(st)==SQLITE_DONE?SQLITE_OK:SQLITE_ERROR);sqlite3_finalize(st);
  }
  check(sqlite3_prepare_v2(db,"INSERT OR REPLACE INTO tiles(x,y,z,s,image) VALUES (?,?,?,0,?)",-1,&insert,nullptr));
  check(sqlite3_prepare_v2(db,"SELECT 1 FROM tiles WHERE x=? AND y=? AND z=? AND s=0",-1,&exists,nullptr));
  check(sqlite3_prepare_v2(db,"INSERT INTO progress(z,y) VALUES(?,?) ON CONFLICT(z) DO UPDATE SET y=excluded.y",-1,&checkpoint,nullptr));exec("BEGIN");
 }
 ~Db(){if(insert)sqlite3_finalize(insert);if(exists)sqlite3_finalize(exists);if(checkpoint)sqlite3_finalize(checkpoint);if(db){sqlite3_exec(db,"COMMIT",nullptr,nullptr,nullptr);sqlite3_close(db);}}
 void check(int rc){if(rc!=SQLITE_OK)fail("SQLite failure: "+std::string(sqlite3_errmsg(db)));}
 void exec(const char*s){char*err=nullptr;int rc=sqlite3_exec(db,s,nullptr,nullptr,&err);if(rc!=SQLITE_OK){std::string msg=err?err:"sqlite error";sqlite3_free(err);fail(msg);}}
 bool has(int x,int y,int z){sqlite3_reset(exists);sqlite3_bind_int(exists,1,x);sqlite3_bind_int(exists,2,y);sqlite3_bind_int(exists,3,z);return sqlite3_step(exists)==SQLITE_ROW;}
 void put(int x,int y,int z,const std::vector<U8>&data){sqlite3_reset(insert);sqlite3_clear_bindings(insert);
  sqlite3_bind_int(insert,1,x);sqlite3_bind_int(insert,2,y);sqlite3_bind_int(insert,3,z);
  check(sqlite3_bind_blob(insert,4,data.data(),int(data.size()),SQLITE_TRANSIENT));
  if(sqlite3_step(insert)!=SQLITE_DONE)fail(sqlite3_errmsg(db));
 }
 int lastRow(int z){
  sqlite3_stmt*st=nullptr;check(sqlite3_prepare_v2(db,"SELECT y FROM progress WHERE z=?",-1,&st,nullptr));
  sqlite3_bind_int(st,1,z);int y=std::numeric_limits<int>::min();if(sqlite3_step(st)==SQLITE_ROW)y=sqlite3_column_int(st,0);
  sqlite3_finalize(st);return y;
 }
 void markRow(int z,int y){sqlite3_reset(checkpoint);sqlite3_clear_bindings(checkpoint);
  sqlite3_bind_int(checkpoint,1,z);sqlite3_bind_int(checkpoint,2,y);
  if(sqlite3_step(checkpoint)!=SQLITE_DONE)fail(sqlite3_errmsg(db));
 }
 void flush(){exec("COMMIT");exec("BEGIN");}
};
static BBox tileBounds(int z,int x,int y){double n=double(U64(1)<<z);return BBox{(double(x)/n)*360-180,invMercY((double(y)+1)/n), (double(x+1)/n)*360-180,invMercY(double(y)/n)};}
static XY project(double lon,double lat,int z,int tx,int ty){double world=256*double(U64(1)<<z);return XY{gAA*(mercX(lon)*world-tx*256),gAA*(mercY(lat)*world-ty*256)};}
static int chooseBits(const Map&m,int z){int target=std::min(24,z+8),bits=-1,diff=99;for(const auto&l:m.levels){int d=std::abs(l.bits-target);if(d<diff){diff=d;bits=l.bits;}}return bits;}
static void drawMapTile(const Map&m,int z,int x,int y,const BBox&b,Image&img,std::vector<Label>&labels){
 const auto&r=m.sections.at("RGN");int bits=chooseBits(m,z);if(bits<0)return;
 auto it=m.byBits.find(bits);if(it==m.byBits.end())return;
 std::vector<int> selected;std::set<int> seen;
 auto append=[&](int index){if(seen.insert(index).second)selected.push_back(index);};
 auto broad=m.broadByBits.find(bits);
 if(broad!=m.broadByBits.end())for(int index:broad->second)append(index);
 auto bucket=m.cellsByBits.find(bits);
 if(bucket!=m.cellsByBits.end()){
  for(int yy=int(std::floor(b.s));yy<=int(std::floor(b.n));yy++)
   for(int xx=int(std::floor(b.w));xx<=int(std::floor(b.e));xx++){
    auto found=bucket->second.find((xx+180)*256+(yy+90));
    if(found!=bucket->second.end())for(int index:found->second)append(index);
   }
 }
 for(int pass=0;pass<4;pass++)for(int index:selected){const auto&s=m.subs[size_t(index)];if(!s.bounds.overlaps(b))continue;
  // Limit reads to 8 MiB for a subdivision; prevents unbounded memory growth on malformed input.
  if(s.to<s.from||s.to-s.from>8*1024*1024)continue;
  auto buf=r.get(s.from,s.to-s.from);if(buf.empty())continue;
  int n=bool(s.flags&0x10)+bool(s.flags&0x20)+bool(s.flags&0x40)+bool(s.flags&0x80);
  if(n==0 || buf.size()<size_t((n-1)*2))continue;
  std::array<size_t,4>start{};size_t p=0,last=std::max(0,(n-1)*2);
  for(int k=0;k<4;k++)if(s.flags&(0x10<<k)){
   size_t off=(p==0?last:u16(buf.data()+2*(p-1)));start[size_t(k)]=off;p++;
  }
  bool offsetsValid=true;size_t previous=0;
  for(int k=0;k<4;k++)if(s.flags&(0x10<<k)){
    if(start[size_t(k)]<previous||start[size_t(k)]>buf.size())offsetsValid=false;
    previous=start[size_t(k)];
  }
  if(!offsetsValid)continue;
  auto drawList=[&](size_t startAt,size_t endAt,bool line,bool extended){
   if(startAt>=buf.size()||endAt>buf.size()||startAt>=endAt)return;
   size_t pos=startAt;int guard=0;
   while(pos<endAt&&guard++<50000){int ty=0;size_t used=0;
    size_t initial=pos;
    auto coord=decodePoly(buf.data()+pos,endAt-pos,s.cx,s.cy,s.shift,line,extended,ty,used);
    if(!used)break;pos+=used;if(coord.size()<(line?2:3))continue;
    std::vector<XY>poly;poly.reserve(coord.size());
    for(const auto&v:coord)if(std::abs(v.first)<=180&&std::abs(v.second)<=90)
       poly.push_back(project(v.first,v.second,z,x,y));
    if(line){
     if(z>=15&&poly.size()>1&&!extended&&initial+4<=buf.size()){
      U32 lbl=u24(buf.data()+initial+1);
      if(!(lbl&0x800000)){auto name=m.label(lbl&0x3fffff);
       if(ty>=0x20&&ty<=0x25&&!name.empty()){
        try{size_t idx=0;double feet=std::stod(name,&idx);
          if(idx>0&&idx==name.size())name=std::to_string(int(std::lround(feet/3.28084)))+" m";
        }catch(const std::exception&){}
       }
       if(!name.empty()&&std::hypot(poly.front().x-poly.back().x,poly.front().y-poly.back().y)>25*gAA)
        labels.push_back({poly[poly.size()/2],name,(ty>=0x20&&ty<=0x25)?0:1});
      }
     }
     auto st=gStyles.line(ty);patternedStroke(img,poly,st);
    }
    else img.polygon(poly,gStyles.area(ty));
   }
  };
  // Draw background polygons before lines. Classic subdivision layout: point, indexed point, line, polygon.
  if(pass==0 && (s.flags&0x80))drawList(start[3],buf.size(),false,false);
  if(pass==1 && (s.flags&0x40))drawList(start[2],(s.flags&0x80)?start[3]:buf.size(),true,false);
  // Points and extended features: extended sections are read independently.
  if(pass==3 && z>=15)for(int k=0;k<2;k++){
   if(!(s.flags&(0x10<<k)))continue;
   size_t st=start[size_t(k)];size_t en=buf.size();for(int kk=k+1;kk<4;kk++)if(s.flags&(0x10<<kk)){en=start[size_t(kk)];break;}
   for(size_t o=st;o+8<=en;){U8 type=buf[o];U32 info=u24(buf.data()+o+1);int xx=s.cx+s16(buf.data()+o+4)*(1<<s.shift),yy=s.cy+s16(buf.data()+o+6)*(1<<s.shift);
    int fullType=(int(type)<<8)|((info&0x800000)&&o+8<en?buf[o+8]:0);
    XY pt=project(geo(xx),geo(yy),z,x,y);
    auto icon=gStyles.icons.find(fullType);
    if(icon!=gStyles.icons.end())drawIcon(img,icon->second,pt);
    if(z>=15&& !(info&0x400000)){auto name=m.label(info&0x3fffff);
     if(!name.empty())labels.push_back({XY{pt.x,pt.y+11*gAA},name,2});}
    o+=((info&0x800000)?9:8);
   }
  }
  for(int k=0;k<3;k++){
   if(k==0&&pass!=0)continue;
   if(k==1&&pass!=2)continue;
   if(k==2&&(pass!=3||z<15))continue;
   if(!s.extLen[size_t(k)]||s.extLen[size_t(k)]>8*1024*1024)continue;
   auto ext=r.get(s.extFrom[size_t(k)],s.extLen[size_t(k)]);
   if(k==2){for(size_t o=0;o+6<=ext.size();){U8 sub=ext[o+1];size_t step=6+bool(sub&0x80)+3*bool(sub&0x20);if(o+step>ext.size())break;
    int xx=s.cx+s16(ext.data()+o+2)*(1<<s.shift),yy=s.cy+s16(ext.data()+o+4)*(1<<s.shift);
    auto p=project(geo(xx),geo(yy),z,x,y);
    int fullType=0x10000+(int(ext[o])<<8)+(sub&31);
    auto icon=gStyles.icons.find(fullType);
    if(icon!=gStyles.icons.end())drawIcon(img,icon->second,p);
    o+=step;
   }}else{
    size_t off=0;for(int guard=0;off<ext.size()&&guard<50000;guard++){
     int ty=0;size_t used=0;auto coords=decodePoly(ext.data()+off,ext.size()-off,s.cx,s.cy,s.shift,k==1,true,ty,used);
     if(!used)break;off+=used;if(coords.size()<(k==1?2u:3u))continue;
     std::vector<XY>ps;ps.reserve(coords.size());for(auto&v:coords)if(std::abs(v.first)<=180&&std::abs(v.second)<=90)
       ps.push_back(project(v.first,v.second,z,x,y));
     if(k==1){auto st=gStyles.line(ty);patternedStroke(img,ps,st);}
     else img.polygon(ps,gStyles.area(ty));
    }
   }
  }
 }
}
static void run(const Options&o){MapSet set(o.input);
 for(const auto&[name,parts]:set.parts){auto it=parts.find("TYP");if(it!=parts.end()){gStyles.load(it->second);break;}}
 std::cerr<<"Embedded Garmin TYP: "<<gStyles.parsed<<" line styles, "<<gStyles.patterns<<" patterned, "<<gStyles.polygonStyles<<" area styles, "<<gStyles.pointStyles<<" point icons\n";
 std::cout<<"Maps: "<<set.maps.size()<<"\n";
 for(const auto&m:set.maps){std::cout<<"  "<<m.name<<" bounds="<<m.bounds.w<<","<<m.bounds.s<<","<<m.bounds.e<<","<<m.bounds.n<<" subdivs="<<m.subs.size()<<" levels="<<m.levels.size()<<"\n";}
 if(o.info)return;
 BBox box=o.customBBox?o.bbox:set.bounds;
 box.w=std::clamp(box.w,-180.,180.);box.e=std::clamp(box.e,-180.,180.);
 box.s=std::clamp(box.s,-85.05112878,85.05112878);box.n=std::clamp(box.n,-85.05112878,85.05112878);
 if(!box.valid())fail("Invalid final bounding box");
 // Sparse regional scheduling: union each IMG submap's tile coverage, scanline by scanline.
 // This avoids allocating a 1.7M-element tile vector or iterating large uncharted rectangle gaps.
 struct TileRect{int x0,x1,y0,y1;};
 struct Coverage{int z=0,x0=0,x1=0,y0=0,y1=0;U64 rectangular=0,candidates=0;
  std::vector<TileRect> rects;
  static std::vector<std::pair<int,int>> rowRanges(const Coverage&r,int y){
   std::vector<std::pair<int,int>> spans;
   for(const auto&t:r.rects)if(t.y0<=y&&y<=t.y1)spans.emplace_back(t.x0,t.x1);
   std::sort(spans.begin(),spans.end());
   std::vector<std::pair<int,int>> merged;
   for(const auto&sp:spans){
    if(merged.empty()||sp.first>merged.back().second+1)merged.push_back(sp);
    else merged.back().second=std::max(merged.back().second,sp.second);
   }
   return merged;
  }
 };
 std::vector<Coverage> cov;U64 total=0,rectangular=0;
 auto toRect=[](const BBox&bb,int z)->TileRect{
  const int n=1<<z;
  const double we=std::clamp(bb.w,-180.0,180.0),ea=std::clamp(bb.e,-180.0,180.0);
  const double so=std::clamp(bb.s,-85.05112878,85.05112878),no=std::clamp(bb.n,-85.05112878,85.05112878);
  return TileRect{std::clamp(int(std::floor(mercX(we)*n)),0,n-1),
    std::clamp(int(std::floor(mercX(ea)*n)),0,n-1),
    std::clamp(int(std::floor(mercY(no)*n)),0,n-1),
    std::clamp(int(std::floor(mercY(so)*n)),0,n-1)};
 };
 for(int z=o.minZoom;z<=o.maxZoom;z++){
  const auto main=toRect(box,z);Coverage r;r.z=z;r.x0=main.x0;r.x1=main.x1;r.y0=main.y0;r.y1=main.y1;
  r.rectangular=U64(r.x1-r.x0+1)*U64(r.y1-r.y0+1);
  for(const auto&m:set.maps){
   if(!box.overlaps(m.bounds))continue;
   const BBox clipped{std::max(box.w,m.bounds.w),std::max(box.s,m.bounds.s),
     std::min(box.e,m.bounds.e),std::min(box.n,m.bounds.n)};
   if(!clipped.valid())continue;
   auto t=toRect(clipped,z);
   t.x0=std::max(t.x0,r.x0);t.x1=std::min(t.x1,r.x1);
   t.y0=std::max(t.y0,r.y0);t.y1=std::min(t.y1,r.y1);
   if(t.x0<=t.x1&&t.y0<=t.y1)r.rects.push_back(t);
  }
  for(int y=r.y0;y<=r.y1;y++){
   for(const auto&span:Coverage::rowRanges(r,y))r.candidates+=U64(span.second-span.first+1);
  }
  std::cout<<"Zoom "<<z<<": "<<r.candidates<<" coverage candidates vs "<<r.rectangular<<" rectangular\n";
  total+=r.candidates;rectangular+=r.rectangular;cov.push_back(std::move(r));
 }
 std::cout<<"Tile candidates: "<<total<<" (rectangle "<<rectangular<<", limit "
          <<(o.maxTiles?std::to_string(o.maxTiles):"unlimited")<<")\n";
 if(o.plan)return;
 if(o.maxTiles&&total>o.maxTiles)fail("Tile candidate limit exceeded. Use --whole-map (unlimited by default), or raise --max-tiles N; no database was created.");
 if(fs::exists(o.output)){
  if(o.overwrite)fs::remove(o.output);
  else if(!o.resume)fail("Output already exists (use --resume or --overwrite)");
 }
 Db db(o,box);U64 written=0,processed=0,resumed=0;
 for(const auto&r:cov){
  std::cerr<<"Zoom "<<r.z<<": "<<r.candidates<<" coverage candidate tiles\n";
  const int completed=o.resume?db.lastRow(r.z):std::numeric_limits<int>::min();
  for(int y=r.y0;y<=r.y1;y++){
   if(y<=completed)continue; // Prior full scanline was committed atomically.
   for(const auto&span:Coverage::rowRanges(r,y))for(int x=span.first;x<=span.second;x++){
    processed++;if(o.resume&&db.has(x,y,r.z)){resumed++;continue;}
    BBox b=tileBounds(r.z,x,y);Image img(256*gAA);std::vector<Label> labels;
    if(o.fill)for(int yy=0;yy<img.h;yy++)for(int xx=0;xx<img.w;xx++)img.px(xx,yy,rgba(246,246,239));
    for(const auto&m:set.maps)if(b.overlaps(m.bounds))drawMapTile(m,r.z,x,y,b,img,labels);
    drawLabels(img,labels);
    if(!img.painted)continue;
    auto bytes=img.downsample(gAA).png();db.put(x,y,r.z,bytes);written++;
    if(written%128==0){db.flush();
     if(o.maxBytes&&fs::file_size(o.output)>o.maxBytes)fail("Output byte budget exceeded; database is resumable. Increase --max-bytes and repeat with --resume.");
     if(o.verbose)std::cerr<<"  checked="<<processed<<" written="<<written<<" already="<<resumed<<" DB="<<fs::file_size(o.output)/1048576<<"MiB\n";
    }
   }
   db.markRow(r.z,y);db.flush();
   if(o.maxBytes&&fs::file_size(o.output)>o.maxBytes)fail("Output byte budget exceeded after completed row; database is resumable. Increase --max-bytes and repeat with --resume.");
  }
  std::cerr<<"Zoom "<<r.z<<" complete; newly written="<<written<<"\n";
 }
 std::cout<<"Total candidate tiles visited="<<processed<<", already existed="<<resumed<<"\n";
 if(written==0 && !o.resume)fail("No geographic features were rasterized: verify IMG format, --bbox and zoom/detail levels. Output may contain metadata only.");
 std::cout<<"Finished: "<<written<<" tiles -> "<<o.output<<"\n";
}
#ifndef GARMIN_LIBRARY_MODE
int main(int argc,char**argv){try{Options o=args(argc,argv);run(o);return 0;}catch(const std::exception&e){std::cerr<<"ERROR: "<<e.what()<<"\n";return 1;}}
#endif
