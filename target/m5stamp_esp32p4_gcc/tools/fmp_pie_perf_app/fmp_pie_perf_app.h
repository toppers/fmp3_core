/*
 *  ESP32-P4 FMP3 / lazy PIE 非オーナ初回ベクタ命令の例外処理時間 計測アプリ ヘッダ
 *
 *  lazy PIE では，非オーナのタスクが初めて PIE(ベクタ)命令を実行すると illegal-instruction
 *  例外でトラップし，pie_exc_handler が旧オーナを保存域へ flush・自分の状態を復元してから
 *  命令を再実行する．この「トラップ往復＋ハンドラ処理」のコストを mcycle 直読みで計測する．
 *
 *  方式: MEAS(prio10) と STEAL(prio9) を ping-pong．MEAS が wup_tsk(STEAL) すると STEAL が
 *  プリエンプトして esp.vadd でオーナを奪い slp_tsk で戻る → MEAS は非オーナ化．MEAS が
 *    t0=mcycle; esp.vadd(非オーナ→トラップ往復); t1=mcycle   → trap_total = t1-t0
 *  続けて(今度はオーナ)
 *    t2=mcycle; esp.vadd(オーナ→トラップなし);   t3=mcycle   → instr_only = t3-t2
 *  を測り，純トラップ処理コスト = min(trap_total) - min(instr_only)．min で tick 割込み等の
 *  外乱を除去する．lazy(PIE_LAZY=1)でのみ意味を持つ．cfg に DEF_EXC(EXCNO_IINST, pie_exc_handler) 必須．
 */
#ifndef FMP_PIE_PERF_APP_H
#define FMP_PIE_PERF_APP_H

#include <kernel.h>
#include <t_syslog.h>

#define STACK_SIZE      8192
#define MEAS_PRIORITY   10
#define STEAL_PRIORITY  9
#define NMEAS           20000u      /* 計測反復回数(min をとる) */
#define SEED            7u

#ifndef TOPPERS_MACRO_ONLY
extern void meas_task(EXINF exinf);
extern void steal_task(EXINF exinf);
#ifdef TOPPERS_PIE_LAZY
extern void pie_exc_handler(void *p_excinf);
#endif
#endif /* TOPPERS_MACRO_ONLY */

#endif /* FMP_PIE_PERF_APP_H */
