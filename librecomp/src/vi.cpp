#include <ultramodern/ultramodern.hpp>
#include <atomic>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "recomp.h"
#include "helpers.hpp"

static inline int hm64_env_truthy(const char* env_name) {
    const char* v = getenv(env_name);
    if (v == NULL) {
        return 0;
    }
    return (v[0] == '1') || (v[0] == 'y') || (v[0] == 'Y') || (v[0] == 't') || (v[0] == 'T');
}

static inline void hm64_swap_log_vi(const char* tag, uint32_t fb) {
    if (!hm64_env_truthy("HM64_SWAP_LOG")) {
        return;
    }

    // osViSwapBuffer can be called from multiple threads; keep logging thread-safe.
    static std::atomic<uint64_t> seq{0};
    const uint64_t n = ++seq;
    fprintf(stderr, "[hm64][swap] #%llu %s fb=0x%08X\n",
        (unsigned long long)n, tag, (unsigned)fb);
}

extern "C" void osViSetYScale_recomp(uint8_t* rdram, recomp_context * ctx) {
    osViSetYScale(ctx->f12.fl);
}

extern "C" void osViSetXScale_recomp(uint8_t* rdram, recomp_context * ctx) {
    osViSetXScale(ctx->f12.fl);
}

extern "C" void osCreateViManager_recomp(uint8_t* rdram, recomp_context* ctx) {
    ;
}

extern "C" void osViBlack_recomp(uint8_t* rdram, recomp_context* ctx) {
    if (hm64_env_truthy("HM64_VI_LOG")) {
        fprintf(stderr, "[hm64][vi] osViBlack active=%u\n", (unsigned)ctx->r4);
    }
    osViBlack((uint32_t)ctx->r4);
}

extern "C" void osViRepeatLine_recomp(uint8_t* rdram, recomp_context* ctx) {
    osViRepeatLine(_arg<0, u8>(rdram, ctx));
}

extern "C" void osViSetSpecialFeatures_recomp(uint8_t* rdram, recomp_context* ctx) {
    if (hm64_env_truthy("HM64_VI_LOG")) {
        fprintf(stderr, "[hm64][vi] osViSetSpecialFeatures func=0x%08X\n", (unsigned)ctx->r4);
    }
    osViSetSpecialFeatures((uint32_t)ctx->r4);
}

extern "C" void osViGetCurrentFramebuffer_recomp(uint8_t* rdram, recomp_context* ctx) {
    ctx->r2 = (gpr)(int32_t)osViGetCurrentFramebuffer();
}

extern "C" void osViGetNextFramebuffer_recomp(uint8_t* rdram, recomp_context* ctx) {
    ctx->r2 = (gpr)(int32_t)osViGetNextFramebuffer();
}

extern "C" void osViSwapBuffer_recomp(uint8_t* rdram, recomp_context* ctx) {
    hm64_swap_log_vi("osViSwapBuffer", (uint32_t)ctx->r4);
    osViSwapBuffer(rdram, (int32_t)ctx->r4);
}

extern "C" void osViSetMode_recomp(uint8_t* rdram, recomp_context* ctx) {
    if (hm64_env_truthy("HM64_VI_LOG")) {
        fprintf(stderr, "[hm64][vi] osViSetMode mode=0x%08X\n", (unsigned)ctx->r4);
    }
    osViSetMode(rdram, (int32_t)ctx->r4);
}

extern uint64_t total_vis;

extern "C" void wait_one_frame(uint8_t* rdram, recomp_context* ctx) {
    uint64_t cur_vis = total_vis;
    while (cur_vis == total_vis) {
        std::this_thread::yield();
    }
}
