#pragma once
namespace bridgeinit {
struct Result { bool configured=false, paused=false, resumed=true; };
// GATT reset/registration is illegal while NimBLE discovery is active.
// Scanner ownership remains with blescan; restore only a scan we actually stopped.
template<class Scanner,class Configure>
Result configure(Scanner& scan,Configure configureGatt) {
    Result result;
    const bool active=scan.isScanning();
    if(active){
        if(!scan.stop())return result;
        result.paused=true;
        if(scan.isScanning()){result.resumed=false;return result;}
    }
    result.configured=configureGatt();
    if(active)result.resumed=scan.start(0,nullptr,false);
    return result;
}
}
