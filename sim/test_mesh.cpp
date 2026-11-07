// Host test for the mesh wire protocol: struct sizes stay within the ESP-NOW
// 250-byte cap, and valid() accepts the new coordinator-tier message kinds.
#include "../device/mesh.h"
#include <stdio.h>
#include <string.h>

static int fails = 0;
static void check(const char* what, bool ok) {
    printf(ok ? "  ok   %s\n" : "  FAIL %s\n", what);
    if (!ok) fails++;
}

int main() {
    printf("every wire struct fits an ESP-NOW frame (<=250):\n");
    check("Hello   <=250", sizeof(mesh::Hello)        <= 250);
    check("Assign  <=250", sizeof(mesh::Assign)       <= 250);
    check("Report  <=250", sizeof(mesh::Report)       <= 250);
    check("CoordReg<=250", sizeof(mesh::CoordReg)     <= 250);
    check("CAssign <=250", sizeof(mesh::CoordAssign)  <= 250);
    check("CBatch  <=250", sizeof(mesh::ClusterBatch) <= 250);

    printf("version bumped so old nodes are rejected:\n");
    check("version is 4", mesh::VERSION == 4);

    printf("valid() accepts a well-formed CBatch and rejects a short one:\n");
    mesh::ClusterBatch b = {};
    b.h.magic = mesh::MAGIC; b.h.ver = mesh::VERSION;
    b.h.netId = mesh::netId; b.h.kind = mesh::MSG_CBATCH;
    check("full CBatch valid",
          mesh::valid((const uint8_t*)&b, sizeof b, mesh::MSG_CBATCH, (int)sizeof b));
    check("truncated CBatch rejected",
          !mesh::valid((const uint8_t*)&b, (int)sizeof(mesh::Hdr), mesh::MSG_CBATCH, (int)sizeof b));

    printf("a batch carries the coordinator's own node count; a frame from an older coordinator (one byte shorter) is still accepted:\n");
    {
        mesh::ClusterBatch nb = b; nb.nodes = 3;
        check("the node count is the one trailing byte", sizeof(mesh::ClusterBatch) == (size_t)mesh::CBATCH_MIN_LEN + 1);
        check("a new frame is valid and reports its nodes",
              mesh::valid((const uint8_t*)&nb, sizeof nb, mesh::MSG_CBATCH, mesh::CBATCH_MIN_LEN) && mesh::batchNodes(&nb, sizeof nb) == 3);
        check("an older coordinator's frame (without the byte) is valid",
              mesh::valid((const uint8_t*)&nb, mesh::CBATCH_MIN_LEN, mesh::MSG_CBATCH, mesh::CBATCH_MIN_LEN));
        check("and says the count is unknown (-1), not 0",
              mesh::batchNodes(&nb, mesh::CBATCH_MIN_LEN) == -1);
        check("a frame shorter than the old minimum is still rejected",
              !mesh::valid((const uint8_t*)&nb, mesh::CBATCH_MIN_LEN - 1, mesh::MSG_CBATCH, mesh::CBATCH_MIN_LEN));
    }

    printf("a frame from another network id is rejected:\n");
    mesh::ClusterBatch other = b;
    other.h.netId = (uint8_t)(mesh::netId + 1);   // someone else's mesh in the room
    check("foreign network id rejected",
          !mesh::valid((const uint8_t*)&other, sizeof other, mesh::MSG_CBATCH, (int)sizeof other));

    printf("Assign carries a report phase offset:\n");
    mesh::Assign a = {}; a.phaseMs = 1234;
    check("phaseMs round-trips", a.phaseMs == 1234);

    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
