/*
 *  ESP32-P4 FMP3 移植 / lazy PIE タスク終了時の所有権解放(release_context)正当性 実証アプリ ヘッダ
 *
 *  終了タスクが PIE オーナのまま ext_tsk すると，所有権を解放しない限り 0x7F2=ON と
 *  stale な pie_owner が残る．直後に新規タスクが start_r(switch-in の 0x7F2 管理を通らない)
 *  で起動すると 0x7F2=ON を継承して「非オーナなのに有効」な幽霊オーナとなり，後続の
 *  プリエンプトで生 PIE 状態を死んだタスクの保存域へ取り違えて flush し計算を壊す．
 *
 *  本アプリは PRC1 上で TERM↔WORKER の自己チェーンを回し，release_context(task_terminate
 *  フック)の効果を実証する:
 *    1. TERM(prio9)が PIE 累算でオーナになり(0x7F2=ON)，同 prio の WORKER を act_tsk で ready 化
 *       してから ext_tsk(自タスク終了)．
 *    2. TERM 終了の次に WORKER(prio9)が start_r で即起動(TERM 実行中に ready 化したので queue 末尾
 *       に並び，TERM 終了の直後に走る)．
 *    3. WORKER は PIE 累算を前半/後半に分割し，分割点で dly_tsk(YIELD)して NOISE(prio10)に PIE
 *       オーナを奪わせる．復帰後に後半を継続し結果を golden 照合する．
 *    4. WORKER は次ラウンドの TERM を act_tsk で ready 化してから ext_tsk(チェーン継続)．
 *    REPORT(prio8)は駆動せず golden 計測と周期集計のみ．
 *
 *  release_context 無し: 3 で WORKER が 0x7F2=ON を継承し幽霊オーナ化．4 の dly 中に NOISE が
 *    「旧オーナ=TERM」を flush＝WORKER の前半状態を死んだ TERM の保存域へ誤って退避し，WORKER
 *    自身の保存域は未更新．復帰時に WORKER は自分の状態を復元できず後半が壊れ errW>0．
 *  release_context 有り: 2 の ext_tsk で TERM の所有権が解放(owner=NULL, 0x7F2=OFF)．WORKER は
 *    start_r で OFF を継承し初回 PIE でトラップして正規オーナになる．NOISE は WORKER を WORKER
 *    自身の保存域へ flush し，復帰時に正しく復元＝errW=0 で PASS．
 *
 *  PASS: WORKER の okW が増え errW=0 のまま多数回．
 *  設計: arch/riscv_gcc/esp32p4/pie_hwlp_design.md §9.2．PRC_NUM=2 でビルド(動作は PRC1 のみ使用)．
 */
#ifndef FMP_PIE_TERM_APP_H
#define FMP_PIE_TERM_APP_H

#include <kernel.h>
#include <t_syslog.h>

#define STACK_SIZE       8192        /* PIE 保存域(底224B)確保のため余裕を持たせる */

#define REPORT_PRIORITY  8           /* golden 計測・周期集計(最上位．logtask=3 より低くし flush 可) */
#define TERM_PRIORITY    9           /* PIE オーナになって ext_tsk するタスク */
#define WORKER_PRIORITY  9           /* TERM と同 prio．TERM 実行中に ready→TERM 終了の次に start_r 起動 */
#define NOISE_PRIORITY   10          /* WORKER の yield 中に PIE オーナを奪う */

/*  WORKER の PIE 累算: q0 += {seed×4} を iters 回．golden=seed*iters．前半/後半に分割し
 *  分割点で dly_tsk(YIELD)．小さめにして 1 サイクルを短くし試行回数を稼ぐ． */
#define WORKER_SEED      7u
#define WORKER_ITERS     4000u

/*  TERM の PIE 累算(オーナになるだけ．結果は不問) */
#define TERM_SEED        3u
#define TERM_ITERS       2000u

/*  noise(WORKER の yield 中に連続 PIE してオーナを奪う) */
#define NOISE_SEED       11u
#define NOISE_ITERS      100000u

#define YIELD_TIME       1000        /* us. WORKER 分割点の dly_tsk(NOISE にオーナを奪わせる) */
#define REPORT_PERIOD    1000000     /* us. REPORT の集計周期 */

#ifndef TOPPERS_MACRO_ONLY
extern void report_task(EXINF exinf);
extern void term_task(EXINF exinf);
extern void worker_task(EXINF exinf);
extern void noise_task(EXINF exinf);
#ifdef TOPPERS_PIE_LAZY
extern void pie_exc_handler(void *p_excinf);
#endif
#endif /* TOPPERS_MACRO_ONLY */

#endif /* FMP_PIE_TERM_APP_H */
