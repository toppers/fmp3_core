/*
 *  ESP32-P4 FMP3 移植 / Step3: 最小周期タスクアプリ ヘッダ
 *
 *  dly_tsk による周期 syslog 出力でタイマ tick 割込み・スケジューラ・
 *  logtask を end-to-end 検証する(serial RX 不要)．
 */
#ifndef FMP_APP_H
#define FMP_APP_H

#include <kernel.h>
#include <t_syslog.h>

/*
 *  タスクの優先度とスタックサイズ
 *    LOGTASK_PRIORITY(=3) より低い優先度にし，本タスクが dly_tsk でブロック
 *    した隙に logtask が走ってログを flush できるようにする．
 */
#define TICK_PRIORITY   10
#define STACK_SIZE      4096

/*
 *  周期(マイクロ秒)
 */
#define TICK_PERIOD     500000      /* 500 ms */

#ifndef TOPPERS_MACRO_ONLY
extern void tick_task(EXINF exinf);
#endif /* TOPPERS_MACRO_ONLY */

#endif /* FMP_APP_H */
