/*
 * RISC-V emulation for qemu: main translation routines.
 *
 * Copyright (c) 2016-2017 Sagar Karandikar, sagark@eecs.berkeley.edu
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "cpu.h"
#include "tcg-op.h"
#include "disas/disas.h"
#include "exec/cpu_ldst.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"
#include "exec/helper-gen.h"

#include "exec/translator.h"
#include "exec/log.h"

#include "instmap.h"

/* global register indices */
static TCGv cpu_gpr[32], cpu_pc;
static TCGv_i64 cpu_fpr[32]; /* assume F and D extensions */
static TCGv load_res;
static TCGv load_val;

#include "exec/gen-icount.h"

typedef struct DisasContext {
    DisasContextBase base;
    /* pc_succ_insn points to the instruction following base.pc_next */
    target_ulong pc_succ_insn;
    uint32_t opcode;
    uint32_t flags;
    uint32_t mem_idx;
    /* Remember the rounding mode encoded in the previous fp instruction,
       which we have already installed into env->fp_status.  Or -1 for
       no previous fp instruction.  Note that we exit the TB when writing
       to any system register, which includes CSR_FRM, so we do not have
       to reset this known value.  */
    int frm;
    CPURISCVState *env;
} DisasContext;

/* convert riscv funct3 to qemu memop for load/store */
static const int tcg_memop_lookup[8] = {
    [0 ... 7] = -1,
    [0] = MO_SB,
    [1] = MO_TESW,
    [2] = MO_TESL,
    [4] = MO_UB,
    [5] = MO_TEUW,
#ifdef TARGET_RISCV64
    [3] = MO_TEQ,
    [6] = MO_TEUL,
#endif
};

#ifdef TARGET_RISCV64
#define CASE_OP_32_64(X) case X: case glue(X, W)
#else
#define CASE_OP_32_64(X) case X
#endif

static void generate_exception(DisasContext *ctx, int excp)
{
    tcg_gen_movi_tl(cpu_pc, ctx->base.pc_next);
    TCGv_i32 helper_tmp = tcg_const_i32(excp);
    gen_helper_raise_exception(cpu_env, helper_tmp);
    tcg_temp_free_i32(helper_tmp);
    ctx->base.is_jmp = DISAS_NORETURN;
}

static void generate_exception_mbadaddr(DisasContext *ctx, int excp)
{
    tcg_gen_movi_tl(cpu_pc, ctx->base.pc_next);
    tcg_gen_st_tl(cpu_pc, cpu_env, offsetof(CPURISCVState, badaddr));
    TCGv_i32 helper_tmp = tcg_const_i32(excp);
    gen_helper_raise_exception(cpu_env, helper_tmp);
    tcg_temp_free_i32(helper_tmp);
    ctx->base.is_jmp = DISAS_NORETURN;
}

static void gen_exception_debug(void)
{
    TCGv_i32 helper_tmp = tcg_const_i32(EXCP_DEBUG);
    gen_helper_raise_exception(cpu_env, helper_tmp);
    tcg_temp_free_i32(helper_tmp);
}

static void gen_exception_illegal(DisasContext *ctx)
{
    generate_exception(ctx, RISCV_EXCP_ILLEGAL_INST);
}

static void gen_exception_inst_addr_mis(DisasContext *ctx)
{
    generate_exception_mbadaddr(ctx, RISCV_EXCP_INST_ADDR_MIS);
}

static inline bool use_goto_tb(DisasContext *ctx, target_ulong dest)
{
    if (unlikely(ctx->base.singlestep_enabled)) {
        return false;
    }

#ifndef CONFIG_USER_ONLY
    return (ctx->base.tb->pc & TARGET_PAGE_MASK) == (dest & TARGET_PAGE_MASK);
#else
    return true;
#endif
}

static void gen_goto_tb(DisasContext *ctx, int n, target_ulong dest)
{
    if (use_goto_tb(ctx, dest)) {
        /* chaining is only allowed when the jump is to the same page */
        tcg_gen_goto_tb(n);
        tcg_gen_movi_tl(cpu_pc, dest);
        tcg_gen_exit_tb(ctx->base.tb, n);
    } else {
        tcg_gen_movi_tl(cpu_pc, dest);
        if (ctx->base.singlestep_enabled) {
            gen_exception_debug();
        } else {
            tcg_gen_lookup_and_goto_ptr();
        }
    }
}

