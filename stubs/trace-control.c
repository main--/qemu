/*
 * Interface for configuring and controlling the state of tracing events.
 *
 * Copyright (C) 2014-2016 Lluís Vilanova <vilanova@ac.upc.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "trace/control.h"


void trace_event_set_state_dynamic_init(TraceEvent *ev, bool state)
{
    trace_event_set_state_dynamic(ev, state);
}

void trace_event_set_state_dynamic(TraceEvent *ev, bool state)
{
    TraceEventID id;
    bool state_pre;
    assert(trace_event_get_state_static(ev));
    id = trace_event_get_id(ev);
    /*
     * We ignore the "vcpu" property here, since there's no target code. Then
     * dstate can only be 1 or 0.
     */
    state_pre = trace_events_dstate[id];
    if (state_pre != state) {
        if (state) {
            trace_events_enabled_count++;
            trace_events_dstate[id] = 1;
        } else {
            trace_events_enabled_count--;
            trace_events_dstate[id] = 0;
        }
    }
}

void trace_event_set_vcpu_state_dynamic(CPUState *vcpu,
                                        TraceEvent *ev, bool state)
{
    /* should never be called on non-target binaries */
    abort();
}
