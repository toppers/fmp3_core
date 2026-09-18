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
 *		動的生成API（acre_sem/del_sem・acre_flg/del_flg・acre_mtx/del_mtx）
 *		のテスト
 *
 * 【テストの目的】
 *
 *	(A) acre_sem → sig/wai/pol の基本動作 → 休止資源での del_sem → E_NOEXS ×7
 *	(B) E_DLT 実証（同一プロセッサ）: 高優先度タスクを wai_sem で待たせ，
 *	    del_sem で E_DLT を受け取ることを check_ercd で確認
 *	(C) E_DLT 実証（別プロセッサ）: PRC2 のタスクを wai_sem で待たせ，
 *	    PRC1 から del_sem して E_DLT を受け取る（init_wait_queue の MP 経路）
 *	(D) スロット枯渇 E_NOID／静的オブジェクトへの del が E_OBJ／
 *	    パラメータ検査 E_PAR・E_RSATR／del → 再 acre で同一 ID（決定形）
 *	(E) flg: acre → set/clr/wai/pol/ini → del → E_NOEXS ×8，TA_CLR の実動作
 *	(F) mtx: acre(TA_CEILING) → loc → 現在優先度が上限へ上がる →
 *	    ★ロック中の del_mtx が成功 → 現在優先度がベース優先度へ復帰することを
 *	    get_pri で実測 → 削除済みであること（E_NOEXS ×7）
 *	(G) mtx エラー系（不正 ceilpri で E_PAR，未定義属性ビットで E_RSATR）と
 *	    スロット再利用
 *
 * 【使用リソース】
 *
 *	TASK1: 中優先度タスク，TA_ACT属性（静的・PRC1）
 *	TASK2: 高優先度タスク，TA_NULL属性（静的・PRC1．E_DLT を受け取る）
 *	TASK3: 高優先度タスク，TA_NULL属性（静的・PRC2．E_DLT を受け取る）
 *	SEM1/FLG1/MTX1: 静的なセマフォ／イベントフラグ／ミューテックス
 *	AID_SEM(2)/AID_FLG(1)/AID_MTX(2): 動的スロット
 *
 * 【チェックポイント】
 *
 *	PRC1（check_count[0]，TASK1 と TASK2 が共有）: 1..11 + check_finish(12)
 *	  TASK1 が 1,2,4,6,7,8,9,10,11／TASK2 が 3,5
 *	PRC2（check_count[1]，TASK3）: 1,2
 *	  ※ syssvc/test_svc.c の syslog_2(LOG_NOTICE, "Check point %d-%d passed.",
 *	    prcid, count) は prcid が先・count が後のため，QEMU 出力上は
 *	    「Check point 2-1 passed.」「Check point 2-2 passed.」と表示される
 *	    （test_dcre2 の DIVERGENCE_MAP 記載と同事実。呼び出し自体は
 *	    check_point_prc(count, prcid) で正しい）．
 */

#include <kernel.h>
#include <t_syslog.h>
#include "syssvc/test_svc.h"
#include "kernel_cfg.h"
#include "test_dcre3.h"

/* DO NOT DELETE THIS LINE -- gentest depends on it. */

/*
 *  TASK2 / TASK3 が待つ動的セマフォの ID（TASK1 が設定してから act_tsk する）
 */
static volatile ID		dlt_semid;
static volatile bool_t	prc2_done;

/*
 *  PRC1 上で E_DLT を受け取るタスク
 */
void
task2(EXINF exinf)
{
	check_point(3);
	/*  資源が無いので待ちに入る．TASK1 の del_sem で E_DLT が返る．  */
	check_ercd(wai_sem(dlt_semid), E_DLT);
	check_point(5);
	ext_tsk();
}

/*
 *  PRC2 上で E_DLT を受け取るタスク
 *
 *  PRC2 のチェックポイントカウンタは PRC1 と独立なので 1 から始まる．
 */
void
task3(EXINF exinf)
{
	check_point_prc(1, 2);
	check_ercd(wai_sem(dlt_semid), E_DLT);
	check_point_prc(2, 2);
	prc2_done = true;
	ext_tsk();
}

