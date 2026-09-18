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
 *		動的生成API（acre_cyc/del_cyc/acre_alm/del_alm）のテスト
 *
 * 【テストの目的】
 *
 *	(A) acre_cyc（TNFY_HANDLER）→ sta_cyc → 発火 → stp_cyc → del_cyc
 *	(B) 動作中のままの del_cyc が成功すること（dcre 意味論）
 *	(C) 削除済みIDへの sta_cyc/stp_cyc/ref_cyc/msta_cyc が E_NOEXS
 *	(D) スロット枯渇時の E_NOID／不正パラメータの E_PAR
 *	(E) msta_cyc による PRC2 への移動と，PRC2 での発火
 *	(F) 非ハンドラ通知（TNFY_SETVAR）が notify_handler 経由で働くこと
 *	(G) alm 側の acre/sta/再sta/del と E_NOEXS/E_NOID
 *	(H) 静的生成オブジェクトへの del_cyc/del_alm が E_OBJ
 *
 * 【使用リソース】
 *
 *	TASK1: 中優先度タスク，TA_ACT属性（静的・PRC1）
 *	CYC1/ALM1: 静的な周期通知／アラーム通知（TA_NULL・起動しない）
 *	AID_CYC(2)/AID_ALM(2): 動的スロット各2個
 */

#include <kernel.h>
#include <t_syslog.h>
#include <sil.h>
#include "syssvc/test_svc.h"
#include "kernel_cfg.h"
#include "test_dcre2.h"

/* DO NOT DELETE THIS LINE -- gentest depends on it. */

static volatile uint_t		cyc_count;
static volatile uint_t		alm_count;
static volatile intptr_t	nfy_var;
static volatile bool_t		prc2_reported;

/*
 *  静的オブジェクト用のハンドラ（TA_NULL なので発火しない）
 */
void
static_cyclic_handler(EXINF exinf)
{
	check_point(0);			/* 到達したら失敗する */
}

void
static_alarm_handler(EXINF exinf)
{
	check_point(0);			/* 到達したら失敗する */
}

/*
 *  動的周期通知のハンドラ（TNFY_HANDLER）
 */
void
dcyc_handler(EXINF exinf)
{
	check_assert(((intptr_t) exinf) == 0xC1);
	cyc_count++;
}

/*
 *  PRC2 へ移動した周期通知のハンドラ
 *
 *  周期通知は繰り返し発火するので，チェックポイントは初回だけ打つ．
 */
void
dcyc_prc2_handler(EXINF exinf)
{
	ID		prcid;

	sil_get_pid(&prcid);
	check_assert(prcid == 2);
	if (!prc2_reported) {
		prc2_reported = true;
		/*  PRC2 側の check_count[1] は独立カウンタ．本テストで
		 *  PRC2 が打つ最初のチェックポイントなので 1 である．  */
		check_point_prc(1, 2);
	}
}

/*
 *  動的アラーム通知のハンドラ（TNFY_HANDLER）
 */
void
dalm_handler(EXINF exinf)
{
	check_assert(((intptr_t) exinf) == 0xA1);
	alm_count++;
}

