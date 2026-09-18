/*
 *  TOPPERS/ASP Kernel
 *      Toyohashi Open Platform for Embedded Real-Time Systems/
 *      Advanced Standard Profile Kernel
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアを TOPPERS ライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソフ
 *  トウェアは無保証で提供される．
 *
 */

/*
 *  テストプログラムのターゲット依存定義（ARM Musca-B1 用）
 */

#ifndef TOPPERS_TARGET_TEST_H
#define TOPPERS_TARGET_TEST_H

#define STACK_SIZE (1024)

/*
 *  int1（割込み管理機能）テスト用の割込み定義
 *
 *  QEMU musca-b1（SSE-200・EXP_NUMIRQ=96）で，デバイスに接続されていない
 *  予備の外部 IRQ 60（→ NVIC例外番号 60+16=76）をソフト割込み源として使う
 *  （デバイス IRQ は uart0=7〜12, uart1=13〜18, rtc=39 に限られる）．
 *  arm_m は ras_int / prb_int をサポートし，NVIC のソフト pend（ISPR）で
 *  発生・ハンドラ入口で自動クリアされるため intno1_clear() は空でよい．
 */
/*
 *  FMP3 では割込み番号をプロセッサ単位（(prcid << 16) | intno）で指定する．
 *
 *  NVIC はコアごとに独立したインスタンスであり，ras_int / prb_int / clr_int は
 *  自コアの NVIC_ISPR / NVIC_ICPR のみを操作する（arch/arm_m_gcc/common/
 *  core_kernel_impl.h）．したがって未接続の予備 IRQ は，生の IRQ 番号を PRC1 と
 *  同じにしたまま上位16bitのプロセッサIDだけを変えて PRC2 にも割り当てられる．
 *
 *  INTNO2 は sample1 が 2 コア構成で inthd_no[] から無条件に参照するため必須．
 */
#define INTNO1			((1U << 16) | (60U + 16U))	/* PRC1, 予備NVIC IRQ60 → INTNO 76 */
#define INTNO1_INTATR	TA_ENAINT
#define INTNO1_INTPRI	(-2)
#define intno1_clear()

#if TNUM_PRCID >= 2
#define INTNO2			((2U << 16) | (60U + 16U))	/* PRC2, 予備NVIC IRQ60 → INTNO 76 */
#define INTNO2_INTATR	TA_ENAINT
#define INTNO2_INTPRI	(-2)
#define intno2_clear()
#endif /* TNUM_PRCID >= 2 */

/*
 *  INTNO3 は test/test_dcre5.c（dcre 動的 ISR 回帰テスト）が，syssvc/serial.cfg
 *  由来の ISR_SIO への依存を解消し自立化するために使う「発火されない静的 ISR
 *  専用」の予備割込み．INTNO1/INTNO2 と同じ流儀で，未接続の予備 IRQ を PRC1 に
 *  割り当てる（IRQ60=INTNO1，IRQ61=INTNO_UNOPTED（test/test_dcre5.h で定義・
 *  意図的に CFG_INT しない），IRQ62=INTNO3）．
 */
#define INTNO3			((1U << 16) | (62U + 16U))	/* PRC1, 予備NVIC IRQ62 → INTNO 78 */
#define INTNO3_INTATR	TA_ENAINT
#define INTNO3_INTPRI	(-2)
#define intno3_clear()

/*
 *  コア依存モジュール（ARM-M 用）
 */
#include "core_test.h"

#endif /* TOPPERS_TARGET_TEST_H */
