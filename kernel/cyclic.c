/*
 *  TOPPERS/FMP Kernel
 *      Toyohashi Open Platform for Embedded Real-Time Systems/
 *      Flexible MultiProcessor Kernel
 * 
 *  Copyright (C) 2000-2003 by Embedded and Real-Time Systems Laboratory
 *                              Toyohashi Univ. of Technology, JAPAN
 *  Copyright (C) 2005-2021 by Embedded and Real-Time Systems Laboratory
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
 *  $Id: cyclic.c 263 2021-01-08 06:08:59Z ertl-honda $
 */

/*
 *		周期通知機能
 */

#include "kernel_impl.h"
#include "check.h"
#include "cyclic.h"
#include "spin_lock.h"

/*
 *  トレースログマクロのデフォルト定義
 */
#ifndef LOG_CYC_ENTER
#define LOG_CYC_ENTER(p_cyccb)
#endif /* LOG_CYC_ENTER */

#ifndef LOG_CYC_LEAVE
#define LOG_CYC_LEAVE(p_cyccb)
#endif /* LOG_CYC_LEAVE */

#ifndef LOG_STA_CYC_ENTER
#define LOG_STA_CYC_ENTER(cycid)
#endif /* LOG_STA_CYC_ENTER */

#ifndef LOG_STA_CYC_LEAVE
#define LOG_STA_CYC_LEAVE(ercd)
#endif /* LOG_STA_CYC_LEAVE */

#ifndef LOG_MSTA_CYC_ENTER
#define LOG_MSTA_CYC_ENTER(cycid, prcid)
#endif /* LOG_MSTA_CYC_ENTER */

#ifndef LOG_MSTA_CYC_LEAVE
#define LOG_MSTA_CYC_LEAVE(ercd)
#endif /* LOG_MSTA_CYC_LEAVE */

#ifndef LOG_CYCMIG
#define LOG_CYCMIG(p_cyccb, src_id, dest_id)
#endif /* LOG_CYCMIG */

#ifndef LOG_STP_CYC_ENTER
#define LOG_STP_CYC_ENTER(cycid)
#endif /* LOG_STP_CYC_ENTER */

#ifndef LOG_STP_CYC_LEAVE
#define LOG_STP_CYC_LEAVE(ercd)
#endif /* LOG_STP_CYC_LEAVE */

#ifndef LOG_REF_CYC_ENTER
#define LOG_REF_CYC_ENTER(cycid, pk_rcyc)
#endif /* LOG_REF_CYC_ENTER */

#ifndef LOG_REF_CYC_LEAVE
#define LOG_REF_CYC_LEAVE(ercd, pk_rcyc)
#endif /* LOG_REF_CYC_LEAVE */

/*
 *  周期通知IDから周期通知管理ブロックを取り出すためのマクロ
 */
#define INDEX_CYC(cycid)	((uint_t)((cycid) - TMIN_CYCID))
#define get_cyccb(cycid)	(p_cyccb_table[INDEX_CYC(cycid)])

/*
 *  周期通知機能の初期化
 */
#ifdef TOPPERS_cycini

/*
 *  使用していない周期通知管理ブロックのリスト
 *
 *  CYCCBの先頭にはキューにつなぐための領域がないため，タイムイベント
 *  ブロック（tmevtb）の領域を用いる．なお64ビット環境ではQUEUEが
 *  tmevtb.callbackまで覆うため，free-listから取り出した側（acre_cyc）
 *  でcallback/argを再設定する必要がある．
 */
QUEUE	free_cyccb;

