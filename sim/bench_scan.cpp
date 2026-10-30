#include <cmath>
#include <cstdint>
static uint64_t trigCalls=0;
namespace std {
inline double measured_sin(double x){++trigCalls;return std::sin(x);}
inline double measured_cos(double x){++trigCalls;return std::cos(x);}
}
#define sin measured_sin
#define cos measured_cos
#include "scan_fixture.h"
#undef sin
#undef cos
#include <chrono>
#include <cstdio>
#include <vector>
int main(){
 for(int w:{480,320})for(int theme:{0,1,2}){
  int h=w==480?320:240;std::vector<uint16_t> fb(w*h);pal::setTheme(theme);
  auto sc=scanfix::demo(theme);trigCalls=0;
  auto start=std::chrono::steady_clock::now();
  for(int frame=0;frame<20;++frame)for(int y=0;y<h;y+=16){scanr::S s{fb.data()+y*w,w,y,std::min(16,h-y)};scanr::composeStrip(s,w,h,sc);}
  auto us=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count()/20;
  uint64_t hash=14695981039346656037ull;for(auto px:fb){hash^=px;hash*=1099511628211ull;}
  std::printf("%dx%d theme%d compose_us=%lld trig/frame=%llu pixels=%016llx\n",w,h,theme,(long long)us,(unsigned long long)(trigCalls/20),(unsigned long long)hash);
 }
}
