#include "../source/input_policy.h"
#include <cassert>
int main(){
 using namespace input_policy;
 Turbo a,b;
 assert(a.step(0,true,4));assert(a.step(0,true,4));assert(!a.step(0,true,4));
 assert(b.step(0,true,4));assert(a.step(1,true,4));
 assert(!a.step(0,false,4));assert(a.step(0,true,4));
 a.reset();assert(a.step(0,true,4));
 Reconnect r;assert(r.filter(true,1)==0);assert(r.filter(true,0)==0);
 assert(r.filter(true,1)==1);assert(r.filter(false,1)==0);
 assert(r.filter(true,1)==0);assert(r.filter(true,0)==0);assert(r.filter(true,2)==2);
 assert(clearOpposites(7,1,2)==4);assert(clearOpposites(5,1,2)==5);
}
