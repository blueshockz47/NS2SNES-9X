#include "../source/audio_policy.h"
#include <cassert>
int main(){
 using namespace audio_policy;
 assert(boundedQueue(-1,800)==0);
 assert(boundedQueue(900,800)==800);
 assert(boundedQueue(400,800)==400);
 assert(boundedQueue(800,0)==0);
 Recovery r; assert(r.target(1920)==1920);
 for(int i=0;i<10;++i)r.starved();
 assert(r.target(1920)==3360);
 for(int i=0;i<599;++i)r.tick();
 assert(r.boost==1440);r.tick();assert(r.boost==960);
 for(int i=0;i<1200;++i)r.tick();
 assert(r.boost==0);r.starved();r.reset();assert(r.boost==0);
}
