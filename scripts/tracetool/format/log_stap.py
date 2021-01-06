# -*- coding: utf-8 -*-

"""
Generate .stp file that printfs log messages (DTrace with SystemTAP only).
"""

__author__     = "Daniel P. Berrange <berrange@redhat.com>"
__copyright__  = "Copyright (C) 2014-2019, Red Hat, Inc."
__license__    = "GPL version 2 or (at your option) any later version"

__maintainer__ = "Daniel Berrange"
__email__      = "berrange@redhat.com"

import re

from tracetool import out
from tracetool.backend.dtrace import binary, probeprefix
from tracetool.backend.simple import is_string
from tracetool.format.stap import stap_escape

def global_var_name(name):
    return probeprefix().replace(".", "_") + "_" + name

STATE_SKIP = 0
STATE_LITERAL = 1
STATE_MACRO = 2

def c_macro_to_format(macro):
    if macro.startswith("PRI"):
        return macro[3]

    raise Exception("Unhandled macro '%s'" % macro)

def c_fmt_to_stap(fmt):
    state = 0
    bits = []
    literal = ""
    macro = ""
    escape = 0;
    for i in range(len(fmt)):
        if fmt[i] == '\\':
            if escape:
                escape = 0
            else:
                escape = 1
            if state != STATE_LITERAL:
                raise Exception("Unexpected escape outside string literal")
            literal = literal + fmt[i]
        elif fmt[i] == '"' and not escape:
            if state == STATE_LITERAL:
                state = STATE_SKIP
                bits.append(literal)
                literal = ""
            else:
                if state == STATE_MACRO:
                    bits.append(c_macro_to_format(macro))
                state = STATE_LITERAL
        elif fmt[i] == ' ' or fmt[i] == '\t':
            if state == STATE_MACRO:
                bits.append(c_macro_to_format(macro))
                macro = ""
                state = STATE_SKIP
            elif state == STATE_LITERAL:
                literal = literal + fmt[i]
        else:
            escape = 0
            if state == STATE_SKIP:
                state = STATE_MACRO

            if state == STATE_LITERAL:
                literal = literal + fmt[i]
            else:
                macro = macro + fmt[i]

    if state == STATE_MACRO:
        bits.append(c_macro_to_format(macro))
    elif state == STATE_LITERAL:
        bits.append(literal)

    # All variables in systemtap are 64-bit in size
    # The "%l" integer size qualifier is thus redundant
    # and "%ll" is not valid at all. Simiarly the size_t
    # based "%z" size qualifier is not valid. We just
    # strip all size qualifiers for sanity.
    fmt = re.sub("%(\d*)(l+|z)(x|u|d)", "%\\1\\3", "".join(bits))
    return fmt

def generate(events, backend, group):
    out('/* This file is autogenerated by tracetool, do not edit. */',
        '')

    for event_id, e in enumerate(events):
        if 'disable' in e.properties:
            continue

        out('probe %(probeprefix)s.log.%(name)s = %(probeprefix)s.%(name)s ?',
            '{',
            probeprefix=probeprefix(),
            name=e.name)

        # Get references to userspace strings
        for type_, name in e.args:
            name = stap_escape(name)
            if is_string(type_):
                out('    try {',
                    '        arg%(name)s_str = %(name)s ? ' +
                    'user_string_n(%(name)s, 512) : "<null>"',
                    '    } catch {}',
                    name=name)

        # Determine systemtap's view of variable names
        fields = ["pid()", "gettimeofday_ns()"]
        for type_, name in e.args:
            name = stap_escape(name)
            if is_string(type_):
                fields.append("arg" + name + "_str")
            else:
                fields.append(name)

        # Emit the entire record in a single SystemTap printf()
        arg_str = ', '.join(arg for arg in fields)
        fmt_str = "%d@%d " + e.name + " " + c_fmt_to_stap(e.fmt) + "\\n"
        out('    printf("%(fmt_str)s", %(arg_str)s)',
            fmt_str=fmt_str, arg_str=arg_str)

        out('}')

    out()
