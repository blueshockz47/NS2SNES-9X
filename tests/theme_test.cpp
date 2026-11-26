#include "../source/ui_theme.h"
#include <cassert>
int main(){using namespace ui_theme;assert(get(0,0).bg==0x0b0d12);assert(get(1,0).bg==0);assert(get(2,0).text==0x182233);assert(get(3,0).accent==0xb99aef);assert(get(3,2).accent==0x3984dc);assert(get(4,0).bg==0xdedbd2);assert(get(5,0).accent==0x20d9ef);assert(get(6,0).accent==0x7cdb62);assert(get(7,0).accent==0x42c7c9);assert(get(8,0).bg==get(0,0).bg);assert(get(-1,99).bg==get(0,0).bg);for(int i=0;i<8;++i){auto p=get(i,0);assert(p.text!=p.bg&&p.text!=p.panel&&p.sel!=p.bg);}}
