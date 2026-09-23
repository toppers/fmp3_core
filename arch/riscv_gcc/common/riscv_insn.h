/*
 *  TOPPERS Software
 *      Toyohashi Open Platform for Embedded Real-Time Systems
 * 
 *  Copyright (C) 2024 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 * 
 *  上記著作権者は，以下の(1)〜(4)の条件を満たす場合に限り，本ソフトウェ
 *  ア（本ソフトウェアを改変したものを含む．以下同じ）を使用・複製・改
 *  変・再配布（以下，利用と呼ぶ）することを無償で許諾する．
 *  (1) 本ソフトウェアをソースコードの形で利用する場合には，上記の著作
 *      権表示，この利用条件および下記の無保証規定が，そのままの形でソー
 *      スコード中に含まれていること．
 *  (2) 本ソフトウェアを，ライブラリ形式など，他のソフトウェア開発に使
 *      用できる形で再配布する場合には，再配布に伴うドキュメント（利用
 *      者マニュアルなど）に，上記の著作権表示，この利用条件および下記
 *      の無保証規定を掲載すること．
 *  (3) 本ソフトウェアを，機器に組み込むなど，他のソフトウェア開発に使
 *      用できない形で再配布する場合には，次のいずれかの条件を満たすこ
 *      と．
 *    (a) 再配布に伴うドキュメント（利用者マニュアルなど）に，上記の著
 *        作権表示，この利用条件および下記の無保証規定を掲載すること．
 *    (b) 再配布の形態を，別に定める方法によって，TOPPERSプロジェクトに
 *        報告すること．
 *  (4) 本ソフトウェアの利用により直接的または間接的に生じるいかなる損
 *      害からも，上記著作権者およびTOPPERSプロジェクトを免責すること．
 *      また，本ソフトウェアのユーザまたはエンドユーザからのいかなる理
 *      由に基づく請求からも，上記著作権者およびTOPPERSプロジェクトを
 *      免責すること．
 * 
 *  本ソフトウェアは，無保証で提供されているものである．上記著作権者お
 *  よびTOPPERSプロジェクトは，本ソフトウェアに関して，特定の使用目的
 *  に対する適合性も含めて，いかなる保証も行わない．また，本ソフトウェ
 *  アの利用により直接的または間接的に生じたいかなる損害に関しても，そ
 *  の責任を負わない．
 * 
 *  $Id: arm_insn.h 335 2023-04-18 10:50:40Z ertl-honda $
 */

/*
 *    RISC-Vコアの特殊命令のインライン関数定義
 *
 *  このヘッダファイルは，riscv.hからインクルードされる．riscv.hから分離し
 *  ているのは，コンパイラによるインラインアセンブラの記述方法の違いを
 *  吸収するために，このファイルのみを置き換えればよいようにするためで
 *  ある．
 */

#ifndef TOPPERS_RISCV_INSN_H
#define TOPPERS_RISCV_INSN_H

#include <t_stddef.h>

/*
 *  メモリが変更されることをコンパイラに伝えるためのマクロ
 */
#define RISCV_MEMORY_CHANGED	Asm("":::"memory")

#ifndef TOPPERS_MACRO_ONLY

/*
 *  mstatus functions
 */

Inline ulong_t
riscv_read_mstatus(void)
{
    ulong_t val;
    Asm("csrr %0, mstatus" : "=r"(val));
    return val;
}

Inline void
riscv_write_mstatus(ulong_t val)
{
    Asm("csrw mstatus, %0" :: "r"(val));
}

Inline void
riscv_set_mstatus_mie(void)
{
    ulong_t tmp;
    Asm("csrrs %0, mstatus, %1" : "=r"(tmp) : "r"(MSTATUS_MIE));
}

Inline void
riscv_clear_mstatus_mie(void)
{
    ulong_t tmp;
    Asm("csrrc %0, mstatus, %1" : "=r"(tmp) : "r"(MSTATUS_MIE));
}

/*
 *  mie functions
 */

Inline ulong_t
riscv_read_mie(void)
{
    ulong_t val;
    Asm("csrr %0, mie" : "=r"(val));
    return val;
}

Inline void
riscv_write_mie(ulong_t val)
{
    Asm("csrw mie, %0" :: "r"(val));
}

Inline void
riscv_set_mie(ulong_t val)
{
    ulong_t tmp;
    Asm("csrrs %0, mie, %1" : "=r"(tmp) : "r"(val));
}

Inline void
riscv_clear_mie(ulong_t val)
{
    ulong_t tmp;
    Asm("csrrc %0, mie, %1" : "=r"(tmp) : "r"(val));
}

/*
 *  mtvec functions
 */

Inline ulong_t
riscv_read_mtvec(void)
{
    ulong_t val;
    Asm("csrr %0, mtvec" : "=r"(val));
    return val;
}

