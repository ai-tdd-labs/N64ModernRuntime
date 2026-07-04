#include <thread>
#include <cstdio>
#include <cstdlib>

#include "blockingconcurrentqueue.h"

#include "ultramodern/ultra64.h"
#include "ultramodern/ultramodern.hpp"

struct QueuedMessage {
    PTR(OSMesgQueue) mq;
    OSMesg mesg;
    bool jam;
    bool requeue_if_blocked;
};

static moodycamel::BlockingConcurrentQueue<QueuedMessage> external_messages {};

static bool wr64_mq_trace_enabled() {
    static const bool enabled = std::getenv("WR64_MQ_TRACE") != nullptr;
    return enabled;
}

static int wr64_current_thread_id() {
    return static_cast<int>(std::hash<std::thread::id>{}(std::this_thread::get_id()) & 0x7FFFFFFF);
}

static void wr64_trace_mq(const char* event, PTR(OSMesgQueue) mq_, OSMesg msg, const OSMesgQueue* mq) {
    if (!wr64_mq_trace_enabled()) {
        return;
    }
    if (mq != nullptr) {
        std::fprintf(
            stderr,
            "[wr64-mq] event=%s tid=%d mq=0x%08X msg=0x%08X valid=%d count=%d first=%d recv=0x%08X send=0x%08X\n",
            event,
            wr64_current_thread_id(),
            static_cast<uint32_t>(mq_),
            static_cast<uint32_t>(msg),
            mq->validCount,
            mq->msgCount,
            mq->first,
            static_cast<uint32_t>(mq->blocked_on_recv),
            static_cast<uint32_t>(mq->blocked_on_send));
    } else {
        std::fprintf(
            stderr,
            "[wr64-mq] event=%s tid=%d mq=0x%08X msg=0x%08X\n",
            event,
            wr64_current_thread_id(),
            static_cast<uint32_t>(mq_),
            static_cast<uint32_t>(msg));
    }
}

static void wr64_trace_mq_guest(RDRAM_ARG const char* event, PTR(OSMesgQueue) mq_, OSMesg msg, const OSMesgQueue* mq) {
    if (!wr64_mq_trace_enabled()) {
        return;
    }
    const PTR(OSThread) self_ = ultramodern::this_thread();
    const OSThread* self = self_ != NULLPTR ? TO_PTR(OSThread, self_) : nullptr;
    const OSThread* recv = mq != nullptr && mq->blocked_on_recv != NULLPTR ? TO_PTR(OSThread, mq->blocked_on_recv) : nullptr;
    const OSThread* send = mq != nullptr && mq->blocked_on_send != NULLPTR ? TO_PTR(OSThread, mq->blocked_on_send) : nullptr;
    if (mq != nullptr) {
        std::fprintf(
            stderr,
            "[wr64-mq] event=%s tid=%d self=%d self_pri=%d mq=0x%08X msg=0x%08X valid=%d count=%d first=%d recv=0x%08X recv_id=%d recv_pri=%d send=0x%08X send_id=%d send_pri=%d\n",
            event,
            wr64_current_thread_id(),
            self != nullptr ? self->id : -1,
            self != nullptr ? self->priority : -1,
            static_cast<uint32_t>(mq_),
            static_cast<uint32_t>(msg),
            mq->validCount,
            mq->msgCount,
            mq->first,
            static_cast<uint32_t>(mq->blocked_on_recv),
            recv != nullptr ? recv->id : -1,
            recv != nullptr ? recv->priority : -1,
            static_cast<uint32_t>(mq->blocked_on_send),
            send != nullptr ? send->id : -1,
            send != nullptr ? send->priority : -1);
    } else {
        wr64_trace_mq(event, mq_, msg, mq);
    }
}

void ultramodern::enqueue_external_message(PTR(OSMesgQueue) mq, OSMesg msg, bool jam, bool requeue_if_blocked, const char* source) {
    if (wr64_mq_trace_enabled()) {
        std::fprintf(
            stderr,
            "[wr64-mqsrc] source=%s event=%s tid=%d mq=0x%08X msg=0x%08X requeue=%d\n",
            source,
            jam ? "external_jam" : "external_send",
            wr64_current_thread_id(),
            static_cast<uint32_t>(mq),
            static_cast<uint32_t>(msg),
            requeue_if_blocked ? 1 : 0);
    }
    wr64_trace_mq(jam ? "external_jam" : "external_send", mq, msg, nullptr);
    external_messages.enqueue({mq, msg, jam, requeue_if_blocked});
}

bool do_send(RDRAM_ARG PTR(OSMesgQueue) mq_, OSMesg msg, bool jam, bool block);

