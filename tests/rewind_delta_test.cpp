#include "../source/rewind_delta.h"
#include <cassert>
using namespace rewind_detail;
int main() {
  std::vector<uint8_t> s{1,2,3}, orig=s;
  std::vector<uint8_t> good{1,0,0,0,2,0,0,0,7,8};
  assert(applyDelta(s,good)); assert((s==std::vector<uint8_t>{1,5,11}));
  assert(applyDelta(s,good)); assert(s==orig);
  for (size_t n=0;n<good.size();++n) {
    auto bad=good; bad.resize(n); assert(!applyDelta(s,bad)); assert(s==orig);
  }
  for (auto bad: {std::vector<uint8_t>{255,255,255,255,0,0,0,0},std::vector<uint8_t>{0,0,0,0,0,0,0,0},std::vector<uint8_t>{1,0,0,0,0,0,0,0}}) {
    assert(!applyDelta(s,bad)); assert(s==orig);
  }
}
