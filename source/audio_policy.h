#pragma once
#include <algorithm>
namespace audio_policy {
inline double boundedQueue(double estimate,int outstanding) {
 return std::max(0.0,std::min(estimate,(double)outstanding));
}
struct Recovery {
 int boost=0, stable=0;
 void reset(){boost=stable=0;}
 int target(int base) const {return base+boost;}
 void starved(){boost=std::min(1440,boost+480);stable=0;}
 void tick(){if(++stable>=600){boost=std::max(0,boost-480);stable=0;}}
};
}
