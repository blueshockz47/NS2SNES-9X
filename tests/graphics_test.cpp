#include "../source/graphics_policy.h"
#include <cassert>
int main(){using namespace graphics_policy;assert(validUpload(256,224,512,512,478));assert(!validUpload(513,224,512,512,478));assert(!validUpload(256,224,255,512,478));assert(!validUpload(0,224,512,512,478));assert(shaderChoice(4,11,true)==4);assert(shaderChoice(11,11,true)==0);assert(shaderChoice(4,11,false)==0);assert(signalReady(1,true,true));assert(!signalReady(1,true,false));}
