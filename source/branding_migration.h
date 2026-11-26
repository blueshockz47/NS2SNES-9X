#pragma once
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <string>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
namespace branding_migration {
inline bool directory(const std::string& p) {
    struct stat st{};
    if (stat(p.c_str(), &st)==0) return S_ISDIR(st.st_mode);
    return mkdir(p.c_str(),0777)==0;
}
inline bool copy_missing(const std::string& from,const std::string& to) {
    struct stat st{};
    if (stat(to.c_str(),&st)==0) return S_ISREG(st.st_mode);
    if (errno!=ENOENT) return false;
    std::string tmp=to+".rc13-migration-tmp";
    FILE* in=fopen(from.c_str(),"rb"); if(!in)return false;
    FILE* out=fopen(tmp.c_str(),"wb"); if(!out){fclose(in);return false;}
    char buf[16384]; size_t n; bool ok=true;
    while((n=fread(buf,1,sizeof(buf),in))!=0) if(fwrite(buf,1,n,out)!=n){ok=false;break;}
    if(ferror(in))ok=false;
    fclose(in); if(fclose(out)!=0)ok=false;
    if(ok && stat(to.c_str(),&st)==0){remove(tmp.c_str());return S_ISREG(st.st_mode);}
    if(ok && rename(tmp.c_str(),to.c_str())==0)return true;
    remove(tmp.c_str());return false;
}
inline bool copy_tree(const std::string& from,const std::string& to) {
    if(!directory(to))return false;
    DIR* d=opendir(from.c_str()); if(!d)return false;
    bool ok=true;
    while(auto* e=readdir(d)){
        std::string n=e->d_name;
        if(n=="."||n==".."||n==".rc13-migrated")continue;
        auto dot=n.find_last_of('.');
        if(dot!=std::string::npos){auto ext=n.substr(dot);if(ext==".nro"||ext==".elf"||ext==".nacp")continue;}
        std::string src=from+"/"+n,dst=to+"/"+n;
        struct stat st{};
        if(stat(src.c_str(),&st)!=0){ok=false;break;}
        if(S_ISDIR(st.st_mode))ok=copy_tree(src,dst);
        else if(S_ISREG(st.st_mode))ok=copy_missing(src,dst);
        else ok=false;
        if(!ok)break;
    }
    closedir(d);return ok;
}
inline bool migrate(const std::string& from,const std::string& to) {
    struct stat st{};
    if(stat(from.c_str(),&st)!=0)return errno==ENOENT;
    if(!S_ISDIR(st.st_mode))return false;
    std::string marker=to+"/.rc13-migrated";
    if(stat(marker.c_str(),&st)==0)return S_ISREG(st.st_mode);
    if(!copy_tree(from,to))return false;
    FILE* f=fopen(marker.c_str(),"wb");if(!f)return false;
    return fclose(f)==0;
}
}
