#pragma once
#include <algorithm>
#include <cmath>
namespace timing_policy {
inline double rate(bool pal){return pal?50.00698:60.0988;}
inline int due(double& acc,double ms,double hz){if(!std::isfinite(ms)||ms<0)ms=0;acc+=std::min(ms,50.0)*hz/1000;int n=(int)acc;acc-=n;return std::min(n,3);}
}
