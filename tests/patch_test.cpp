#include "../source/rom_patch.h"
#include <cassert>
static void advanced();
int main(){advanced();using namespace rom_patch;std::vector<uint8_t> r={1,2,3},p={'P','A','T','C','H',0,0,1,0,1,9,'E','O','F'};assert(ips(r,p)&&r[1]==9);auto old=r;p.pop_back();assert(!ips(r,p)&&r==old);p={'P','A','T','C','H',0,0,3,0,0,0,3,7,'E','O','F'};assert(ips(r,p)&&r.size()==6&&r[5]==7);p={'P','A','T','C','H',255,255,255,0,1,0,'E','O','F'};old=r;assert(!ips(r,p)&&r==old);}

static void footer(std::vector<uint8_t>& p,const std::vector<uint8_t>& a,const std::vector<uint8_t>& b){auto put=[&](uint32_t v){for(int i=0;i<4;i++)p.push_back(uint8_t(v>>(8*i)));};put(rom_patch::crc(a,a.size()));put(rom_patch::crc(b,b.size()));put(rom_patch::crc(p,p.size()));}
static void advanced(){using namespace rom_patch;std::vector<uint8_t> a={1,2,3},b={1,9,3},r=a;
 std::vector<uint8_t> p={'B','P','S','1',0x83,0x83,0x80,0x80,0x81,9,0x80};footer(p,a,b);assert(bps(r,p)&&r==b);auto first=p;auto other=b;other[1]=8;auto second=first;second.resize(second.size()-12);second[9]=8;footer(second,a,other);assert(crc(first,first.size())==crc(second,second.size()));assert(identity(first)!=identity(second));r=a;p.back()^=1;assert(!bps(r,p)&&r==a);
 p={'U','P','S','1',0x83,0x83,0x81,11,0};footer(p,a,b);assert(ups(r,p)&&r==b);r=a;p[7]^=1;assert(!ups(r,p)&&r==a);
}
