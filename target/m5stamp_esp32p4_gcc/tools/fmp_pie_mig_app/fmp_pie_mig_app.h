/*
 *  ESP32-P4 FMP3 移植 / lazy PIE マイグレーション(mig_tsk)正当性 実証アプリ ヘッダ
 *
 *  完全 lazy PIE の「移行時に PIE 状態を保存域へ flush し，移行先 PE でトラップ復元する」
 *  正当性を実機で検証する．WORKER は PIE 累算(q0 へ seed を iters 回足し込む)を
 *  mig_tsk(TSK_SELF, 他PE) で **前半/後半に分割**し，分割点で自タスクを他 PE へ移行する
 *  (pattern A=自タスク移行)．q0/q1 は分割を跨いで物理レジスタに live なので，lazy が移行時に
 *  正しく flush→移行先で復元しない限り，後半の累算が壊れ結果が golden と食い違う．
 *  各 PE に PIE noise タスクを置き，WORKER 到着時に「非オーナ→トラップ復元」を確実に誘発する．
 *
 *  PASS: WORKER の okW が増え errW=0 のまま多数回(=多数回の移行で q0/q1 round-trip 成立)．
 *  設計: arch/riscv_gcc/esp32p4/pie_hwlp_design.md §9.2 / §9.2.1．PRC_NUM=2 前提．
 */
#ifndef FMP_PIE_MIG_APP_H
#define FMP_PIE_MIG_APP_H

#include <kernel.h>
#include <t_syslog.h>

#define STACK_SIZE       8192        /* PIE 保存域(底224B)確保のため余裕を持たせる */

#define REPORT_PRIORITY  8
#define WORKER_PRIORITY  9
#define NOISE_PRIORITY   10

/*  WORKER の PIE 累算: q0 += {seed×4} を iters 回．golden=seed*iters．
 *  iters は前半/後半に分割され分割点で自タスク移行する．1回が短すぎると移行頻度が
 *  上がりすぎ tick を跨がないので中程度にする． */
#define WORKER_SEED      5u
#define WORKER_ITERS     200000u

/*  noise(各PEで連続 PIE してオーナを奪い合う) */
#define NOISE_A_SEED     11u
#define NOISE_B_SEED     13u
#define NOISE_ITERS      150000u

#define REPORT_PERIOD    1000000     /* us */

#ifndef TOPPERS_MACRO_ONLY
extern void worker_task(EXINF exinf);
extern void noise_task(EXINF exinf);
extern void report_task(EXINF exinf);
#ifdef TOPPERS_PIE_LAZY
extern void pie_exc_handler(void *p_excinf);
#endif
#endif /* TOPPERS_MACRO_ONLY */

#endif /* FMP_PIE_MIG_APP_H */