/* Wrapper for getting reg values - need to check of reg is zero since
 * cpu_gpr[0] is not actually allocated
 */
static inline void gen_get_gpr(TCGv t, int reg_num)
{
    if (reg_num == 0) {
        tcg_gen_movi_tl(t, 0);
    } else {
        tcg_gen_mov_tl(t, cpu_gpr[reg_num]);
    }
}

/* Wrapper for setting reg values - need to check of reg is zero since
 * cpu_gpr[0] is not actually allocated. this is more for safety purposes,
 * since we usually avoid calling the OP_TYPE_gen function if we see a write to
 * $zero
 */
static inline void gen_set_gpr(int reg_num_dst, TCGv t)
{
    if (reg_num_dst != 0) {
        tcg_gen_mov_tl(cpu_gpr[reg_num_dst], t);
    }
}

static void gen_mulhsu(TCGv ret, TCGv arg1, TCGv arg2)
{
    TCGv rl = tcg_temp_new();
    TCGv rh = tcg_temp_new();

    tcg_gen_mulu2_tl(rl, rh, arg1, arg2);
    /* fix up for one negative */
    tcg_gen_sari_tl(rl, arg1, TARGET_LONG_BITS - 1);
    tcg_gen_and_tl(rl, rl, arg2);
    tcg_gen_sub_tl(ret, rh, rl);

    tcg_temp_free(rl);
    tcg_temp_free(rh);
}

