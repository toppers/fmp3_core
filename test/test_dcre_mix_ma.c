/*
 *  TOPPERS Software
 *      Toyohashi Open Platform for Embedded Real-Time Systems
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，以下の(1)〜(4)の条件を満たす場合に限り，本ソフトウェ
 *  ア（本ソフトウェアを改変したものを含む．以下同じ）を使用・複製・改
 *  変・再配布（以下，利用と呼ぶ）することを無償で許諾する．
 *  (1) 本ソフトウェアをソースコードの形で利用する場合には，上記の著作
 *      権表示，この利用条件および下記の無保証規定が，そのままの形でソー
 *      スコード中に含まれていること．
 *  (2) 本ソフトウェアを，ライブラリ形式など，他のソフトウェア開発に使
 *      用できる形で再配布する場合には，再配布に伴うドキュメント（利用
 *      者マニュアルなど）に，上記の著作権表示，この利用条件および下記
 *      の無保証規定を掲載すること．
 *  (3) 本ソフトウェアを，機器に組み込むなど，他のソフトウェア開発に使
 *      用できない形で再配布する場合には，次のいずれかの条件を満たすこ
 *      と．
 *    (a) 再配布に伴うドキュメント（利用者マニュアルなど）に，上記の著
 *        作権表示，この利用条件および下記の無保証規定を掲載すること．
 *    (b) 再配布の形態を，別に定める方法によって，TOPPERSプロジェクトに
 *        報告すること．
 *  (4) 本ソフトウェアの利用により直接的または間接的に生じるいかなる損
 *      害からも，上記著作権者およびTOPPERSプロジェクトを免責すること．
 *      また，本ソフトウェアのユーザまたはエンドユーザからのいかなる理
 *      由に基づく請求からも，上記著作権者およびTOPPERSプロジェクトを
 *      免責すること．
 *
 *  本ソフトウェアは，無保証で提供されているものである．上記著作権者お
 *  よびTOPPERSプロジェクトは，本ソフトウェアに関して，特定の使用目的
 *  に対する適合性も含めて，いかなる保証も行わない．また，本ソフトウェ
 *  アの利用により直接的または間接的に生じたいかなる損害に関しても，そ
 *  の責任を負わない．
 *
 *  $Id$
 */

/*
 *		マルチaffinityクラス上の ENA_DYNISR 生成検査用サンプル
 *
 * 【このテストの目的】
 *
 *	`ENA_DYNISR` を、複数プロセッサに割り付けられた（affinityPrcListが
 *	2要素以上の）クラス（kria_arm64 の `CLS_ALL_PRC1`）の囲みの中に置いた
 *	構成で、cfg 生成が「対象クラスの affinityPrcList に含まれる全プロセッサ
 *	ぶんの DEF_INH 相当」を正しく生成し、かつそれらが同一の割込みキュー
 *	（同じ `_kernel_inthdr_<intno>`／同じ `isr_queue_table` 添字）を指す
 *	ことを、生成物（`kernel_cfg.c`）から実証するための最小サンプルである。
 *
 *	★このプログラムは cfg_equivalence.sh と生成物 grep のための入力であり、
 *	  機能の検査は行わない（QEMU で走らせる必要も無い）。走らせた場合は
 *	  ただちに check_finish(1) で PASS して終わる（test_dcre_mix.c と同型）。
 */

#include <kernel.h>
#include <t_syslog.h>
#include "syssvc/test_svc.h"
#include "kernel_cfg.h"
#include "test_dcre_mix_ma.h"

/* DO NOT DELETE THIS LINE -- gentest depends on it. */

void
task1(EXINF exinf)
{
	test_start(__FILE__);
	check_finish(1);
}

/*
 *  マルチaffinityクラス（CLS_ALL_PRC1）に属する CFG_INT/ENA_DYNISR の対象
 *  割込みサービスルーチン．リンク検査のみが目的のため中身は空でよい．
 */
void
ma_isr1(EXINF exinf)
{
}
