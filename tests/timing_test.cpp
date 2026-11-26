#include "../source/timing_policy.h"
#include <cassert>
int main(){using namespace timing_policy;for(bool pal:{false,true})for(int display:{60,120}){double a=0;int count=0;for(int i=0;i<display*10;i++)count+=due(a,1000.0/display,rate(pal));assert(std::abs(count-rate(pal)*10)<1.1);}double a=0;assert(due(a,-1,rate(true))==0);assert(due(a,10000,rate(false))<=3);}