void
initialize_cyclic(PCB *p_my_pcb)
{
	uint_t	i, j;
	CYCCB	*p_cyccb;
	CYCINIB	*p_cycinib;

	if (p_my_pcb->p_tevtcb == NULL){
		return;
	}

	for (i = 0; i < tnum_scyc; i++) {
		if(cycinib_table[i].iprcid == p_my_pcb->prcid) {
			p_cyccb = p_cyccb_table[i];
			p_cyccb->p_cycinib = &(cycinib_table[i]);
			p_cyccb->tmevtb.callback = (CBACK) call_cyclic;
			p_cyccb->tmevtb.arg = (void *) p_cyccb;
			p_cyccb->p_pcb = p_my_pcb;
			if ((p_cyccb->p_cycinib->cycatr & TA_STA) != 0U) {
				/*
				 *  初回の起動のためのタイムイベントを登録する［ASPD1035］
				 *  ［ASPD1062］．
				 */
				p_cyccb->cycsta = true;
				p_cyccb->tmevtb.evttim = (EVTTIM)(p_cyccb->p_cycinib->cycphs);
				tmevtb_register(&(p_cyccb->tmevtb), p_my_pcb);
			}
			else {
				p_cyccb->cycsta = false;
			}
		}
	}

	/*
	 *  動的生成用スロットの初期化（マスタプロセッサのみ）
	 *
	 *  動的生成された周期通知はiprcid=TOPPERS_MASTER_PRCID固定で生成
	 *  されるため，スロットの初期化もマスタプロセッサが一括して行う．
	 *  他プロセッサへの可視性は，本関数の呼出し後のbarrier_syncが保証
	 *  する（段階1のfree_tcbと同じ論証）．
	 */
	if (p_my_pcb->prcid == TOPPERS_MASTER_PRCID) {
		queue_initialize(&free_cyccb);
		for (i = tnum_scyc, j = 0; i < tnum_cyc; i++, j++) {
			p_cyccb = p_cyccb_table[i];
			p_cycinib = &(acycinib_table[j]);
			p_cycinib->cycatr = TA_NOEXS;
			p_cyccb->p_cycinib = ((const CYCINIB *) p_cycinib);
			p_cyccb->cycsta = false;
			p_cyccb->p_pcb = p_my_pcb;
			p_cyccb->tmevtb.callback = (CBACK) call_cyclic;
			p_cyccb->tmevtb.arg = (void *) p_cyccb;
			queue_insert_prev(&free_cyccb, ((QUEUE *) &(p_cyccb->tmevtb)));
		}
	}
}

#endif /* TOPPERS_cycini */

/*
 *  周期通知の生成
 */
#ifdef TOPPERS_acre_cyc

#ifndef LOG_ACRE_CYC_ENTER
#define LOG_ACRE_CYC_ENTER(pk_ccyc)
#endif /* LOG_ACRE_CYC_ENTER */

#ifndef LOG_ACRE_CYC_LEAVE
#define LOG_ACRE_CYC_LEAVE(ercd)
#endif /* LOG_ACRE_CYC_LEAVE */

