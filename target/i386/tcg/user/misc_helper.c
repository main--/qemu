/*
 *  x86 misc helpers
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/helper-proto.h"

void helper_outb(CPUX86State *env, uint32_t port, uint32_t data)
{
    fprintf(stderr, "outb: port=0x%04x, data=%02x\n", port, data);
}

target_ulong helper_inb(CPUX86State *env, uint32_t port)
{
    fprintf(stderr, "inb: port=0x%04x\n", port);
    return 0;
}

void helper_outw(CPUX86State *env, uint32_t port, uint32_t data)
{
    fprintf(stderr, "outw: port=0x%04x, data=%04x\n", port, data);
}

target_ulong helper_inw(CPUX86State *env, uint32_t port)
{
    fprintf(stderr, "inw: port=0x%04x\n", port);
    return 0;
}

void helper_outl(CPUX86State *env, uint32_t port, uint32_t data)
{
    fprintf(stderr, "outl: port=0x%04x, data=%08x\n", port, data);
}

target_ulong helper_inl(CPUX86State *env, uint32_t port)
{
    fprintf(stderr, "inl: port=0x%04x\n", port);
    return 0;
}

target_ulong helper_read_crN(CPUX86State *env, int reg)
{
    return 0;
}

void helper_write_crN(CPUX86State *env, int reg, target_ulong t0)
{
}

void helper_wrmsr(CPUX86State *env)
{
}

void helper_rdmsr(CPUX86State *env)
{
}
