#include "../device/mesh_timing.h"
#include "../device/node_diagnostics.h"
#include "../ui/router.h"
#include "../ui/project.h"
#include <cassert>
#include <cstring>
#include <cstdio>
int main(){
 using namespace meshtiming;
 uint8_t mac[6]={0x10,0xbd,0xa3,0xf3,0xc5,0x5c};
 Telemetry t{},v{};t.h={mesh::MAGIC,mesh::VERSION,mesh::netId,TELEMETRY,{0}};memcpy(t.h.mac,mac,6);
 t.nChan=3;t.dwellMs=150;t.active=1;t.capacity=192;t.depth=8;t.peak=40;t.boot=7;t.uptime=100;
 auto valid=[&]{return telemetry(reinterpret_cast<uint8_t*>(&t),sizeof t,mac,v);};assert(valid());
 for(int n=0;n<(int)sizeof t;n++)assert(!telemetry(reinterpret_cast<uint8_t*>(&t),n,mac,v));
 assert(!telemetry(reinterpret_cast<uint8_t*>(&t),sizeof t+1,mac,v));
 assert(!telemetry(nullptr,sizeof t,mac,v));
 auto good=t;t.format=2;assert(!valid());t=good;t.active=2;assert(!valid());t=good;t.camp=2;assert(!valid());t=good;t.depth=193;assert(!valid());t=good;t.peak=1;assert(!valid());t=good;t.capacity=0;assert(!valid());t=good;t.nChan=0;assert(!valid());t=good;t.dwellMs=99;assert(!valid());t=good;t.h.mac[0]^=1;assert(!valid());t=good;t.h.netId++;assert(!valid());t=good;
 t.profile=2;assert(!valid());t=good;t.nChan=mesh::MAX_ASSIGN+1;assert(!valid());t=good;t.peak=193;assert(!valid());t=good;t.dwellMs=601;assert(!valid());t=good;t.h.ver++;assert(!valid());t=good;
 TelemetryState state;assert(state.accept(t,10)&&state.known&&state.receivedAt==10);
 assert(!state.accept(t,20)&&state.receivedAt==10);t.uptime=99;assert(!state.accept(t,30));t.uptime=101;assert(state.accept(t,40));t.boot++;t.uptime=1;assert(state.accept(t,50));
 t.uptime=0xfffffff0;state={};assert(state.accept(t,1));t.uptime=10;assert(state.accept(t,2));
 ui::State s;uiproj::Chrome c;s.nNodes=1;s.nodes[0].live=true;s.nodes[0].id[0]=0xf3;s.nodes[0].id[1]=0xc5;s.nodes[0].id[2]=0x5c;
 s.localDiag.valid=true;s.localDiag.active=true;s.localDiag.dwellMs=150;s.localDiag.txRetries=5;
 mshpres::MeshStore a,b;uiproj::mesh(a,s,c);assert(a.scene().local.active&&a.scene().local.txRetries==5&&!a.scene().rows[0].diag.known);
 s.meshPage=1;s.nodes[0].diag.known=true;s.nodes[0].diag.active=true;s.nodes[0].diag.dwellMs=150;s.nodes[0].diag.ageS=11;
 uiproj::mesh(b,s,c);assert(b.scene().page==1&&b.scene().rows[0].diag.ageS==11);bool st,body;mshpres::sceneDirty(a.scene(),b.scene(),st,body);assert(body&&!st);
 for(int w:{320,480}){auto hits=uieng::meshHits(w,w==480?320:240);bool pager=false;for(int i=0;i<hits.count;i++)if(hits.items[i].action==uieng::Action::MeshPage){pager=true;assert(hits.items[i].rect.w>=44&&hits.items[i].rect.h>=44);}assert(pager);}
 // A hello/status packet does not pretend that a sighting report arrived.
 meshcore::NodeRec node{};node.lastMs=10000;node.assigned=true;node.nChan=3;
 node.chans[0]=1;node.chans[1]=36;node.chans[2]=149;
 auto diag=meshcore::nodeDiagnostics(node,10000);assert(!diag.hasReport&&diag.assignedCount==3&&diag.assignedChannels[2]==149);
 node.reports=1;node.lastReportMs=2000;diag=meshcore::nodeDiagnostics(node,10000);assert(diag.hasReport&&diag.reportAgeS==8);
 node.lastReportMs=10001;assert(meshcore::nodeDiagnostics(node,10000).reportAgeS==0);
 node.lastReportMs=0xffffff00;assert(meshcore::nodeDiagnostics(node,1744).reportAgeS==2);
 node.assigned=false;assert(meshcore::nodeDiagnostics(node,2000).assignedCount==0);
 mshr::MeshScene scene;ui::NodeDiagnostics nd;
 assert(!strcmp(mshr::timingStatus(scene,nd),"UNKNOWN"));
 nd.known=true;nd.dwellMs=220;assert(!strcmp(mshr::timingStatus(scene,nd),"MATCH"));
 nd.dwellMs=150;assert(!strcmp(mshr::timingStatus(scene,nd),"PENDING"));
 scene.targetDwell=150;assert(!strcmp(mshr::timingStatus(scene,nd),"MATCH"));
 nd.ageS=10;assert(!strcmp(mshr::timingStatus(scene,nd),"STALE"));
 char channels[96];diag.assignedCount=3;diag.assignedChannels[0]=1;diag.assignedChannels[1]=36;diag.assignedChannels[2]=149;
 mshr::channelLabel(diag,channels,sizeof channels);assert(!strcmp(channels,"ASSIGNED 1 36 149"));
 s.nodes[0].diag=diag;uiproj::mesh(a,s,c);s.nodes[0].diag.assignedChannels[2]=165;uiproj::mesh(b,s,c);mshpres::sceneDirty(a.scene(),b.scene(),st,body);assert(body);
 assert(mshr::pageCount(0)==1&&mshr::pageCount(16)==33);
 for(int w:{320,480}){auto hits=uieng::meshHits(w,w==480?320:240);bool tune=false;
  for(int i=0;i<hits.count;i++)if(!strcmp(hits.items[i].slot,"node-tune")){uinav::Router r;r.dispatch(hits.items[i]);assert(r.screen()==uinav::Screen::Attune);tune=true;}assert(tune);}
 puts("Mesh telemetry: strict packet validation, sender binding, ordering/reboot/wrap, unknown/stale state, local bridge projection and pager PASS");
}
