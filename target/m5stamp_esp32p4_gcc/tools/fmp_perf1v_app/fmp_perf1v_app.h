/*
 *  ESP32-P4 FMP3 / perf1(タスク切換え時間) の「ベクタ命令使用」改造版 ヘッダ
 *
 *  test/perf1.c と同じ slp_tsk/wup_tsk の片方向タスク切換え時間を histogram で計測するが，
 *  各タスクが切換え直後(計測区間内)に PIE ベクタ命令(esp.vadd)を1回使う点が異なる．これにより
 *  両タスクが交互に PIE オーナになり，相手は次に走るとき非オーナになる:
 *    - eager : 切換えで毎回 PIE(216B)を退避/復元するので，ベクタ使用はオーナとして安価(差はほぼ無)．
 *    - lazy  : 切換えは安い(csrwi)が，切換え後の初回ベクタが非オーナ→illegal トラップ(往復~411cyc)
 *              になり，計測区間に含まれる．→ ベクタを使うワークロードでは lazy が eager より高くなる．
 *
 *  ベクタ命令は TOPPERS_SUPPORT_PIE のとき有効(eager/lazy 両構成)．lazy のみ cfg に
 *  DEF_EXC(EXCNO_IINST, pie_exc_handler) が要る．histogram.o のリンクが要る(EXTRA_SOBJS)．
 */
#ifndef FMP_PERF1V_APP_H
#define FMP_PERF1V_APP_H

#include <kernel.h>
#include <t_syslog.h>

#define TASK1_PRIORITY	9		/* 計測タスク1(高) */
#define TASK2_PRIORITY	10		/* 計測タスク2(中) */
#define MAIN_PRIORITY	11		/* メインタスク(低) */
#define STACK_SIZE		8192
#define NO_MEASURE		10000U

#ifndef TOPPERS_MACRO_ONLY
extern void task1(EXINF exinf);
extern void task2(EXINF exinf);
extern void main_task(EXINF exinf);
#ifdef TOPPERS_PIE_LAZY
extern void pie_exc_handler(void *p_excinf);
#endif
#endif /* TOPPERS_MACRO_ONLY */

#endif /* FMP_PERF1V_APP_H */
