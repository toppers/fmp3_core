/*
 *  ESP32-P4 FMP3 移植 / PIE コプロセッサコンテキスト save/restore 実証アプリ ヘッダ
 *
 *  HWLP の実証(fmp_hwlp_app)と同型．同一コア(PRC1)に LOW/HIGH/REPORT を置き，
 *  LOW と HIGH が共に PIE ベクタ演算(q レジスタへの累算)を回す．HIGH(高優先度・周期起床)が
 *  LOW(低優先度・連続)を PIE 演算中にプリエンプトする．eager 方式の PIE save/restore が
 *  正しく働かない限り，HIGH の演算が LOW の q レジスタ(累算器)を壊し結果が golden と食い違う．
 *  → errA/errB が 0 のまま多数回 PASS すれば eager PIE の値 round-trip が機能している実証．
 */
#ifndef FMP_PIE_APP_H
#define FMP_PIE_APP_H

#include <kernel.h>
#include <t_syslog.h>

#define STACK_SIZE       8192        /* PIE 退避(216B)が毎切替で積まれるため余裕を持たせる */

#define REPORT_PRIORITY  8
#define HIGH_PRIORITY    9
#define LOW_PRIORITY     10

/*  PIE ベクタ累算の反復数．1 回 ~数 ms にしてカーネル tick を跨ぎ，プリエンプトが
 *  PIE 演算中(q0 累算器が live)に入るようにする．LOW/HIGH で別 seed・別反復にして
 *  衝突を露呈させる． */
#define LOW_SEED         3u
#define LOW_ITERS        800000u
#define HIGH_SEED        7u
#define HIGH_ITERS       600000u

#define HIGH_PERIOD      2000        /* us */
#define REPORT_PERIOD    1000000     /* us */

#ifndef TOPPERS_MACRO_ONLY
extern void low_task(EXINF exinf);
extern void high_task(EXINF exinf);
extern void report_task(EXINF exinf);
#ifdef TOPPERS_PIE_LAZY
/*  lazy PIE: 非オーナの初回 PIE 命令(illegal)トラップハンドラ(chip_kernel_impl.c)  */
extern void pie_exc_handler(void *p_excinf);
#endif /* TOPPERS_PIE_LAZY */
#endif /* TOPPERS_MACRO_ONLY */

#endif /* FMP_PIE_APP_H */
