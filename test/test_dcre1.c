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
 *		動的生成機能のテスト(1)
 *
 * 【テストの目的】
 *
 *  FMP3 動的生成API（acre_tsk/del_tsk）の段階1移植が，実機（QEMU）上で
 *  仕様どおりに動作することを確認する．
 *
 * 【テスト項目】
 *
 *	(A) acre_tsk による動的生成 → act_tsk → 自然終了
 *	(B) 生成スロット（AID_TSK(2)）を使い切ってからの del_tsk による解放
 *	    と，解放直後の再 acre_tsk が同一IDを返すこと（free-listはFIFO）
 *	(C) スロット枯渇時の E_NOID
 *	(D) del_tsk のエラー系（静的タスクへの適用 E_OBJ／休止でないタスク
 *	    への適用 E_OBJ）
 *	(E) 強制終了（ter_tsk）後の休止状態遷移と del_tsk の成功
 *	(F) 削除済みIDへのサービスコールが E_NOEXS を返すこと
 *	(G) stk=NULL 自動確保時のプール枯渇 E_NOMEM と成功パス
 *	(H) 動的生成タスクの affinity が全プロセッサであること
 *	    （iprcid=1固定で生成されても mact_tsk で他プロセッサへ移せる）
 *
 * 【使用リソース】
 *
 *	TASK1: 中優先度タスク，TA_ACT属性（静的）
 *	AID_TSK(2): 動的タスクIDスロット2個
 *	DEF_MPK: 動的生成用メモリプール（MPK_SIZE = STACK_SIZE + 1024）
 */

#include <kernel.h>
#include <t_syslog.h>
#include "syssvc/test_svc.h"
#include "kernel_cfg.h"
#include "test_dcre1.h"

/* DO NOT DELETE THIS LINE -- gentest depends on it. */

/*
 *  ユーザ供給スタック（シナリオ1で使用）
 */
static STK_T	user_stk[COUNT_STK_T(STACK_SIZE)];

static ID	dtskid1;
static ID	dtskid2;

/*
 *  動的タスクA（PRC1・ユーザ供給スタック・即終了）
 */
void
dtask_a(EXINF exinf)
{
	ID		prcid;

	check_ercd(get_pid(&prcid), E_OK);
	check_assert(prcid == 1);
	check_assert(((intptr_t) exinf) == 0x11);
	check_point(2);
	ext_tsk();
}

/*
 *  動的タスクB（低優先度・待ち続ける．del_tsk(E_OBJ) の的）
 */
void
dtask_b(EXINF exinf)
{
	check_point(7);
	slp_tsk();				/* 待ち中に ter_tsk で強制終了される（戻ってこない） */
	check_point(0);			/* 到達したら「Unexpected check point 0」で失敗する */
}

/*
 *  動的タスクC（PRC2 で走行することを自ら検証）
 */
void
dtask_prc2(EXINF exinf)
{
	ID		prcid;

	check_ercd(get_pid(&prcid), E_OK);
	check_assert(prcid == 2);
	/*  test_svc.c の check_point_prc は check_count[prcid-1] を使う独立
	 *  カウンタ（PRC1 の check_point とは無関係）なので，PRC2 側の
	 *  シーケンスとしては本テスト初のチェックポイント＝1 になる。  */
	check_point_prc(1, 2);
	check_ercd(wup_tsk(TASK1), E_OK);
	ext_tsk();
}

