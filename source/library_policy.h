#pragma once
#include <string>
#include <cctype>
#include <algorithm>
namespace library {
inline std::string fold(std::string s){for(char& c:s)c=(char)std::tolower((unsigned char)c);return s;}
inline bool matches(const std::string& name,const std::string& query){return fold(name).find(fold(query))!=std::string::npos;}
inline int pageTop(int selected,int columns,int rows){return (selected/(columns*rows))*(columns*rows);}
inline int movePage(int selected,int count,int size,int step){
  if(count<=0||size<=0)return 0;
  selected=std::max(0,std::min(count-1,selected));
  int page=selected/size,target=std::max(0,std::min((count-1)/size,page+step));
  return target==page?selected:std::min(count-1,target*size+selected%size);
}
struct PageRepeat {
  int direction=0; double started=0,next=0;
  int update(int dir,bool edge,bool active,double now){
    if(!active||!dir){direction=0;return 0;}
    if(dir!=direction){direction=0;if(!edge)return 0;direction=dir;started=now;next=now+350;return dir;}
    if(now<next)return 0;
    next=now+(now-started>=1200?70:120);return dir;
  }
};
}
