#pragma once
#include <algorithm>
namespace adaptive {
enum Profile { Manual, Battery, Balanced, Performance, LowLatency };
struct Governor {
  int penalty=0, bad=0, good=0;
  void reset(){penalty=bad=good=0;}
  void sample(double utilization,bool valid) {
    if(!valid){bad=good=0;return;}
    if(utilization>0.85){good=0;if(++bad>=2){penalty=std::min(4,penalty+1);bad=0;}}
    else if(utilization<0.55){bad=0;if(++good>=8){penalty=std::max(0,penalty-1);good=0;}}
    else {bad=good=0;}
  }
  int ahead(int profile,int requested) const {
    if(profile==Manual)return requested;
    int cap=profile==Battery?0:profile==Balanced?1:2;
    return std::max(0,std::min(cap,requested)-std::max(0,penalty-1));
  }
  int delay(int profile,int requested) const {
    if(profile==Manual)return requested;
    int cap=profile==Battery||profile==Performance?0:profile==Balanced?1:2;
    return std::max(0,std::min(cap,requested)-penalty);
  }
};
}
