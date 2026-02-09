#include <mutex>
#include <vector>
#include <condition_variable>
#include <chrono>

#include "ultramodern/extensions.h"
#include "ultramodern/ultramodern.hpp"

struct DLEvent {
    PTR(OSMesgQueue) mq;
    OSMesg mesg;
    PTR(void) displaylist;
    u32 event_type;
};

static struct {
    struct {
        std::mutex dl_event_mutex;
        std::vector<DLEvent> pending_events;
    } dl_events;
} extension_state;

static std::mutex dl_wait_mutex;
static std::condition_variable dl_wait_cv;
static PTR(void) last_parsed_displaylist = 0;

// Declared/defined in ultramodern/src/mesgqueue.cpp.
void enqueue_external_message(PTR(OSMesgQueue) mq, OSMesg msg, bool jam);

extern "C" void osExQueueDisplaylistEvent(PTR(OSMesgQueue) mq, OSMesg mesg, PTR(void) displaylist, u32 event_type) {
    std::lock_guard lock{ extension_state.dl_events.dl_event_mutex };

    assert(
        event_type == OS_EX_DISPLAYLIST_EVENT_SUBMITTED ||
        event_type == OS_EX_DISPLAYLIST_EVENT_PARSED ||
        event_type == OS_EX_DISPLAYLIST_EVENT_COMPLETED);

    extension_state.dl_events.pending_events.emplace_back(DLEvent{ mq, mesg, displaylist, event_type });
}

static void dispatch_displaylist_events(PTR(void) displaylist, u32 event_type) {
    std::lock_guard lock{ extension_state.dl_events.dl_event_mutex };

    // Check every pending DL event to see if they match this displaylist and event type.
    for (auto iter = extension_state.dl_events.pending_events.begin(); iter != extension_state.dl_events.pending_events.end();) {
        if (iter->displaylist == displaylist && iter->event_type == event_type) {
            // Send the provided message to the corresponding message queue for this event, then remove this event from the queue.
            enqueue_external_message(iter->mq, iter->mesg, false);
            iter = extension_state.dl_events.pending_events.erase(iter);
        } else {
            ++iter;
        }
    }
}

void ultramodern::extensions::on_displaylist_submitted(PTR(void) displaylist) {
    dispatch_displaylist_events(displaylist, OS_EX_DISPLAYLIST_EVENT_SUBMITTED);
}

void ultramodern::extensions::on_displaylist_parsed(PTR(void) displaylist) {
    dispatch_displaylist_events(displaylist, OS_EX_DISPLAYLIST_EVENT_PARSED);

    {
        std::lock_guard lock{ dl_wait_mutex };
        last_parsed_displaylist = displaylist;
    }
    dl_wait_cv.notify_all();
}

void ultramodern::extensions::on_displaylist_completed(PTR(void) displaylist) {
    dispatch_displaylist_events(displaylist, OS_EX_DISPLAYLIST_EVENT_COMPLETED);
}

bool ultramodern::extensions::wait_for_displaylist_parsed(PTR(void) displaylist, uint32_t timeout_ms) {
    std::unique_lock lock{ dl_wait_mutex };
    return dl_wait_cv.wait_for(lock, std::chrono::milliseconds(timeout_ms), [&]() {
        return last_parsed_displaylist == displaylist;
    });
}
