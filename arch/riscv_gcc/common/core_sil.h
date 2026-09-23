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
 *  $Id: core_sil.h 178 2019-10-08 13:55:00Z ertl-honda $
 */

/*
 *      sil.hのコア依存部（RISC-V用）
 *
 *  このヘッダファイルは，target_sil.h（または，そこからインクルードさ
 *  れるファイル）のみからインクルードされる．他のファイルから直接イン
 *  クルードしてはならない．
 */

#ifndef TOPPERS_CORE_SIL_H
#define TOPPERS_CORE_SIL_H

#include <t_stddef.h>

#ifndef TOPPERS_MACRO_ONLY

/*
 *  メモリが変更されることをコンパイラに伝えるためのマクロ
 */
#define TOPPERS_MEMORY_CHANGED Asm("":::"memory")

/*
 *  メモリバリア
 */
#define TOPPERS_DMB()      Asm("fence iorw, iorw" : : : "memory");
#define TOPPERS_DMB_SMP()  Asm("fence rw, rw" : : : "memory");

/*
 *  mstatus の現在値の読出し
 */
Inline ulong_t
TOPPERS_read_mstatus(void)
{
    ulong_t val;
    Asm("csrr %0, mstatus" : "=r"(val));
    return val;
}

/*
 *  mstatus の現在値の変更
 */
Inline void
TOPPERS_write_mstatus(ulong_t val)
{
    Asm("csrw mstatus, %0" :: "r"(val));
}

/*
 *  mstatus_mie のビットセット
 */
Inline void
TOPPERS_set_mstatus_mie(void)
{
    ulong_t tmp;
    Asm("csrrs %0, mstatus, %1" : "=r"(tmp) : "r"(0x08U) : "memory");
}

/*
 *  mstatus_mie のビットクリア
 */
Inline void
TOPPERS_clear_mstatus_mie(void)
{
    ulong_t tmp;
    Asm("csrrc %0, mstatus, %1" : "=r"(tmp) : "r"(0x08U) : "memory");
}

/*
 *  すべての割込みの禁止
 */
Inline ulong_t
TOPPERS_disint(void)
{
    ulong_t val;
    ulong_t mie_mask;

    val = TOPPERS_read_mstatus();
    mie_mask = val & 0x08U;
    TOPPERS_clear_mstatus_mie();
    TOPPERS_MEMORY_CHANGED;
    return(mie_mask);
}

/*
 *  MIEビットの復帰
 */
Inline void
TOPPERS_set_mie(ulong_t mie_mask)
{
    if (mie_mask == 0) {
        TOPPERS_clear_mstatus_mie();
    }
    else {
        TOPPERS_set_mstatus_mie();
    }
}

/*
 *  全割込みロック状態の制御
 */
#define SIL_PRE_LOC     ulong_t TOPPERS_mie_mask
#define SIL_LOC_INT()   ((void)(TOPPERS_mie_mask = TOPPERS_disint()))
#define SIL_UNL_INT()   (TOPPERS_set_mie(TOPPERS_mie_mask))

/*
 *  スピンロック変数（core_kernel_impl.c）
 */
extern volatile uint32_t TOPPERS_sil_spn_var[];

/*
 *  Test&Assign操作 
 */
Inline bool_t
TOPPERS_test_and_assign(volatile uint32_t *p_var, uint32_t prcid)
{
    uint32_t  failed;

#if !defined(__riscv_atomic)
    /*  A 拡張なし（RV32IMC）向け。理由と適用範囲は riscv_insn.h の
     *  riscv_tas_uint32() のコメントを読むこと。  */
#if defined(TNUM_PRCID) && (TNUM_PRCID >= 2)
#error "RISC-V without the A extension cannot host TNUM_PRCID >= 2 (no atomic test-and-assign)."
#endif
    {
        ulong_t  saved;

        Asm("csrrc %0, mstatus, %1" : "=r"(saved) : "r"(MSTATUS_MIE));
        failed = *p_var;
        if (failed == 0U) {
            *p_var = prcid;
        }
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
        : "r"(prcid)
        : );
#else /* !USE_RISCV_LLSC */
    Asm("lw           %0, (%1)     \n" 
        "bnez         %0, 1f       \n"
        "amoswap.w.aq %0, %2, (%1) \n"
        "1:"
        : "=&r"(failed), "+r"(p_var)   /* + : read/write */
        : "r"(prcid)
        : );
#endif /* USE_RISCV_LLSC */
    return(failed != 0);
}

/*
 *  スピンロックの取得
 */
Inline uint32_t
TOPPERS_sil_loc_spn(void)
{
    ulong_t  mie_mask;
    ID       prcid;

    /* 全割込みロック状態に */
    mie_mask = TOPPERS_disint();

    /* スピンロックのチェック */
    sil_get_pid(&prcid);
    if (TOPPERS_sil_spn_var[0] == prcid) {
        /* スピンロックを取得している場合 */
        mie_mask |= 0x01U;
    }
    else {
        /* スピンロックの取得 */
        while (TOPPERS_test_and_assign(&TOPPERS_sil_spn_var[0], prcid)) {
            /* TOPPERS_sil_loc_spn()呼び出し時の MIEの状態にする */
            TOPPERS_set_mie(mie_mask); 
            TOPPERS_clear_mstatus_mie();
        }
        /* ロック取得成功 */
        /* amoswap.w.aq を使用しているためDMBは必要ない */
        TOPPERS_MEMORY_CHANGED;
    }
    return(mie_mask);
}

/*
 *  スピンロックの返却
 */
Inline void
TOPPERS_sil_unl_spn(ulong_t mie_mask)
{
    if ((mie_mask & 0x01U) != 0U) {
        /* スピンロックを取得していた場合 */
        mie_mask &= ~(0x01U);
    }
    else {
        TOPPERS_MEMORY_CHANGED;
        Asm("fence rw, w" : : : "memory");
        TOPPERS_sil_spn_var[0] = 0U;
    }
    TOPPERS_set_mie(mie_mask);
}

/*
 *  SILスピンロック
 */
#define SIL_LOC_SPN() ((void)(TOPPERS_mie_mask = TOPPERS_sil_loc_spn()))
#define SIL_UNL_SPN() (TOPPERS_sil_unl_spn(TOPPERS_mie_mask))

/*
 *  スピンロックの強制解放
 *
 *  自プロセッサがスピンロックを取得している場合に解放する．
 */
Inline void
TOPPERS_sil_force_unl_spn(void)
{
    ID	prcid;

    sil_get_pid(&prcid);
    if (TOPPERS_sil_spn_var[0] == prcid) {
        TOPPERS_sil_spn_var[0] = 0U;
    }
}

/*
 *  メモリ同期バリア
 */
#define TOPPERS_SIL_WRITE_SYNC()    TOPPERS_DMB()

/*
 *  64ビット単位の読み出し
 */
Inline uint64_t
sil_redw_mem(const uint64_t *mem)
{
    uint64_t data;

    data = *((const volatile uint64_t *) mem);
    return(data);
}

/*
 *  64ビット単位の書込み
 */
Inline void
sil_wrdw_mem(uint64_t *mem, uint64_t data)
{
    *((volatile uint64_t *) mem) = data;
}

/*
 *  64ビット単位の同期書込み
 */
Inline void
sil_swrdw_mem(uint64_t *mem, uint64_t data)
{
    *((volatile uint64_t *) mem) = data;
    TOPPERS_SIL_WRITE_SYNC()
}

#endif /* TOPPERS_MACRO_ONLY */
#endif /* TOPPERS_CORE_SIL_H */
