#include <cstdio>
#include <fstream>
#include <cstdlib>
#include <ultramodern/ultramodern.hpp>
#include <ultramodern/extensions.h>
#include "recomp.h"

extern "C" void osSpTaskLoad_recomp(uint8_t* rdram, recomp_context* ctx) {
    // Nothing to do here
}

bool dump_frame = false;

static inline bool env_truthy(const char* name) {
    const char* v = std::getenv(name);
    if (v == nullptr) {
        return false;
    }
    return (v[0] == '1') || (v[0] == 'y') || (v[0] == 'Y') || (v[0] == 't') || (v[0] == 'T');
}

extern "C" void osSpTaskStartGo_recomp(uint8_t* rdram, recomp_context* ctx) {
    //printf("[sp] osSpTaskStartGo(0x%08X)\n", (uint32_t)ctx->r4);
    OSTask* task = TO_PTR(OSTask, ctx->r4);
    if (task->t.type == M_GFXTASK) {
        //printf("[sp] Gfx task: %08X\n", (uint32_t)ctx->r4);
    } else if (task->t.type == M_AUDTASK) {
        //printf("[sp] Audio task: %08X\n", (uint32_t)ctx->r4);
    }
    // For debugging
    if (dump_frame) {
        char addr_str[32];
        constexpr size_t ram_size = 0x800000;
        std::unique_ptr<char[]> ram_unswapped = std::make_unique<char[]>(ram_size);
        snprintf(addr_str, sizeof(addr_str) - 1, "%08X", task->t.data_ptr);
        addr_str[sizeof(addr_str) - 1] = '\0';
        std::ofstream dump_file{ "ramdump" + std::string{ addr_str } + ".bin", std::ios::binary};

        for (size_t i = 0; i < ram_size; i++) {
            ram_unswapped[i] = rdram[i ^ 3];
        }

        dump_file.write(ram_unswapped.get(), ram_size);
        dump_frame = false;
    }
    ultramodern::submit_rsp_task(rdram, ctx->r4);

    // Some games modify vertex/texture data immediately after submitting the displaylist.
    // Our renderer parses DLs asynchronously; wait until it has parsed this DL to remove that race.
    // Opt-in via HM64_DL_SYNC_PARSED=1 (HM64 project can default this on).
    if (task->t.type == M_GFXTASK && env_truthy("HM64_DL_SYNC_PARSED")) {
        (void)ultramodern::extensions::wait_for_displaylist_parsed(task->t.data_ptr, 200);
    }
}

extern "C" void osSpTaskYield_recomp(uint8_t* rdram, recomp_context* ctx) {
    // Ignore yield requests (acts as if the task completed before it received the yield request)
}

extern "C" void osSpTaskYielded_recomp(uint8_t* rdram, recomp_context* ctx) {
    // Task yield requests are ignored, so always return 0 as tasks will never be yielded
    ctx->r2 = 0;
}

extern "C" void __osSpSetPc_recomp(uint8_t* rdram, recomp_context* ctx) {
    assert(false);
}
