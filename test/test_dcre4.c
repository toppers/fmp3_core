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
 *		動的生成API（acre_dtq/del_dtq・acre_pdq/del_pdq・acre_mpf/del_mpf）
 *		のテスト
 *
 * 【テストの目的】
 *
 *	(A) acre_dtq → psnd/prcv/fsnd の実通信（データ整合）→ del_dtq →
 *	    E_NOEXS ×9．dtqcnt==0 のデータキュー（管理領域なし）も覆う．
 *	(B) E_NOMEM の実証と E_NOID との順序：同じ過大な dtqcnt が
 *	    「スロットが空いていれば E_NOMEM」「枯渇していれば E_NOID」に
 *	    なることを実測し，E_NOMEM のとき free-list が減っていないことを
 *	    直後の acre_dtq ×2 の成功で示す．
 *	(C) 送信待ちの E_DLT（満杯 dtq に snd_dtq で待つ TASK2・同一プロセッサ）と
 *	    受信待ちの E_DLT（空 dtq に rcv_dtq で待つ TASK3・別プロセッサ）の両方．
 *	    del_dtq が swait_queue と rwait_queue の両方を解放することの実証．
 *	(D) del → 再 acre 後のキュー状態リセットの確認：データを入れたまま del →
 *	    再 acre で count/head/tail が初期化されること（acre_dtq 側のリセットの
 *	    実証であり，del_dtq 側の滞留データ破棄そのものは公開 API から観測不能
 *	    なため判別できない — 既知の検査限界）．
 *	(E) pdq: acre → 優先度順配送 → maxdpri 検査（ロック内へ移した分岐）→
 *	    del → E_NOEXS ×8．VALID_DPRI による acre_pdq の E_PAR．
 *	(F) dtq のスロット枯渇 E_NOID／静的オブジェクトへの del が E_OBJ／
 *	    「空きが1個だけの状態で del → 再 acre」による決定形の同一 ID／
 *	    ★ユーザ供給 dtqmb（TA_MBALLOC を立てない経路）の生成・通信・削除．
 *	(G) mpf: acre（mpf=NULL 自動確保）→ pget/get/rel → ★ブロック内容の
 *	    書込み読出し → 未返却ブロックを持ったままの del → E_NOEXS ×6 →
 *	    ブロック領域が入らない blksz*blkcnt での E_NOMEM →
 *	    ★acre/del の反復（MPF_CYCLES 回）でプールが実際に返っていること．
 *
 * 【使用リソース】
 *
 *	TASK1: 中優先度タスク，TA_ACT属性（静的・PRC1）
 *	TASK2: 高優先度タスク，TA_NULL属性（静的・PRC1．送信待ちの E_DLT）
 *	TASK3: 高優先度タスク，TA_NULL属性（静的・PRC2．受信待ちの E_DLT）
 *	DTQ1/PDQ1/MPF1: 静的なデータキュー／優先度データキュー／固定長メモリプール
 *	AID_DTQ(2)/AID_PDQ(1)/AID_MPF(1): 動的スロット
 *	DEF_MPK({ MPK_SIZE, NULL }): 管理領域の確保元（必須）
 *
 * 【チェックポイント】
 *
 *	PRC1（check_count[0]，TASK1 と TASK2 が共有）: 1..12 + check_finish(13)
 *	  TASK1 が 1,2,3,5,7,8,9,10,11,12／TASK2 が 4,6
 *	PRC2（check_count[1]，TASK3）: 1,2（出力は "Check point 2-1/2-2 passed."）
 *	  ＝ログ中の "Check point" 行は合計 15 本（check_finish 自身の 1 本を含む）
 */

#include <kernel.h>
#include <t_syslog.h>
#include "syssvc/test_svc.h"
#include "kernel_cfg.h"
#include "test_dcre4.h"

/* DO NOT DELETE THIS LINE -- gentest depends on it. */

/*
 *  TASK2 / TASK3 が待つ動的データキューの ID（TASK1 が設定してから act_tsk する）
 */
static volatile ID		dlt_dtqid;
static volatile bool_t	prc2_done;

