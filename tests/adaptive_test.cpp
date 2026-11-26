#include "../source/adaptive_performance.h"
#include <cassert>
int main(){
 using namespace adaptive; Governor g;
 assert(g.ahead(Manual,2)==2 && g.delay(Manual,2)==2);
 assert(g.ahead(Battery,2)==0 && g.delay(Battery,2)==0);
 assert(g.ahead(Balanced,2)==1 && g.delay(Balanced,2)==1);
 assert(g.ahead(Performance,2)==2 && g.delay(Performance,2)==0);
 assert(g.ahead(LowLatency,2)==2 && g.delay(LowLatency,2)==2);
 g.sample(.9,true); assert(g.penalty==0);
 g.sample(.9,true); assert(g.penalty==1 && g.delay(LowLatency,2)==1);
 for(int i=0;i<6;i++)g.sample(.9,true);
 assert(g.penalty==4 && g.ahead(LowLatency,2)==0);
 for(int i=0;i<7;i++)g.sample(.4,true);
 assert(g.penalty==4);g.sample(.4,true);assert(g.penalty==3);
 g.sample(.9,false);assert(g.bad==0 && g.good==0);
 assert(g.ahead(Manual,2)==2 && g.delay(Manual,2)==2);
 g.reset();assert(g.penalty==0);
}
