#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include <cstring>
namespace save_policy {
constexpr int slots=10;
constexpr long limit=16*1024*1024;
inline bool valid(long n){return n>0&&n<=limit;}
inline uint32_t checksum(const void* data,size_t n){auto p=static_cast<const uint8_t*>(data);uint32_t h=2166136261u;for(size_t i=0;i<n;++i)h=(h^p[i])*16777619u;return h;}
constexpr long fileLimit=limit+16;
inline void put(std::vector<uint8_t>& d,uint32_t n){for(int i=0;i<4;i++)d.push_back(uint8_t(n>>(8*i)));}
inline uint32_t get(const uint8_t* p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
inline std::vector<uint8_t> pack(const void* data,long n){
 if(!data||!valid(n))return {};
 std::vector<uint8_t> d;d.reserve(n+16);const uint8_t magic[]={'N','S','2','S','T','A','T','E'};d.insert(d.end(),magic,magic+8);put(d,uint32_t(n));put(d,checksum(data,n));auto b=static_cast<const uint8_t*>(data);d.insert(d.end(),b,b+n);return d;
}
enum class Format{legacy,verified,invalid};
inline Format inspect(const std::vector<uint8_t>& d,size_t& offset){
 offset=0;if(d.size()>=8&&!std::memcmp(d.data(),"NS2STATE",8)){
 if(d.size()<16||d.size()>size_t(fileLimit)||get(d.data()+8)!=d.size()-16||!valid(long(d.size()-16))||get(d.data()+12)!=checksum(d.data()+16,d.size()-16))return Format::invalid;
 offset=16;return Format::verified;
 }
 return d.size()<=size_t(limit)&&!d.empty()?Format::legacy:Format::invalid;
}
}
