/*
 * String parsing Visitor
 *
 * Copyright Red Hat, Inc. 2012
 *
 * Author: Paolo Bonzini <pbonzini@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#ifndef STRING_INPUT_VISITOR_H
#define STRING_INPUT_VISITOR_H

#include "qapi/visitor.h"

typedef struct StringInputVisitor StringInputVisitor;

/*
 * The string input visitor does not implement support for visiting
 * QAPI structs, null, or arbitrary QTypes.  It also
 * requires a non-null list argument to visit_start_list().
 *
 * Support for alternates is very limited: only bool and enum
 * members are supported, and only when the enum members'
 * representations can't be confused with a bool value.
 */
Visitor *string_input_visitor_new(const char *str);

#endif
