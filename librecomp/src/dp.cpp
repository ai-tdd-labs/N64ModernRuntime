#include "recomp.h"
#include <cstdio>
#include <cstdlib>

enum class RDPStatusBit {
    XbusDmem = 0,
    Freeze = 1,
    Flush = 2,
    CommandBusy = 6,
    BufferReady = 7,
    DmaBusy = 8,
    EndValid = 9,
    StartValid = 10,
};

constexpr void update_bit(uint32_t& state, uint32_t flags, RDPStatusBit bit) {
    int reset_bit_pos = (int)bit * 2 + 0;
    int set_bit_pos = (int)bit * 2 + 1;
    bool set = (flags & (1U << set_bit_pos)) != 0;
    bool reset = (flags & (1U << reset_bit_pos)) != 0;

    if (set ^ reset) {
        if (set) {
            state |= (1U << (int)bit);
        }
        else {
            state &= ~(1U << (int)bit);
        }
    }
}

uint32_t rdp_state = 1 << (int)RDPStatusBit::BufferReady;

static bool wr64_spdp_trace_enabled() {
    static const bool enabled = std::getenv("WR64_SPDP_TRACE") != nullptr;
    return enabled;
}

extern "C" void osDpSetNextBuffer_recomp(uint8_t* rdram, recomp_context* ctx) {
    assert(false);
}

extern "C" void osDpGetStatus_recomp(uint8_t* rdram, recomp_context* ctx) {
    ctx->r2 = rdp_state;
    if (wr64_spdp_trace_enabled()) {
        std::fprintf(stderr, "[wr64-spdp] event=osDpGetStatus result=0x%08X\n", rdp_state);
    }
}

extern "C" void osDpSetStatus_recomp(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t before = rdp_state;
    update_bit(rdp_state, ctx->r4, RDPStatusBit::XbusDmem);
    update_bit(rdp_state, ctx->r4, RDPStatusBit::Freeze);
    update_bit(rdp_state, ctx->r4, RDPStatusBit::Flush);
    if (wr64_spdp_trace_enabled()) {
        std::fprintf(
            stderr,
            "[wr64-spdp] event=osDpSetStatus flags=0x%08X before=0x%08X after=0x%08X\n",
            static_cast<uint32_t>(ctx->r4),
            before,
            rdp_state);
    }
}
