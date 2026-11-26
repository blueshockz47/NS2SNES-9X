#include "../source/branding_migration.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <cstdlib>
namespace fs=std::filesystem;
int main(){
 char p[]="/tmp/ns2-brand-XXXXXX";assert(mkdtemp(p));
 auto root=fs::path(p),old=root/"old",next=root/"new";
 fs::create_directories(old/"covers");fs::create_directories(next);
 std::ofstream(old/"settings.json")<<"old";std::ofstream(next/"settings.json")<<"new";
 std::ofstream(old/"game.srm")<<"save";std::ofstream(old/"covers"/"game.png")<<"cover";
 std::ofstream(old/"ns2snes-9x.nro")<<"binary";
 assert(branding_migration::migrate(old.string(),next.string()));
 assert(fs::exists(old/"game.srm")&&fs::exists(next/"game.srm"));
 assert(fs::exists(next/"covers"/"game.png")&&!fs::exists(next/"ns2snes-9x.nro"));
 std::string s;std::ifstream(next/"settings.json")>>s;assert(s=="new");
 fs::remove(next/"game.srm");assert(branding_migration::migrate(old.string(),next.string()));assert(!fs::exists(next/"game.srm"));
 assert(branding_migration::migrate((root/"absent").string(),(root/"fresh").string()));
 fs::remove_all(root);
}