void dequeue_external_messages(RDRAM_ARG1) {
    QueuedMessage to_send;
    std::vector<QueuedMessage> requeued_messages{};
    while (external_messages.try_dequeue(to_send)) {
        if (!do_send(PASS_RDRAM to_send.mq, to_send.mesg, to_send.jam, false) && to_send.requeue_if_blocked) {
            requeued_messages.push_back(to_send);
        }
    }
    for (QueuedMessage& cur_mesg : requeued_messages) {
        external_messages.enqueue(cur_mesg);
    }
}

void ultramodern::wait_for_external_message(RDRAM_ARG1) {
    QueuedMessage to_send;
    external_messages.wait_dequeue(to_send);
    if (!do_send(PASS_RDRAM to_send.mq, to_send.mesg, to_send.jam, false) && to_send.requeue_if_blocked) {
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
        external_messages.enqueue(to_send);
    }
}

void ultramodern::wait_for_external_message_timed(RDRAM_ARG u32 millis) {
    QueuedMessage to_send;
    if (external_messages.wait_dequeue_timed(to_send, std::chrono::milliseconds{millis})) {
        if (!do_send(PASS_RDRAM to_send.mq, to_send.mesg, to_send.jam, false) && to_send.requeue_if_blocked) {
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
            external_messages.enqueue(to_send);
        }
    }
}

extern "C" void osCreateMesgQueue(RDRAM_ARG PTR(OSMesgQueue) mq_, PTR(OSMesg) msg, s32 count) {
    OSMesgQueue *mq = TO_PTR(OSMesgQueue, mq_);
    mq->blocked_on_recv = NULLPTR;
    mq->blocked_on_send = NULLPTR;
    mq->msgCount = count;
    mq->msg = msg;
    mq->validCount = 0;
    mq->first = 0;
    wr64_trace_mq_guest(PASS_RDRAM "create", mq_, 0, mq);
}

s32 MQ_GET_COUNT(OSMesgQueue *mq) {
    return mq->validCount;
}

s32 MQ_IS_EMPTY(OSMesgQueue *mq) {
    return mq->validCount == 0;
}

s32 MQ_IS_FULL(OSMesgQueue* mq) {
    return MQ_GET_COUNT(mq) >= mq->msgCount;
}

bool do_send(RDRAM_ARG PTR(OSMesgQueue) mq_, OSMesg msg, bool jam, bool block) {
    OSMesgQueue* mq = TO_PTR(OSMesgQueue, mq_);
    wr64_trace_mq_guest(PASS_RDRAM jam ? "jam_try" : "send_try", mq_, msg, mq);
    if (!block) {
        // If non-blocking, fail if the queue is full.
        if (MQ_IS_FULL(mq)) {
            wr64_trace_mq_guest(PASS_RDRAM jam ? "jam_full" : "send_full", mq_, msg, mq);
            return false;
        }
    }
    else {
        // Otherwise, yield this thread until the queue has room.
        while (MQ_IS_FULL(mq)) {
            debug_printf("[Message Queue] Thread %d is blocked on send\n", TO_PTR(OSThread, ultramodern::this_thread())->id);
            wr64_trace_mq_guest(PASS_RDRAM jam ? "jam_block" : "send_block", mq_, msg, mq);
            ultramodern::thread_queue_insert(PASS_RDRAM GET_MEMBER(OSMesgQueue, mq_, blocked_on_send), ultramodern::this_thread());
            ultramodern::run_next_thread_and_wait(PASS_RDRAM1);
        }
    }
    
    if (jam) {
        // Jams insert at the head of the message queue's buffer.
        mq->first = (mq->first + mq->msgCount - 1) % mq->msgCount;
        TO_PTR(OSMesg, mq->msg)[mq->first] = msg;
        mq->validCount++;
    }
    else {
        // Sends insert at the tail of the message queue's buffer.
        s32 last = (mq->first + mq->validCount) % mq->msgCount;
        TO_PTR(OSMesg, mq->msg)[last] = msg;
        mq->validCount++;
    }

    // If any threads were blocked on receiving from this message queue, pop the first one and schedule it.
    PTR(PTR(OSThread)) blocked_queue = GET_MEMBER(OSMesgQueue, mq_, blocked_on_recv);
    if (!ultramodern::thread_queue_empty(PASS_RDRAM blocked_queue)) {
        ultramodern::schedule_running_thread(PASS_RDRAM ultramodern::thread_queue_pop(PASS_RDRAM blocked_queue));
    }
    wr64_trace_mq_guest(PASS_RDRAM jam ? "jam_ok" : "send_ok", mq_, msg, mq);
    
    return true;
}