/*
 *  ユーザ供給のデータキュー管理領域
 *
 *  acre_dtq に dtqmb を渡す経路（TA_MBALLOC を立てない経路）の検査に使う．
 *  del_dtq がこれを free_mpk に渡してはならない．
 *
 *  ★DTQMB 型（kernel/dataqueue.h）はカーネル内部ヘッダにありアプリからは
 *  <kernel.h> 経由で見えないため，等価な intptr_t 1個の配列を代わりに使う
 *  （DTQMB は intptr_t data; 1個の構造体なのでサイズ・アラインとも等価）．
 */
static intptr_t			user_dtqmb_area[USER_DTQCNT];

/*
 *  PRC1 上で送信待ちの E_DLT を受け取るタスク
 */
void
task2(EXINF exinf)
{
	check_point(4);
	/*  データキューが満杯なので送信待ちに入る．TASK1 の del_dtq で E_DLT．  */
	check_ercd(snd_dtq(dlt_dtqid, 0xB1), E_DLT);
	check_point(6);
	ext_tsk();
}

/*
 *  PRC2 上で受信待ちの E_DLT を受け取るタスク
 *
 *  PRC2 のチェックポイントカウンタは PRC1 と独立なので 1 から始まる．
 */
void
task3(EXINF exinf)
{
	intptr_t	data;

	check_point_prc(1, 2);
	/*  データキューが空なので受信待ちに入る．TASK1 の del_dtq で E_DLT．  */
	check_ercd(rcv_dtq(dlt_dtqid, &data), E_DLT);
	check_point_prc(2, 2);
	prc2_done = true;
	ext_tsk();
}