static void gen_arith(DisasContext *ctx, uint32_t opc, int rd, int rs1,
        int rs2)
{
    TCGv source1, source2, cond1, cond2, zeroreg, resultopt1;
    source1 = tcg_temp_new();
    source2 = tcg_temp_new();
    gen_get_gpr(source1, rs1);
    gen_get_gpr(source2, rs2);

    switch (opc) {
    CASE_OP_32_64(OPC_RISC_ADD):
        tcg_gen_add_tl(source1, source1, source2);
        break;
    CASE_OP_32_64(OPC_RISC_SUB):
        tcg_gen_sub_tl(source1, source1, source2);
        break;
#if defined(TARGET_RISCV64)
    case OPC_RISC_SLLW:
        tcg_gen_andi_tl(source2, source2, 0x1F);
        tcg_gen_shl_tl(source1, source1, source2);
        break;
#endif
    case OPC_RISC_SLL:
        tcg_gen_andi_tl(source2, source2, TARGET_LONG_BITS - 1);
        tcg_gen_shl_tl(source1, source1, source2);
        break;
    case OPC_RISC_SLT:
        tcg_gen_setcond_tl(TCG_COND_LT, source1, source1, source2);
        break;
    case OPC_RISC_SLTU:
        tcg_gen_setcond_tl(TCG_COND_LTU, source1, source1, source2);
        break;
    case OPC_RISC_XOR:
        tcg_gen_xor_tl(source1, source1, source2);
        break;
#if defined(TARGET_RISCV64)
    case OPC_RISC_SRLW:
        /* clear upper 32 */
        tcg_gen_ext32u_tl(source1, source1);
        tcg_gen_andi_tl(source2, source2, 0x1F);
        tcg_gen_shr_tl(source1, source1, source2);
        break;
#endif
    case OPC_RISC_SRL:
        tcg_gen_andi_tl(source2, source2, TARGET_LONG_BITS - 1);
        tcg_gen_shr_tl(source1, source1, source2);
        break;
#if defined(TARGET_RISCV64)
    case OPC_RISC_SRAW:
        /* first, trick to get it to act like working on 32 bits (get rid of
        upper 32, sign extend to fill space) */
        tcg_gen_ext32s_tl(source1, source1);
        tcg_gen_andi_tl(source2, source2, 0x1F);
        tcg_gen_sar_tl(source1, source1, source2);
        break;
#endif
    case OPC_RISC_SRA:
        tcg_gen_andi_tl(source2, source2, TARGET_LONG_BITS - 1);
        tcg_gen_sar_tl(source1, source1, source2);
        break;
    case OPC_RISC_OR:
        tcg_gen_or_tl(source1, source1, source2);
        break;
    case OPC_RISC_AND:
        tcg_gen_and_tl(source1, source1, source2);
        break;
    CASE_OP_32_64(OPC_RISC_MUL):
        tcg_gen_mul_tl(source1, source1, source2);
        break;
    case OPC_RISC_MULH:
        tcg_gen_muls2_tl(source2, source1, source1, source2);
        break;
    case OPC_RISC_MULHSU:
        gen_mulhsu(source1, source1, source2);
        break;
    case OPC_RISC_MULHU:
        tcg_gen_mulu2_tl(source2, source1, source1, source2);
        break;
#if defined(TARGET_RISCV64)
    case OPC_RISC_DIVW:
        tcg_gen_ext32s_tl(source1, source1);
        tcg_gen_ext32s_tl(source2, source2);
        /* fall through to DIV */
#endif
    case OPC_RISC_DIV:
        /* Handle by altering args to tcg_gen_div to produce req'd results:
         * For overflow: want source1 in source1 and 1 in source2
         * For div by zero: want -1 in source1 and 1 in source2 -> -1 result */
        cond1 = tcg_temp_new();
        cond2 = tcg_temp_new();
        zeroreg = tcg_const_tl(0);
        resultopt1 = tcg_temp_new();

        tcg_gen_movi_tl(resultopt1, (target_ulong)-1);
        tcg_gen_setcondi_tl(TCG_COND_EQ, cond2, source2, (target_ulong)(~0L));
        tcg_gen_setcondi_tl(TCG_COND_EQ, cond1, source1,
                            ((target_ulong)1) << (TARGET_LONG_BITS - 1));
        tcg_gen_and_tl(cond1, cond1, cond2); /* cond1 = overflow */
        tcg_gen_setcondi_tl(TCG_COND_EQ, cond2, source2, 0); /* cond2 = div 0 */
        /* if div by zero, set source1 to -1, otherwise don't change */
        tcg_gen_movcond_tl(TCG_COND_EQ, source1, cond2, zeroreg, source1,
                resultopt1);
        /* if overflow or div by zero, set source2 to 1, else don't change */
        tcg_gen_or_tl(cond1, cond1, cond2);
        tcg_gen_movi_tl(resultopt1, (target_ulong)1);
        tcg_gen_movcond_tl(TCG_COND_EQ, source2, cond1, zeroreg, source2,
                resultopt1);
        tcg_gen_div_tl(source1, source1, source2);

        tcg_temp_free(cond1);
        tcg_temp_free(cond2);
        tcg_temp_free(zeroreg);
        tcg_temp_free(resultopt1);
        break;
#if defined(TARGET_RISCV64)
    case OPC_RISC_DIVUW:
        tcg_gen_ext32u_tl(source1, source1);
        tcg_gen_ext32u_tl(source2, source2);
        /* fall through to DIVU */
#endif
    case OPC_RISC_DIVU:
        cond1 = tcg_temp_new();
        zeroreg = tcg_const_tl(0);
        resultopt1 = tcg_temp_new();

        tcg_gen_setcondi_tl(TCG_COND_EQ, cond1, source2, 0);
        tcg_gen_movi_tl(resultopt1, (target_ulong)-1);
        tcg_gen_movcond_tl(TCG_COND_EQ, source1, cond1, zeroreg, source1,
                resultopt1);
        tcg_gen_movi_tl(resultopt1, (target_ulong)1);
        tcg_gen_movcond_tl(TCG_COND_EQ, source2, cond1, zeroreg, source2,
                resultopt1);
        tcg_gen_divu_tl(source1, source1, source2);

        tcg_temp_free(cond1);
        tcg_temp_free(zeroreg);
        tcg_temp_free(resultopt1);
        break;
#if defined(TARGET_RISCV64)
    case OPC_RISC_REMW:
        tcg_gen_ext32s_tl(source1, source1);
        tcg_gen_ext32s_tl(source2, source2);
        /* fall through to REM */
#endif
    case OPC_RISC_REM:
        cond1 = tcg_temp_new();
        cond2 = tcg_temp_new();
        zeroreg = tcg_const_tl(0);
        resultopt1 = tcg_temp_new();

        tcg_gen_movi_tl(resultopt1, 1L);
        tcg_gen_setcondi_tl(TCG_COND_EQ, cond2, source2, (target_ulong)-1);
        tcg_gen_setcondi_tl(TCG_COND_EQ, cond1, source1,
                            (target_ulong)1 << (TARGET_LONG_BITS - 1));
        tcg_gen_and_tl(cond2, cond1, cond2); /* cond1 = overflow */
        tcg_gen_setcondi_tl(TCG_COND_EQ, cond1, source2, 0); /* cond2 = div 0 */
        /* if overflow or div by zero, set source2 to 1, else don't change */
        tcg_gen_or_tl(cond2, cond1, cond2);
        tcg_gen_movcond_tl(TCG_COND_EQ, source2, cond2, zeroreg, source2,
                resultopt1);
        tcg_gen_rem_tl(resultopt1, source1, source2);
        /* if div by zero, just return the original dividend */
        tcg_gen_movcond_tl(TCG_COND_EQ, source1, cond1, zeroreg, resultopt1,
                source1);

        tcg_temp_free(cond1);
        tcg_temp_free(cond2);
        tcg_temp_free(zeroreg);
        tcg_temp_free(resultopt1);
        break;
#if defined(TARGET_RISCV64)
    case OPC_RISC_REMUW:
        tcg_gen_ext32u_tl(source1, source1);
        tcg_gen_ext32u_tl(source2, source2);
        /* fall through to REMU */
#endif
    case OPC_RISC_REMU:
        cond1 = tcg_temp_new();
        zeroreg = tcg_const_tl(0);
        resultopt1 = tcg_temp_new();

        tcg_gen_movi_tl(resultopt1, (target_ulong)1);
        tcg_gen_setcondi_tl(TCG_COND_EQ, cond1, source2, 0);
        tcg_gen_movcond_tl(TCG_COND_EQ, source2, cond1, zeroreg, source2,
                resultopt1);
        tcg_gen_remu_tl(resultopt1, source1, source2);
        /* if div by zero, just return the original dividend */
        tcg_gen_movcond_tl(TCG_COND_EQ, source1, cond1, zeroreg, resultopt1,
                source1);

        tcg_temp_free(cond1);
        tcg_temp_free(zeroreg);
        tcg_temp_free(resultopt1);
        break;
    default:
        gen_exception_illegal(ctx);
        return;
    }

    if (opc & 0x8) { /* sign extend for W instructions */
        tcg_gen_ext32s_tl(source1, source1);
    }

    gen_set_gpr(rd, source1);
    tcg_temp_free(source1);
    tcg_temp_free(source2);
}

