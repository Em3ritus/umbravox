#pragma once
#include "meshcore.h"
#include "meshlive.h"
namespace meshcore {
// Pure projection of a locked node snapshot; no I/O or fabricated ACK state.
inline ui::NodeDiagnostics nodeDiagnostics(const NodeRec& node,uint32_t nowMs){
    ui::NodeDiagnostics diag{};
    diag.hasReport=node.reports>0;
    diag.reportAgeS=diag.hasReport?meshlive::ageMs(nowMs,node.lastReportMs)/1000:0;
    diag.assignedCount=node.assigned?(node.nChan<10?node.nChan:10):0;
    for(int k=0;k<diag.assignedCount;k++)diag.assignedChannels[k]=node.chans[k];
    const auto& t=node.telemetry;
    if(t.known){
        const auto& d=t.data;diag.known=true;diag.ageS=meshlive::ageMs(nowMs,t.receivedAt)/1000;
        diag.active=d.active;diag.profile=d.profile;diag.camp=d.camp;diag.dwellMs=d.dwellMs;
        diag.depth=d.depth;diag.capacity=d.capacity;diag.peak=d.peak;
        diag.drops=d.drops;diag.sendFailed=d.sendFailed;diag.reportRejected=d.reportRejected;diag.scanFailed=d.scanFailed;
    }
    return diag;
}
}
