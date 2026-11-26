#pragma once
#include <vector>
#include <cstdint>
#include <cstddef>
#include <algorithm>
#include <cstring>
namespace rom_patch {
constexpr size_t limit=12*1024*1024;
inline bool ips(std::vector<uint8_t>& rom,const std::vector<uint8_t>& p){
 if(p.size()<8||std::memcmp(p.data(),"PATCH",5)||rom.size()>limit)return false;
 auto out=rom;size_t i=5;
 auto read=[&](size_t n){size_t v=0;while(n--)v=(v<<8)|p[i++];return v;};
 while(p.size()-i>=3){
  if(!std::memcmp(p.data()+i,"EOF",3)){
   i+=3;if(p.size()-i==3){size_t n=read(3);if(n>limit)return false;out.resize(n);}
   if(i!=p.size())return false;rom.swap(out);return true;
  }
  size_t off=read(3);if(p.size()-i<2)return false;size_t n=read(2);
  bool rle=n==0;uint8_t val=0;
  if(rle){if(p.size()-i<3)return false;n=read(2);val=p[i++];if(!n)return false;}
  else if(p.size()-i<n)return false;
  if(off>limit||n>limit-off)return false;
  if(out.size()<off+n)out.resize(off+n);
  if(rle)std::fill_n(out.begin()+off,n,val);
  else{std::copy_n(p.begin()+i,n,out.begin()+off);i+=n;}
 }
 return false;
}

inline uint32_t crc(const std::vector<uint8_t>& d,size_t n){
 uint32_t c=~0u;for(size_t i=0;i<n;i++){c^=d[i];for(int j=0;j<8;j++)c=(c>>1)^(0xedb88320u & (0u-(c&1)));}return ~c;
}
// BPS/UPS include their own CRC footer; hashing that footer yields a fixed residue.
inline uint32_t identity(const std::vector<uint8_t>& p){
 bool footer=p.size()>=16&&(!std::memcmp(p.data(),"BPS1",4)||!std::memcmp(p.data(),"UPS1",4));
 return crc(p,p.size()-(footer?4:0));
}
inline uint32_t le(const std::vector<uint8_t>& p,size_t i){return uint32_t(p[i])|(uint32_t(p[i+1])<<8)|(uint32_t(p[i+2])<<16)|(uint32_t(p[i+3])<<24);}
struct Reader {
 const std::vector<uint8_t>& p;size_t i,end;
 bool num(uint64_t& v){v=0;uint64_t shift=1;for(int k=0;k<5&&i<end;k++){uint8_t b=p[i++];v+=(b&127)*shift;if(b&128)return v<=uint64_t(limit)*4;shift*=128;v+=shift;}return false;}
};
inline bool header(const std::vector<uint8_t>& r,const std::vector<uint8_t>& p,const char* magic){
 return r.size()<=limit&&p.size()>=16&&!std::memcmp(p.data(),magic,4)&&crc(p,p.size()-4)==le(p,p.size()-4)&&crc(r,r.size())==le(p,p.size()-12);
}
inline bool bps(std::vector<uint8_t>& r,const std::vector<uint8_t>& p){
 if(!header(r,p,"BPS1"))return false;Reader a{p,4,p.size()-12};uint64_t s,n,meta;
 if(!a.num(s)||!a.num(n)||!a.num(meta)||s!=r.size()||n>limit||meta>a.end-a.i)return false;a.i+=meta;
 std::vector<uint8_t> out;out.reserve(n);int64_t src=0,dst=0;
 while(out.size()<n){uint64_t cmd;if(!a.num(cmd))return false;size_t len=(cmd>>2)+1,pos=out.size();if(len>n-pos)return false;
 switch(cmd&3){
 case 0:if(pos>r.size()||len>r.size()-pos)return false;out.insert(out.end(),r.begin()+pos,r.begin()+pos+len);break;
 case 1:if(len>a.end-a.i)return false;out.insert(out.end(),p.begin()+a.i,p.begin()+a.i+len);a.i+=len;break;
 default:{uint64_t delta;if(!a.num(delta))return false;int64_t& off=(cmd&3)==2?src:dst;off+=(delta&1)?-int64_t(delta>>1):int64_t(delta>>1);
 for(size_t k=0;k<len;k++){const auto& from=(cmd&3)==2?r:out;if(off<0||uint64_t(off)>=from.size())return false;uint8_t v=from[size_t(off++)];out.push_back(v);}break;}
 }}
 if(a.i!=a.end||crc(out,out.size())!=le(p,p.size()-8))return false;r.swap(out);return true;
}
inline bool ups(std::vector<uint8_t>& r,const std::vector<uint8_t>& p){
 if(!header(r,p,"UPS1"))return false;Reader a{p,4,p.size()-12};uint64_t s,n;if(!a.num(s)||!a.num(n)||s!=r.size()||n>limit)return false;
 size_t span=std::max<size_t>(s,n);auto out=r;out.resize(span);uint64_t off=0;
 while(a.i<a.end){uint64_t d;if(!a.num(d)||off>span||d>span-off)return false;off+=d;bool ended=false;
 while(a.i<a.end){uint8_t v=p[a.i++];if(!v){++off;ended=true;break;}if(off>=span)return false;out[off++]^=v;}if(!ended)return false;}
 out.resize(n);if(crc(out,out.size())!=le(p,p.size()-8))return false;r.swap(out);return true;
}
}
