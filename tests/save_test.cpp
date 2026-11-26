#include "../source/save_policy.h"
#include <cassert>
int main(){using namespace save_policy;assert(slots==10);assert(!valid(0));assert(!valid(-1));assert(valid(limit));assert(!valid(limit+1));assert(checksum("hello",5)==0x4f9f2cabu);assert(checksum("hello",5)!=checksum("hella",5));size_t off=0;auto d=pack("hello",5);assert(inspect(d,off)==Format::verified&&off==16);d.back()^=1;assert(inspect(d,off)==Format::invalid);d=pack("hello",5);d.pop_back();assert(inspect(d,off)==Format::invalid);d={'a','b'};assert(inspect(d,off)==Format::legacy&&off==0);assert(pack(nullptr,5).empty());}
