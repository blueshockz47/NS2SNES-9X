#pragma once
#include <string>
#include <vector>
#include <algorithm>
namespace content_policy {
inline std::string normalize(std::string p){
 if(p=="sdmc:")return "sdmc:/";
 if(p.rfind("sdmc:/",0)!=0||p.find(char(92))!=std::string::npos)return {};
 while(p.size()>6&&p.back()=='/')p.pop_back();
 size_t i=6;while(i<p.size()){size_t j=p.find('/',i);auto s=p.substr(i,j==std::string::npos?j:j-i);if(s.empty()||s=="."||s=="..")return {};if(j==std::string::npos)break;i=j+1;}return p;
}
inline bool add(std::vector<std::string>& dirs,std::string p){p=normalize(p);if(p.empty()||dirs.size()>=16||std::find(dirs.begin(),dirs.end(),p)!=dirs.end())return false;dirs.push_back(p);return true;}
}
