/*
 *  ESP32-P4 FMP3 移植 / HWLP コプロセッサコンテキスト save/restore 実証アプリ ヘッダ
 *
 *  同一コア(PRC1)に LOW/HIGH/REPORT の 3 タスクを置く．LOW と HIGH は共に
 *  ハードウェアループ(HWLP)を回し，HIGH(高優先度・周期起床)が LOW(低優先度・連続)を
 *  hwloop 実行中にプリエンプトする．切替で HWLP ループ CSR が正しく退避/復帰されない
 *  限り，LOW の hwloop は HIGH の loop CSR 設定に破壊され結果が golden と食い違う．
 *  → err が 0 のまま多数回 PASS すれば eager 方式の save/restore が機能している実証．
 */
#ifndef FMP_HWLP_APP_H
#define FMP_HWLP_APP_H

#include <kernel.h>
#include <t_syslog.h>

#define STACK_SIZE       4096

/*  優先度(小さいほど高優先). いずれも LOGTASK_PRIORITY(=3) より低くしログを flush 可能に.
 *  REPORT(8) > HIGH(9) > LOW(10). HIGH が LOW をプリエンプトし, REPORT が両者をプリエンプト. */
#define REPORT_PRIORITY  8
#define HIGH_PRIORITY    9
#define LOW_PRIORITY     10

/*  hwloop の反復数. LOW は 1 回 ~9ms(@360MHz)でカーネル tick を跨ぐ大きさにし,
 *  プリエンプトが hwloop 実行中に確実に入るようにする. HIGH は別 count(別の loop CSR
 *  値で衝突を露呈させる). */
#define LOW_COUNT        200000u
#define HIGH_COUNT       150000u

#define HIGH_PERIOD      2000        /* us: HIGH の起床周期(LOW を頻繁にプリエンプト) */
#define REPORT_PERIOD    1000000     /* us: 1s 毎にレポート */

#ifndef TOPPERS_MACRO_ONLY
extern void low_task(EXINF exinf);
extern void high_task(EXINF exinf);
extern void report_task(EXINF exinf);
#endif /* TOPPERS_MACRO_ONLY */

#endif /* FMP_HWLP_APP_H */
