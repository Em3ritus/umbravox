#include "active_discovery.h"
#include "wifi_auth.h"
#include <Arduino.h>
#include <esp_event.h>
#include <atomic>
#include <esp_heap_caps.h>
namespace discovery {
namespace {
std::atomic<int> completion{0}; // 0 pending, 1 complete, 2 failed; dedicated mailbox
Metrics counters;
bool initialized=false, owned=false;
uint32_t started=0;
esp_event_handler_instance_t handler;
void done(void*, esp_event_base_t, int32_t, void* data) {
    const auto* e=static_cast<const wifi_event_sta_scan_done_t*>(data);
    completion.store(e && e->status==0 ? 1 : 2, std::memory_order_release);
}
}
bool begin() {
    if(initialized)return true;
    initialized=esp_event_handler_instance_register(WIFI_EVENT,WIFI_EVENT_SCAN_DONE,done,nullptr,&handler)==ESP_OK;
    return initialized;
}
const Metrics& metrics(){return counters;}
bool busy(){return owned;}
bool start(uint8_t channel,uint16_t dwell,uint32_t now){
    if(!initialized || owned || !channel)return false;
    wifi_scan_config_t c{};
    c.channel=channel;c.show_hidden=true;c.scan_type=WIFI_SCAN_TYPE_ACTIVE;
    c.scan_time.active.max=dwell? (dwell>1500?1500:dwell):120;
    completion.store(0,std::memory_order_release);
    owned=true;started=now;
    const esp_err_t err=esp_wifi_scan_start(&c,false);
    if(err!=ESP_OK){owned=false;Serial.printf("discovery: start rejected %d\n",(int)err);return false;}
    ++counters.started;
    return true;
}
bool service(uint32_t now,Sink sink){
    if(!owned)return false;
    const int result=completion.exchange(0,std::memory_order_acq_rel);
    if(!result){
        // A scan whose completion event never arrives must not hold the radio
        // forever: capture hopping, mesh and node loops all wait on busy(). After
        // 5 s stop the scan, count it failed and hand the radio back.
        if((int32_t)(now-started)>=5000){
            Serial.println("discovery: completion missing after 5 s; stopping the scan and releasing the radio");
            esp_wifi_scan_stop();
            esp_wifi_clear_ap_list();
            ++counters.completed;++counters.failed;
            owned=false;
            return false;
        }
        return true;
    }
    ++counters.completed;
    if(result!=1)++counters.failed;
    uint16_t total=0,n=32;
    if(result==1 && esp_wifi_scan_get_ap_num(&total)==ESP_OK && total){
        // 3 KB: held in PSRAM only while a finished scan is read out (it was a static in internal RAM, which is scarce).
        auto* records=static_cast<wifi_ap_record_t*>(heap_caps_malloc(32*sizeof(wifi_ap_record_t),MALLOC_CAP_SPIRAM));
        if(!records)records=static_cast<wifi_ap_record_t*>(heap_caps_malloc(32*sizeof(wifi_ap_record_t),MALLOC_CAP_8BIT));   // a board without PSRAM
        if(records&&esp_wifi_scan_get_ap_records(&n,records)==ESP_OK){
            for(uint16_t i=0;i<n;++i)if(sink)sink(records[i]);
            if(total>n)counters.omitted+=total-n;
            if(total>n)Serial.printf("discovery: omitted %u AP records\n",(unsigned)(total-n));
        }else esp_wifi_clear_ap_list();
        heap_caps_free(records);
    }else esp_wifi_clear_ap_list();
    owned=false;
    return false;
}
uint16_t auth(const wifi_ap_record_t& r){
    uint16_t a=ui::AUTH_ESS;
    switch(r.authmode){
    case WIFI_AUTH_OPEN:break;
    case WIFI_AUTH_WEP:a|=ui::AUTH_PRIVACY|ui::AUTH_WEP;break;
    case WIFI_AUTH_WPA_PSK:a|=ui::AUTH_PRIVACY|ui::AUTH_WPA|ui::AUTH_PSK;break;
    case WIFI_AUTH_WPA2_PSK:a|=ui::AUTH_PRIVACY|ui::AUTH_WPA2|ui::AUTH_PSK;break;
    case WIFI_AUTH_WPA_WPA2_PSK:a|=ui::AUTH_PRIVACY|ui::AUTH_WPA|ui::AUTH_WPA2|ui::AUTH_PSK;break;
    case WIFI_AUTH_WPA2_ENTERPRISE:a|=ui::AUTH_PRIVACY|ui::AUTH_WPA2|ui::AUTH_EAP;break;
    case WIFI_AUTH_WPA3_PSK:a|=ui::AUTH_PRIVACY|ui::AUTH_WPA3|ui::AUTH_SAE;break;
    case WIFI_AUTH_WPA2_WPA3_PSK:a|=ui::AUTH_PRIVACY|ui::AUTH_WPA2|ui::AUTH_WPA3|ui::AUTH_PSK|ui::AUTH_SAE;break;
    case WIFI_AUTH_OWE:a|=ui::AUTH_PRIVACY|ui::AUTH_OWE;break;
    default:a|=ui::AUTH_PRIVACY;break;
    }
    for(auto c:{r.pairwise_cipher,r.group_cipher}){
        if(c==WIFI_CIPHER_TYPE_TKIP || c==WIFI_CIPHER_TYPE_TKIP_CCMP)a|=ui::AUTH_TKIP;
        if(c==WIFI_CIPHER_TYPE_CCMP || c==WIFI_CIPHER_TYPE_TKIP_CCMP)a|=ui::AUTH_CCMP;
    }
    return a;
}
}
