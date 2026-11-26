#pragma once
#include <cstdint>
namespace input_policy {
struct Turbo {
 unsigned phase[6]={};
 void reset(){for(auto &p:phase)p=0;}
 bool step(int button,bool held,unsigned period){
   if(!held||period<2){phase[button]=0;return false;}
   bool on=phase[button]*2<period;
   phase[button]=(phase[button]+1)%period;
   return on;
 }
};
struct Reconnect {
 bool connected=false, armed=false;
 uint64_t filter(bool present,uint64_t bits){
   if(!present){connected=armed=false;return 0;}
   if(!connected){connected=true;armed=false;}
   if(!armed){if(!bits)armed=true;return 0;}
   return bits;
 }
};
inline int clearOpposites(int p,int a,int b){return (p&(a|b))==(a|b)?p&~(a|b):p;}
}
