#include "../device/pcap.h"
#include "../device/sdlog.h"
#include <map>
#include <string>
#include <vector>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <thread>
#include <atomic>
#include <fstream>
namespace {std::map<std::string,std::string> files;std::string current;bool card=true,shortWrite=false,closeFail=false,rollbackFail=false,openFail=false;unsigned writes=0;}
namespace sdlog {
bool present(){return card;}bool exists(const char* f){return files.count(f);}
bool openAppend(const char* f,uint32_t& size){if(openFail)return false;assert(current.empty());current=f;size=files[f].size();return true;}
bool writeAppend(const char* p,uint32_t n){++writes;assert(!current.empty());files[current].append(p,shortWrite?n/2:n);return !shortWrite;}
bool closeAppend(){current.clear();return !closeFail;}
bool truncateTo(const char* f,uint32_t n){if(rollbackFail)return false;files[f].resize(n);return true;}
}
static uint32_t le(const std::string& s,size_t p){assert(p+4<=s.size());return uint8_t(s[p])|uint32_t(uint8_t(s[p+1]))<<8|uint32_t(uint8_t(s[p+2]))<<16|uint32_t(uint8_t(s[p+3]))<<24;}
int main(){
 card=false;assert(!pcap::start()&&!pcap::active());card=true;
 assert(pcap::start());const std::string name=pcap::fileName();
 auto& f=files[name];assert(f.size()==24&&le(f,0)==0xa1b2c3d4&&le(f,16)==1536&&le(f,20)==105);
 std::vector<uint8_t> packet(2000,0);packet[0]=0x80;unsigned before=writes;
 for(int i=0;i<20;i++){packet[1]=i;pcap::receive(packet.data(),packet.size(),12345678+i);}
 assert(writes==before&&pcap::dropped()==4); // Callback never writes SD; fixed sixteen records.
 pcap::service();assert(pcap::written()==2&&f.size()==24+2*(16+1536));
 pcap::stop();pcap::receive(packet.data(),packet.size(),1);while(pcap::pending())pcap::service();
 assert(pcap::written()==16&&pcap::dropped()==4&&!pcap::active());
 size_t off=24;for(int i=0;i<16;i++){assert(le(f,off)==12&&le(f,off+4)==345678u+i&&le(f,off+8)==1536&&le(f,off+12)==2000);assert(uint8_t(f[off+17])==i);off+=16+1536;}assert(off==f.size());
 std::ofstream out("/tmp/umbravox-pcap-test.pcap",std::ios::binary);out.write(f.data(),f.size());out.close();
 assert(pcap::start()&&name!=pcap::fileName());const std::string name2=pcap::fileName();
 pcap::receive(packet.data(),100,1);shortWrite=true;pcap::service();assert(!pcap::active()&&files[name2].size()==24&&pcap::dropped()==1);pcap::service();assert(!pcap::pending());shortWrite=false;
 assert(pcap::start());pcap::receive(packet.data(),100,1);closeFail=true;pcap::service();assert(!pcap::active()&&files[pcap::fileName()].size()==24);pcap::service();closeFail=false;
 assert(pcap::start());pcap::receive(packet.data(),100,1);shortWrite=true;rollbackFail=true;pcap::service();assert(std::string(pcap::status())=="DAMAGED FILE");pcap::service();shortWrite=rollbackFail=false;
 openFail=true;assert(!pcap::start());openFail=false;
 assert(pcap::start());pcap::receive(nullptr,20,1);pcap::receive(packet.data(),9,1);pcap::stop();pcap::service();assert(pcap::written()==0&&!pcap::pending());
 // Size cap stops cleanly; queued frames are counted as dropped, not appended.
 assert(pcap::start());while(pcap::active()){pcap::receive(packet.data(),2000,1);pcap::receive(packet.data(),2000,1);pcap::service();}while(pcap::pending())pcap::service();assert(files[pcap::fileName()].size()<=32*1024*1024&&std::string(pcap::status())=="32MB LIMIT");
 // Producer/consumer contention: no waiting or SD operations in receive().
 assert(pcap::start());std::atomic<bool> done{false};
 std::thread producer([&]{for(int i=0;i<10000;i++)pcap::receive(packet.data(),100,1000000+i);done=true;});
 while(!done.load()){pcap::service();std::this_thread::yield();}producer.join();pcap::stop();while(pcap::pending())pcap::service();
 assert(pcap::written()+pcap::dropped()==10000);
 // A complete beacon fixture, independently decoded by tcpdump after this test.
 assert(pcap::start());std::vector<uint8_t> beacon(49,0);beacon[0]=0x80;for(int i=4;i<10;i++)beacon[i]=255;
 beacon[10]=beacon[16]=0x00;beacon[11]=beacon[17]=0x00;beacon[12]=beacon[18]=0x0c;
 beacon[32]=100;beacon[34]=1;beacon[36]=0;beacon[37]=11;memcpy(beacon.data()+38,"TEST-SURVEY",11);
 pcap::receive(beacon.data(),beacon.size(),12345678);pcap::stop();while(pcap::pending())pcap::service();
 const auto& finalFile=files[pcap::fileName()];std::ofstream decoded("/tmp/umbravox-pcap-test.pcap",std::ios::binary);decoded.write(finalFile.data(),finalFile.size());decoded.close();
 puts("PCAP: binary format, truncation, overflow, bounded service, drain, fresh files, write/close/rollback/open failures, size cap PASS");
}
