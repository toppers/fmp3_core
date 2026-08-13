/*
 *  ESP32-P4 FMP3 移植 / lazy PIE: ras_ter 要求による終了経路の release_context 正当性 実証アプリ
 *
 *  ext_tsk(自タスク終了)版(fmp_pie_term_app)に対し，本アプリは **ras_ter で要求された終了**が
 *  release_context を正しく通すことを検証する．ras_ter→task_terminate の経路は2つある:
 *    (1) 対象が終了許可(enater)なら即 task_terminate(task_term.c:204)．
 *    (2) 対象が終了禁止(dis_ter)なら raster をセットし，対象が次に ena_ter したとき自タスク
 *        終了する(task_term.c:291 → exit_and_dispatch)．
 *  本アプリは hard bug を誘発する (2) を突く: TERM が dis_ter 下で PIE オーナ(0x7F2=ON)になり，
 *  REQUESTER の ras_ter で raster をセット，TERM が ena_ter で**オーナのまま自タスク終了**する．
 *  直後に WORKER が start_r 起動し，release_context が無いと 0x7F2=ON を継承して幽霊オーナ化→
 *  yield 中に NOISE がオーナを奪うと WORKER の前半状態が死んだ TERM の保存域へ誤 flush され壊れる．
 *
 *  チェーン: REPORT が golden 計測しチェーンを kick．以後
 *    TERM: dis_ter→PIE オーナ化→wup_tsk(REQUESTER)→act_tsk(WORKER)→ena_ter(raster で自終了)
 *    REQUESTER: slp_tsk で待機→TERM の wup で起き ras_ter(TERM) で raster セット→slp_tsk
 *    WORKER: yield を跨ぐ PIE 累算→golden 照合→act_tsk(TERM)→ext_tsk
 *    NOISE: WORKER の yield 中に PIE オーナを奪う(free-run)
 *
 *  PASS: WORKER の okW が増え errW=0 のまま多数回(ras_ter 要求の自終了でも release_context が機能)．
 *  設計: arch/riscv_gcc/esp32p4/pie_hwlp_design.md §9.2．PRC_NUM=2 でビルド(動作は PRC1 のみ)．
 */
#ifndef FMP_PIE_RASTER_APP_H
#define FMP_PIE_RASTER_APP_H

#include <kernel.h>
#include <t_syslog.h>

#define STACK_SIZE        8192

#define REPORT_PRIORITY   8          /* golden 計測・周期集計 */
#define REQUESTER_PRIORITY 8         /* TERM の wup で起き ras_ter を発行 */
#define TERM_PRIORITY     9          /* dis_ter 下で PIE オーナ→ena_ter で自終了 */
#define WORKER_PRIORITY   9          /* TERM 自終了の次に start_r 起動 */
#define NOISE_PRIORITY    10         /* WORKER の yield 中に PIE オーナを奪う */

#define WORKER_SEED       7u
#define WORKER_ITERS      4000u
#define TERM_SEED         3u
#define TERM_ITERS        2000u
#define NOISE_SEED        11u
#define NOISE_ITERS       100000u

#define YIELD_TIME        1000       /* us. WORKER 分割点の dly_tsk */
#define REPORT_PERIOD     1000000    /* us. REPORT の集計周期 */

#ifndef TOPPERS_MACRO_ONLY
extern void report_task(EXINF exinf);
extern void requester_task(EXINF exinf);
extern void term_task(EXINF exinf);
extern void worker_task(EXINF exinf);
extern void noise_task(EXINF exinf);
#ifdef TOPPERS_PIE_LAZY
extern void pie_exc_handler(void *p_excinf);
#endif
#endif /* TOPPERS_MACRO_ONLY */

#endif /* FMP_PIE_RASTER_APP_H */