static void gen_arith_imm(DisasContext *ctx, uint32_t opc, int rd,
        int rs1, target_long imm)
{
    TCGv source1 = tcg_temp_new();
    int shift_len = TARGET_LONG_BITS;
    int shift_a;

    gen_get_gpr(source1, rs1);

    switch (opc) {
    case OPC_RISC_ADDI:
#if defined(TARGET_RISCV64)
    case OPC_RISC_ADDIW:
#endif
        tcg_gen_addi_tl(source1, source1, imm);
        break;
    case OPC_RISC_SLTI:
        tcg_gen_setcondi_tl(TCG_COND_LT, source1, source1, imm);
        break;
    case OPC_RISC_SLTIU:
        tcg_gen_setcondi_tl(TCG_COND_LTU, source1, source1, imm);
        break;
    case OPC_RISC_XORI:
        tcg_gen_xori_tl(source1, source1, imm);
        break;
    case OPC_RISC_ORI:
        tcg_gen_ori_tl(source1, source1, imm);
        break;
    case OPC_RISC_ANDI:
        tcg_gen_andi_tl(source1, source1, imm);
        break;
#if defined(TARGET_RISCV64)
    case OPC_RISC_SLLIW:
        shift_len = 32;
        /* FALLTHRU */
#endif
    case OPC_RISC_SLLI:
        if (imm >= shift_len) {
            goto do_illegal;
        }
        tcg_gen_shli_tl(source1, source1, imm);
        break;
#if defined(TARGET_RISCV64)
    case OPC_RISC_SHIFT_RIGHT_IW:
        shift_len = 32;
        /* FALLTHRU */
#endif
    case OPC_RISC_SHIFT_RIGHT_I:
        /* differentiate on IMM */
        shift_a = imm & 0x400;
        imm &= 0x3ff;
        if (imm >= shift_len) {
            goto do_illegal;
        }
        if (imm != 0) {
            if (shift_a) {
                /* SRAI[W] */
                tcg_gen_sextract_tl(source1, source1, imm, shift_len - imm);
            } else {
                /* SRLI[W] */
                tcg_gen_extract_tl(source1, source1, imm, shift_len - imm);
            }
            /* No further sign-extension needed for W instructions.  */
            opc &= ~0x8;
        }
        break;
    default:
    do_illegal:
        gen_exception_illegal(ctx);
        return;
    }

    if (opc & 0x8) { /* sign-extend for W instructions */
        tcg_gen_ext32s_tl(source1, source1);
    }

    gen_set_gpr(rd, source1);
    tcg_temp_free(source1);
}