void
task1(EXINF exinf)
{
	T_CSEM	csem;
	T_CFLG	cflg;
	T_CMTX	cmtx;
	T_RSEM	rsem;
	T_RFLG	rflg;
	T_RMTX	rmtx;
	ER_ID	erid;
	ID		semid1, semid2, flgid1, mtxid1, mtxid2;
	FLGPTN	flgptn;
	PRI		tskpri;

	test_start(__FILE__);
	check_point(1);

	/*
	 *  1) acre_sem → sig/wai/pol の基本動作 → del_sem → E_NOEXS ×6
	 */
	csem.sematr = TA_TPRI;
	csem.isemcnt = 0U;
	csem.maxsem = 2U;
	erid = acre_sem(&csem);
	check_assert(erid > SEM1);		/*  動的IDは静的レンジの外＝2レンジSEMIDの直接検証  */
	semid1 = (ID) erid;

	check_ercd(ref_sem(semid1, &rsem), E_OK);
	check_assert(rsem.semcnt == 0U);
	check_assert(rsem.wtskid == TSK_NONE);
	check_ercd(sig_sem(semid1), E_OK);
	check_ercd(wai_sem(semid1), E_OK);			/*  資源1個あるので即取得  */
	check_ercd(pol_sem(semid1), E_TMOUT);		/*  資源0個  */
	check_ercd(ini_sem(semid1), E_OK);
	check_ercd(del_sem(semid1), E_OK);

	check_ercd(sig_sem(semid1), E_NOEXS);
	check_ercd(wai_sem(semid1), E_NOEXS);
	check_ercd(pol_sem(semid1), E_NOEXS);
	check_ercd(twai_sem(semid1, TMO_POL), E_NOEXS);
	check_ercd(ini_sem(semid1), E_NOEXS);
	check_ercd(ref_sem(semid1, &rsem), E_NOEXS);
	check_ercd(del_sem(semid1), E_NOEXS);
	check_point(2);

	/*
	 *  2) E_DLT 実証（同一プロセッサ PRC1）
	 *
	 *  TASK2 は TASK1 より高優先度なので act_tsk で即座に走り，
	 *  wai_sem で待ちに入ったところで TASK1 に戻ってくる．
	 */
	erid = acre_sem(&csem);						/*  isemcnt = 0  */
	check_assert(erid > SEM1);
	semid1 = (ID) erid;
	dlt_semid = semid1;

	check_ercd(act_tsk(TASK2), E_OK);			/*  → TASK2 が cp(3) を打って待つ  */
	check_point(4);
	check_ercd(ref_sem(semid1, &rsem), E_OK);
	check_assert(rsem.wtskid == TASK2);			/*  待ちタスクが実在する  */
	check_ercd(del_sem(semid1), E_OK);			/*  → TASK2 が E_DLT で起き cp(5) → ext_tsk  */
	check_point(6);
	check_ercd(del_sem(semid1), E_NOEXS);

	/*
	 *  3) E_DLT 実証（別プロセッサ PRC2）＝ init_wait_queue の MP 経路
	 *
	 *  TASK3 は PRC2 で並行に走るため，dly_tsk で待ちに入るのを待つ．
	 */
	erid = acre_sem(&csem);
	check_assert(erid > SEM1);
	semid1 = (ID) erid;
	dlt_semid = semid1;
	prc2_done = false;

	check_ercd(act_tsk(TASK3), E_OK);
	check_ercd(dly_tsk(TEST_TIME_PROC), E_OK);	/*  TASK3 が wai_sem に入るのを待つ  */
	check_ercd(ref_sem(semid1, &rsem), E_OK);
	check_assert(rsem.wtskid == TASK3);
	check_ercd(del_sem(semid1), E_OK);
	check_ercd(dly_tsk(TEST_TIME_PROC), E_OK);	/*  TASK3 が cp(2,2) を打つのを待つ  */
	check_assert(prc2_done);
	check_point(7);

	/*
	 *  4) スロット枯渇 E_NOID／静的への del は E_OBJ／パラメータ検査／再利用
	 */
	erid = acre_sem(&csem);
	check_assert(erid > SEM1);
	semid1 = (ID) erid;
	erid = acre_sem(&csem);
	check_assert(erid > SEM1);
	semid2 = (ID) erid;
	check_assert(semid1 != semid2);
	check_assert(acre_sem(&csem) == E_NOID);	/*  スロット2個を使い切った  */

	check_ercd(del_sem(SEM1), E_OBJ);			/*  静的生成オブジェクト  */

	check_ercd(del_sem(semid2), E_OK);			/*  空きが1個だけの状態を作る  */
	erid = acre_sem(&csem);
	check_assert(erid == semid2);				/*  FIFO/LIFO 不問で決定的  */
	check_ercd(del_sem(semid2), E_OK);
	check_ercd(del_sem(semid1), E_OK);

	csem.maxsem = 0U;							/*  1 <= maxsem を破る  */
	check_assert(acre_sem(&csem) == E_PAR);
	csem.maxsem = 2U;
	csem.isemcnt = 3U;							/*  isemcnt <= maxsem を破る  */
	check_assert(acre_sem(&csem) == E_PAR);
	csem.isemcnt = 0U;
	csem.sematr = TA_TPRI | 0x04U;				/*  未定義ビット → E_RSATR  */
	check_assert(acre_sem(&csem) == E_RSATR);
	csem.sematr = TA_TPRI;
	check_point(8);

	/*
	 *  5) flg: acre → set/clr/wai/pol/ini → del → E_NOEXS ×7
	 */
	cflg.flgatr = TA_TPRI | TA_WMUL | TA_CLR;
	cflg.iflgptn = 0x01U;
	erid = acre_flg(&cflg);
	check_assert(erid > FLG1);					/*  2レンジFLGIDの直接検証  */
	flgid1 = (ID) erid;

	check_ercd(ref_flg(flgid1, &rflg), E_OK);
	check_assert(rflg.flgptn == 0x01U);			/*  iflgptn が反映されている  */
	check_ercd(pol_flg(flgid1, 0x01U, TWF_ORW, &flgptn), E_OK);
	check_assert(flgptn == 0x01U);
	check_ercd(ref_flg(flgid1, &rflg), E_OK);
	check_assert(rflg.flgptn == 0U);			/*  TA_CLR でクリアされた  */
	check_ercd(set_flg(flgid1, 0x02U), E_OK);
	check_ercd(wai_flg(flgid1, 0x02U, TWF_ORW, &flgptn), E_OK);
	check_assert(flgptn == 0x02U);
	check_ercd(set_flg(flgid1, 0x03U), E_OK);
	check_ercd(clr_flg(flgid1, 0x01U), E_OK);
	check_ercd(ref_flg(flgid1, &rflg), E_OK);
	check_assert(rflg.flgptn == 0x01U);
	check_ercd(ini_flg(flgid1), E_OK);
	check_ercd(ref_flg(flgid1, &rflg), E_OK);
	check_assert(rflg.flgptn == 0x01U);			/*  iflgptn に戻る  */

	check_assert(acre_flg(&cflg) == E_NOID);	/*  AID_FLG(1) を使い切っている  */
	check_ercd(del_flg(FLG1), E_OBJ);			/*  静的生成オブジェクト  */
	check_ercd(del_flg(flgid1), E_OK);

	check_ercd(set_flg(flgid1, 0x01U), E_NOEXS);
	check_ercd(clr_flg(flgid1, 0U), E_NOEXS);
	check_ercd(wai_flg(flgid1, 0x01U, TWF_ORW, &flgptn), E_NOEXS);
	check_ercd(pol_flg(flgid1, 0x01U, TWF_ORW, &flgptn), E_NOEXS);
	check_ercd(twai_flg(flgid1, 0x01U, TWF_ORW, &flgptn, TMO_POL), E_NOEXS);
	check_ercd(ini_flg(flgid1), E_NOEXS);
	check_ercd(ref_flg(flgid1, &rflg), E_NOEXS);
	check_ercd(del_flg(flgid1), E_NOEXS);

	cflg.flgatr = TA_TPRI | 0x08U;				/*  未定義ビット → E_RSATR  */
	check_assert(acre_flg(&cflg) == E_RSATR);
	cflg.flgatr = TA_TPRI | TA_WMUL | TA_CLR;
	check_point(9);

	/*
	 *  6) mtx: acre(TA_CEILING) → loc → ★ロック中の del_mtx → 優先度復帰
	 */
	cmtx.mtxatr = TA_CEILING;
	cmtx.ceilpri = HIGH_PRIORITY;
	erid = acre_mtx(&cmtx);
	check_assert(erid > MTX1);					/*  2レンジMTXIDの直接検証  */
	mtxid1 = (ID) erid;

	check_ercd(get_pri(TSK_SELF, &tskpri), E_OK);
	check_assert(tskpri == MID_PRIORITY);		/*  ロック前はベース優先度  */
	check_ercd(loc_mtx(mtxid1), E_OK);
	check_ercd(get_pri(TSK_SELF, &tskpri), E_OK);
	check_assert(tskpri == HIGH_PRIORITY);		/*  上限優先度へ上がった  */
	check_ercd(ref_mtx(mtxid1, &rmtx), E_OK);
	check_assert(rmtx.htskid == TASK1);

	check_ercd(del_mtx(mtxid1), E_OK);			/*  ★ロック中でも削除できる  */
	check_ercd(get_pri(TSK_SELF, &tskpri), E_OK);
	check_assert(tskpri == MID_PRIORITY);		/*  ★ベース優先度へ復帰した  */

	check_ercd(loc_mtx(mtxid1), E_NOEXS);
	check_ercd(ploc_mtx(mtxid1), E_NOEXS);
	check_ercd(tloc_mtx(mtxid1, TMO_POL), E_NOEXS);
	check_ercd(unl_mtx(mtxid1), E_NOEXS);
	check_ercd(ini_mtx(mtxid1), E_NOEXS);
	check_ercd(ref_mtx(mtxid1, &rmtx), E_NOEXS);
	check_ercd(del_mtx(mtxid1), E_NOEXS);
	check_point(10);

	/*
	 *  7) mtx エラー系とスロット再利用
	 */
	cmtx.mtxatr = TA_CEILING;
	cmtx.ceilpri = TMIN_TPRI - 1;				/*  VALID_TPRI を破る（下限外）  */
	check_assert(acre_mtx(&cmtx) == E_PAR);
	cmtx.ceilpri = TMAX_TPRI + 1;				/*  VALID_TPRI を破る（上限外）  */
	check_assert(acre_mtx(&cmtx) == E_PAR);
	cmtx.mtxatr = TA_TPRI | 0x08U;				/*  未定義ビット → E_RSATR  */
	cmtx.ceilpri = HIGH_PRIORITY;
	check_assert(acre_mtx(&cmtx) == E_RSATR);

	cmtx.mtxatr = TA_TPRI;						/*  ceilpri は参照されない  */
	erid = acre_mtx(&cmtx);
	check_assert(erid > MTX1);
	mtxid1 = (ID) erid;
	erid = acre_mtx(&cmtx);
	check_assert(erid > MTX1);
	mtxid2 = (ID) erid;
	check_assert(mtxid1 != mtxid2);
	check_assert(acre_mtx(&cmtx) == E_NOID);	/*  スロット2個を使い切った  */

	check_ercd(del_mtx(MTX1), E_OBJ);			/*  静的生成オブジェクト  */

	check_ercd(del_mtx(mtxid2), E_OK);			/*  空きが1個だけの状態を作る  */
	erid = acre_mtx(&cmtx);
	check_assert(erid == mtxid2);				/*  FIFO/LIFO 不問で決定的  */
	check_ercd(del_mtx(mtxid2), E_OK);
	check_ercd(del_mtx(mtxid1), E_OK);
	check_point(11);

	check_finish(12);
}
