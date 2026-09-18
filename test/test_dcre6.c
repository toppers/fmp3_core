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
 *		カーネルメモリプールの完全一致フリーリスト再利用のテスト
 *
 * 【テストの目的】
 *
 *	kernel/startup.c の既定メモリプール（バンプ＋完全一致サイズ再利用）の
 *	freelist 経路を runtime で実証する．
 *
 *	(A) 枯渇しないこと：長寿命割付（dtq 1本）を保持したまま同一サイズの
 *	    acre_mpf/del_mpf を MPF_CYCLES(=16) 周反復しても E_NOMEM に
 *	    ならない．旧実装（bump のみ）ではこの手順は 8 周目で必ず
 *	    E_NOMEM になる（★変異 control で再現する＝positive control）．
 *	(B) 完全一致再利用が同一番地を返すこと：acre_mpf → pget_mpf の
 *	    ブロック番地が del → 再 acre をまたいで一致する．
 *	(C) サイズ不一致は再利用されないこと：freelist に 256B/16B が載った
 *	    状態での 8B 要求が bump を前進させることを，残量ちょうどの fill
 *	    成功と直後の E_NOMEM の挟み撃ちで観測する（機序は test_dcre6.h
 *	    の机上算術コメント参照）．
 *	(D) count==0 backstop：全解放後に 2000B の大型割付が入る＝brk
 *	    リセットと freelist クリアの実証．
 *
 * 【使用リソース】
 *
 *	TASK1: 中優先度タスク，TA_ACT属性（静的・PRC1）
 *	DTQ1/MPF1: 静的なデータキュー／固定長メモリプール（AID_* の訂正E
 *	           ガード要件のため．プールは消費しない）
 *	AID_DTQ(4)/AID_MPF(1): 動的スロット
 *	DEF_MPK({ MPK_SIZE, NULL }): カーネルメモリプール（2048B）
 *
 * 【チェックポイント】
 *
 *	PRC1（TASK1 のみ）: 1..5 + check_finish(6)
 *	  ＝ログ中の "Check point" 行は合計 6 本（check_finish 自身の 1 本を含む）
 *
 * 【前提】
 *
 *	musca_b1-2core でのみ実行する（32bit・机上算術の前提．実行時に
 *	check_assert(sizeof(size_t) == 4U) で固定する）．
 */

#include <kernel.h>
#include <t_syslog.h>
#include "syssvc/test_svc.h"
#include "kernel_cfg.h"
#include "test_dcre6.h"

/* DO NOT DELETE THIS LINE -- gentest depends on it. */

void
task1(EXINF exinf)
{
	T_CDTQ		cdtq;
	T_CMPF		cmpf;
	ER_ID		erid;
	ID			dtqid_long, dtqid_probe, dtqid_fill, dtqid_big, mpfid1;
	void		*blk_a;
	void		*blk_b;
	uint_t		i;

	test_start(__FILE__);
	check_point(1);

	/*  本テストの机上算術は 32bit（sizeof(size_t)==4）前提（test_dcre6.h）  */
	check_assert(sizeof(size_t) == 4U);

	/*
	 *  フェーズ0: 長寿命割付（count が 0 に戻らない状況を作る）
	 */
	cdtq.dtqatr = TA_TPRI;
	cdtq.dtqcnt = LONG_DTQCNT;
	cdtq.dtqmb = NULL;
	erid = acre_dtq(&cdtq);
	check_assert(erid > DTQ1);
	dtqid_long = (ID) erid;

	/*
	 *  フェーズA: 長寿命割付を保持したままの acre_mpf/del_mpf × MPF_CYCLES
	 *
	 *  旧実装（bump のみ）では 8 周目の①（ブロック領域 260B）で E_NOMEM
	 *  になる（机上算術は test_dcre6.h）．新実装では 2 周目以降①②とも
	 *  freelist の完全一致で再利用され，E_NOMEM にならない．
	 *  ★変異 control（freelist 走査の無効化）では 8 周目（i==7）で
	 *  下の check_assert(erid > MPF1) が落ちる．
	 */
	cmpf.mpfatr = TA_TPRI;
	cmpf.blkcnt = MPF_BLKCNT;
	cmpf.blksz = MPF_BLKSZ;
	cmpf.mpf = NULL;							/*  ブロック領域を自動確保  */
	cmpf.mpfmb = NULL;							/*  管理領域を自動確保  */
	for (i = 0U; i < MPF_CYCLES; i++) {
		erid = acre_mpf(&cmpf);
		check_assert(erid > MPF1);
		mpfid1 = (ID) erid;
		check_ercd(del_mpf(mpfid1), E_OK);
	}
	check_point(2);

	/*
	 *  フェーズB: 完全一致再利用が同一番地を返すことの直接観測
	 *
	 *  直前の del でブロック領域（256B）と管理領域（16B）が freelist に
	 *  載っている．同一サイズの acre_mpf はどちらも完全一致で再利用する
	 *  ので，pget_mpf が返すブロック番地は del/再 acre をまたいで一致する．
	 */
	erid = acre_mpf(&cmpf);
	check_assert(erid > MPF1);
	mpfid1 = (ID) erid;
	check_ercd(pget_mpf(mpfid1, &blk_a), E_OK);
	check_ercd(rel_mpf(mpfid1, blk_a), E_OK);
	check_ercd(del_mpf(mpfid1), E_OK);

	erid = acre_mpf(&cmpf);
	check_assert(erid > MPF1);
	mpfid1 = (ID) erid;
	check_ercd(pget_mpf(mpfid1, &blk_b), E_OK);
	check_assert(blk_a == blk_b);				/*  ★同一番地の再利用  */
	check_ercd(rel_mpf(mpfid1, blk_b), E_OK);
	check_ercd(del_mpf(mpfid1), E_OK);
	check_point(3);

	/*
	 *  フェーズC: サイズ不一致は再利用されない（bump 前進の観測）
	 *
	 *  ここで brk=304・freelist={256B, 16B}（机上算術は test_dcre6.h）．
	 */
	cdtq.dtqcnt = PROBE_DTQCNT;					/*  8B：どちらとも不一致  */
	erid = acre_dtq(&cdtq);
	check_assert(erid > DTQ1);
	dtqid_probe = (ID) erid;

	cdtq.dtqcnt = FILL_DTQCNT;					/*  1728B：残量ちょうど  */
	erid = acre_dtq(&cdtq);
	check_assert(erid > DTQ1);					/*  ★brk が 316 以下だった証拠  */
	dtqid_fill = (ID) erid;

	cdtq.dtqcnt = BOUND_DTQCNT;					/*  4B：残量 0  */
	check_assert(acre_dtq(&cdtq) == E_NOMEM);	/*  ★probe の bump 前進の決定的観測  */
	check_point(4);

	/*
	 *  フェーズD: 全解放 → count==0 backstop の全域リセット
	 */
	check_ercd(del_dtq(dtqid_fill), E_OK);
	check_ercd(del_dtq(dtqid_probe), E_OK);
	check_ercd(del_dtq(dtqid_long), E_OK);		/*  ここで count==0 → リセット  */

	cdtq.dtqcnt = BIG_DTQCNT;					/*  2000B  */
	erid = acre_dtq(&cdtq);
	check_assert(erid > DTQ1);					/*  ★全域リセットの実証  */
	dtqid_big = (ID) erid;
	check_ercd(del_dtq(dtqid_big), E_OK);
	check_point(5);

	check_finish(6);
}
