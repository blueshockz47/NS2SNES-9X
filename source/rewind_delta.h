#pragma once
#include <cstdint>
#include <cstring>
#include <vector>
#include <cstddef>
namespace rewind_detail {
// Validate the entire stream before modifying the destination.
inline bool applyDelta(std::vector<uint8_t>& s, const std::vector<uint8_t>& d) {
  for (int pass=0; pass<2; ++pass) {
    size_t i=0, p=0;
    while (p<d.size()) {
      if (d.size()-p<8) return false;
      uint32_t z,l;
      std::memcpy(&z,d.data()+p,4); std::memcpy(&l,d.data()+p+4,4); p+=8;
      if ((!z && !l) || z>s.size()-i) return false;
      i+=z;
      if (l>s.size()-i || l>d.size()-p) return false;
      if (pass) for (size_t k=0;k<l;++k) s[i+k]^=d[p+k];
      i+=l; p+=l;
    }
    if (i!=s.size()) return false;
  }
  return true;
}
}