static void gen_jal(CPURISCVState *env, DisasContext *ctx, int rd,
                    target_ulong imm)
{
    target_ulong next_pc;

    /* check misaligned: */
    next_pc = ctx->base.pc_next + imm;
    if (!riscv_has_ext(env, RVC)) {
        if ((next_pc & 0x3) != 0) {
            gen_exception_inst_addr_mis(ctx);
            return;
        }
    }
    if (rd != 0) {
        tcg_gen_movi_tl(cpu_gpr[rd], ctx->pc_succ_insn);
    }

    gen_goto_tb(ctx, 0, ctx->base.pc_next + imm); /* must use this for safety */
    ctx->base.is_jmp = DISAS_NORETURN;
}

static void gen_branch(CPURISCVState *env, DisasContext *ctx, uint32_t opc,
                       int rs1, int rs2, target_long bimm)
{
    TCGLabel *l = gen_new_label();
    TCGv source1, source2;
    source1 = tcg_temp_new();
    source2 = tcg_temp_new();
    gen_get_gpr(source1, rs1);
    gen_get_gpr(source2, rs2);

    switch (opc) {
    case OPC_RISC_BEQ:
        tcg_gen_brcond_tl(TCG_COND_EQ, source1, source2, l);
        break;
    case OPC_RISC_BNE:
        tcg_gen_brcond_tl(TCG_COND_NE, source1, source2, l);
        break;
    case OPC_RISC_BLT:
        tcg_gen_brcond_tl(TCG_COND_LT, source1, source2, l);
        break;
    case OPC_RISC_BGE:
        tcg_gen_brcond_tl(TCG_COND_GE, source1, source2, l);
        break;
    case OPC_RISC_BLTU:
        tcg_gen_brcond_tl(TCG_COND_LTU, source1, source2, l);
        break;
    case OPC_RISC_BGEU:
        tcg_gen_brcond_tl(TCG_COND_GEU, source1, source2, l);
        break;
    default:
        gen_exception_illegal(ctx);
        return;
    }
    tcg_temp_free(source1);
    tcg_temp_free(source2);

    gen_goto_tb(ctx, 1, ctx->pc_succ_insn);
    gen_set_label(l); /* branch taken */
    if (!riscv_has_ext(env, RVC) && ((ctx->base.pc_next + bimm) & 0x3)) {
        /* misaligned */
        gen_exception_inst_addr_mis(ctx);
    } else {
        gen_goto_tb(ctx, 0, ctx->base.pc_next + bimm);
    }
    ctx->base.is_jmp = DISAS_NORETURN;
}

static void gen_load(DisasContext *ctx, uint32_t opc, int rd, int rs1,
        target_long imm)
{
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    gen_get_gpr(t0, rs1);
    tcg_gen_addi_tl(t0, t0, imm);
    int memop = tcg_memop_lookup[(opc >> 12) & 0x7];

    if (memop < 0) {
        gen_exception_illegal(ctx);
        return;
    }

    tcg_gen_qemu_ld_tl(t1, t0, ctx->mem_idx, memop);
    gen_set_gpr(rd, t1);
    tcg_temp_free(t0);
    tcg_temp_free(t1);
}

