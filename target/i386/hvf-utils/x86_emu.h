/*
 * Copyright (C) 2016 Veertu Inc,
 * Copyright (C) 2017 Google Inc,
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */
#ifndef __X86_EMU_H__
#define __X86_EMU_H__

#include "x86.h"
#include "x86_decode.h"

void init_emu(struct CPUState *cpu);
bool exec_instruction(struct CPUState *cpu, struct x86_decode *ins);

void load_regs(struct CPUState *cpu);
void store_regs(struct CPUState *cpu);

void simulate_rdmsr(struct CPUState *cpu);
void simulate_wrmsr(struct CPUState *cpu);

#endif
