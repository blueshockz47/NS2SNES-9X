#include "../source/library_policy.h"
#include <cassert>
int main(){
  assert(library::movePage(0,784,14,1)==14);
  assert(library::movePage(783,784,14,1)==783);
  assert(library::movePage(13,15,14,1)==14);
  assert(library::movePage(0,0,14,1)==0);
  library::PageRepeat repeat;
  assert(repeat.update(1,true,true,0)==1);
  assert(repeat.update(1,false,true,349)==0);
  assert(repeat.update(1,false,true,350)==1);
  assert(repeat.update(1,false,true,469)==0);
  assert(repeat.update(1,false,true,470)==1);
  assert(repeat.update(1,false,true,1200)==1);
  assert(repeat.update(1,false,true,1269)==0);
  assert(repeat.update(1,false,true,1270)==1);
  assert(repeat.update(1,false,false,1300)==0);
  assert(repeat.update(1,false,true,1500)==0);
  assert(repeat.update(0,false,true,1501)==0);
  assert(repeat.update(-1,true,true,1502)==-1);
assert(library::matches("Super Mario World","mARIO"));assert(!library::matches("F-Zero","Mario"));assert(library::matches("F-Zero",""));assert(library::pageTop(7,4,2)==0);assert(library::pageTop(8,4,2)==8);}
