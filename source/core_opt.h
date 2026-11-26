#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#if defined(__aarch64__) && defined(__ARM_NEON)
#include <arm_neon.h>
#endif
namespace core_opt {
inline uint32_t pixel(uint16_t p) {
  uint32_t r=(p>>11)&31,g=(p>>5)&63,b=p&31;
  return 0xff000000u | (((b<<3)|(b>>2))<<16) | (((g<<2)|(g>>4))<<8) | ((r<<3)|(r>>2));
}
inline void convert(const uint16_t* in,uint32_t* out,size_t n) {
  size_t i=0;
#if defined(__aarch64__) && defined(__ARM_NEON)
  for (;n-i>=8;i+=8) {
    uint16x8_t p=vld1q_u16(in+i);
    uint16x8_t r=vshrq_n_u16(p,11),g=vandq_u16(vshrq_n_u16(p,5),vdupq_n_u16(63)),b=vandq_u16(p,vdupq_n_u16(31));
    uint8x8x4_t rgba;
    rgba.val[0]=vmovn_u16(vorrq_u16(vshlq_n_u16(r,3),vshrq_n_u16(r,2)));
    rgba.val[1]=vmovn_u16(vorrq_u16(vshlq_n_u16(g,2),vshrq_n_u16(g,4)));
    rgba.val[2]=vmovn_u16(vorrq_u16(vshlq_n_u16(b,3),vshrq_n_u16(b,2)));
    rgba.val[3]=vdup_n_u8(255);
    vst4_u8(reinterpret_cast<uint8_t*>(out+i),rgba);
  }
#endif
  for (;i<n;++i) out[i]=pixel(in[i]);
}
inline size_t equalPrefix(const uint8_t* a,const uint8_t* b,size_t n) {
  size_t i=0;
  for (;n-i>=8;i+=8) {
    uint64_t x,y; std::memcpy(&x,a+i,8); std::memcpy(&y,b+i,8);
    if (x!=y) break;
  }
  while(i<n && a[i]==b[i]) ++i;
  return i;
}
}