Inline void
riscv_write_mtvec(ulong_t val)
{
    Asm("csrw mtvec, %0" :: "r"(val));
}

/*
 *  mhartid functions
 */

Inline ulong_t
riscv_read_mhartid(void)
{
    ulong_t val;
    Asm("csrr %0, mhartid" : "=r"(val));
    return val;
}

/*
 *  mcause functions
 */
Inline ulong_t
riscv_read_mcause(void)
{
    ulong_t val;
    Asm("csrr %0, mcause" : "=r"(val));
    return val;
}

/*
 *  mepc functions
 */
Inline ulong_t
riscv_read_mepc(void)
{
    ulong_t val;
    Asm("csrr %0, mepc" : "=r"(val));
    return val;
}

/*
 *  メモリバリア
 */
Inline void
riscv_dmb(void)
{
    Asm("fence iorw, iorw" : : : "memory");
}

Inline void
riscv_dmb_smp(void)
{
    Asm("fence rw, rw" : : : "memory");
}

/*
 *  SMP向けのライト同期
 */
Inline void
riscv_dmwb_smp(void)
{
    Asm("fence rw, w" : : : "memory");
}

/*
 *  Test&Set操作
 *   true  : 取得に失敗
 *   false : 取得に成功
 */
Inline bool_t
riscv_tas_uint32(volatile uint32_t *p_var)
{
    uint32_t  failed;
    
#if !defined(__riscv_atomic)
/*
 *  A 拡張（アトミック命令）を持たないチップ向けの実装
 *
 *  ESP32-C3 の ISA は RV32IMC で、`amoswap` も `lr/sc` も**命令として存在
 *  しない**（ESP32-C6 以降は RV32IMAC）。上の 2 経路はどちらも A 拡張を
 *  前提にしているため、そのままではアセンブルできない。
 *
 *  単一プロセッサに限れば、割込みを禁止したうえでの「読んで、0 なら書く」
 *  は不可分である（他に競合するハートが無く、同一ハート上の割込みは
 *  mstatus.MIE で止まっているため）。よってこの経路は
 *  **TNUM_PRCID == 1 のときだけ**正しい。2 プロセッサ以上で A 拡張が無い
 *  構成は成立しないので、下で `#error` にして黙って通さない。
 *
 *  `__riscv_atomic` は -march に 'a' が含まれるとコンパイラが定義する。
 *  既存チップ（C6 / C5 / P4）は A 拡張つきなのでこの分岐に入らず、
 *  **生成コードは 1 バイトも変わらない**。
 */
#if defined(TNUM_PRCID) && (TNUM_PRCID >= 2)
#error "RISC-V without the A extension cannot host TNUM_PRCID >= 2 (no atomic test-and-set)."
#endif
    {
        ulong_t  saved;

        /* 割込み禁止（mstatus.MIE をクリアし、元の値を保存） */
        Asm("csrrc %0, mstatus, %1" : "=r"(saved) : "r"(MSTATUS_MIE));
        failed = *p_var;
        if (failed == 0U) {
            *p_var = 1U;
        }
        /* MIE が立っていたときだけ戻す */
        if ((saved & MSTATUS_MIE) != 0U) {
            Asm("csrs mstatus, %0" :: "r"(MSTATUS_MIE));
        }
    }
#elif USE_RISCV_LLSC
    Asm("lr.w.aq  %0, (%1)     \n"
        "bnez     %0, 1f       \n"
        "sc.w.rl  %0, %2, (%1) \n"  /* succeed $0 = 0, fail $0 = nonezero */
        "1:"
        : "=&r"(failed), "+r"(p_var)   /* + : read/write */
        : "r"(1)
        : );
#else /* !USE_RISCV_LLSC */
    Asm("lw           %0, (%1)     \n"
        "bnez         %0, 1f       \n"
        "amoswap.w.aq %0, %2, (%1) \n"
        "1:"
        : "=&r"(failed), "+r"(p_var)   /* + : read/write */
        : "r"(1)
        : );    
#endif /* USE_RISCV_LLSC */    
    return(failed != 0);
}

/*
 *  Read Cycle Counter 
 */
Inline uint64_t
riscv_read_ccount(void) {
#if __riscv_xlen == 64
    uint64_t count;
    Asm("rdcycle %0" : "=r"(count));
    return(count);
#elif __riscv_xlen == 32
    uint32_t  count_l, count_u, prev_count_u;

    Asm("rdcycleh %0" : "=r"(count_u));
    do {
        prev_count_u = count_u;
        Asm("rdcycle  %0" : "=r"(count_l));
        Asm("rdcycleh %0" : "=r"(count_u));
    } while (count_u != prev_count_u);
    return((((uint64_t) count_u) << 32) | ((uint64_t) count_l));
#endif /* __riscv_xlen == 64 */
}

#endif /* TOPPERS_MACRO_ONLY */
#endif /* TOPPERS_RISCV_INSN_H */