bool do_recv(RDRAM_ARG PTR(OSMesgQueue) mq_, PTR(OSMesg) msg_, bool block) {
    OSMesgQueue* mq = TO_PTR(OSMesgQueue, mq_);
    wr64_trace_mq_guest(PASS_RDRAM "recv_try", mq_, 0, mq);
    if (!block) {
        // If non-blocking, fail if the queue is empty
        if (MQ_IS_EMPTY(mq)) {
            wr64_trace_mq_guest(PASS_RDRAM "recv_empty", mq_, 0, mq);
            return false;
        }
    } else {
        // Otherwise, yield this thread in a loop until the queue is no longer full
        while (MQ_IS_EMPTY(mq)) {
            debug_printf("[Message Queue] Thread %d is blocked on receive\n", TO_PTR(OSThread, ultramodern::this_thread())->id);
            wr64_trace_mq_guest(PASS_RDRAM "recv_block", mq_, 0, mq);
            ultramodern::thread_queue_insert(PASS_RDRAM GET_MEMBER(OSMesgQueue, mq_, blocked_on_recv), ultramodern::this_thread());
            ultramodern::run_next_thread_and_wait(PASS_RDRAM1);
        }
    }

    OSMesg received_msg = 0;
    if (msg_ != NULLPTR) {
        received_msg = TO_PTR(OSMesg, mq->msg)[mq->first];
        *TO_PTR(OSMesg, msg_) = received_msg;
    } else {
        received_msg = TO_PTR(OSMesg, mq->msg)[mq->first];
    }
    
    mq->first = (mq->first + 1) % mq->msgCount;
    mq->validCount--;

    // If any threads were blocked on sending to this message queue, pop the first one and schedule it.
    PTR(PTR(OSThread)) blocked_queue = GET_MEMBER(OSMesgQueue, mq_, blocked_on_send);
    if (!ultramodern::thread_queue_empty(PASS_RDRAM blocked_queue)) {
        ultramodern::schedule_running_thread(PASS_RDRAM ultramodern::thread_queue_pop(PASS_RDRAM blocked_queue));
    }
    wr64_trace_mq_guest(PASS_RDRAM "recv_ok", mq_, received_msg, mq);

    return true;
}

extern "C" s32 osSendMesg(RDRAM_ARG PTR(OSMesgQueue) mq_, OSMesg msg, s32 flags) {
    OSMesgQueue *mq = TO_PTR(OSMesgQueue, mq_);
    bool jam = false;
    
    // Don't directly send to the message queue if this isn't a game thread to avoid contention.
    if (!ultramodern::is_game_thread()) {
        ultramodern::enqueue_external_message(mq_, msg, jam, false, "nongame-osSendMesg");
        return 0;
    }
    
    // Handle any messages that have been received from an external thread.
    dequeue_external_messages(PASS_RDRAM1);

    // Try to send the message.
    bool sent = do_send(PASS_RDRAM mq_, msg, jam, flags == OS_MESG_BLOCK);
    
    // Check the queue to see if this thread should swap execution to another.
    ultramodern::check_running_queue(PASS_RDRAM1);

    return sent ? 0 : -1;
}

extern "C" s32 osJamMesg(RDRAM_ARG PTR(OSMesgQueue) mq_, OSMesg msg, s32 flags) {
    OSMesgQueue *mq = TO_PTR(OSMesgQueue, mq_);
    bool jam = true;
    
    // Don't directly send to the message queue if this isn't a game thread to avoid contention.
    if (!ultramodern::is_game_thread()) {
        ultramodern::enqueue_external_message(mq_, msg, jam, false, "nongame-osJamMesg");
        return 0;
    }
    
    // Handle any messages that have been received from an external thread.
    dequeue_external_messages(PASS_RDRAM1);

    // Try to send the message.
    bool sent = do_send(PASS_RDRAM mq_, msg, jam, flags == OS_MESG_BLOCK);
    
    // Check the queue to see if this thread should swap execution to another.
    ultramodern::check_running_queue(PASS_RDRAM1);

    return sent ? 0 : -1;
}

extern "C" s32 osRecvMesg(RDRAM_ARG PTR(OSMesgQueue) mq_, PTR(OSMesg) msg_, s32 flags) {
    OSMesgQueue *mq = TO_PTR(OSMesgQueue, mq_);
    
    assert(ultramodern::is_game_thread() && "RecvMesg not allowed outside of game threads.");
    
    // Handle any messages that have been received from an external thread.
    dequeue_external_messages(PASS_RDRAM1);

    // Try to receive a message.
    bool received = do_recv(PASS_RDRAM mq_, msg_, flags == OS_MESG_BLOCK);
    
    // Check the queue to see if this thread should swap execution to another.
    ultramodern::check_running_queue(PASS_RDRAM1);

    return received ? 0 : -1;
}