ER_ID
acre_cyc(const T_CCYC *pk_ccyc)
{
	CYCCB		*p_cyccb;
	CYCINIB		*p_cycinib;
	ATR			cycatr;
	RELTIM		cyctim, cycphs;
	T_NFYINFO	*p_nfyinfo;
	ER			ercd;

	LOG_ACRE_CYC_ENTER(pk_ccyc);
	CHECK_TSKCTX_UNL();

	cycatr = pk_ccyc->cycatr;
	cyctim = pk_ccyc->cyctim;
	cycphs = pk_ccyc->cycphs;

	CHECK_VALIDATR(cycatr, TA_STA);
	ercd = check_nfyinfo(&(pk_ccyc->nfyinfo));
	if (ercd != E_OK) {
		goto error_exit;
	}
	CHECK_PAR(0 < cyctim && cyctim <= TMAX_RELTIM);
	CHECK_PAR(cycphs <= TMAX_RELTIM);

	lock_cpu();
	acquire_glock();
	if (tnum_cyc == tnum_scyc || queue_empty(&free_cyccb)) {
		ercd = E_NOID;
	}
	else {
		p_cyccb = ((CYCCB *)(((char *) queue_delete_next(&free_cyccb))
											- offsetof(CYCCB, tmevtb)));
		p_cycinib = (CYCINIB *)(p_cyccb->p_cycinib);
		p_cycinib->cycatr = cycatr;
		if (pk_ccyc->nfyinfo.nfymode == TNFY_HANDLER) {
			p_cycinib->exinf = pk_ccyc->nfyinfo.nfy.handler.exinf;
			p_cycinib->nfyhdr = (NFYHDR)(pk_ccyc->nfyinfo.nfy.handler.tmehdr);
		}
		else {
			p_nfyinfo = &acyc_nfyinfo_table[p_cycinib - acycinib_table];
			*p_nfyinfo = pk_ccyc->nfyinfo;
			p_cycinib->exinf = (EXINF) p_nfyinfo;
			p_cycinib->nfyhdr = notify_handler;
		}
		p_cycinib->cyctim = cyctim;
		p_cycinib->cycphs = cycphs;
		/*
		 *  動的生成周期通知の割付けプロセッサ（Global Constraint 4）．
		 *  affinityはTOPPERS_TEPP_PRC（時間イベント処理プロセッサ集合）．
		 *  全プロセッサにするとmsta_cycでp_tevtcb==NULLのプロセッサへ
		 *  移せてしまうため（静的側はcyclic.py:65-68が同じ制約を課す）．
		 */
		p_cycinib->iprcid = TOPPERS_MASTER_PRCID;
		p_cycinib->affinity = ((uint_t) TOPPERS_TEPP_PRC);

		/*
		 *  free-listのリンクにtmevtb領域を転用しているため，64ビット
		 *  環境ではcallbackが上書きされている．必ず再設定する．
		 */
		p_cyccb->p_pcb = get_pcb(TOPPERS_MASTER_PRCID);
		p_cyccb->tmevtb.callback = (CBACK) call_cyclic;
		p_cyccb->tmevtb.arg = (void *) p_cyccb;

		if ((cycatr & TA_STA) != 0U) {
			p_cyccb->cycsta = true;
			tmevtb_enqueue_reltim(&(p_cyccb->tmevtb), cycphs, p_cyccb->p_pcb);
		}
		else {
			p_cyccb->cycsta = false;
		}
		ercd = CYCID(p_cyccb);
	}
	release_glock();
	unlock_cpu();

  error_exit:
	LOG_ACRE_CYC_LEAVE(ercd);
	return(ercd);
}

#endif /* TOPPERS_acre_cyc */

/*
 *  周期通知の削除
 */
#ifdef TOPPERS_del_cyc

#ifndef LOG_DEL_CYC_ENTER
#define LOG_DEL_CYC_ENTER(cycid)
#endif /* LOG_DEL_CYC_ENTER */

#ifndef LOG_DEL_CYC_LEAVE
#define LOG_DEL_CYC_LEAVE(ercd)
#endif /* LOG_DEL_CYC_LEAVE */

