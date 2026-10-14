#include "rtt_events.h"

void ev_init(EventBus* bus) { memset(bus, 0, sizeof(*bus)); }

void ev_subscribe(EventBus* bus, EventId id, EventFn fn, void* user) {
    if (bus->sub_count >= RTT_MAX_SUBSCRIBERS) return;
    bus->subs[bus->sub_count].id = id;
    bus->subs[bus->sub_count].fn = fn;
    bus->subs[bus->sub_count].user = user;
    bus->sub_count++;
}

void ev_send(EventBus* bus, const Event* e) {
    if (bus->count >= RTT_MAX_EVENTS) return;          /* full: drop, like RTT's fixed queues */
    bus->queue[(bus->head + bus->count) % RTT_MAX_EVENTS] = *e;
    bus->count++;
}

void ev_post(EventBus* bus, EventId id, int side) {
    Event e;
    memset(&e, 0, sizeof(e));
    e.id = id;
    e.side = side;
    ev_send(bus, &e);
}

void ev_dispatch(EventBus* bus) {
    while (bus->count > 0) {
        Event e = bus->queue[bus->head];
        bus->head = (bus->head + 1) % RTT_MAX_EVENTS;
        bus->count--;
        for (int i = 0; i < bus->sub_count; i++)
            if (bus->subs[i].id == e.id || bus->subs[i].id == EV_COUNT_SENTINEL)
                bus->subs[i].fn(bus->subs[i].user, &e);
    }
}