static void gen_store(DisasContext *ctx, uint32_t opc, int rs1, int rs2,
        target_long imm)
{
    TCGv t0 = tcg_temp_new();
    TCGv dat = tcg_temp_new();
    gen_get_gpr(t0, rs1);
    tcg_gen_addi_tl(t0, t0, imm);
    gen_get_gpr(dat, rs2);
    int memop = tcg_memop_lookup[(opc >> 12) & 0x7];

    if (memop < 0) {
        gen_exception_illegal(ctx);
        return;
    }

    tcg_gen_qemu_st_tl(dat, t0, ctx->mem_idx, memop);
    tcg_temp_free(t0);
    tcg_temp_free(dat);
}

#ifdef TARGET_RISCV32
static void gen_fp_load(DisasContext *ctx, uint32_t opc, int rd,
        int rs1, target_long imm)
{
    TCGv t0;

    if (!(ctx->flags & TB_FLAGS_FP_ENABLE)) {
        gen_exception_illegal(ctx);
        return;
    }

    t0 = tcg_temp_new();
    gen_get_gpr(t0, rs1);
    tcg_gen_addi_tl(t0, t0, imm);

    switch (opc) {
    case OPC_RISC_FLW:
        tcg_gen_qemu_ld_i64(cpu_fpr[rd], t0, ctx->mem_idx, MO_TEUL);
        /* RISC-V requires NaN-boxing of narrower width floating point values */
        tcg_gen_ori_i64(cpu_fpr[rd], cpu_fpr[rd], 0xffffffff00000000ULL);
        break;
    case OPC_RISC_FLD:
        tcg_gen_qemu_ld_i64(cpu_fpr[rd], t0, ctx->mem_idx, MO_TEQ);
        break;
    default:
        gen_exception_illegal(ctx);
        break;
    }
    tcg_temp_free(t0);
}

static void gen_fp_store(DisasContext *ctx, uint32_t opc, int rs1,
        int rs2, target_long imm)
{
    TCGv t0;

    if (!(ctx->flags & TB_FLAGS_FP_ENABLE)) {
        gen_exception_illegal(ctx);
        return;
    }

    t0 = tcg_temp_new();
    gen_get_gpr(t0, rs1);
    tcg_gen_addi_tl(t0, t0, imm);

    switch (opc) {
    case OPC_RISC_FSW:
        tcg_gen_qemu_st_i64(cpu_fpr[rs2], t0, ctx->mem_idx, MO_TEUL);
        break;
    case OPC_RISC_FSD:
        tcg_gen_qemu_st_i64(cpu_fpr[rs2], t0, ctx->mem_idx, MO_TEQ);
        break;
    default:
        gen_exception_illegal(ctx);
        break;
    }

    tcg_temp_free(t0);
}
#endif

static void gen_set_rm(DisasContext *ctx, int rm)
{
    TCGv_i32 t0;

    if (ctx->frm == rm) {
        return;
    }
    ctx->frm = rm;
    t0 = tcg_const_i32(rm);
    gen_helper_set_rounding_mode(cpu_env, t0);
    tcg_temp_free_i32(t0);
}


static void gen_system(CPURISCVState *env, DisasContext *ctx, uint32_t opc,
                      int rd, int rs1, int csr)
{
    tcg_gen_movi_tl(cpu_pc, ctx->base.pc_next);

    switch (opc) {
    case OPC_RISC_ECALL:
        switch (csr) {
        case 0x0: /* ECALL */
            /* always generates U-level ECALL, fixed in do_interrupt handler */
            generate_exception(ctx, RISCV_EXCP_U_ECALL);
            tcg_gen_exit_tb(NULL, 0); /* no chaining */
            ctx->base.is_jmp = DISAS_NORETURN;
            break;
        case 0x1: /* EBREAK */
            generate_exception(ctx, RISCV_EXCP_BREAKPOINT);
            tcg_gen_exit_tb(NULL, 0); /* no chaining */
            ctx->base.is_jmp = DISAS_NORETURN;
            break;
        default:
            gen_exception_illegal(ctx);
            break;
        }
        break;
    }
}