ER
del_cyc(ID cycid)
{
	CYCCB	*p_cyccb;
	CYCINIB	*p_cycinib;
	ER		ercd;

	LOG_DEL_CYC_ENTER(cycid);
	CHECK_TSKCTX_UNL();
	CHECK_ID(VALID_CYCID(cycid));
	p_cyccb = get_cyccb(cycid);

	lock_cpu();
	acquire_glock();
	if (p_cyccb->p_cycinib->cycatr == TA_NOEXS) {
		ercd = E_NOEXS;
	}
	else if (cycid <= tmax_scycid) {
		ercd = E_OBJ;
	}
	else {
		/*
		 *  動作中でも削除できる［dcre仕様］．動作中ならタイムイベント
		 *  キューから外してからfree-listへ返却する．dequeueの対象は
		 *  当該周期通知の割付けプロセッサ（stp_cycと同じ手順）．
		 */
		if (p_cyccb->cycsta) {
			p_cyccb->cycsta = false;
			tmevtb_dequeue(&(p_cyccb->tmevtb), p_cyccb->p_pcb);
		}

		p_cycinib = (CYCINIB *)(p_cyccb->p_cycinib);
		p_cycinib->cycatr = TA_NOEXS;
		/*
		 *  p_cyccb->p_pcbはfree-list滞在中staleなまま残るが，acre_cycが
		 *  取り出し時に無条件で再設定する（get_pcb(TOPPERS_MASTER_PRCID)）
		 *  ため支障はない．TA_NOEXS状態のCBのp_pcbを読む経路は存在しない
		 *  ことがこの不変量の前提である（段階2最終レビュー triage ①）．
		 */
		queue_insert_prev(&free_cyccb, ((QUEUE *) &(p_cyccb->tmevtb)));
		ercd = E_OK;
	}
	release_glock();
	unlock_cpu();

  error_exit:
	LOG_DEL_CYC_LEAVE(ercd);
	return(ercd);
}

#endif /* TOPPERS_del_cyc */

/*
 *  周期通知の動作開始
 */
#ifdef TOPPERS_sta_cyc

ER
sta_cyc(ID cycid)
{
	CYCCB	*p_cyccb;
	ER		ercd;

	LOG_STA_CYC_ENTER(cycid);
	CHECK_TSKCTX_UNL();
	CHECK_ID(VALID_CYCID(cycid));
	p_cyccb = get_cyccb(cycid);

	lock_cpu();
	acquire_glock();
	if (p_cyccb->p_cycinib->cycatr == TA_NOEXS) {
		ercd = E_NOEXS;
	}
	else {
		if (p_cyccb->cycsta) {
			tmevtb_dequeue(&(p_cyccb->tmevtb), p_cyccb->p_pcb);
		}
		else {
			p_cyccb->cycsta = true;
		}
		/*
		 *  初回の起動のためのタイムイベントを登録する［ASPD1036］．
		 */
		tmevtb_enqueue_reltim(&(p_cyccb->tmevtb), p_cyccb->p_cycinib->cycphs,
										p_cyccb->p_pcb);
		ercd = E_OK;
	}
	release_glock();
	unlock_cpu();

  error_exit:
	LOG_STA_CYC_LEAVE(ercd);
	return(ercd);
}

#endif /* TOPPERS_sta_cyc */

/*
 *  割付けプロセッサ指定での周期通知の動作開始
 */
#ifdef TOPPERS_msta_cyc

ER
msta_cyc(ID cycid, ID prcid)
{
	CYCCB	*p_cyccb;
	ER		ercd;

	LOG_MSTA_CYC_ENTER(cycid, prcid);
	CHECK_TSKCTX_UNL();
	CHECK_ID(VALID_CYCID(cycid));
	p_cyccb = get_cyccb(cycid);
	if (prcid == TPRC_INI) {
		prcid = p_cyccb->p_cycinib->iprcid;
	}
	else {
		CHECK_PRCID(prcid);
	}
	CHECK_MIG(p_cyccb->p_cycinib->affinity, prcid);

	lock_cpu();
	acquire_glock();
	if (p_cyccb->p_cycinib->cycatr == TA_NOEXS) {
		ercd = E_NOEXS;
	}
	else {
		if (p_cyccb->cycsta) {
			tmevtb_dequeue(&(p_cyccb->tmevtb), p_cyccb->p_pcb);
		}
		else {
			p_cyccb->cycsta = true;
		}
		LOG_CYCMIG(p_cyccb, p_cyccb->p_pcb->prcid, prcid);
		p_cyccb->p_pcb = get_pcb(prcid);
		/*
		 *  初回の起動のためのタイムイベントを登録する［ASPD1036］．
		 */
		tmevtb_enqueue_reltim(&(p_cyccb->tmevtb), p_cyccb->p_cycinib->cycphs,
													p_cyccb->p_pcb);
		ercd = E_OK;
	}
	release_glock();
	unlock_cpu();

  error_exit:
	LOG_MSTA_CYC_LEAVE(ercd);
	return(ercd);
}

