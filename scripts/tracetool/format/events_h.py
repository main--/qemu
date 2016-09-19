#!/usr/bin/env python
# -*- coding: utf-8 -*-

"""
trace/generated-events.h
"""

__author__     = "Lluís Vilanova <vilanova@ac.upc.edu>"
__copyright__  = "Copyright 2012-2016, Lluís Vilanova <vilanova@ac.upc.edu>"
__license__    = "GPL version 2 or (at your option) any later version"

__maintainer__ = "Stefan Hajnoczi"
__email__      = "stefanha@linux.vnet.ibm.com"


from tracetool import out


def generate(events, backend):
    out('/* This file is autogenerated by tracetool, do not edit. */',
        '',
        '#ifndef TRACE__GENERATED_EVENTS_H',
        '#define TRACE__GENERATED_EVENTS_H',
        '',
        '#include "trace/event-internal.h"',
        )

    for e in events:
        out('extern TraceEvent TRACE_%s_EV;' % e.name.upper())

    for e in events:
        out('extern uint16_t %s;' % e.api(e.QEMU_DSTATE))

    numvcpu = 0
    for e in events:
        if "vcpu" in e.properties:
            numvcpu += 1

    out("#define TRACE_VCPU_EVENT_COUNT %d" % numvcpu)

    # static state
    for e in events:
        if 'disable' in e.properties:
            enabled = 0
        else:
            enabled = 1
        if "tcg-exec" in e.properties:
            # a single define for the two "sub-events"
            out('#define TRACE_%(name)s_ENABLED %(enabled)d',
                name=e.original.name.upper(),
                enabled=enabled)
        out('#define TRACE_%s_ENABLED %d' % (e.name.upper(), enabled))

    out('',
        '#endif  /* TRACE__GENERATED_EVENTS_H */')
