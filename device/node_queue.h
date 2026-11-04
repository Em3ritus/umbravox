#pragma once
#include "mesh.h"
#include <string.h>
namespace nodequeue {
// Retain one pending observation per BSSID. Refresh signal, channel and age;
// a hidden-name repeat must not erase a name already learned in this batch.
template<int N> bool put(mesh::Rec (&rows)[N], uint32_t (&ages)[N], int& count,
                         const mesh::Rec& record, uint32_t now) {
    for(int i=0;i<count;++i){
        if(memcmp(rows[i].bssid,record.bssid,6))continue;
        mesh::Rec next=record;
        if(!next.ssidLen && rows[i].ssidLen){
            next.ssidLen=rows[i].ssidLen;
            memcpy(next.ssid,rows[i].ssid,sizeof next.ssid);
        }
        rows[i]=next;ages[i]=now;return true;
    }
    if(count>=N)return false;
    rows[count]=record;ages[count++]=now;return true;
}
}
