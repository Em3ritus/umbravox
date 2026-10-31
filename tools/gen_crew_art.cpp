// Host-only artwork baker. No dynamic allocation or trig in device artwork drawing.
#include "../ui/scan_render.h"
#include <vector>
#include <algorithm>
#include <cstdio>
using namespace scanr;
void paint(S& s,int fcx,int fcy,int fr,bool cvlt){
 bool mascotBold=false;
 auto oval=[&](int cx,int cy,int rx,int ry,uint16_t color){for(int a=0;a<360;a++){double t=a*3.14159265/180;pt(s,cx+int(rx*cos(t)),cy+int(ry*sin(t)),color);}};
 if(cvlt){          const uint16_t ink=pal::rgb(156,102,221),quiet=pal::rgb(71,43,100),bright=pal::rgb(208,171,245);
          auto polar=[&](float angle,float radius){double t=angle*3.14159265/180.;return std::pair<int,int>{fcx+int(cos(t)*radius),fcy+int(sin(t)*radius)};};
          auto radial=[&](float a,float r1,float r2,uint16_t c){auto x=polar(a,r1),y=polar(a,r2);line(s,x.first,x.second,y.first,y.second,c);};
          // Interrupted outer annulus and fine calibration ticks.
          for(int a=0;a<360;a++){if(a%90>9&&a%90<81){auto z=polar(a,fr*.94);pt(s,z.first,z.second,ink);}}
          for(int a=0;a<360;a+=10)radial(a,fr*.85,fr*(a%30==0?.91:.88),a%30==0?bright:quiet);
          // Four cardinal sigils, deliberately sparse at compact size.
          for(int a=0;a<360;a+=90){auto z=polar(a,fr*.94);int k=fr>=60?4:2;diamondInto(s,z.first,z.second,k,bright);}
          // Inverted five-point ceremonial seal with seven satellite nodes.
          int vx[5],vy[5];for(int i=0;i<5;i++){auto z=polar(90+i*72,fr*.57);vx[i]=z.first;vy[i]=z.second;}
          for(int i=0;i<5;i++)line(s,vx[i],vy[i],vx[(i+2)%5],vy[(i+2)%5],ink);
          oval(fcx,fcy,int(fr*.61),int(fr*.61),quiet);
          for(int i=0;i<7;i++){auto z=polar(-90+i*360.f/7,fr*.73);int k=fr>=60?2:1;diamondInto(s,z.first,z.second,k,ink);if(fr>=60){line(s,z.first-2,z.second-6,z.first+2,z.second-9,quiet);}}
          // A small central lozenge keeps the field origin legible.
          diamondInto(s,fcx,fcy,fr>=60?7:3,bright);
}else{          // Code-native pixel emblem proposal: curled horns, X eyes, toothy grin.
          const float scale=fr*(mascotBold?.70f:.56f)/50.f;
          const uint16_t outline=mascotBold?pal::rgb(155,91,211):pal::rgb(91,59,116);
          const uint16_t shade=mascotBold?pal::rgb(54,25,77):pal::rgb(27,20,36);
          const uint16_t fur=mascotBold?pal::rgb(87,42,119):pal::rgb(44,29,56);
          const uint16_t eye=mascotBold?pal::rgb(225,197,255):pal::rgb(159,127,186);
          const uint16_t tooth=mascotBold?pal::rgb(190,234,139):pal::rgb(116,151,104);
          auto poly=[&](std::initializer_list<std::pair<int,int>> v,uint16_t fill,uint16_t stroke){
            std::vector<std::pair<int,int>> q;for(auto z:v)q.push_back({fcx+int(z.first*scale),fcy+int(z.second*scale)});
            for(int y=s.y0;y<s.y0+s.h;y++){int xs[64],n=0;for(size_t i=0,j=q.size()-1;i<q.size();j=i++){auto a=q[j],b=q[i];if((a.second<=y&&b.second>y)||(b.second<=y&&a.second>y))xs[n++]=a.first+(y-a.second)*(b.first-a.first)/(b.second-a.second);}std::sort(xs,xs+n);for(int i=0;i+1<n;i+=2)box(s,xs[i],y,xs[i+1]-xs[i]+1,1,fill);}
            for(size_t i=0;i<q.size();i++){auto a=q[i],b=q[(i+1)%q.size()];line(s,a.first,a.second,b.first,b.second,stroke);}
          };
          // Mirrored ram horns with an interior spiral and ridges.
          for(int side:{-1,1}){
            auto horn=[&](std::initializer_list<std::pair<int,int>> v,uint16_t c){std::vector<std::pair<int,int>> q(v);for(auto& z:q)z.first*=side;for(size_t i=1;i<q.size();i++)line(s,fcx+int(q[i-1].first*scale),fcy+int(q[i-1].second*scale),fcx+int(q[i].first*scale),fcy+int(q[i].second*scale),c);};
            horn({{18,-22},{21,-40},{32,-50},{44,-48},{51,-38},{49,-25},{42,-20},{34,-25},{33,-33},{39,-38},{43,-34},{41,-29}},outline);
            horn({{20,-24},{28,-37},{34,-42},{42,-42},{46,-36},{44,-28},{39,-26}},fur);
            horn({{24,-40},{29,-36}},outline);horn({{31,-48},{34,-42}},outline);horn({{42,-47},{41,-41}},outline);horn({{49,-39},{44,-36}},outline);
          }
          poly({{-30,-25},{-21,-30},{-14,-27},{-8,-31},{0,-27},{8,-31},{14,-27},{21,-30},{30,-25},{33,-15},{40,-12},{34,-4},{38,4},{32,7},{34,17},{28,19},{27,30},{20,29},{17,39},{8,36},{0,44},{-8,36},{-17,39},{-20,29},{-27,30},{-28,19},{-34,17},{-32,7},{-38,4},{-34,-4},{-40,-12},{-33,-15}},shade,outline);
          poly({{-28,-15},{-20,-22},{-5,-18},{-4,-7},{-20,-4}},fur,fur);
          poly({{28,-15},{20,-22},{5,-18},{4,-7},{20,-4}},fur,fur);
          // Bold X eyes; two physical pixels at full size, one on compact.
          const int thick=fr>=60?2:1;
          for(int side:{-1,1})for(int t=0;t<thick;t++){
            int cx=fcx+int(side*15*scale),cy=fcy-int(12*scale),d=std::max(2,int(5*scale));
            line(s,cx-d,cy-d+t,cx+d,cy+d+t,eye);line(s,cx-d,cy+d+t,cx+d,cy-d+t,eye);
          }
          poly({{-27,1},{-16,5},{0,7},{16,5},{27,1},{23,19},{14,29},{0,34},{-14,29},{-23,19}},pal::VOID,outline);
          for(int x=-20;x<=20;x+=8){int y=4+(20-std::abs(x))/7;poly({{x-3,y},{x+3,y},{x,y+7}},tooth,tooth);}
          if(fr>=60)for(int x=-12;x<=12;x+=8){int y=29-std::abs(x)/4;poly({{x-3,y},{x+3,y},{x,y-5}},tooth,tooth);}
          // Restrained acid-green flame accent.
          poly({{-5,-30},{-8,-37},{-2,-43},{-2,-51},{6,-42},{4,-37},{7,-32},{0,-28}},mascotBold?pal::rgb(94,185,83):pal::rgb(54,104,57),mascotBold?pal::rgb(145,225,98):pal::rgb(81,139,73));

}}
void paintExtra(S& s,int cx,int cy,int r,int style){
 auto polar=[&](double a,double radius){double t=a*3.14159265/180;return std::pair<int,int>{cx+int(cos(t)*radius),cy+int(sin(t)*radius)};};
 auto circle=[&](double radius,uint16_t c){for(int a=0;a<720;a++){auto z=polar(a*.5,radius);pt(s,z.first,z.second,c);}};
 if(style==2){
  auto blue=pal::rgb(77,224,255),violet=pal::rgb(88,66,139),pink=pal::rgb(255,95,208);
  circle(r*.34,blue);
  for(int a=0;a<360;a++){double t=a*3.14159265/180;int x=int(r*.69*cos(t)),y=int(r*.18*sin(t));pt(s,cx+x,cy+y-x/3,a<180?blue:violet);}
  // Latitude arcs and two small satellites, distinct from a ritual seal.
  for(int a=20;a<160;a++){double t=a*3.14159265/180;pt(s,cx+int(r*.29*cos(t)),cy+int(r*.10*sin(t)),violet);}
  auto z=polar(-35,r*.73);diamondInto(s,z.first,z.second,r>=60?3:1,pink);
  auto q=polar(135,r*.68);diamondInto(s,q.first,q.second,r>=60?2:1,blue);
 }else{
  auto gold=pal::rgb(181,134,59),bone=pal::rgb(216,196,154),dim=pal::rgb(80,55,24);
  circle(r*.72,dim);circle(r*.77,gold);circle(r*.48,dim);
  int x[5],y[5];for(int i=0;i<5;i++){auto z=polar(-90+72*i,r*.61);x[i]=z.first;y[i]=z.second;}
  for(int i=0;i<5;i++)line(s,x[i],y[i],x[(i+2)%5],y[(i+2)%5],gold);
  for(int a=0;a<360;a+=30){auto p=polar(a,r*.81),q=polar(a,r*.85);line(s,p.first,p.second,q.first,q.second,bone);}
  for(int a:{-90,30,150}){auto z=polar(a,r*.9);diamondInto(s,z.first,z.second,r>=60?3:1,bone);}
  diamondInto(s,cx,cy,r>=60?5:2,bone);
 }
}
int main(){
 puts("// Generated by tools/gen_crew_art.cpp; do not edit.\n#pragma once\n#include <cstdint>\nnamespace crewart {\nstruct Span { int8_t x,y; uint8_t n,c; }; ");
 for(int style:{0,1,2,3})for(int radius:{85,39}){
 pal::setTheme(style==1?1:0);pal::VOID=pal::rgb(7,9,10);
 std::vector<uint16_t> p(171*171,0xffff);S s{p.data(),171,0,171};if(style<2)paint(s,85,85,radius,style==1);else paintExtra(s,85,85,radius,style);
 std::vector<uint16_t> colors;struct Run{int x,y,n,c;};std::vector<Run> runs;
 for(int y=0;y<171;y++)for(int x=0;x<171;){uint16_t col=p[y*171+x];if(col==0xffff){x++;continue;}int start=x;while(x<171&&p[y*171+x]==col)x++;auto it=std::find(colors.begin(),colors.end(),col);int idx=int(it-colors.begin());if(it==colors.end())colors.push_back(col);runs.push_back({start-85,y-85,x-start,idx});}
 const char* name=style==0?"mascot":style==1?"cvlt":style==2?"orbit":"sigil";printf("inline constexpr uint16_t %s%dColors[]={",name,radius);for(auto color:colors)printf("0x%04x,",color);puts("};");printf("inline constexpr Span %s%d[]={\n",name,radius);for(auto run:runs)printf("{%d,%d,%d,%d},\n",run.x,run.y,run.n,run.c);puts("};");
 }puts("}");
}
