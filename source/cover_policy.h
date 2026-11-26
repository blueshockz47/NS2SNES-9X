#pragma once
#include <algorithm>
#include <string>
#include <map>
#include <vector>
namespace cover_policy {
inline bool valid(unsigned w,unsigned h){return w>0&&h>0&&w<=1024&&h<=1024;}
inline std::string sidecar(const std::string& dir,const std::string& name){auto p=name.find_last_of('.');return dir+"/"+name.substr(0,p)+".png";}
inline float scale(unsigned w,unsigned h,float boxW,float boxH){return valid(w,h)?std::min(boxW/w,boxH/h):0;}
struct Fit {float x,y,w,h;};
inline Fit fit(unsigned w,unsigned h,float bw,float bh){
  if(!valid(w,h)||bw<=0||bh<=0)return {0,0,0,0};
  float s=scale(w,h,bw,bh),dw=w*s,dh=h*s;
  return {(bw-dw)/2,(bh-dh)/2,dw,dh};
}
constexpr unsigned cacheLimit=16;
inline std::string stem(const std::string& n){auto p=n.find_last_of('/');auto b=n.substr(p==std::string::npos?0:p+1);return b.substr(0,b.find_last_of('.'));}
inline std::string fold(std::string s){for(char& c:s)if(c>='A'&&c<='Z')c+=32;return s;}
inline int format(const std::string& n){auto p=n.find_last_of('.');if(p==std::string::npos)return 3;auto e=fold(n.substr(p));return e==".png"?0:e==".jpg"?1:e==".jpeg"?2:3;}
inline bool preferred(const std::string& a,const std::string& b){return format(a)!=format(b)?format(a)<format(b):a<b;}
inline bool tag(const std::string& s){
  static const std::vector<std::string> tags={"u","e","j","usa","europe","japan","world","australia","asia","korea","germany","france","spain","italy","uk","canada","!","pal","ntsc"};
  if(std::find(tags.begin(),tags.end(),s)!=tags.end())return true;
  if(s.compare(0,4,"rev ")==0||s.compare(0,8,"revision")==0)return true;
  if(s=="en"||s=="fr"||s=="de"||s=="es"||s=="it"||s=="ja")return true;
  auto p=s.find(',');if(p!=std::string::npos){auto a=s.substr(0,p),b=s.substr(p+1);while(!b.empty()&&b.front()==' ')b.erase(0,1);return tag(a)&&tag(b);}
  return false;
}
inline std::string region(std::string s){
  if(s=="u"||s=="usa")return "usa";if(s=="e"||s=="europe")return "europe";if(s=="j"||s=="japan")return "japan";
  for(const char* r:{"world","australia","asia","korea","germany","france","spain","italy","uk","canada"})if(s==r)return s;
  return "";
}
struct Name {std::string key;std::vector<std::string> regions;};
inline Name parse(std::string s){
  s=fold(s);Name n;
  auto addRegion=[&](std::string t){while(!t.empty()&&t.front()==' ')t.erase(0,1);while(!t.empty()&&t.back()==' ')t.pop_back();auto r=region(t);if(!r.empty()&&std::find(n.regions.begin(),n.regions.end(),r)==n.regions.end())n.regions.push_back(r);};
  while(!s.empty()){
    while(!s.empty()&&s.back()==' ')s.pop_back();
    if(s.empty()||(s.back()!=')'&&s.back()!=']'))break;
    auto p=s.find_last_of(s.back()==')'?'(':'[');if(p==std::string::npos)break;
    auto t=s.substr(p+1,s.size()-p-2);
    bool dump=s.back()==']'&&(t=="!"||t=="b"||t=="h"||t=="o"||t=="p"||t=="t"||(t.size()>1&&(t[0]=='b'||t[0]=='h'||t[0]=='o'||t[0]=='t')&&std::all_of(t.begin()+1,t.end(),[](char x){return x>='0'&&x<='9';})));
    if(!tag(t)&&!dump)break;
    size_t start=0;while(start<t.size()){auto end=t.find(',',start);addRegion(t.substr(start,end==std::string::npos?end:end-start));if(end==std::string::npos)break;start=end+1;}s.resize(p);
  }
  // Only strip explicit underscore-delimited full region names, not arbitrary title words.
  while(true){auto p=s.find_last_of('_');if(p==std::string::npos)break;auto t=s.substr(p+1);if(t.size()<3||region(t).empty())break;addRegion(t);s.resize(p);}
  for(char& x:s)if(x=='_')x=' ';
  while(!s.empty()&&s.back()==' ')s.pop_back();
  for(const std::string article:{"the","an","a"}){
    if(s.compare(0,article.size()+1,article+" ")==0){s=s.substr(article.size()+1)+" "+article;break;}
    auto p=s.find(" "+article+" -");if(p!=std::string::npos){s=article+" "+s.substr(0,p)+s.substr(p+article.size()+1);break;}
    auto q=s.find(", "+article+" -");if(q!=std::string::npos){s=article+" "+s.substr(0,q)+s.substr(q+article.size()+2);break;}
  }
  // Normalize articles at the beginning of a title even when it has a subtitle.
  for(const std::string article:{"the","an","a"}){
    if(s.compare(0,article.size()+1,article+" ")==0){s=s.substr(article.size()+1);break;}
    if(s.size()>article.size()+1&&s.compare(s.size()-article.size()-1,article.size()+1," "+article)==0){s.resize(s.size()-article.size()-1);break;}
  }
  for(size_t p=0;(p=s.find('&',p))!=std::string::npos;p+=3)s.replace(p,1,"and");
  for(unsigned char x:s)if((x>='a'&&x<='z')||(x>='0'&&x<='9')||x>=128)n.key+=char(x);
  return n;
}
inline std::string normalized(std::string s){return parse(s).key;}
inline std::string subtitleKey(std::string s){
  s=fold(s);for(char& x:s)if(x=='_')x=' ';
  auto p=s.find(" - ");if(p==std::string::npos)p=s.find(": ");if(p==std::string::npos)return "";
  auto tail=s.substr(p+2);if(std::any_of(tail.begin(),tail.end(),[](char x){return x>='0'&&x<='9';}))return "";
  auto key=normalized(s.substr(0,p));return key.size()>=6?key:"";
}
struct Index{
  std::map<std::string,std::vector<std::string>> exact,smart,aliases;
  void clear(){exact.clear();smart.clear();aliases.clear();}
  void add(const std::string& n){
    if(format(n)==3)return;
    auto k=fold(stem(n));exact[k].push_back(n);
    auto norm=normalized(stem(n));if(!norm.empty())smart[norm].push_back(n);auto alias=subtitleKey(stem(n));if(!alias.empty())aliases[alias].push_back(n);
  }
  std::vector<std::string> candidates(const std::string& rom)const{
    std::vector<std::string> out;auto append=[&](const std::string& n){if(std::find(out.begin(),out.end(),n)==out.end())out.push_back(n);};
    auto base=stem(rom);auto e=exact.find(fold(base));
    if(e!=exact.end()){auto v=e->second;std::sort(v.begin(),v.end(),preferred);for(auto& n:v)if(stem(n)==base)append(n);for(auto& n:v)append(n);}
    auto k=normalized(base);auto i=smart.find(k);
    if(i!=smart.end()){
      auto v=i->second;std::sort(v.begin(),v.end(),preferred);
      auto target=parse(base);
      std::vector<std::string> regional,generic;
      for(auto& n:v){auto art=parse(stem(n));if(art.regions.empty())generic.push_back(n);else if(std::any_of(art.regions.begin(),art.regions.end(),[&](const std::string& r){return std::find(target.regions.begin(),target.regions.end(),r)!=target.regions.end();}))regional.push_back(n);}
      auto uniqueStem=[](const std::vector<std::string>& a){return !a.empty()&&std::all_of(a.begin(),a.end(),[&](const std::string& n){return fold(stem(n))==fold(stem(a.front()));});};
      if(!generic.empty())for(auto& n:generic)append(n);
      if(!regional.empty())for(auto& n:regional)append(n);
      if(regional.empty()&&generic.empty()&&uniqueStem(v))for(auto& n:v)append(n);

    }
    if(out.empty()){
      std::vector<std::string> v;auto a=aliases.find(k);if(a!=aliases.end())v=a->second;
      auto shortKey=subtitleKey(base);if(!shortKey.empty()){auto b=smart.find(shortKey);if(b!=smart.end())v.insert(v.end(),b->second.begin(),b->second.end());}
      if(!v.empty()&&std::all_of(v.begin(),v.end(),[&](const std::string& n){return normalized(stem(n))==normalized(stem(v.front()));})){
        std::sort(v.begin(),v.end(),preferred);auto target=parse(base);for(auto& n:v){auto art=parse(stem(n));if(art.regions.empty()||target.regions.empty()||std::any_of(art.regions.begin(),art.regions.end(),[&](const std::string& r){return std::find(target.regions.begin(),target.regions.end(),r)!=target.regions.end();}))append(n);}
      }
    }
    return out;
  }
};
}
