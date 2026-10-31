#include "ble_bridge.h"
#if defined(WS_C5_35) && defined(UMBRAVOX_SCAN_ENGINE)
#include "ble_bridge_protocol.h"
#include "ble_bridge_init.h"
#include "capture.h"
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <esp_heap_caps.h>
#include <atomic>
#include <new>
#include <host/ble_hs.h>
#include <services/gap/ble_svc_gap.h>

namespace blebridge {
namespace {
using namespace bridgewire;
constexpr uint16_t NO_CONN=0xffff;
std::atomic<uint16_t> connection{NO_CONN}, mtu{23};
std::atomic<unsigned> epoch{0};
std::atomic<bool> subscribed{false}, streaming{false};
std::atomic<uint8_t> selected{OFF};
portMUX_TYPE rxMux=portMUX_INITIALIZER_UNLOCKED;
struct Rx { unsigned epoch; uint16_t len; uint8_t data[244]; };
struct State {
    Outbox out; Command command; char scratch[MAX_LINE]{};
    Rx rx[4]{}; unsigned rxHead=0,rxCount=0; uint32_t rxDrops=0;
    unsigned seenEpoch=0;bool greeted=false,timeSynced=false;
    uint8_t band=0;uint32_t reply=1,cursor=0,generation=0,sent=0;
    uint32_t connectedAt=0,lastTx=0,lastAp=0,roundAt=0,timeAt=0,diagAt=0,advAt=0,notifyFails=0;
    bool roundDone=false;
};
State* st=nullptr;
BLEServer* server=nullptr; BLECharacteristic *wdTx=nullptr,*bisTx=nullptr,*bisStatus=nullptr;
char name[24]{};
void clearSession(uint32_t now){
    st->out.clear();st->command.reset();st->greeted=false;st->timeSynced=false;
    st->reply=1;st->cursor=0;st->sent=0;st->band=0;st->roundDone=false;st->connectedAt=now;
    streaming=false;
}
void resetRx(){if(st){portENTER_CRITICAL(&rxMux);st->rxHead=st->rxCount=0;portEXIT_CRITICAL(&rxMux);}}
class ServerCallbacks:public BLEServerCallbacks {
    void onConnect(BLEServer* s,ble_gap_conn_desc* d) override {
        if(selected==OFF || connection!=NO_CONN){s->disconnect(d->conn_handle);return;}
        connection=d->conn_handle;mtu=23;subscribed=false;resetRx();++epoch;
        s->updateConnParams(d->conn_handle,24,48,0,600);
    }
    void onDisconnect(BLEServer*,ble_gap_conn_desc* d) override {
        if(connection!=d->conn_handle)return;
        connection=NO_CONN;subscribed=false;streaming=false;resetRx();++epoch;
    }
    void onMtuChanged(BLEServer*,ble_gap_conn_desc* d,uint16_t m) override {if(connection==d->conn_handle)mtu=m;}
} serverCallbacks;
class Callbacks:public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* c,ble_gap_conn_desc* d) override {
        if(!st || selected==OFF || connection!=d->conn_handle)return;
        const auto uuid=c->getUUID().toString();
        const bool biscuit=uuid==BIS_CMD;
        if(biscuit!=(selected==BISCUIT))return;
        auto value=c->getValue();const size_t n=value.length();
        portENTER_CRITICAL(&rxMux);
        if(n && n<=sizeof(Rx::data) && st->rxCount<4){
            Rx& r=st->rx[(st->rxHead+st->rxCount)%4];r.epoch=epoch.load();r.len=n;memcpy(r.data,value.c_str(),n);++st->rxCount;
        }else ++st->rxDrops;
        portEXIT_CRITICAL(&rxMux);
    }
    void onSubscribe(BLECharacteristic* c,ble_gap_conn_desc* d,uint16_t sub) override {
        if(connection!=d->conn_handle)return;
        if((selected==WARDRIVEGO && c==wdTx)||(selected==BISCUIT && c==bisTx)){
            subscribed=(sub&1)!=0;resetRx();++epoch;
        }
    }
    void onRead(BLECharacteristic* c,ble_gap_conn_desc*) override {
        if(c==bisStatus)c->setValue(streaming?"{\"status\":2,\"battery\":-1}":"{\"status\":1,\"battery\":-1}");
    }
} callbacks;
bool init(){
    if(server)return server->isStarted();
    if(!BLEDevice::getInitialized())return false; // blescan owns init
    if(!st){
        void* p=heap_caps_malloc(sizeof(State),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(!p)return false;
        st=new(p)State{};
    }
    BLEDevice::setMTU(247);
    auto addr=BLEDevice::getAddress();const uint8_t* mac=addr.getNative();
    snprintf(name,sizeof name,"Umbravox-%02X%02X",mac[4],mac[5]);
    auto* scanner=BLEDevice::getScan();
    const auto result=bridgeinit::configure(*scanner,[&]() {
    server=BLEDevice::createServer();if(!server)return false;server->setCallbacks(&serverCallbacks);server->advertiseOnDisconnect(false);
    auto* wd=server->createService(WD_SERVICE);
    wdTx=wd->createCharacteristic(WD_TX,BLECharacteristic::PROPERTY_NOTIFY);wdTx->setCallbacks(&callbacks);
    for(const char* uuid:{WD_RX,WD_CTRL})wd->createCharacteristic(uuid,BLECharacteristic::PROPERTY_WRITE|BLECharacteristic::PROPERTY_WRITE_NR)->setCallbacks(&callbacks);
    if(!wd->start())return false;
    auto* bs=server->createService(BIS_SERVICE);
    bisTx=bs->createCharacteristic(BIS_RSP,BLECharacteristic::PROPERTY_NOTIFY);bisTx->setCallbacks(&callbacks);
    bs->createCharacteristic(BIS_CMD,BLECharacteristic::PROPERTY_WRITE|BLECharacteristic::PROPERTY_WRITE_NR)->setCallbacks(&callbacks);
    bisStatus=bs->createCharacteristic(BIS_STA,BLECharacteristic::PROPERTY_READ);bisStatus->setCallbacks(&callbacks);
    if(!bs->start())return false;
    auto* info=server->createService(BLEUUID(uint16_t(0x180a)));
    info->createCharacteristic(BLEUUID(uint16_t(0x2a24)),BLECharacteristic::PROPERTY_READ)->setValue("Biscuit DIY");
    info->createCharacteristic(BLEUUID(uint16_t(0x2a29)),BLECharacteristic::PROPERTY_READ)->setValue("Umbravox");
    info->createCharacteristic(BLEUUID(uint16_t(0x2a26)),BLECharacteristic::PROPERTY_READ)->setValue(FW_VERSION);
    if(!info->start())return false;
    server->start();
    return server->isStarted();
    });
    if(!result.configured)Serial.println("bridge: GATT initialization failed; bridge remains off");
    if(!result.resumed)Serial.println("bridge: BLE scan resume failed; scanner recovery remains active");
    return result.configured && result.resumed;
}
void advertise(){
    auto* adv=BLEDevice::getAdvertising();adv->stop();
    const char* gap=selected==BISCUIT?"Biscuit":name;
    ble_svc_gap_device_name_set(gap);
    BLEAdvertisementData data,response;data.setFlags(6);
    data.setCompleteServices(BLEUUID(selected==BISCUIT?BIS_SERVICE:WD_SERVICE));response.setName(gap);
    adv->setAdvertisementData(data);adv->setScanResponseData(response);adv->setScanResponse(true);
    adv->setMinPreferred(24);adv->setMaxPreferred(48);
    if(!adv->start())Serial.println("bridge: advertising start failed; will retry");
}
void greet(){
    if(selected==WARDRIVEGO){
        char channels[128];cap::bridgePlan(channels,sizeof channels);
        snprintf(st->scratch,sizeof st->scratch,"%s chans=%s\n",WD_CAPS,channels);
        st->out.put(st->scratch,st->reply);
        st->out.put("WD:MODE mode=wifi\n",st->reply);
        st->out.put("WD:BEGIN type=wifi interval=0 channel=auto\n",st->reply);
        snprintf(st->scratch,sizeof st->scratch,"WD:PLAN chans=%s\n",channels);st->out.put(st->scratch,st->reply);
    }else{
        st->out.put("TIME:REQUEST:\n");st->out.put("STATUS:1:Ready\n");
    }
    st->greeted=true;st->timeAt=millis();
}
void command(const Rx& rx,uint32_t now){
    const int result=st->command.accept(rx.data,rx.len,now);
    if(result==0)return;
    if(result<0){st->out.put(selected==BISCUIT?"RSP:command:ERROR:malformed\n":"Malformed command",st->reply,5,1);return;}
    st->reply=st->command.id;
    const auto parsed=parse(st->command.text,Mode(selected.load()));
    if(parsed.action==STOP || parsed.action==START)discardUnsent(st->out);
    if(selected==WARDRIVEGO && st->command.framed)st->out.put("",st->reply,2);
    char reply[128];
    if(parsed.action==START){
        streaming=true;st->band=parsed.band;st->cursor=0;st->sent=0;st->roundDone=false;st->roundAt=now;
        if(selected==WARDRIVEGO)greet();
        else {snprintf(reply,sizeof reply,"RSP:%s:OK\nSTATUS:2:Scan started: %s\n",parsed.name,parsed.name);st->out.put(reply);}
    }else if(parsed.action==STOP){
        streaming=false;
        st->out.put(selected==WARDRIVEGO?"WD:END reason=stop\n":"RSP:stopscan:OK\nSTATUS:1:Ready\n",st->reply);
        if(selected==WARDRIVEGO)st->out.put("",st->reply,4);
    }else if(parsed.action==TIME_SYNC){st->timeSynced=true;}
    else if(selected==BISCUIT){snprintf(reply,sizeof reply,"RSP:%s:ERROR:unsupported\n",parsed.name[0]?parsed.name:"command");st->out.put(reply);}
    else st->out.put("Unsupported command",st->reply,5,1);
}
void transmit(uint32_t now){
    if(!subscribed || connection==NO_CONN || st->seenEpoch!=epoch.load())return;
    if(uint32_t(now-st->lastTx)<15)return;
    auto* line=st->out.front();if(!line)return;
    const bool wd=selected==WARDRIVEGO;
    uint8_t packetBytes[244];size_t n=0;
    const size_t len=packet(*line,Mode(selected.load()),mtu.load(),packetBytes,sizeof packetBytes,n);
    if(!len)return;
    auto* buffer=ble_hs_mbuf_from_flat(packetBytes,len);
    int rc=buffer?ble_gatts_notify_custom(connection.load(),(wd?wdTx:bisTx)->getHandle(),buffer):BLE_HS_ENOMEM;
    st->lastTx=now;
    if(rc){++st->notifyFails;return;} // same bytes retained; never advance on failure
    line->offset+=n;if(line->offset==line->len)st->out.pop();
}
}
void setMode(uint8_t mode){
    if(mode>2)mode=0;
    if(mode==selected)return;
    selected=mode;
    if(server){BLEDevice::getAdvertising()->stop();if(connection!=NO_CONN)server->disconnect(connection.load());}
    subscribed=false;streaming=false;resetRx();++epoch;
    if(mode && !init()){selected=OFF;Serial.println("bridge: initialization unavailable");return;}
    if(st){clearSession(millis());st->seenEpoch=epoch.load();}
    Serial.printf("bridge: %s\n",mode==1?"WarDriveGo enabled":mode==2?"Biscuit enabled":"off");
}
void tick(uint32_t now){
    if(!st || !server || selected==OFF)return;
    if(st->seenEpoch!=epoch.load()){clearSession(now);st->seenEpoch=epoch.load();}
    if(connection==NO_CONN){
        if(uint32_t(now-st->advAt)>=1500){st->advAt=now;if(!BLEDevice::getAdvertising()->isAdvertising())advertise();}
        return;
    }
    if(!subscribed)return;
    if(!st->greeted){greet();if(selected==WARDRIVEGO){streaming=true;st->roundAt=now;}}
    { // RX control remains responsive even while notifications are congested.
        Rx rx{};bool have=false;
        portENTER_CRITICAL(&rxMux);
        if(st->rxCount){rx=st->rx[st->rxHead];st->rxHead=(st->rxHead+1)%4;--st->rxCount;have=true;}
        portEXIT_CRITICAL(&rxMux);
        if(have && rx.epoch==st->seenEpoch)command(rx,now);
    }
    if(selected==BISCUIT && !st->timeSynced && uint32_t(now-st->timeAt)>=5000 && st->out.free()){
        st->out.put("TIME:REQUEST:\n");st->timeAt=now;
    }
    if(streaming && uint32_t(now-st->connectedAt)>=3000 && st->out.free()>=6 && uint32_t(now-st->lastAp)>=100){
        if(restartRound(st->roundDone,now,st->roundAt)){st->roundDone=false;st->cursor=0;st->roundAt=now;}
        {
            Ap ap;const int result=cap::bridgeNext(st->cursor,st->generation,ap,now);
            if(result<0)st->lastAp=now;
            if(result<0 && !st->roundDone){st->roundDone=true;
                if(selected==WARDRIVEGO){char line[80];snprintf(line,sizeof line,"WD:STATUS aps=%lu scans=0\n",(unsigned long)st->sent);st->out.put(line,st->reply);}}
            else if(result>0 && allows(st->band,ap.channel)){st->roundDone=false;if(apLine(st->scratch,sizeof st->scratch,ap,Mode(selected.load()))){st->out.put(st->scratch,st->reply);++st->sent;st->lastAp=now;}}
        }
    }
    transmit(now);
    if(uint32_t(now-st->diagAt)>=10000){
        st->diagAt=now;Serial.printf("[bridge] mode %u stream %u queued %lu queue %u rx-drop %lu tx-retry %lu heap %u\n",unsigned(selected.load()),unsigned(streaming.load()),(unsigned long)st->sent,st->out.count,(unsigned long)st->rxDrops,(unsigned long)st->notifyFails,unsigned(ESP.getFreeHeap()));
    }
}
Stats stats(){
    Stats s;s.mode=selected.load();s.connected=connection!=NO_CONN;s.streaming=streaming.load();
    if(st){s.queued=st->out.count;s.txRetries=st->notifyFails;portENTER_CRITICAL(&rxMux);s.rxDrops=st->rxDrops;portEXIT_CRITICAL(&rxMux);}
    return s;
}
uint8_t mode(){return selected.load();}
const char* status(){return selected==OFF?"OFF":streaming?"STREAMING":connection!=NO_CONN?"CONNECTED":"ADVERTISING";}
}
#else
namespace blebridge {Stats stats(){return {};} uint8_t mode(){return 0;} void setMode(uint8_t){} void tick(uint32_t){} const char* status(){return "UNAVAILABLE";}}
#endif