#endif /* TOPPERS_msta_cyc */

/*
 *  周期通知の動作停止
 */
#ifdef TOPPERS_stp_cyc

ER
stp_cyc(ID cycid)
{
	CYCCB	*p_cyccb;
	ER		ercd;

	LOG_STP_CYC_ENTER(cycid);
	CHECK_TSKCTX_UNL();
	CHECK_ID(VALID_CYCID(cycid));
	p_cyccb = get_cyccb(cycid);

	lock_cpu();
	acquire_glock();
	if (p_cyccb->p_cycinib->cycatr == TA_NOEXS) {
		ercd = E_NOEXS;
	}
	else {
		if (p_cyccb->cycsta) {
			p_cyccb->cycsta = false;
			tmevtb_dequeue(&(p_cyccb->tmevtb), p_cyccb->p_pcb);
		}
		ercd = E_OK;
	}
	release_glock();
	unlock_cpu();

  error_exit:
	LOG_STP_CYC_LEAVE(ercd);
	return(ercd);
}

#endif /* TOPPERS_stp_cyc */

/*
 *  周期通知の状態参照
 */
#ifdef TOPPERS_ref_cyc

ER
ref_cyc(ID cycid, T_RCYC *pk_rcyc)
{
	CYCCB	*p_cyccb;
	ER		ercd;

	LOG_REF_CYC_ENTER(cycid, pk_rcyc);
	CHECK_TSKCTX_UNL();
	CHECK_ID(VALID_CYCID(cycid));
	p_cyccb = get_cyccb(cycid);

	lock_cpu();
	acquire_glock();
	if (p_cyccb->p_cycinib->cycatr == TA_NOEXS) {
		ercd = E_NOEXS;
	}
	else {
		if (p_cyccb->cycsta) {
			pk_rcyc->cycstat = TCYC_STA;
			pk_rcyc->lefttim = tmevt_lefttim(&(p_cyccb->tmevtb));
		}
		else {
			pk_rcyc->cycstat = TCYC_STP;
		}
		pk_rcyc->prcid = p_cyccb->p_pcb->prcid;
		ercd = E_OK;
	}
	release_glock();
	unlock_cpu();

  error_exit:
	LOG_REF_CYC_LEAVE(ercd, pk_rcyc);
	return(ercd);
}

#endif /* TOPPERS_ref_cyc */

/*
 *  周期通知起動ルーチン
 */
#ifdef TOPPERS_cyccal

void
call_cyclic(PCB *p_my_pcb, CYCCB *p_cyccb)
{
	/*
	 *  次回の起動のためのタイムイベントを登録する［ASPD1037］．
	 *
	 *  tmevtb_enqueueを用いるのが素直であるが，この関数は高分解能タイ
	 *  マ割込みの処理中でのみ呼び出されるため，tmevtb_registerを用い
	 *  ている．
	 */
	p_cyccb->tmevtb.evttim += p_cyccb->p_cycinib->cyctim;	/*［ASPD1038］*/
	tmevtb_register(&(p_cyccb->tmevtb), p_cyccb->p_pcb);

	/*
	 *  通知ハンドラを，CPUロック解除状態で呼び出す．
	 */
	release_glock();
	unlock_cpu();

	LOG_CYC_ENTER(p_cyccb);
	(*(p_cyccb->p_cycinib->nfyhdr))(p_cyccb->p_cycinib->exinf);
	LOG_CYC_LEAVE(p_cyccb);

	if (sense_lock()) {
		force_unlock_spin(p_my_pcb);
	}
	else {
		lock_cpu();
	}
	acquire_glock();
}

#endif /* TOPPERS_cyccal */