void
task1(EXINF exinf)
{
	T_CCYC	ccyc;
	T_CALM	calm;
	T_RCYC	rcyc;
	T_RALM	ralm;
	ER_ID	erid;
	ID		cycid1, cycid2, almid1, almid2;
	uint_t	snapshot;

	test_start(__FILE__);
	check_point(1);

	/*
	 *  1) acre_cyc（TNFY_HANDLER）→ sta_cyc → 発火 → stp_cyc → del_cyc
	 */
	ccyc.cycatr = TA_NULL;
	ccyc.nfyinfo.nfymode = TNFY_HANDLER;
	ccyc.nfyinfo.nfy.handler.exinf = (EXINF) 0xC1;
	ccyc.nfyinfo.nfy.handler.tmehdr = dcyc_handler;
	ccyc.cyctim = CYC_TIME;
	ccyc.cycphs = CYC_TIME;
	erid = acre_cyc(&ccyc);
	check_assert(erid > CYC1);		/* 動的IDは静的レンジの外＝2レンジCYCIDの直接検証 */
	cycid1 = (ID) erid;

	cyc_count = 0;
	check_ercd(sta_cyc(cycid1), E_OK);
	check_ercd(ref_cyc(cycid1, &rcyc), E_OK);
	check_assert(rcyc.cycstat == TCYC_STA);
	check_assert(rcyc.prcid == 1);				/* iprcid=1 固定の検証 */
	check_ercd(dly_tsk(TEST_TIME_PROC), E_OK);
	check_assert(cyc_count >= 1U);
	check_ercd(stp_cyc(cycid1), E_OK);
	check_ercd(ref_cyc(cycid1, &rcyc), E_OK);
	check_assert(rcyc.cycstat == TCYC_STP);
	check_ercd(del_cyc(cycid1), E_OK);
	check_point(2);

	/*
	 *  2) 動作中のままの del_cyc が成功すること（dcre 意味論）
	 */
	erid = acre_cyc(&ccyc);
	check_assert(erid > CYC1);
	cycid1 = (ID) erid;
	cyc_count = 0;
	check_ercd(sta_cyc(cycid1), E_OK);
	check_ercd(dly_tsk(TEST_TIME_PROC), E_OK);
	check_assert(cyc_count >= 1U);
	check_ercd(del_cyc(cycid1), E_OK);		/* 動作中でも削除できる */
	snapshot = cyc_count;
	check_ercd(dly_tsk(TEST_TIME_PROC), E_OK);
	check_assert(cyc_count == snapshot);	/* 削除後は発火しない */
	check_point(3);

	/*
	 *  3) 削除済みIDへのサービスコールが E_NOEXS
	 */
	check_ercd(sta_cyc(cycid1), E_NOEXS);
	check_ercd(stp_cyc(cycid1), E_NOEXS);
	check_ercd(ref_cyc(cycid1, &rcyc), E_NOEXS);
	check_ercd(msta_cyc(cycid1, 1), E_NOEXS);
	check_ercd(del_cyc(cycid1), E_NOEXS);
	check_point(4);

	/*
	 *  4) パラメータ検査（E_PAR）とスロット枯渇（E_NOID）
	 */
	ccyc.cyctim = 0;						/* 0 < cyctim を破る */
	check_assert(acre_cyc(&ccyc) == E_PAR);
	ccyc.cyctim = CYC_TIME;
	ccyc.nfyinfo.nfy.handler.tmehdr = NULL;	/* FUNC_NONNULL を破る（check_nfyinfo） */
	check_assert(acre_cyc(&ccyc) == E_PAR);
	ccyc.nfyinfo.nfy.handler.tmehdr = dcyc_handler;
	ccyc.cycatr = TA_STA | 0x04U;			/* 未定義ビット → E_RSATR */
	check_assert(acre_cyc(&ccyc) == E_RSATR);
	ccyc.cycatr = TA_NULL;

	erid = acre_cyc(&ccyc);
	check_assert(erid > CYC1);
	cycid1 = (ID) erid;
	erid = acre_cyc(&ccyc);
	check_assert(erid > CYC1);
	cycid2 = (ID) erid;
	check_assert(cycid1 != cycid2);
	check_assert(acre_cyc(&ccyc) == E_NOID);	/* スロット2個を使い切った */
	check_ercd(del_cyc(cycid2), E_OK);
	check_ercd(del_cyc(cycid1), E_OK);
	check_point(5);

	/*
	 *  5) msta_cyc による PRC2 への移動（affinity = TOPPERS_TEPP_PRC の実証）
	 */
	ccyc.nfyinfo.nfy.handler.exinf = (EXINF) 0;
	ccyc.nfyinfo.nfy.handler.tmehdr = dcyc_prc2_handler;
	erid = acre_cyc(&ccyc);
	check_assert(erid > CYC1);
	cycid1 = (ID) erid;
	prc2_reported = false;
	check_ercd(msta_cyc(cycid1, 2), E_OK);
	check_ercd(dly_tsk(TEST_TIME_PROC), E_OK);
	check_assert(prc2_reported);			/* PRC2 側が cp(1,2) を打った */
	check_ercd(ref_cyc(cycid1, &rcyc), E_OK);
	check_assert(rcyc.prcid == 2);
	check_ercd(stp_cyc(cycid1), E_OK);
	check_ercd(del_cyc(cycid1), E_OK);
	check_point(6);

	/*
	 *  6) 非ハンドラ通知（TNFY_SETVAR）＝ notify_handler トランポリンの実証
	 */
	nfy_var = 0;
	ccyc.cycatr = TA_NULL;
	ccyc.nfyinfo.nfymode = TNFY_SETVAR;
	ccyc.nfyinfo.nfy.setvar.p_var = (intptr_t *) &nfy_var;
	ccyc.nfyinfo.nfy.setvar.value = 0x5A;
	ccyc.cyctim = CYC_TIME;
	ccyc.cycphs = CYC_TIME;
	erid = acre_cyc(&ccyc);
	check_assert(erid > CYC1);
	cycid1 = (ID) erid;
	check_ercd(sta_cyc(cycid1), E_OK);
	check_ercd(dly_tsk(TEST_TIME_PROC), E_OK);
	check_assert(nfy_var == 0x5A);
	check_ercd(del_cyc(cycid1), E_OK);
	check_point(7);

	/*
	 *  7) alm 側：acre → sta → 発火 → 再 sta → 発火 → del → E_NOEXS/E_NOID
	 */
	calm.almatr = TA_NULL;
	calm.nfyinfo.nfymode = TNFY_HANDLER;
	calm.nfyinfo.nfy.handler.exinf = (EXINF) 0xA1;
	calm.nfyinfo.nfy.handler.tmehdr = dalm_handler;
	erid = acre_alm(&calm);
	check_assert(erid > ALM1);		/* 動的IDは静的レンジの外＝2レンジALMIDの検証 */
	almid1 = (ID) erid;

	alm_count = 0;
	check_ercd(sta_alm(almid1, ALM_TIME), E_OK);
	check_ercd(ref_alm(almid1, &ralm), E_OK);
	check_assert(ralm.almstat == TALM_STA);
	check_assert(ralm.prcid == 1);
	check_ercd(dly_tsk(TEST_TIME_PROC), E_OK);
	check_assert(alm_count == 1U);				/* アラームは1回だけ */
	check_ercd(sta_alm(almid1, ALM_TIME), E_OK);	/* 再起動できる */
	check_ercd(dly_tsk(TEST_TIME_PROC), E_OK);
	check_assert(alm_count == 2U);

	erid = acre_alm(&calm);
	check_assert(erid > ALM1);
	almid2 = (ID) erid;
	check_assert(acre_alm(&calm) == E_NOID);	/* スロット2個を使い切った */
	check_ercd(sta_alm(almid2, ALM_TIME), E_OK);
	check_ercd(del_alm(almid2), E_OK);			/* 動作中でも削除できる */
	check_ercd(del_alm(almid1), E_OK);
	check_ercd(sta_alm(almid1, ALM_TIME), E_NOEXS);
	check_ercd(stp_alm(almid1), E_NOEXS);
	check_ercd(ref_alm(almid1, &ralm), E_NOEXS);
	check_ercd(msta_alm(almid1, ALM_TIME, 1), E_NOEXS);
	check_ercd(del_alm(almid1), E_NOEXS);
	check_point(8);

	/*
	 *  8) 静的生成オブジェクトの削除は E_OBJ
	 */
	check_ercd(del_cyc(CYC1), E_OBJ);
	check_ercd(del_alm(ALM1), E_OBJ);
	check_point(9);

	check_finish(10);
}
