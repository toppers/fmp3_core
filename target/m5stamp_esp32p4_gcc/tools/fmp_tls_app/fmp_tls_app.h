/*
 *  ESP32-P4 FMP3 移植 / TLS(スレッドローカルストレージ)初期化 検証アプリ ヘッダ
 *
 *  各 PE のタスクが異なる種で srand()/rand() を呼び，picolibc の LCG 式で
 *  自前計算した期待値と突き合わせる。tp(スレッドポインタ)がタスクごとに
 *  正しく独立していなければ，他タスクの rand() 呼出しで内部状態
 *  (_tls_rand_next)が混線し，期待値と食い違う値が出る。
 *  (init_additional_regs_start_r の実機検証用。詳細は
 *  docs/esp32s31_bringup_checklist.md 参照)
 */
#ifndef FMP_TLS_APP_H
#define FMP_TLS_APP_H

#include <kernel.h>
#include <t_syslog.h>

#define TLS_PRIORITY    10
#define STACK_SIZE      4096

/*
 *  rand() 呼出し回数(PEあたり)。呼出し間に dly_tsk で他タスクへ切り替える
 *  余地を作り，インターリーブでの内部状態混線を検出しやすくする。
 */
#define TLS_TEST_COUNT      200
#define TLS_TEST_INTERVAL   1000    /* usec (=1ms) */

#ifndef TOPPERS_MACRO_ONLY
extern void tls_task(EXINF exinf);
#endif /* TOPPERS_MACRO_ONLY */

#endif /* FMP_TLS_APP_H */
