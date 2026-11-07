#include "ble_bridge.h"
#if defined(UMBRAVOX_SCAN_ENGINE)
#include "ble_bridge_protocol.h"
#include "ble_bridge_init.h"
#include "ble_bridge_auth.h"
#include "capture.h"
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <esp_heap_caps.h>
#include <esp_mac.h>
#include <esp_random.h>
#include <atomic>
#include <stdarg.h>
#include <new>
#include <host/ble_hs.h>
#include <services/gap/ble_svc_gap.h>
#include <services/gatt/ble_svc_gatt.h>

// The bridge streams every captured AP to whoever connects, so by default it
// requires a bonded, passkey-authenticated link (see ble_bridge_auth.h). Build with
// -DUMBRAVOX_BRIDGE_AUTH=0 only to test a phone app that cannot handle pairing.
#ifndef UMBRAVOX_BRIDGE_AUTH
#define UMBRAVOX_BRIDGE_AUTH 1
#endif

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
    ChannelFilter channels;
    uint8_t band=0;uint32_t reply=1,cursor=0,generation=0,sent=0;
    uint32_t connectedAt=0,lastTx=0,roundAt=0,timeAt=0,diagAt=0,advAt=0,notifyFails=0;
    uint32_t staleVisits=0,txLines=0;
    bool roundDone=false;
};
State* st=nullptr;
// Every bridge line carries the uptime in ms, so security events can be ordered against each other
// and against the phone's actions. Never pass a pairing code to this.
void blog(const char* fmt,...) __attribute__((format(printf,1,2)));
void blog(const char* fmt,...){
    char buf[160];va_list ap;va_start(ap,fmt);vsnprintf(buf,sizeof buf,fmt,ap);va_end(ap);
    Serial.printf("[%lu] bridge: %s\n",(unsigned long)millis(),buf);
}
bridgeauth::PinWindow pinWin;
bridgeauth::PairLimiter pairLimiter;
uint32_t bondsAt=0;int bondsCached=-1;   // loop task only: cached bond count
bool securityRequested=false,dropRequested=false;   // loop task only: one Security Request and at most one drop per connection
std::atomic<bool> codeShown{false};   // a pairing attempt has displayed the current code
// One code per pairing window: the stack gets a static passkey, so a phone that
// reconnects mid-pairing is asked for the same code the owner is reading. It is
// replaced (loop task) only after the window expires or a phone has paired.
uint32_t newCode(){return esp_random()%1000000;}
// Encryption / authentication state of a live link, from the host stack.
bool linkSecure(uint16_t conn){
    ble_gap_conn_desc d{};
    if(conn==NO_CONN || ble_gap_conn_find(conn,&d)!=0)return false;
    return bridgeauth::mayServe(UMBRAVOX_BRIDGE_AUTH!=0,d.sec_state.encrypted,d.sec_state.authenticated);
}
#if UMBRAVOX_BRIDGE_AUTH
// Runs on the BLE host task: only record the code and the result.
class SecurityCallbacks:public BLESecurityCallbacks {
    void onPassKeyNotify(uint32_t passkey) override {
        codeShown=true;   // this code has now been used for a pairing: tick() rotates it
        if(pairLimiter.blocked(millis())){
            // Locked out: refuse the NEW pairing at the link layer, before any guess is
            // checked (a bonded phone reconnects with its stored keys and never gets here).
            const uint16_t c=connection.load();
            if(c!=NO_CONN)ble_gap_terminate(c,BLE_ERR_REM_USER_CONN_TERM);
            blog("new pairing refused: locked out");
            return;
        }
        const bool started=pinWin.offer(passkey,millis());
        if(started)blog("phone is pairing; the code is on ATTUNE > BRIDGE > LINK STATUS");   // never log the code itself
    }
    void onAuthenticationComplete(ble_gap_conn_desc* d) override {
        if(!d)return;
        blog("link security encrypted=%d authenticated=%d bonded=%d",(int)d->sec_state.encrypted,(int)d->sec_state.authenticated,(int)d->sec_state.bonded);
        if(d->sec_state.encrypted && d->sec_state.authenticated){pairLimiter.noteSuccess();pinWin.clear();}
        else{
            // A failed pairing retires its code: the window ends and tick() rotates it, so
            // no code is ever reused after a failure. Three failures also lock out new pairing.
            pinWin.clear();
            if(pairLimiter.noteFailure(millis()))blog("pairing locked out after repeated failures; try again shortly");
        }
    }
} securityCallbacks;
constexpr uint32_t WRITE_SEC=BLECharacteristic::PROPERTY_WRITE_AUTHEN, READ_SEC=BLECharacteristic::PROPERTY_READ_AUTHEN;
#else
constexpr uint32_t WRITE_SEC=0, READ_SEC=0;
#endif
BLEServer* server=nullptr; BLECharacteristic *wdTx=nullptr,*bisTx=nullptr,*bisStatus=nullptr;
BLEService *wdService=nullptr,*biscuitService=nullptr;
BLECharacteristic* modelInfo=nullptr;
int identityMode=-1;
char name[24]{};
void clearSession(uint32_t now){
    st->out.clear();st->command.reset();st->greeted=false;st->timeSynced=false;
    st->staleVisits=st->txLines=0;st->reply=1;st->cursor=0;st->sent=0;st->band=0;st->channels=ChannelFilter{};st->roundDone=false;st->connectedAt=now;
    streaming=false;
}
void resetRx(){if(st){portENTER_CRITICAL(&rxMux);st->rxHead=st->rxCount=0;portEXIT_CRITICAL(&rxMux);}}
class ServerCallbacks:public BLEServerCallbacks {
    void onConnect(BLEServer* s,ble_gap_conn_desc* d) override {
        if(selected==OFF || connection!=NO_CONN){s->disconnect(d->conn_handle);return;}
        connection=d->conn_handle;mtu=23;subscribed=false;resetRx();++epoch;securityRequested=dropRequested=false;
        s->updateConnParams(d->conn_handle,24,48,0,600);
    }
    void onDisconnect(BLEServer*,ble_gap_conn_desc* d) override {
        if(connection!=d->conn_handle)return;
        connection=NO_CONN;subscribed=false;streaming=false;resetRx();++epoch;pinWin.clear();
    }
    void onMtuChanged(BLEServer*,ble_gap_conn_desc* d,uint16_t m) override {if(connection==d->conn_handle)mtu=m;}
} serverCallbacks;
class Callbacks:public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* c,ble_gap_conn_desc* d) override {
        if(!st || selected==OFF || connection!=d->conn_handle || !linkSecure(d->conn_handle))return;   // unpaired writes are ignored
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
#if UMBRAVOX_BRIDGE_AUTH
    // Handheld shows the code, phone types it: bonding + MITM + LE Secure Connections,
    // and a random 6-digit code per pairing window.
    BLESecurity::setAuthenticationMode(true,true,true);
    BLESecurity::setCapability(ESP_IO_CAP_OUT);
    BLESecurity::setPassKey(true,newCode());   // static for the window; rotated in tick()
    BLEDevice::setSecurityCallbacks(&securityCallbacks);
#endif
    uint8_t mac[6]{};
    if(esp_read_mac(mac,ESP_MAC_BT)!=ESP_OK)return false;
    formatDeviceName(name,sizeof name,mac);
    auto* scanner=BLEDevice::getScan();
    const auto result=bridgeinit::configure(*scanner,[&]() {
    server=BLEDevice::createServer();if(!server)return false;server->setCallbacks(&serverCallbacks);server->advertiseOnDisconnect(false);
    auto* wd=server->createService(WD_SERVICE);wdService=wd;
    wdTx=wd->createCharacteristic(WD_TX,BLECharacteristic::PROPERTY_NOTIFY);wdTx->setCallbacks(&callbacks);
    for(const char* uuid:{WD_RX,WD_CTRL})wd->createCharacteristic(uuid,BLECharacteristic::PROPERTY_WRITE|BLECharacteristic::PROPERTY_WRITE_NR|WRITE_SEC)->setCallbacks(&callbacks);
    if(!wd->start())return false;
    auto* bs=server->createService(BIS_SERVICE);biscuitService=bs;
    bisTx=bs->createCharacteristic(BIS_RSP,BLECharacteristic::PROPERTY_NOTIFY);bisTx->setCallbacks(&callbacks);
    bs->createCharacteristic(BIS_CMD,BLECharacteristic::PROPERTY_WRITE|BLECharacteristic::PROPERTY_WRITE_NR|WRITE_SEC)->setCallbacks(&callbacks);
    bisStatus=bs->createCharacteristic(BIS_STA,BLECharacteristic::PROPERTY_READ|READ_SEC);bisStatus->setCallbacks(&callbacks);
    if(!bs->start())return false;
    auto* info=server->createService(BLEUUID(uint16_t(0x180a)));
    modelInfo=info->createCharacteristic(BLEUUID(uint16_t(0x2a24)),BLECharacteristic::PROPERTY_READ);
    modelInfo->setValue(identity(Mode(selected.load())).model);
    info->createCharacteristic(BLEUUID(uint16_t(0x2a29)),BLECharacteristic::PROPERTY_READ)->setValue("Umbravox");
    info->createCharacteristic(BLEUUID(uint16_t(0x2a26)),BLECharacteristic::PROPERTY_READ)->setValue(FW_VERSION);
    if(!info->start())return false;
    server->start();
    return server->isStarted();
    });
    if(!result.configured)blog("GATT initialization failed; bridge remains off");
    if(!result.resumed)blog("BLE scan resume failed; scanner recovery remains active");
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
    if(!adv->start())blog("advertising start failed; will retry");
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
        streaming=true;st->band=parsed.band;st->channels=parsed.channels;st->cursor=0;st->sent=0;st->roundDone=false;st->roundAt=now;
        if(selected==WARDRIVEGO)greet();
        else {snprintf(reply,sizeof reply,"RSP:%s:OK\nSTATUS:2:Scan started: %s\n",parsed.name,parsed.name);st->out.put(reply);}
    }else if(parsed.action==STOP){
        streaming=false;
        st->out.put(selected==WARDRIVEGO?"WD:END reason=stop\n":"RSP:stopscan:OK\nSTATUS:1:Ready\n",st->reply);
        if(selected==WARDRIVEGO)st->out.put("",st->reply,4);
    }else if(parsed.action==TIME_SYNC){st->timeSynced=true;}
    else if(selected==BISCUIT){snprintf(reply,sizeof reply,"RSP:%s:ERROR:unsupported\n",parsed.name[0]?parsed.name:"command");st->out.put(reply);}
    else if(parsed.action==IDENTITY){ /* WDGO phone announcement; framed ACK already queued. */ }
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
    line->offset+=n;if(line->offset==line->len){st->out.pop();++st->txLines;}
}
}
void setMode(uint8_t mode){
    if(mode>2)mode=0;
    if(mode==selected)return;
    selected=mode;
    if(server){BLEDevice::getAdvertising()->stop();if(connection!=NO_CONN)server->disconnect(connection.load());}
    subscribed=false;streaming=false;resetRx();++epoch;
    if(mode && !init()){selected=OFF;blog("initialization unavailable");return;}
    if(st){clearSession(millis());st->seenEpoch=epoch.load();}
    blog("%s",mode==1?"WarDriveGo enabled":mode==2?"Biscuit enabled":"off");
}
void tick(uint32_t now){
    if(!st || !server || selected==OFF)return;
#if UMBRAVOX_BRIDGE_AUTH
    {uint32_t shown;
     // The window ended (expired, or a phone paired): the next phone gets a new code.
     if(codeShown.load() && !pinWin.get(now,shown)){codeShown=false;BLESecurity::setPassKey(true,newCode());blog("pairing code rotated");}}
#endif
    if(st->seenEpoch!=epoch.load()){clearSession(now);st->seenEpoch=epoch.load();}
    if(identityMode!=selected.load()){
        // setMode requests disconnect first; never change discovery while connected.
        if(connection!=NO_CONN)return;
        const Mode mode=Mode(selected.load());
        uint16_t handles[2]{};
        const BLEUUID wdUuid(WD_SERVICE),bisUuid(BIS_SERVICE);
        const int wdRc=ble_gatts_find_svc(&wdUuid.getNative()->u,&handles[0]);
        const int bisRc=ble_gatts_find_svc(&bisUuid.getNative()->u,&handles[1]);
        const bool ok=wdRc==0 && bisRc==0 && selectServices(mode,[&](bool biscuit,bool visible){
            const int rc=ble_gatts_svc_set_visibility(handles[biscuit?1:0],visible?1:0);
            if(rc)blog("visibility handle=%u visible=%u rc=%d",handles[biscuit?1:0],unsigned(visible),rc);
            return rc==0;
        });
        if(wdRc || bisRc)blog("lookup wd=%d biscuit=%d",wdRc,bisRc);
        if(!ok){selected=OFF;blog("service visibility failed; advertising remains off");return;}
        modelInfo->setValue(identity(mode).model);
        ble_svc_gatt_changed(1,0xFFFF); // let bonded peers invalidate cached discovery
        identityMode=mode;
    }
    if(connection==NO_CONN){
        if(uint32_t(now-st->advAt)>=1500){st->advAt=now;if(!BLEDevice::getAdvertising()->isAdvertising())advertise();}
        return;
    }
#if UMBRAVOX_BRIDGE_AUTH
    if(!linkSecure(connection.load())){
        // Unpaired phone: serve nothing, ask it to pair once (Android opens its
        // passkey dialog), and drop it if it never does so it cannot hold the slot.
        if(!securityRequested){
            securityRequested=true;int rc=0;const bool ok=BLESecurity::startSecurity(connection.load(),&rc);
            blog("phone connected unpaired; security request %s rc=%d",ok?"sent":"failed",rc);
        }
        // Only a connection that never secures itself is dropped. A lockout never drops a link
        // here: a bonded phone is briefly "unsecure" while it re-encrypts with its stored keys.
        if(!dropRequested && bridgeauth::pairingExpired(now,st->connectedAt)){
            dropRequested=true;server->disconnect(connection.load());   // never paired: free the slot
            blog("dropping a connection that never paired");
        }
        return;
    }
#endif
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
    if(streaming && apReady(now,st->connectedAt,st->out.free())){
        if(restartRound(st->roundDone,now,st->roundAt)){st->roundDone=false;st->cursor=0;st->roundAt=now;}
        {
            Ap ap;const int result=cap::bridgeNext(st->cursor,st->generation,ap,now,st->staleVisits);
            if(result<0 && !st->roundDone){st->roundDone=true;
                if(selected==WARDRIVEGO){char line[80];snprintf(line,sizeof line,"WD:STATUS aps=%lu scans=0\n",(unsigned long)st->sent);st->out.put(line,st->reply);}}
            else if(result>0 && allows(st->band,ap.channel) && st->channels.allows(ap.channel)){st->roundDone=false;if(apLine(st->scratch,sizeof st->scratch,ap,Mode(selected.load()))){st->out.put(st->scratch,st->reply);++st->sent;}}
        }
    }
    transmit(now);
    if(uint32_t(now-st->diagAt)>=10000){
        st->diagAt=now;Serial.printf("[bridge] t %lu mode %u stream %u queued %lu queue %u rx-drop %lu tx-retry %lu heap %u stale-visits %lu tx-lines %lu mtu %u\n",(unsigned long)millis(),unsigned(selected.load()),unsigned(streaming.load()),(unsigned long)st->sent,st->out.count,(unsigned long)st->rxDrops,(unsigned long)st->notifyFails,unsigned(ESP.getFreeHeap()),(unsigned long)st->staleVisits,(unsigned long)st->txLines,unsigned(mtu.load()));
    }
}
const char* deviceName(){return name[0]?name:"NOT STARTED";}
Stats stats(){
    Stats s;s.mode=selected.load();s.connected=connection!=NO_CONN;s.streaming=streaming.load();
    {uint32_t p=0;if(pinWin.get(millis(),p))s.pin=(int32_t)p;}
    if(st && server){   // bonded-phone count, refreshed at most once a second
        const uint32_t now=millis();
        if(bondsAt==0 || (int32_t)(now-bondsAt)>=1000){int n=0;bondsCached=ble_store_util_count(BLE_STORE_OBJ_TYPE_PEER_SEC,&n)==0?n:-1;bondsAt=now?now:1;}
        s.bonds=bondsCached;
    }
    if(st){s.queued=st->out.count;s.txRetries=st->notifyFails;portENTER_CRITICAL(&rxMux);s.rxDrops=st->rxDrops;portEXIT_CRITICAL(&rxMux);}
    return s;
}
uint8_t mode(){return selected.load();}
void forgetPhones(){
    if(!st || !server)return;   // BLE stack never started: nothing is bonded through us
    const int rc=ble_store_clear();
    pinWin.clear();
    bondsAt=0;bondsCached=0;
    const uint16_t c=connection.load();
    if(c!=NO_CONN)server->disconnect(c);
    blog("forgot all paired phones rc=%d",rc);
}
const char* status(){return selected==OFF?"OFF":streaming?"STREAMING":connection!=NO_CONN?"CONNECTED":"ADVERTISING";}
}
#else
namespace blebridge {const char* deviceName(){return "UNAVAILABLE";} Stats stats(){return {};} uint8_t mode(){return 0;} void setMode(uint8_t){} void tick(uint32_t){} void forgetPhones(){} const char* status(){return "UNAVAILABLE";}}
#endif
