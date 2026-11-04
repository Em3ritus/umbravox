#include "../device/active_discovery.h"
#include "esp_event.h"
#include "Arduino.h"
#include "../device/mesh_timing.h"
#include "../device/chanplan.h"
#include <cassert>
#include <cstdio>
SerialMock Serial;
Handler callback=nullptr;
int startResult=0,readResult=0,countResult=0,clears=0,received=0,registrations=0;
uint16_t count=3;
wifi_scan_config_t last;
esp_err_t esp_event_handler_instance_register(int,int,Handler h,void*,int*){callback=h;++registrations;return 0;}
esp_err_t esp_wifi_scan_start(const wifi_scan_config_t* c,bool block){assert(!block);last=*c;return startResult;}
esp_err_t esp_wifi_scan_get_ap_num(uint16_t* n){*n=count;return countResult;}
esp_err_t esp_wifi_scan_get_ap_records(uint16_t* n,wifi_ap_record_t* p){if(readResult)return readResult;*n=*n<count?*n:count;for(int i=0;i<*n;i++)p[i].primary=6;return 0;}
esp_err_t esp_wifi_clear_ap_list(){++clears;return 0;}
int stops=0;
esp_err_t esp_wifi_scan_stop(){++stops;return 0;}
void sink(const wifi_ap_record_t& r){assert(r.primary==6);++received;}
void done(int status=0){wifi_event_sta_scan_done_t e{status};callback(nullptr,WIFI_EVENT,WIFI_EVENT_SCAN_DONE,&e);}
int main(){
 assert(!discovery::start(6,150,0));assert(discovery::begin());assert(discovery::begin());assert(registrations==1);
 assert(!discovery::start(0,150,0));assert(discovery::start(6,150,0));assert(last.channel==6 && last.show_hidden && last.scan_time.active.max==150);assert(!discovery::start(11,150,0));
 assert(discovery::service(4999,sink));assert(discovery::busy());done();assert(!discovery::service(5001,sink));assert(received==3);assert(stops==0);
 // A scan whose completion never arrives must not hold the radio forever: after
 // 5 s (a signed difference, so the clock wrap is safe) it is stopped, counted as
 // failed and released, and a new scan can start.
 {const auto before=discovery::metrics();const int clears0=clears;
  assert(discovery::start(6,150,10000));assert(discovery::service(14999,sink));assert(discovery::busy());assert(stops==0);
  assert(!discovery::service(15000,sink));assert(!discovery::busy());assert(stops==1 && clears==clears0+1);
  assert(discovery::metrics().failed==before.failed+1 && discovery::metrics().completed==before.completed+1);
  assert(discovery::start(6,150,20000));done();assert(!discovery::service(20001,nullptr));assert(!discovery::busy());
  assert(discovery::start(6,150,0xFFFFFFF0u));assert(discovery::service(0xFFFFFFF0u+4000,sink));assert(!discovery::service(0xFFFFFFF0u+5000,sink));assert(stops==2);clears=0;}
 startResult=-1;assert(!discovery::start(6,150,0));assert(!discovery::busy());startResult=0;
 assert(discovery::start(6,0,0));assert(last.scan_time.active.max==120);done(1);discovery::service(1,sink);assert(clears==1);
 assert(discovery::start(6,2000,0));assert(last.scan_time.active.max==1500);readResult=-1;done();discovery::service(1,sink);assert(clears==2);readResult=0;
 count=0;assert(discovery::start(6,150,0));done();discovery::service(1,sink);assert(clears==3);
 count=40;assert(discovery::start(6,150,0));done();discovery::service(1,sink);assert(received==35);
 countResult=-1;assert(discovery::start(6,150,0));done();discovery::service(1,sink);assert(clears==4);
 meshtiming::Config c{},out;c.h.magic=mesh::MAGIC;c.h.ver=mesh::VERSION;c.h.netId=mesh::netId;c.h.kind=meshtiming::CONFIG;
 assert(meshtiming::config((uint8_t*)&c,sizeof c,out));c.active=2;assert(!meshtiming::config((uint8_t*)&c,sizeof c,out));c.active=1;c.dwellMs=99;assert(!meshtiming::config((uint8_t*)&c,sizeof c,out));c.dwellMs=150;assert(meshtiming::config((uint8_t*)&c,sizeof c,out));assert(!meshtiming::config((uint8_t*)&c,sizeof(c)-1,out));assert(!meshtiming::config((uint8_t*)&c,sizeof(c)+1,out));
 chanplan::Plan plans[16];for(int n=1;n<=16;n++){chanplan::planChannels(n,plans,true);for(int i=0;i<n;i++)assert(!plans[i].camp);}
 puts("active discovery: lifecycle, timeout ownership, cleanup, bounded results, config validation and discovery planner passed");
}
