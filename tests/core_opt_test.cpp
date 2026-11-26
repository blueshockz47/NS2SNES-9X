#include "../source/core_opt.h"
#include <cassert>
#include <vector>
int main() {
  std::vector<uint16_t> in(65536);
  std::vector<uint32_t> out(65538,0x12345678);
  for(size_t i=0;i<in.size();++i) in[i]=i;
  core_opt::convert(in.data(),out.data()+1,in.size());
  for(size_t i=0;i<in.size();++i) {
    unsigned r=(i>>11)&31,g=(i>>5)&63,b=i&31;
    assert(out[i+1]==(0xff000000u|(((b<<3)|(b>>2))<<16)|(((g<<2)|(g>>4))<<8)|((r<<3)|(r>>2))));
  }
  assert(out.front()==0x12345678 && out.back()==0x12345678);
  for(size_t n=0;n<40;++n) {
    std::vector<uint8_t> a(n+2,7),b=a;
    assert(core_opt::equalPrefix(a.data()+1,b.data()+1,n)==n);
    for(size_t k=0;k<n;++k) {b[k+1]=8;assert(core_opt::equalPrefix(a.data()+1,b.data()+1,n)==k);b[k+1]=7;}
    std::vector<uint32_t> tail(n+2,0x12345678);
    core_opt::convert(in.data()+1,tail.data()+1,n);
    for(size_t k=0;k<n;++k) assert(tail[k+1]==core_opt::pixel(in[k+1]));
    assert(tail.front()==0x12345678 && tail.back()==0x12345678);
  }
}
