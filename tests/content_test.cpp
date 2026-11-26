#include "../source/content_policy.h"
#include <cassert>
int main(){using namespace content_policy;assert(normalize("sdmc:")=="sdmc:/");assert(normalize("sdmc:/roms/snes/")=="sdmc:/roms/snes");assert(normalize("sdmc:/roms/../x").empty());assert(normalize("/tmp").empty());std::vector<std::string> d;assert(add(d,"sdmc:/roms/"));assert(!add(d,"sdmc:/roms"));for(int i=0;i<15;i++)assert(add(d,"sdmc:/"+std::to_string(i)));assert(!add(d,"sdmc:/extra"));}
