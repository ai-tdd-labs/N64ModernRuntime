#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <ultramodern/ultramodern.hpp>
#include "recomp.h"

static bool wr64_spdp_trace_enabled() {
    static const bool enabled = std::getenv("WR64_SPDP_TRACE") != nullptr;
    return enabled;
}

static void wr64_trace_sp(const char* event, const OSTask* task, uint32_t task_addr) {
    if (!wr64_spdp_trace_enabled()) {
        return;
    }
    std::fprintf(
        stderr,
        "[wr64-spdp] event=%s task=0x%08X type=%u flags=0x%08X data=0x%08X data_size=0x%08X\n",
        event,
        task_addr,
        task != nullptr ? task->t.type : 0,
        task != nullptr ? task->t.flags : 0,
        task != nullptr ? static_cast<uint32_t>(task->t.data_ptr) : 0,
        task != nullptr ? task->t.data_size : 0);
}

extern "C" void osSpTaskLoad_recomp(uint8_t* rdram, recomp_context* ctx) {
    wr64_trace_sp("osSpTaskLoad", TO_PTR(OSTask, ctx->r4), ctx->r4);
    // Nothing to do here
}

bool dump_frame = false;

extern "C" void osSpTaskStartGo_recomp(uint8_t* rdram, recomp_context* ctx) {
    //printf("[sp] osSpTaskStartGo(0x%08X)\n", (uint32_t)ctx->r4);
    OSTask* task = TO_PTR(OSTask, ctx->r4);
    wr64_trace_sp("osSpTaskStartGo", task, ctx->r4);
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
}

extern "C" void osSpTaskYield_recomp(uint8_t* rdram, recomp_context* ctx) {
    if (wr64_spdp_trace_enabled()) {
        std::fprintf(stderr, "[wr64-spdp] event=osSpTaskYield\n");
    }
    // Ignore yield requests (acts as if the task completed before it received the yield request)
}

extern "C" void osSpTaskYielded_recomp(uint8_t* rdram, recomp_context* ctx) {
    // Task yield requests are ignored, so always return 0 as tasks will never be yielded
    ctx->r2 = 0;
    if (wr64_spdp_trace_enabled()) {
        std::fprintf(stderr, "[wr64-spdp] event=osSpTaskYielded result=0 task=0x%08X\n", static_cast<uint32_t>(ctx->r4));
    }
}

extern "C" void __osSpSetPc_recomp(uint8_t* rdram, recomp_context* ctx) {
    assert(false);
}
