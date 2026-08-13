/*
 *  ESP32-P4 FMP3 移植 / lazy PIE マイグレーション pattern B/C/D 実証アプリ ヘッダ
 *
 *  pattern A(自タスク移行)は fmp_pie_mig_app で検証済．本アプリは **コントローラが
 *  WORKER(自タスク以外)を移行**する pattern B/C/D を検証する:
 *    B: 同PE 他 RUNNABLE タスクの移行(WORKER は busy-wait で RUNNABLE)
 *    C: 無タイムアウト待ちタスクの移行(WORKER は slp_tsk)
 *    D: タイムアウト付き待ちタスクの移行(WORKER は tslp_tsk)
 *
 *  WORKER は PIE 累算を前半/後半に分割し，分割点で同期して上記いずれかの状態になる．
 *  CONTROLLER(PRC1, 高優先)が ref_tsk で WORKER の状態を判別し mig_tsk(WORKER, PRC2) で
 *  PRC2 へ移行(B/C/D 各分岐)→ 起床．WORKER は後半を PRC2 で実行(非オーナ→トラップ復元)し
 *  golden 照合，その後 mig_tsk(TSK_SELF, PRC1) で PRC1 へ戻り次反復(pattern A 再利用)．
 *  q0/q1 は分割を跨いで物理レジスタに live なので，移行で lazy が保存域へ flush(save_context)
 *  → 移行先で復元しない限り結果が壊れる．
 *
 *  PASS: okW 増加・errW=0，かつ cntB/cntC/cntD すべて増加(各分岐を実際に通過)．PRC_NUM=2．
 */
#ifndef FMP_PIE_MIG2_APP_H
#define FMP_PIE_MIG2_APP_H

#include <kernel.h>
#include <t_syslog.h>

#define STACK_SIZE       8192

#define REPORT_PRIORITY    8
#define CONTROLLER_PRIORITY 9
#define WORKER_PRIORITY    10
#define NOISE_PRIORITY     11

#define WORKER_SEED      5u
#define WORKER_ITERS     100000u

#define NOISE_B_SEED     13u
#define NOISE_ITERS      120000u

#define REPORT_PERIOD    1000000     /* us */

#ifndef TOPPERS_MACRO_ONLY
extern void worker_task(EXINF exinf);
extern void controller_task(EXINF exinf);
extern void noise_task(EXINF exinf);
extern void report_task(EXINF exinf);
#ifdef TOPPERS_PIE_LAZY
extern void pie_exc_handler(void *p_excinf);
#endif
#endif /* TOPPERS_MACRO_ONLY */

#endif /* FMP_PIE_MIG2_APP_H */