void
task1(EXINF exinf)
{
	T_CTSK	ctsk;
	ER_ID	erid;
	ER		ercd;
	T_RTSK	rtsk;

	test_start(__FILE__);
	check_point(1);

	/*  1) acre → act → 実行 → 自然終了  */
	ctsk.tskatr = TA_NULL;
	ctsk.exinf = (EXINF) 0x11;
	ctsk.task = dtask_a;
	ctsk.itskpri = HIGH_PRIORITY;
	ctsk.stksz = STACK_SIZE;
	ctsk.stk = user_stk;
	erid = acre_tsk(&ctsk);
	check_assert(erid > TASK1);	/* 動的タスクIDは静的レンジの外＝2レンジTSKIDの直接検証 */
	dtskid1 = (ID) erid;
	check_ercd(act_tsk(dtskid1), E_OK);		/* HIGH が MID を横取り → cp2 */
	check_point(3);

	/*  2) 2個目の生成でスロットを使い切ってから del → 再 acre で同一 ID．
	 *  ★free-list は dcre 忠実移植で FIFO（del_tsk は queue_insert_prev で
	 *    末尾へ・acre_tsk は queue_delete_next で先頭から）．LIFO ではない．
	 *    空きが1個だけの状態での再 acre なら，削除したスロット＝同一 ID の
	 *    返却が FIFO/LIFO どちらでも決定的に成立する．  */
	ctsk.task = dtask_b;
	ctsk.itskpri = LOW_PRIORITY;
	erid = acre_tsk(&ctsk);
	check_assert(erid > 0 && ((ID) erid) != dtskid1);
	dtskid2 = (ID) erid;
	check_ercd(del_tsk(dtskid1), E_OK);
	ctsk.task = dtask_a;
	ctsk.itskpri = HIGH_PRIORITY;
	/*  この再生成タスクは以後activateしない（user_stkをdtask_bと共有しているため，
	 *  activateすると別名スタックで壊れる）．  */
	erid = acre_tsk(&ctsk);
	check_assert(((ID) erid) == dtskid1);
	check_point(4);

	/*  3) スロット枯渇 E_NOID（全スロット使用中に追加を要求）  */
	erid = acre_tsk(&ctsk);
	check_assert(erid == E_NOID);
	check_point(5);

	/*  4) del_tsk のエラー系：静的タスク → E_OBJ / 休止でない → E_OBJ  */
	check_ercd(del_tsk(TASK1), E_OBJ);
	check_point(6);
	check_ercd(act_tsk(dtskid2), E_OK);		/* LOW なので READY のまま */
	check_ercd(del_tsk(dtskid2), E_OBJ);	/* 休止でない */
	check_ercd(dly_tsk(TEST_TIME_PROC), E_OK);	/* LOW に実行機会 → cp7 → slp */

	/*  5) 強制終了 → 休止 → del 成功  */
	check_ercd(ter_tsk(dtskid2), E_OK);		/* 待ち中タスクの強制終了 */
	check_ercd(get_tst(dtskid2, &(rtsk.tskstat)), E_OK);
	check_assert(rtsk.tskstat == TTS_DMT);
	check_ercd(del_tsk(dtskid2), E_OK);
	check_point(8);

	/*  6) 削除済み ID へのサービスコール → E_NOEXS  */
	check_ercd(wup_tsk(dtskid2), E_NOEXS);
	check_ercd(act_tsk(dtskid2), E_NOEXS);
	check_point(9);

	/*  7) stk=NULL 自動確保：プール超過 E_NOMEM と，成功
	 *  ※E_NOMEM を先に試す．逆順だと空きスロットが尽きて E_NOID が先に
	 *    返り，メモリ枯渇経路を通らない（acre_tsk は free_tcb 検査が先）．  */
	ctsk.task = dtask_prc2;
	ctsk.itskpri = HIGH_PRIORITY;
	ctsk.stk = NULL;
	ctsk.stksz = MPK_SIZE * 4;				/* プールに入らない大きさ */
	erid = acre_tsk(&ctsk);
	check_assert(erid == E_NOMEM);
	ctsk.stksz = STACK_SIZE;

	/*
	 *  ★スタックサイズの丸めがあふれる場合は E_PAR
	 *  （hardening パス Task 1 の H-4。ROUND_STK_T(stksz) の加算あふれ）
	 *
	 *  stksz は size_t なので，32bit/64bit のどちらでも到達可能である
	 *  （dtqcnt 等と違って #if のガードが要らない）．検査が無いと丸め結果が
	 *  0 付近へ落ち，aligned_alloc_mpk が「成功」してゼロ長スタックの
	 *  タスクができあがる．
	 */
	ctsk.stksz = SIZE_MAX;
	erid = acre_tsk(&ctsk);
	check_assert(erid == E_PAR);
	ctsk.stksz = STACK_SIZE;

	erid = acre_tsk(&ctsk);
	check_assert(erid > 0);
	dtskid1 = (ID) erid;
	check_point(10);

	/*  8) 全コア affinity の実証：PRC2 へ mact_tsk  */
	check_ercd(mact_tsk(dtskid1, 2), E_OK);
	check_ercd(slp_tsk(), E_OK);			/* dtask_prc2 が cp2-1 → wup */
	/*  PRC1 側の check_point シーケンス（check_count[0]）はここまで
	 *  cp1..cp10 の10回進んでいるので，続きは11（PRC2側の独立カウンタの
	 *  cp2-1 とは無関係）。  */
	check_point(11);
	do {
		check_ercd(get_tst(dtskid1, &(rtsk.tskstat)), E_OK);
	} while (rtsk.tskstat != TTS_DMT);
	check_ercd(del_tsk(dtskid1), E_OK);

	check_finish(12);
}