static void decode_RV32_64C0(DisasContext *ctx)
{
    uint8_t funct3 = extract32(ctx->opcode, 13, 3);
    uint8_t rd_rs2 = GET_C_RS2S(ctx->opcode);
    uint8_t rs1s = GET_C_RS1S(ctx->opcode);

    switch (funct3) {
    case 3:
#if defined(TARGET_RISCV64)
        /* C.LD(RV64/128) -> ld rd', offset[7:3](rs1')*/
        gen_load(ctx, OPC_RISC_LD, rd_rs2, rs1s,
                 GET_C_LD_IMM(ctx->opcode));
#else
        /* C.FLW (RV32) -> flw rd', offset[6:2](rs1')*/
        gen_fp_load(ctx, OPC_RISC_FLW, rd_rs2, rs1s,
                    GET_C_LW_IMM(ctx->opcode));
#endif
        break;
    case 7:
#if defined(TARGET_RISCV64)
        /* C.SD (RV64/128) -> sd rs2', offset[7:3](rs1')*/
        gen_store(ctx, OPC_RISC_SD, rs1s, rd_rs2,
                  GET_C_LD_IMM(ctx->opcode));
#else
        /* C.FSW (RV32) -> fsw rs2', offset[6:2](rs1')*/
        gen_fp_store(ctx, OPC_RISC_FSW, rs1s, rd_rs2,
                     GET_C_LW_IMM(ctx->opcode));
#endif
        break;
    }
}

static void decode_RV32_64C(CPURISCVState *env, DisasContext *ctx)
{
    uint8_t op = extract32(ctx->opcode, 0, 2);

    switch (op) {
    case 0:
        decode_RV32_64C0(ctx);
        break;
    }
}

#define EX_SH(amount) \
    static int ex_shift_##amount(int imm) \
    {                                         \
        return imm << amount;                 \
    }
EX_SH(1)
EX_SH(2)
EX_SH(3)
EX_SH(4)
EX_SH(12)

static int ex_rvc_register(int reg)
{
    return 8 + reg;
}

bool decode_insn32(DisasContext *ctx, uint32_t insn);
/* Include the auto-generated decoder for 32 bit insn */
#include "decode_insn32.inc.c"
/* Include insn module translation function */
#include "insn_trans/trans_rvi.inc.c"
#include "insn_trans/trans_rvm.inc.c"
#include "insn_trans/trans_rva.inc.c"
#include "insn_trans/trans_rvf.inc.c"
#include "insn_trans/trans_rvd.inc.c"
#include "insn_trans/trans_privileged.inc.c"

bool decode_insn16(DisasContext *ctx, uint16_t insn);
/* auto-generated decoder*/
#include "decode_insn16.inc.c"
#include "insn_trans/trans_rvc.inc.c"

static void decode_RV32_64G(CPURISCVState *env, DisasContext *ctx)
{
    int rs1, rd;
    uint32_t op;

    /* We do not do misaligned address check here: the address should never be
     * misaligned at this point. Instructions that set PC must do the check,
     * since epc must be the address of the instruction that caused us to
     * perform the misaligned instruction fetch */

    op = MASK_OP_MAJOR(ctx->opcode);
    rs1 = GET_RS1(ctx->opcode);
    rd = GET_RD(ctx->opcode);

    switch (op) {
    case OPC_RISC_SYSTEM:
        gen_system(env, ctx, MASK_OP_SYSTEM(ctx->opcode), rd, rs1,
                   (ctx->opcode & 0xFFF00000) >> 20);
        break;
    default:
        gen_exception_illegal(ctx);
        break;
    }
}

static void decode_opc(DisasContext *ctx)
{
    /* check for compressed insn */
    if (extract32(ctx->opcode, 0, 2) != 3) {
        if (!riscv_has_ext(ctx->env, RVC)) {
            gen_exception_illegal(ctx);
        } else {
            ctx->pc_succ_insn = ctx->base.pc_next + 2;
            if (!decode_insn16(ctx, ctx->opcode)) {
                /* fall back to old decoder */
                decode_RV32_64C(ctx->env, ctx);
            }
        }
    } else {
        ctx->pc_succ_insn = ctx->base.pc_next + 4;
        if (!decode_insn32(ctx, ctx->opcode)) {
            /* fallback to old decoder */
            decode_RV32_64G(ctx->env, ctx);
        }
    }
}