void
task1(EXINF exinf)
{
	T_CDTQ		cdtq;
	T_CPDQ		cpdq;
	T_CMPF		cmpf;
	T_RDTQ		rdtq;
	T_RPDQ		rpdq;
	T_RMPF		rmpf;
	ER_ID		erid;
	ID			dtqid1, dtqid2, pdqid1, mpfid1;
	intptr_t	data;
	PRI			datapri;
	void		*blk1;
	void		*blk2;
	uint_t		i;

	test_start(__FILE__);
	check_point(1);

	/*
	 *  1) acre_dtq → 実通信 → del_dtq → E_NOEXS ×9
	 */
	cdtq.dtqatr = TA_TPRI;
	cdtq.dtqcnt = 2U;
	cdtq.dtqmb = NULL;
	erid = acre_dtq(&cdtq);
	check_assert(erid > DTQ1);		/*  動的IDは静的レンジの外＝2レンジDTQIDの直接検証  */
	dtqid1 = (ID) erid;

	check_ercd(ref_dtq(dtqid1, &rdtq), E_OK);
	check_assert(rdtq.sdtqcnt == 0U);
	check_assert(rdtq.stskid == TSK_NONE);
	check_assert(rdtq.rtskid == TSK_NONE);
	check_ercd(psnd_dtq(dtqid1, 0x11), E_OK);
	check_ercd(psnd_dtq(dtqid1, 0x22), E_OK);
	check_ercd(psnd_dtq(dtqid1, 0x33), E_TMOUT);	/*  満杯（dtqcnt=2）  */
	check_ercd(ref_dtq(dtqid1, &rdtq), E_OK);
	check_assert(rdtq.sdtqcnt == 2U);
	check_ercd(prcv_dtq(dtqid1, &data), E_OK);
	check_assert(data == 0x11);						/*  FIFO 順で取り出される  */
	check_ercd(prcv_dtq(dtqid1, &data), E_OK);
	check_assert(data == 0x22);
	check_ercd(prcv_dtq(dtqid1, &data), E_TMOUT);	/*  空  */
	check_ercd(fsnd_dtq(dtqid1, 0x44), E_OK);
	check_ercd(prcv_dtq(dtqid1, &data), E_OK);
	check_assert(data == 0x44);
	check_ercd(ini_dtq(dtqid1), E_OK);
	check_ercd(del_dtq(dtqid1), E_OK);

	check_ercd(snd_dtq(dtqid1, 0x11), E_NOEXS);
	check_ercd(psnd_dtq(dtqid1, 0x11), E_NOEXS);
	check_ercd(tsnd_dtq(dtqid1, 0x11, TMO_POL), E_NOEXS);
	check_ercd(fsnd_dtq(dtqid1, 0x11), E_NOEXS);
	check_ercd(rcv_dtq(dtqid1, &data), E_NOEXS);
	check_ercd(prcv_dtq(dtqid1, &data), E_NOEXS);
	check_ercd(trcv_dtq(dtqid1, &data, TMO_POL), E_NOEXS);
	check_ercd(ini_dtq(dtqid1), E_NOEXS);
	check_ercd(ref_dtq(dtqid1, &rdtq), E_NOEXS);
	check_ercd(del_dtq(dtqid1), E_NOEXS);

	/*
	 *  dtqcnt == 0 のデータキュー（管理領域を持たない）
	 *
	 *  ★fsnd_dtq の E_ILUSE をロック内へ移した効果（訂正E）の直接検証：
	 *    生存中は E_ILUSE，削除後は（残留 dtqcnt が 0 でも）E_NOEXS になる．
	 *    ロック前の CHECK_ILUSE が残っていると削除後も E_ILUSE が返る．
	 */
	cdtq.dtqcnt = 0U;
	erid = acre_dtq(&cdtq);
	check_assert(erid > DTQ1);
	dtqid2 = (ID) erid;
	check_ercd(fsnd_dtq(dtqid2, 0x55), E_ILUSE);
	check_ercd(psnd_dtq(dtqid2, 0x55), E_TMOUT);	/*  容量0・受信待ち無し  */
	check_ercd(del_dtq(dtqid2), E_OK);
	check_ercd(fsnd_dtq(dtqid2, 0x55), E_NOEXS);	/*  ★E_ILUSE ではない  */
	check_point(2);

	/*
	 *  2) E_NOMEM の実証と E_NOID との順序
	 *
	 *  sizeof(DTQMB) は 4（32bit）または 8（64bit）なので
	 *  sizeof(DTQMB) * MPK_SIZE は MPK_SIZE の 4〜8 倍＝プールに入らない．
	 *  size_t の桁あふれも起きない大きさである（訂正G-3）．
	 */
	cdtq.dtqcnt = MPK_SIZE;
	check_assert(acre_dtq(&cdtq) == E_NOMEM);

	/*  E_NOMEM で free-list が減っていないこと＝直後に2個とも取れる  */
	cdtq.dtqcnt = 1U;
	erid = acre_dtq(&cdtq);
	check_assert(erid > DTQ1);
	dtqid1 = (ID) erid;
	erid = acre_dtq(&cdtq);
	check_assert(erid > DTQ1);
	dtqid2 = (ID) erid;
	check_assert(dtqid1 != dtqid2);

	/*  ★同じ過大な dtqcnt でも，枯渇していれば E_NOMEM ではなく E_NOID  */
	cdtq.dtqcnt = MPK_SIZE;
	check_assert(acre_dtq(&cdtq) == E_NOID);
	cdtq.dtqcnt = 1U;
	check_ercd(del_dtq(dtqid2), E_OK);
	check_ercd(del_dtq(dtqid1), E_OK);
	check_point(3);

	/*
	 *  3) 送信待ちの E_DLT（同一プロセッサ PRC1）
	 *
	 *  TASK2 は TASK1 より高優先度なので act_tsk で即座に走り，
	 *  満杯のデータキューへの snd_dtq で待ちに入ったところで TASK1 へ戻る．
	 */
	cdtq.dtqcnt = 1U;
	erid = acre_dtq(&cdtq);
	check_assert(erid > DTQ1);
	dtqid1 = (ID) erid;
	check_ercd(psnd_dtq(dtqid1, 0xA1), E_OK);	/*  満杯にする  */
	dlt_dtqid = dtqid1;

	check_ercd(act_tsk(TASK2), E_OK);			/*  → TASK2 が cp(4) を打って待つ  */
	check_point(5);
	check_ercd(ref_dtq(dtqid1, &rdtq), E_OK);
	check_assert(rdtq.stskid == TASK2);			/*  送信待ちタスクが実在する  */
	check_ercd(del_dtq(dtqid1), E_OK);			/*  → TASK2 が E_DLT で起き cp(6) → ext_tsk  */
	check_point(7);
	check_ercd(del_dtq(dtqid1), E_NOEXS);

	/*
	 *  4) 受信待ちの E_DLT（別プロセッサ PRC2）＝ init_wait_queue の MP 経路
	 *
	 *  TASK3 は PRC2 で並行に走るため，dly_tsk で待ちに入るのを待つ．
	 */
	erid = acre_dtq(&cdtq);
	check_assert(erid > DTQ1);
	dtqid1 = (ID) erid;
	dlt_dtqid = dtqid1;
	prc2_done = false;

	check_ercd(act_tsk(TASK3), E_OK);
	check_ercd(dly_tsk(TEST_TIME_PROC), E_OK);	/*  TASK3 が rcv_dtq に入るのを待つ  */
	check_ercd(ref_dtq(dtqid1, &rdtq), E_OK);
	check_assert(rdtq.rtskid == TASK3);			/*  受信待ちタスクが実在する  */
	check_ercd(del_dtq(dtqid1), E_OK);
	check_ercd(dly_tsk(TEST_TIME_PROC), E_OK);	/*  TASK3 が cp(2,2) を打つのを待つ  */
	check_assert(prc2_done);
	check_point(8);

	/*
	 *  5) del → 再 acre 後のキュー状態リセットの確認
	 *
	 *  ※本検査が実証するのは「再 acre 時に acre_dtq がキュー状態
	 *    （count/head/tail）を初期化すること」である。del_dtq 側の滞留データ
	 *    破棄そのものは公開 API から観測不能（count が全読出しをゲートする）
	 *    ため、本検査では判別できない — 既知の検査限界として記録。
	 */
	cdtq.dtqcnt = 2U;
	erid = acre_dtq(&cdtq);
	check_assert(erid > DTQ1);
	dtqid1 = (ID) erid;
	check_ercd(psnd_dtq(dtqid1, 0xD1), E_OK);
	check_ercd(psnd_dtq(dtqid1, 0xD2), E_OK);
	check_ercd(ref_dtq(dtqid1, &rdtq), E_OK);
	check_assert(rdtq.sdtqcnt == 2U);
	check_ercd(del_dtq(dtqid1), E_OK);			/*  滞留データを持ったまま削除  */

	erid = acre_dtq(&cdtq);
	check_assert(erid > DTQ1);
	dtqid2 = (ID) erid;
	check_ercd(ref_dtq(dtqid2, &rdtq), E_OK);
	check_assert(rdtq.sdtqcnt == 0U);			/*  ★acre_dtq がcountを初期化する（del側の破棄は本検査では判別不能）  */
	check_ercd(prcv_dtq(dtqid2, &data), E_TMOUT);
	check_ercd(del_dtq(dtqid2), E_OK);
	check_point(9);

	/*
	 *  6) pdq: 優先度順配送・maxdpri 検査・del → E_NOEXS ×8
	 */
	cpdq.pdqatr = TA_TPRI;
	cpdq.pdqcnt = 3U;
	cpdq.maxdpri = 3;
	cpdq.pdqmb = NULL;
	erid = acre_pdq(&cpdq);
	check_assert(erid > PDQ1);					/*  2レンジPDQIDの直接検証  */
	pdqid1 = (ID) erid;

	check_ercd(psnd_pdq(pdqid1, 0x30, 3), E_OK);
	check_ercd(psnd_pdq(pdqid1, 0x10, 1), E_OK);
	check_ercd(psnd_pdq(pdqid1, 0x20, 2), E_OK);
	check_ercd(ref_pdq(pdqid1, &rpdq), E_OK);
	check_assert(rpdq.spdqcnt == 3U);
	check_ercd(prcv_pdq(pdqid1, &data, &datapri), E_OK);
	check_assert(data == 0x10);					/*  ★優先度順に配送される  */
	check_assert(datapri == 1);
	check_ercd(prcv_pdq(pdqid1, &data, &datapri), E_OK);
	check_assert(data == 0x20);
	check_assert(datapri == 2);
	check_ercd(prcv_pdq(pdqid1, &data, &datapri), E_OK);
	check_assert(data == 0x30);
	check_assert(datapri == 3);
	check_ercd(prcv_pdq(pdqid1, &data, &datapri), E_TMOUT);

	check_ercd(psnd_pdq(pdqid1, 0x40, 4), E_PAR);	/*  maxdpri=3 を超える（ロック内の検査）  */
	check_ercd(psnd_pdq(pdqid1, 0x40, 0), E_PAR);	/*  TMIN_DPRI 未満（ロック前の検査）  */
	check_ercd(ini_pdq(pdqid1), E_OK);

	check_assert(acre_pdq(&cpdq) == E_NOID);	/*  AID_PDQ(1) を使い切っている  */
	check_ercd(del_pdq(PDQ1), E_OBJ);			/*  静的生成オブジェクト  */
	check_ercd(del_pdq(pdqid1), E_OK);

	check_ercd(snd_pdq(pdqid1, 0x10, 1), E_NOEXS);
	check_ercd(psnd_pdq(pdqid1, 0x10, 1), E_NOEXS);
	check_ercd(tsnd_pdq(pdqid1, 0x10, 1, TMO_POL), E_NOEXS);
	check_ercd(rcv_pdq(pdqid1, &data, &datapri), E_NOEXS);
	check_ercd(prcv_pdq(pdqid1, &data, &datapri), E_NOEXS);
	check_ercd(trcv_pdq(pdqid1, &data, &datapri, TMO_POL), E_NOEXS);
	check_ercd(ini_pdq(pdqid1), E_NOEXS);
	check_ercd(ref_pdq(pdqid1, &rpdq), E_NOEXS);
	check_ercd(del_pdq(pdqid1), E_NOEXS);
	/*
	 *  ★maxdpri 検査をロック内へ移した効果（訂正E）の直接検証：
	 *    削除済みスロットの残留 maxdpri（=3）を超える datapri でも
	 *    E_PAR ではなく E_NOEXS が返る．
	 */
	check_ercd(psnd_pdq(pdqid1, 0x40, 4), E_NOEXS);

	cpdq.pdqcnt = MPK_SIZE;
	check_assert(acre_pdq(&cpdq) == E_NOMEM);
	cpdq.pdqcnt = 3U;
	cpdq.maxdpri = TMAX_DPRI + 1;				/*  VALID_DPRI を破る（上限外）  */
	check_assert(acre_pdq(&cpdq) == E_PAR);
	cpdq.maxdpri = TMIN_DPRI - 1;				/*  VALID_DPRI を破る（下限外）  */
	check_assert(acre_pdq(&cpdq) == E_PAR);
	cpdq.maxdpri = 3;
	cpdq.pdqatr = TA_TPRI | 0x04U;				/*  未定義ビット → E_RSATR  */
	check_assert(acre_pdq(&cpdq) == E_RSATR);
	cpdq.pdqatr = TA_TPRI;

	erid = acre_pdq(&cpdq);
	check_assert(erid == pdqid1);				/*  空きが1個だけ＝決定形  */
	check_ercd(del_pdq(pdqid1), E_OK);
	check_point(10);

	/*
	 *  7) dtq の枯渇 E_NOID／静的 E_OBJ／決定形の再 acre／ユーザ供給 dtqmb
	 */
	cdtq.dtqatr = TA_TPRI;
	cdtq.dtqcnt = 1U;
	cdtq.dtqmb = NULL;
	erid = acre_dtq(&cdtq);
	check_assert(erid > DTQ1);
	dtqid1 = (ID) erid;
	erid = acre_dtq(&cdtq);
	check_assert(erid > DTQ1);
	dtqid2 = (ID) erid;
	check_assert(dtqid1 != dtqid2);
	check_assert(acre_dtq(&cdtq) == E_NOID);	/*  スロット2個を使い切った  */

	check_ercd(del_dtq(DTQ1), E_OBJ);			/*  静的生成オブジェクト  */

	check_ercd(del_dtq(dtqid2), E_OK);			/*  空きが1個だけの状態を作る  */
	erid = acre_dtq(&cdtq);
	check_assert(erid == dtqid2);				/*  FIFO/LIFO 不問で決定的  */
	check_ercd(del_dtq(dtqid2), E_OK);
	check_ercd(del_dtq(dtqid1), E_OK);

	cdtq.dtqatr = TA_TPRI | 0x04U;				/*  未定義ビット → E_RSATR  */
	check_assert(acre_dtq(&cdtq) == E_RSATR);
	cdtq.dtqatr = TA_TPRI;

	/*
	 *  ★ユーザ供給の管理領域（TA_MBALLOC を立てない経路）
	 *
	 *  del_dtq がこの静的配列を free_mpk に渡してはならない．渡すと
	 *  プールの count が壊れ，以後の acre がプールを使い切って
	 *  E_NOMEM になる（手順8のループが検出する）．
	 */
	cdtq.dtqcnt = USER_DTQCNT;
	cdtq.dtqmb = (void *) user_dtqmb_area;
	erid = acre_dtq(&cdtq);
	check_assert(erid > DTQ1);
	dtqid1 = (ID) erid;
	check_ercd(psnd_dtq(dtqid1, 0xE1), E_OK);
	check_ercd(psnd_dtq(dtqid1, 0xE2), E_OK);
	check_ercd(prcv_dtq(dtqid1, &data), E_OK);
	check_assert(data == 0xE1);
	check_ercd(prcv_dtq(dtqid1, &data), E_OK);
	check_assert(data == 0xE2);
	check_ercd(del_dtq(dtqid1), E_OK);
	cdtq.dtqmb = NULL;
	check_point(11);

	/*
	 *  8) mpf: 自動確保・ブロックの読み書き・削除・E_NOEXS ×6・
	 *     E_NOMEM・プール解放の反復実証
	 */
	cmpf.mpfatr = TA_TPRI;
	cmpf.blkcnt = MPF_BLKCNT;
	cmpf.blksz = MPF_BLKSZ;
	cmpf.mpf = NULL;							/*  ブロック領域を自動確保  */
	cmpf.mpfmb = NULL;							/*  管理領域を自動確保  */
	erid = acre_mpf(&cmpf);
	check_assert(erid > MPF1);					/*  2レンジMPFIDの直接検証  */
	mpfid1 = (ID) erid;

	check_ercd(ref_mpf(mpfid1, &rmpf), E_OK);
	check_assert(rmpf.fblkcnt == MPF_BLKCNT);
	check_assert(rmpf.wtskid == TSK_NONE);
	check_ercd(pget_mpf(mpfid1, &blk1), E_OK);
	check_ercd(pget_mpf(mpfid1, &blk2), E_OK);
	check_assert(blk1 != blk2);

	/*  ★確保したブロック領域が本当に読み書きできること  */
	for (i = 0U; i < MPF_BLKSZ; i++) {
		((char *) blk1)[i] = (char)(i + 1U);
		((char *) blk2)[i] = (char)(0x80U - i);
	}
	for (i = 0U; i < MPF_BLKSZ; i++) {
		check_assert(((char *) blk1)[i] == (char)(i + 1U));
		check_assert(((char *) blk2)[i] == (char)(0x80U - i));
	}

	check_ercd(ref_mpf(mpfid1, &rmpf), E_OK);
	check_assert(rmpf.fblkcnt == MPF_BLKCNT - 2);
	check_ercd(rel_mpf(mpfid1, blk2), E_OK);
	check_ercd(rel_mpf(mpfid1, blk1), E_OK);
	check_ercd(ref_mpf(mpfid1, &rmpf), E_OK);
	check_assert(rmpf.fblkcnt == MPF_BLKCNT);
	check_ercd(get_mpf(mpfid1, &blk1), E_OK);	/*  空きがあるので待たない  */
	check_ercd(rel_mpf(mpfid1, blk1), E_OK);
	check_ercd(ini_mpf(mpfid1), E_OK);

	check_assert(acre_mpf(&cmpf) == E_NOID);	/*  AID_MPF(1) を使い切っている  */
	check_ercd(del_mpf(MPF1), E_OBJ);			/*  静的生成オブジェクト  */

	check_ercd(pget_mpf(mpfid1, &blk1), E_OK);	/*  未返却のブロックを持ったまま削除する  */
	check_ercd(del_mpf(mpfid1), E_OK);

	check_ercd(get_mpf(mpfid1, &blk2), E_NOEXS);
	check_ercd(pget_mpf(mpfid1, &blk2), E_NOEXS);
	check_ercd(tget_mpf(mpfid1, &blk2, TMO_POL), E_NOEXS);
	/*
	 *  ★rel_mpf の検査をロック内へ移した効果（訂正E）の直接検証：
	 *    削除済みプールでは p_mpfinib->mpf も p_mpfmb も free_mpk 済みの
	 *    番地である．ロック前に p_mpfmb[blkidx] を読む実装では未定義動作
	 *    になるが，正しい実装は何も触らずに E_NOEXS を返す．
	 */
	check_ercd(rel_mpf(mpfid1, blk1), E_NOEXS);
	check_ercd(ini_mpf(mpfid1), E_NOEXS);
	check_ercd(ref_mpf(mpfid1, &rmpf), E_NOEXS);
	check_ercd(del_mpf(mpfid1), E_NOEXS);

	/*  ブロック領域が入らない大きさ → E_NOMEM  */
	cmpf.blkcnt = MPK_SIZE;
	cmpf.blksz = 32U;
	check_assert(acre_mpf(&cmpf) == E_NOMEM);

	/*
	 *  ★2段確保の②で失敗したときの巻き戻し（段階3b Task 5 の未実証経路）
	 *
	 *  上の E_NOMEM は①（ブロック領域）の時点で失敗するので，②の失敗と
	 *  巻き戻しの経路（mempfix.c の pk_cmpf->mpf 判定）を通らない．ここは
	 *  「①は入るが①+②は入らない」サイジングで②を失敗させる．
	 *
	 *  【算術】カーネルメモリプールの実効容量は
	 *    MPK_SIZE(2048) - sizeof(MEMPOOLCB)(16) = 2032 バイト
	 *  である（32bit ターゲット・freelist 実装）．各割付けには 4B の
	 *  ヘッダ（MPHDR）が付く．ROUND_MPF_T(4) == sizeof(MPF_T) == 4，
	 *  sizeof(MPFMB) == 4 なので，
	 *
	 *    ケースA: blkcnt=400 → ① 4+4*400=1604 ≤ 2032（成功）
	 *                          ② 4+4*400=1604 > 2032-1604=428（失敗）→ E_NOMEM
	 *    ケースB: blkcnt=200 → ① 4+4*200=804 ＋ ② 4+4*200=804
	 *                          = 1608 ≤ 2032（成功）
	 *
	 *  【なぜケースBが巻き戻しの証拠になるか】
	 *  kernel/startup.c の free_mempool は，count が 0 になったときに
	 *  brk を先頭へ戻し freelist を空にする（backstop）．この検査の直前
	 *  で count は 0・freelist は空なので，ケースA/B の割付けはすべて
	 *  bump 経路を通る（freelist の完全一致再利用はここでは効かない）．
	 *  ケースAで①が巻き戻されれば count は 1→0 に戻り backstop が効く
	 *  ので，直後のケースB（計1608B）は入る．巻き戻しが無ければ count
	 *  は 1 のまま，残量は 428B しかなく，ケースBは①（804B）の時点で
	 *  E_NOMEM になる．
	 *  ★したがって「ケースBが成功すること」が①の解放の直接の観測である．
	 *
	 *  ★このサイジングは 32bit ターゲット（musca_b1）の値である．本テストは
	 *  musca_b1-2core でのみ実行される（DIVERGENCE_MAP.md 参照）．
	 */
	cmpf.blkcnt = 400U;							/*  ケースA  */
	cmpf.blksz = 4U;
	check_assert(acre_mpf(&cmpf) == E_NOMEM);	/*  ②で失敗 → ①を巻き戻す  */

	cmpf.blkcnt = 200U;							/*  ケースB  */
	cmpf.blksz = 4U;
	erid = acre_mpf(&cmpf);
	check_assert(erid == mpfid1);				/*  ★①が返っていなければ E_NOMEM  */
	check_ercd(ref_mpf(mpfid1, &rmpf), E_OK);
	check_assert(rmpf.fblkcnt == 200U);
	check_ercd(del_mpf(mpfid1), E_OK);

	/*  E_NOMEM で free-list が減っていないこと＝同じスロットが取れる  */
	cmpf.blkcnt = MPF_BLKCNT;
	cmpf.blksz = MPF_BLKSZ;
	erid = acre_mpf(&cmpf);
	check_assert(erid == mpfid1);
	check_ercd(del_mpf(mpfid1), E_OK);

	cmpf.blkcnt = 0U;							/*  blkcnt != 0 を破る  */
	check_assert(acre_mpf(&cmpf) == E_PAR);
	cmpf.blkcnt = MPF_BLKCNT;
	cmpf.blksz = 0U;							/*  blksz != 0 を破る  */
	check_assert(acre_mpf(&cmpf) == E_PAR);
	cmpf.blksz = MPF_BLKSZ;
	cmpf.mpfatr = TA_TPRI | 0x04U;				/*  未定義ビット → E_RSATR  */
	check_assert(acre_mpf(&cmpf) == E_RSATR);
	cmpf.mpfatr = TA_TPRI;

	/*
	 *  ★サイズ計算のあふれ検査（hardening パス Task 1 で追加した CHECK_PAR）
	 *
	 *  dtqcnt/pdqcnt/blkcnt は uint_t，sizeof(DTQMB) 等は size_t である．
	 *  両者が同幅のターゲット（32bit）でのみ積があふれうるので，検査に
	 *  到達できるのも 32bit ターゲットに限る．64bit では同じ入力が検査を
	 *  素通りして E_NOMEM になる（あふれないので正しい挙動である）．
	 *
	 *  ★この #if が丸ごと消えていて 1 行も実行されていない，という事態を
	 *  防ぐため，#if の外で同じ条件を実行時にも確かめる（下の check_assert）．
	 *  本テストは musca_b1-2core でのみ実行するので，ここは必ず真である．
	 */
	check_assert(sizeof(size_t) == sizeof(uint_t));

#if SIZE_MAX == UINT_MAX
	/*  (a) acre_dtq: dtqcnt * sizeof(DTQMB) があふれる  */
	cdtq.dtqcnt = UINT_MAX;
	check_assert(acre_dtq(&cdtq) == E_PAR);

	/*  (b) acre_pdq: pdqcnt * sizeof(PDQMB) があふれる  */
	cpdq.pdqcnt = UINT_MAX;
	check_assert(acre_pdq(&cpdq) == E_PAR);

	/*  (c) acre_mpf: ROUND_MPF_T(blksz) * blkcnt があふれる  */
	cmpf.blkcnt = UINT_MAX;
	cmpf.blksz = MPF_BLKSZ;
	check_assert(acre_mpf(&cmpf) == E_PAR);

	/*  (d) acre_mpf: ROUND_MPF_T(blksz) の丸めそのものがあふれる
	 *      （検査が無いと丸め結果が 0 になり，blksz==0 のプールができる）  */
	cmpf.blkcnt = MPF_BLKCNT;
	cmpf.blksz = UINT_MAX;
	check_assert(acre_mpf(&cmpf) == E_PAR);
#endif /* SIZE_MAX == UINT_MAX */

	/*  以降の手順のためにパケットを元へ戻す  */
	cdtq.dtqcnt = 1U;
	cpdq.pdqcnt = 3U;
	cmpf.blkcnt = MPF_BLKCNT;
	cmpf.blksz = MPF_BLKSZ;

	/*
	 *  ★プールが実際に返っていることの実証
	 *
	 *  del_mpf が TA_MEMALLOC のブロック領域を free_mpk しなければ，
	 *  ブロック領域（4+ROUND_MPF_T(64)*4 = 260B）が周回ごとに返らず
	 *  count が 0 に戻らないため，brk の全域リセット（backstop）も
	 *  256B の完全一致再利用（freelist に載るのは管理領域 16B だけ）も
	 *  効かず，brk が単調に進んで MPK_SIZE を数周で使い切り acre_mpf が
	 *  E_NOMEM になる．正常な実装では1周ごとに count が 0 に戻り brk が
	 *  リセットされる（freelist 実装でも backstop として不変）ので，
	 *  MPF_CYCLES 周まわしても消費は頭打ちである．
	 */
	for (i = 0U; i < MPF_CYCLES; i++) {
		erid = acre_mpf(&cmpf);
		check_assert(erid > MPF1);
		mpfid1 = (ID) erid;
		check_ercd(pget_mpf(mpfid1, &blk1), E_OK);
		check_ercd(rel_mpf(mpfid1, blk1), E_OK);
		check_ercd(del_mpf(mpfid1), E_OK);
	}
	check_point(12);

	check_finish(13);
}