static void riscv_tr_init_disas_context(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);

    ctx->pc_succ_insn = ctx->base.pc_first;
    ctx->flags = ctx->base.tb->flags;
    ctx->mem_idx = ctx->base.tb->flags & TB_FLAGS_MMU_MASK;
    ctx->frm = -1;  /* unknown rounding mode */
}

static void riscv_tr_tb_start(DisasContextBase *db, CPUState *cpu)
{
}

static void riscv_tr_insn_start(DisasContextBase *dcbase, CPUState *cpu)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);

    tcg_gen_insn_start(ctx->base.pc_next);
}

static bool riscv_tr_breakpoint_check(DisasContextBase *dcbase, CPUState *cpu,
                                      const CPUBreakpoint *bp)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);

    tcg_gen_movi_tl(cpu_pc, ctx->base.pc_next);
    ctx->base.is_jmp = DISAS_NORETURN;
    gen_exception_debug();
    /* The address covered by the breakpoint must be included in
       [tb->pc, tb->pc + tb->size) in order to for it to be
       properly cleared -- thus we increment the PC here so that
       the logic setting tb->size below does the right thing.  */
    ctx->base.pc_next += 4;
    return true;
}


static void riscv_tr_translate_insn(DisasContextBase *dcbase, CPUState *cpu)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);
    ctx->env = cpu->env_ptr;

    ctx->opcode = cpu_ldl_code(ctx->env, ctx->base.pc_next);
    decode_opc(ctx);
    ctx->base.pc_next = ctx->pc_succ_insn;

    if (ctx->base.is_jmp == DISAS_NEXT) {
        target_ulong page_start;

        page_start = ctx->base.pc_first & TARGET_PAGE_MASK;
        if (ctx->base.pc_next - page_start >= TARGET_PAGE_SIZE) {
            ctx->base.is_jmp = DISAS_TOO_MANY;
        }
    }
}

static void riscv_tr_tb_stop(DisasContextBase *dcbase, CPUState *cpu)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);

    switch (ctx->base.is_jmp) {
    case DISAS_TOO_MANY:
        gen_goto_tb(ctx, 0, ctx->base.pc_next);
        break;
    case DISAS_NORETURN:
        break;
    default:
        g_assert_not_reached();
    }
}

static void riscv_tr_disas_log(const DisasContextBase *dcbase, CPUState *cpu)
{
    qemu_log("IN: %s\n", lookup_symbol(dcbase->pc_first));
    log_target_disas(cpu, dcbase->pc_first, dcbase->tb->size);
}

static const TranslatorOps riscv_tr_ops = {
    .init_disas_context = riscv_tr_init_disas_context,
    .tb_start           = riscv_tr_tb_start,
    .insn_start         = riscv_tr_insn_start,
    .breakpoint_check   = riscv_tr_breakpoint_check,
    .translate_insn     = riscv_tr_translate_insn,
    .tb_stop            = riscv_tr_tb_stop,
    .disas_log          = riscv_tr_disas_log,
};

void gen_intermediate_code(CPUState *cs, TranslationBlock *tb)
{
    DisasContext ctx;

    translator_loop(&riscv_tr_ops, &ctx.base, cs, tb);
}

void riscv_translate_init(void)
{
    int i;

    /* cpu_gpr[0] is a placeholder for the zero register. Do not use it. */
    /* Use the gen_set_gpr and gen_get_gpr helper functions when accessing */
    /* registers, unless you specifically block reads/writes to reg 0 */
    cpu_gpr[0] = NULL;

    for (i = 1; i < 32; i++) {
        cpu_gpr[i] = tcg_global_mem_new(cpu_env,
            offsetof(CPURISCVState, gpr[i]), riscv_int_regnames[i]);
    }

    for (i = 0; i < 32; i++) {
        cpu_fpr[i] = tcg_global_mem_new_i64(cpu_env,
            offsetof(CPURISCVState, fpr[i]), riscv_fpr_regnames[i]);
    }

    cpu_pc = tcg_global_mem_new(cpu_env, offsetof(CPURISCVState, pc), "pc");
    load_res = tcg_global_mem_new(cpu_env, offsetof(CPURISCVState, load_res),
                             "load_res");
    load_val = tcg_global_mem_new(cpu_env, offsetof(CPURISCVState, load_val),
                             "load_val");
}
