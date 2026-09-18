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
 *  $Id: dataqueue.c 263 2021-01-08 06:08:59Z ertl-honda $
 */

/*
 *		データキュー機能
 */

#include "kernel_impl.h"
#include "check.h"
#include "task.h"
#include "wait.h"
#include "dataqueue.h"

/*
 *  トレースログマクロのデフォルト定義
 */
#ifndef LOG_SND_DTQ_ENTER
#define LOG_SND_DTQ_ENTER(dtqid, data)
#endif /* LOG_SND_DTQ_ENTER */

#ifndef LOG_SND_DTQ_LEAVE
#define LOG_SND_DTQ_LEAVE(ercd)
#endif /* LOG_SND_DTQ_LEAVE */

#ifndef LOG_PSND_DTQ_ENTER
#define LOG_PSND_DTQ_ENTER(dtqid, data)
#endif /* LOG_PSND_DTQ_ENTER */

#ifndef LOG_PSND_DTQ_LEAVE
#define LOG_PSND_DTQ_LEAVE(ercd)
#endif /* LOG_PSND_DTQ_LEAVE */

#ifndef LOG_TSND_DTQ_ENTER
#define LOG_TSND_DTQ_ENTER(dtqid, data, tmout)
#endif /* LOG_TSND_DTQ_ENTER */

#ifndef LOG_TSND_DTQ_LEAVE
#define LOG_TSND_DTQ_LEAVE(ercd)
#endif /* LOG_TSND_DTQ_LEAVE */

#ifndef LOG_FSND_DTQ_ENTER
#define LOG_FSND_DTQ_ENTER(dtqid, data)
#endif /* LOG_FSND_DTQ_ENTER */

#ifndef LOG_FSND_DTQ_LEAVE
#define LOG_FSND_DTQ_LEAVE(ercd)
#endif /* LOG_FSND_DTQ_LEAVE */

#ifndef LOG_RCV_DTQ_ENTER
#define LOG_RCV_DTQ_ENTER(dtqid, p_data)
#endif /* LOG_RCV_DTQ_ENTER */

#ifndef LOG_RCV_DTQ_LEAVE
#define LOG_RCV_DTQ_LEAVE(ercd, p_data)
#endif /* LOG_RCV_DTQ_LEAVE */

#ifndef LOG_PRCV_DTQ_ENTER
#define LOG_PRCV_DTQ_ENTER(dtqid, p_data)
#endif /* LOG_PRCV_DTQ_ENTER */

#ifndef LOG_PRCV_DTQ_LEAVE
#define LOG_PRCV_DTQ_LEAVE(ercd, p_data)
#endif /* LOG_PRCV_DTQ_LEAVE */

#ifndef LOG_TRCV_DTQ_ENTER
#define LOG_TRCV_DTQ_ENTER(dtqid, p_data, tmout)
#endif /* LOG_TRCV_DTQ_ENTER */

#ifndef LOG_TRCV_DTQ_LEAVE
#define LOG_TRCV_DTQ_LEAVE(ercd, p_data)
#endif /* LOG_TRCV_DTQ_LEAVE */

#ifndef LOG_INI_DTQ_ENTER
#define LOG_INI_DTQ_ENTER(dtqid)
#endif /* LOG_INI_DTQ_ENTER */

#ifndef LOG_INI_DTQ_LEAVE
#define LOG_INI_DTQ_LEAVE(ercd)
#endif /* LOG_INI_DTQ_LEAVE */

#ifndef LOG_REF_DTQ_ENTER
#define LOG_REF_DTQ_ENTER(dtqid, pk_rdtq)
#endif /* LOG_REF_DTQ_ENTER */

#ifndef LOG_REF_DTQ_LEAVE
#define LOG_REF_DTQ_LEAVE(ercd, pk_rdtq)
#endif /* LOG_REF_DTQ_LEAVE */

/*
 *  データキューIDからデータキュー管理ブロックを取り出すためのマクロ
 */
#define INDEX_DTQ(dtqid)	((uint_t)((dtqid) - TMIN_DTQID))
#define get_dtqcb(dtqid)	(p_dtqcb_table[INDEX_DTQ(dtqid)])

/*
 *  データキュー機能の初期化
 */
#ifdef TOPPERS_dtqini

/*
 *  使用していないデータキュー管理ブロックのリスト
 *
 *  DTQCBの先頭フィールドがQUEUE（swait_queue）なので，そのまま
 *  free-listのリンクに流用する．acre_dtqは取り出した直後に
 *  queue_initializeで作り直すため，TA_TPRI（優先度順待ちキュー）と
 *  干渉しない．
 */
QUEUE	free_dtqcb;

void
initialize_dataqueue(PCB *p_my_pcb)
{
	uint_t	i, j;
	DTQCB	*p_dtqcb;
	DTQINIB	*p_dtqinib;

	if (p_my_pcb->prcid == TOPPERS_MASTER_PRCID) {
		for (i = 0; i < tnum_sdtq; i++) {
			p_dtqcb = p_dtqcb_table[i];
			queue_initialize(&(p_dtqcb->swait_queue));
			p_dtqcb->p_dtqinib = &(dtqinib_table[i]);
			queue_initialize(&(p_dtqcb->rwait_queue));
			p_dtqcb->count = 0U;
			p_dtqcb->head = 0U;
			p_dtqcb->tail = 0U;
		}

		/*
		 *  動的生成用スロットの初期化
		 *
		 *  データキューはプロセッサ親和を持たない（DTQINIBに
		 *  iprcid/affinityが無く，DTQCBにp_pcbが無い）ため，段階2の
		 *  cyc/almのようなプロセッサ判定や充填は一切不要である．本関数は
		 *  元からマスタプロセッサ限定なので，そのブロックの中で続けて
		 *  初期化する．他プロセッサへの可視性は，本関数の呼出し後の
		 *  barrier_syncが保証する（段階1のfree_tcbと同じ論証）．
		 */
		queue_initialize(&free_dtqcb);
		for (j = 0; i < tnum_dtq; i++, j++) {
			p_dtqcb = p_dtqcb_table[i];
			p_dtqinib = &(adtqinib_table[j]);
			p_dtqinib->dtqatr = TA_NOEXS;
			p_dtqcb->p_dtqinib = ((const DTQINIB *) p_dtqinib);
			queue_insert_prev(&free_dtqcb, &(p_dtqcb->swait_queue));
		}
	}
}

#endif /* TOPPERS_dtqini */

/*
 *  データキュー管理領域へのデータの格納
 */
#ifdef TOPPERS_dtqenq

void
enqueue_data(DTQCB *p_dtqcb, intptr_t data)
{
	(p_dtqcb->p_dtqinib->p_dtqmb + p_dtqcb->tail)->data = data;
	p_dtqcb->count++;
	p_dtqcb->tail++;
	if (p_dtqcb->tail >= p_dtqcb->p_dtqinib->dtqcnt) {
		p_dtqcb->tail = 0U;
	}
}

#endif /* TOPPERS_dtqenq */

/*
 *  データキュー管理領域へのデータの強制格納
 */
#ifdef TOPPERS_dtqfenq

void
force_enqueue_data(DTQCB *p_dtqcb, intptr_t data)
{
	(p_dtqcb->p_dtqinib->p_dtqmb + p_dtqcb->tail)->data = data;
	p_dtqcb->tail++;
	if (p_dtqcb->tail >= p_dtqcb->p_dtqinib->dtqcnt) {
		p_dtqcb->tail = 0U;
	}
	if (p_dtqcb->count < p_dtqcb->p_dtqinib->dtqcnt) {
		p_dtqcb->count++;
	}
	else {
		p_dtqcb->head = p_dtqcb->tail;
	}
}

#endif /* TOPPERS_dtqfenq */

/*
 *  データキュー管理領域からのデータの取出し
 */
#ifdef TOPPERS_dtqdeq

void
dequeue_data(DTQCB *p_dtqcb, intptr_t *p_data)
{
	*p_data = (p_dtqcb->p_dtqinib->p_dtqmb + p_dtqcb->head)->data;
	p_dtqcb->count--;
	p_dtqcb->head++;
	if (p_dtqcb->head >= p_dtqcb->p_dtqinib->dtqcnt) {
		p_dtqcb->head = 0U;
	}
}

#endif /* TOPPERS_dtqdeq */

/*
 *  データキューへのデータ送信
 */
#ifdef TOPPERS_dtqsnd

bool_t
send_data(PCB *p_my_pcb, DTQCB *p_dtqcb, intptr_t data)
{
	TCB		*p_tcb;

	if (!queue_empty(&(p_dtqcb->rwait_queue))) {
		p_tcb = (TCB *) queue_delete_next(&(p_dtqcb->rwait_queue));
		p_tcb->winfo_obj.rdtq.data = data;
		wait_complete(p_my_pcb, p_tcb);
		return(true);
	}
	else if (p_dtqcb->count < p_dtqcb->p_dtqinib->dtqcnt) {
		enqueue_data(p_dtqcb, data);
		return(true);
	}
	else {
		return(false);
	}
}

#endif /* TOPPERS_dtqsnd */

/*
 *  データキューへのデータ強制送信
 */
#ifdef TOPPERS_dtqfsnd

void
force_send_data(PCB *p_my_pcb, DTQCB *p_dtqcb, intptr_t data)
{
	TCB		*p_tcb;

	if (!queue_empty(&(p_dtqcb->rwait_queue))) {
		p_tcb = (TCB *) queue_delete_next(&(p_dtqcb->rwait_queue));
		p_tcb->winfo_obj.rdtq.data = data;
		wait_complete(p_my_pcb, p_tcb);
	}
	else {
		force_enqueue_data(p_dtqcb, data);
	}
}

#endif /* TOPPERS_dtqfsnd */

/*
 *  データキューからのデータ受信
 */
#ifdef TOPPERS_dtqrcv

bool_t
receive_data(PCB *p_my_pcb, DTQCB *p_dtqcb, intptr_t *p_data)
{
	TCB		*p_tcb;
	intptr_t data;

	if (p_dtqcb->count > 0U) {
		dequeue_data(p_dtqcb, p_data);
		if (!queue_empty(&(p_dtqcb->swait_queue))) {
			p_tcb = (TCB *) queue_delete_next(&(p_dtqcb->swait_queue));
			data = p_tcb->winfo_obj.sdtq.data;
			enqueue_data(p_dtqcb, data);
			wait_complete(p_my_pcb, p_tcb);
		}
		return(true);
	}
	else if (!queue_empty(&(p_dtqcb->swait_queue))) {
		p_tcb = (TCB *) queue_delete_next(&(p_dtqcb->swait_queue));
		*p_data = p_tcb->winfo_obj.sdtq.data;
		wait_complete(p_my_pcb, p_tcb);
		return(true);
	}
	else {
		return(false);
	}
}

#endif /* TOPPERS_dtqrcv */

/*
 *  データキューの生成
 */
#ifdef TOPPERS_acre_dtq

#ifndef LOG_ACRE_DTQ_ENTER
#define LOG_ACRE_DTQ_ENTER(pk_cdtq)
#endif /* LOG_ACRE_DTQ_ENTER */

#ifndef LOG_ACRE_DTQ_LEAVE
#define LOG_ACRE_DTQ_LEAVE(ercd)
#endif /* LOG_ACRE_DTQ_LEAVE */

ER_ID
acre_dtq(const T_CDTQ *pk_cdtq)
{
	DTQCB	*p_dtqcb;
	DTQINIB	*p_dtqinib;
	ATR		dtqatr;
	uint_t	dtqcnt;
	DTQMB	*p_dtqmb;
	ER		ercd;

	LOG_ACRE_DTQ_ENTER(pk_cdtq);
	CHECK_TSKCTX_UNL();

	dtqatr = pk_cdtq->dtqatr;
	dtqcnt = pk_cdtq->dtqcnt;
	p_dtqmb = pk_cdtq->dtqmb;

	CHECK_VALIDATR(dtqatr, TA_TPRI);

	/*
	 *  管理領域のサイズ計算があふれないこと
	 *
	 *  ★dcre（extension/dcre/kernel/dataqueue.c）にこの検査は無い．
	 *  dtqcntはuint_t（unsigned int），sizeof(DTQMB)はsize_tである．
	 *  size_tとunsigned intが同幅のターゲット（32bit）では，両者の積が
	 *  size_tの中であふれ，要求よりはるかに小さい領域の確保に成功して
	 *  しまう（その領域にdtqcnt個のDTQMBは入らない）．enqueue_dataは
	 *  p_dtqinib->dtqcntを信じてp_dtqmb[tail]へ書くので，プール外破壊に
	 *  なる．64bitターゲットではuint_tの最大値でも積があふれないため，
	 *  この検査は恒真に落ちる（無害）．
	 *
	 *  検査はロック取得前に置く．引数だけから決まりコア非依存である
	 *  （既存のCHECK_VALIDATR/MB_ALIGNと同じ位置づけ）．
	 */
	CHECK_PAR(dtqcnt <= (SIZE_MAX / sizeof(DTQMB)));

	if (p_dtqmb != NULL) {
		CHECK_PAR(MB_ALIGN(p_dtqmb));
	}

	lock_cpu();
	acquire_glock();
	if (tnum_dtq == tnum_sdtq || queue_empty(&free_dtqcb)) {
		ercd = E_NOID;
	}
	else {
		/*
		 *  管理領域の確保
		 *
		 *  dtqcntが0のデータキューは管理領域を必要としない（データを
		 *  1個も保持できず，送信は必ず受信待ちタスクへ直接渡すか待ちに
		 *  なる）．ユーザがdtqmbを与えた場合はそれを使い，TA_MBALLOCを
		 *  立てない（del_dtqがfree_mpkしてはならないため）．
		 *
		 *  ★E_NOMEMのときfree-listからCBを取り出していないことが重要
		 *  である（確保に成功してから初めてqueue_delete_nextする）．
		 *  段階1のacre_tskと同じ順序．
		 */
		if (dtqcnt != 0 && p_dtqmb == NULL) {
			p_dtqmb = malloc_mpk(sizeof(DTQMB) * dtqcnt);
			dtqatr |= TA_MBALLOC;
		}
		if (dtqcnt != 0 && p_dtqmb == NULL) {
			ercd = E_NOMEM;
		}
		else {
			p_dtqcb = ((DTQCB *) queue_delete_next(&free_dtqcb));
			p_dtqinib = (DTQINIB *)(p_dtqcb->p_dtqinib);
			p_dtqinib->dtqatr = dtqatr;
			p_dtqinib->dtqcnt = dtqcnt;
			p_dtqinib->p_dtqmb = p_dtqmb;

			queue_initialize(&(p_dtqcb->swait_queue));
			queue_initialize(&(p_dtqcb->rwait_queue));
			p_dtqcb->count = 0U;
			p_dtqcb->head = 0U;
			p_dtqcb->tail = 0U;
			ercd = DTQID(p_dtqcb);
		}
	}
	release_glock();
	unlock_cpu();

  error_exit:
	LOG_ACRE_DTQ_LEAVE(ercd);
	return(ercd);
}

#endif /* TOPPERS_acre_dtq */

/*
 *  データキューの削除
 */
#ifdef TOPPERS_del_dtq

#ifndef LOG_DEL_DTQ_ENTER
#define LOG_DEL_DTQ_ENTER(dtqid)
#endif /* LOG_DEL_DTQ_ENTER */

#ifndef LOG_DEL_DTQ_LEAVE
#define LOG_DEL_DTQ_LEAVE(ercd)
#endif /* LOG_DEL_DTQ_LEAVE */

ER
del_dtq(ID dtqid)
{
	DTQCB	*p_dtqcb;
	DTQINIB	*p_dtqinib;
	ER		ercd;
	TCB		*p_selftsk;
	PCB		*p_my_pcb;

	LOG_DEL_DTQ_ENTER(dtqid);
	CHECK_TSKCTX_UNL_MYSTATE(&p_selftsk);
	CHECK_ID(VALID_DTQID(dtqid));
	p_dtqcb = get_dtqcb(dtqid);

	lock_cpu();
	acquire_glock();
	p_my_pcb = get_my_pcb();
	if (p_dtqcb->p_dtqinib->dtqatr == TA_NOEXS) {
		ercd = E_NOEXS;
	}
	else if (dtqid <= tmax_sdtqid) {
		ercd = E_OBJ;
	}
	else {
		/*
		 *  待ちタスクがいても削除は成功し，送信待ち・受信待ちの両方の
		 *  タスクがE_DLTで強制解除される．init_wait_queueはMP対応済み
		 *  （wait.c:215-228）で，既存のini_dtqと同一の機構である．
		 *  新規の解除機構は書かない．管理領域に滞留していたデータは
		 *  破棄される（countをクリアせずCBごとfree-listへ戻すが，
		 *  acre_dtqが取り出し時にcount/head/tailを0に初期化する）．
		 */
		init_wait_queue(p_my_pcb, &(p_dtqcb->swait_queue));
		init_wait_queue(p_my_pcb, &(p_dtqcb->rwait_queue));
		p_dtqinib = (DTQINIB *)(p_dtqcb->p_dtqinib);
		/*
		 *  ★順序制約：属性の読みはTA_NOEXSの書込みより前で行う．
		 *  TA_NOEXSは((ATR)(-1))＝全ビットが1であるため，
		 *  TA_NOEXSを書いた後では(dtqatr & TA_MBALLOC) != 0Uが
		 *  必ず真になり，ユーザ供給の管理領域まで解放してしまう
		 *  （プールの破壊）．dcre dataqueue.c:427-430 も同じ順序．
		 */
		if ((p_dtqinib->dtqatr & TA_MBALLOC) != 0U) {
			free_mpk(p_dtqinib->p_dtqmb);
		}
		p_dtqinib->dtqatr = TA_NOEXS;
		queue_insert_prev(&free_dtqcb, &(p_dtqcb->swait_queue));
		if (p_selftsk != p_my_pcb->p_schedtsk) {
			release_glock();
			dispatch();
			ercd = E_OK;
			goto unlock_and_exit;
		}
		ercd = E_OK;
	}
	release_glock();
  unlock_and_exit:
	unlock_cpu();

  error_exit:
	LOG_DEL_DTQ_LEAVE(ercd);
	return(ercd);
}

#endif /* TOPPERS_del_dtq */

/*
 *  データキューへの送信
 */
#ifdef TOPPERS_snd_dtq

ER
snd_dtq(ID dtqid, intptr_t data)
{
	DTQCB	*p_dtqcb;
	ER		ercd;
	TCB		*p_selftsk;
	PCB		*p_my_pcb;

	LOG_SND_DTQ_ENTER(dtqid, data);
	CHECK_DISPATCH();
	CHECK_ID(VALID_DTQID(dtqid));
	p_dtqcb = get_dtqcb(dtqid);

	lock_cpu_dsp();
	acquire_glock();
	p_my_pcb = get_my_pcb();
	p_selftsk = p_my_pcb->p_runtsk;
	if (p_dtqcb->p_dtqinib->dtqatr == TA_NOEXS) {
		ercd = E_NOEXS;
	}
	else if (p_selftsk->raster) {
		ercd = E_RASTER;
	}
	else if (send_data(p_my_pcb, p_dtqcb, data)) {
		if (p_selftsk != p_my_pcb->p_schedtsk) {
			release_glock();
			dispatch();
			ercd = E_OK;
			goto unlock_and_exit;
		}
		ercd = E_OK;
	}
	else {
		p_selftsk->winfo_obj.sdtq.data = data;
		wobj_make_wait(p_my_pcb, (WOBJCB *) p_dtqcb, TS_WAITING_SDTQ,
															p_selftsk);
		release_glock();
		dispatch();
		ercd = p_selftsk->winfo.wercd;
		goto unlock_and_exit;
	}
	release_glock();
  unlock_and_exit:
	unlock_cpu_dsp();

  error_exit:
	LOG_SND_DTQ_LEAVE(ercd);
	return(ercd);
}

#endif /* TOPPERS_snd_dtq */

/*
 *  データキューへの送信（ポーリング）
 */
#ifdef TOPPERS_psnd_dtq

ER
psnd_dtq(ID dtqid, intptr_t data)
{
	DTQCB	*p_dtqcb;
	ER		ercd;
	TCB		*p_selftsk;
	bool_t	context;
	PCB		*p_my_pcb;

	LOG_PSND_DTQ_ENTER(dtqid, data);
	CHECK_UNL_MYSTATE(&p_selftsk, &context);
	CHECK_ID(VALID_DTQID(dtqid));
	p_dtqcb = get_dtqcb(dtqid);

	lock_cpu();
	acquire_glock();
	p_my_pcb = get_my_pcb();
	if (p_dtqcb->p_dtqinib->dtqatr == TA_NOEXS) {
		ercd = E_NOEXS;
	}
	else if (send_data(p_my_pcb, p_dtqcb, data)) {
		if (p_selftsk != p_my_pcb->p_schedtsk) {
			if (!context) {
				release_glock();
				dispatch();
				ercd = E_OK;
				goto unlock_and_exit;
			}
			else {
				request_dispatch_retint();
			}
		}
		ercd = E_OK;
	}
	else {
		ercd = E_TMOUT;
	}
	release_glock();
  unlock_and_exit:
	unlock_cpu();

  error_exit:
	LOG_PSND_DTQ_LEAVE(ercd);
	return(ercd);
}

#endif /* TOPPERS_psnd_dtq */

/*
 *  データキューへの送信（タイムアウトあり）
 */
#ifdef TOPPERS_tsnd_dtq

ER
tsnd_dtq(ID dtqid, intptr_t data, TMO tmout)
{
	DTQCB	*p_dtqcb;
	ER		ercd;
	TCB		*p_selftsk;
	PCB		*p_my_pcb;

	LOG_TSND_DTQ_ENTER(dtqid, data, tmout);
	CHECK_DISPATCH_MYSTATE(&p_selftsk);
	CHECK_ID(VALID_DTQID(dtqid));
	CHECK_PAR(VALID_TMOUT(tmout));
	p_dtqcb = get_dtqcb(dtqid);

	lock_cpu_dsp();
	acquire_glock();
	p_my_pcb = get_my_pcb();
	if (p_dtqcb->p_dtqinib->dtqatr == TA_NOEXS) {
		ercd = E_NOEXS;
	}
	else if (p_selftsk->raster) {
		ercd = E_RASTER;
	}
	else if (send_data(p_my_pcb, p_dtqcb, data)) {
		if (p_selftsk != p_my_pcb->p_schedtsk) {
			release_glock();
			dispatch();
			ercd = E_OK;
			goto unlock_and_exit;
		}
		ercd = E_OK;
	}
	else if (tmout == TMO_POL) {
		ercd = E_TMOUT;
	}
	else {
		p_selftsk->winfo_obj.sdtq.data = data;
		wobj_make_wait_tmout(p_my_pcb, (WOBJCB *) p_dtqcb, TS_WAITING_SDTQ,
															p_selftsk, tmout);
		release_glock();
		dispatch();
		ercd = p_selftsk->winfo.wercd;
		goto unlock_and_exit;
	}
	release_glock();
  unlock_and_exit:
	unlock_cpu_dsp();

  error_exit:
	LOG_TSND_DTQ_LEAVE(ercd);
	return(ercd);
}

#endif /* TOPPERS_tsnd_dtq */

/*
 *  データキューへの強制送信
 */
#ifdef TOPPERS_fsnd_dtq

ER
fsnd_dtq(ID dtqid, intptr_t data)
{
	DTQCB	*p_dtqcb;
	ER		ercd;
	TCB		*p_selftsk;
	bool_t	context;
	PCB		*p_my_pcb;

	LOG_FSND_DTQ_ENTER(dtqid, data);
	CHECK_UNL_MYSTATE(&p_selftsk, &context);
	CHECK_ID(VALID_DTQID(dtqid));
	p_dtqcb = get_dtqcb(dtqid);

	lock_cpu();
	acquire_glock();
	p_my_pcb = get_my_pcb();
	if (p_dtqcb->p_dtqinib->dtqatr == TA_NOEXS) {
		ercd = E_NOEXS;
	}
	else if (!(p_dtqcb->p_dtqinib->dtqcnt > 0U)) {
		/*
		 *  ★dcreに倣い，dtqcntの検査をロック取得前のCHECK_ILUSEから
		 *  ロック内のこの位置へ移した（dcre dataqueue.c:604-606）．
		 *  E_NOEXSゲートより前にp_dtqinibを読むと，削除済み
		 *  （TA_NOEXS）スロットの残留dtqcntを読むことになるため．
		 */
		ercd = E_ILUSE;
	}
	else {
		force_send_data(p_my_pcb, p_dtqcb, data);
		if (p_selftsk != p_my_pcb->p_schedtsk) {
			if (!context) {
				release_glock();
				dispatch();
				ercd = E_OK;
				goto unlock_and_exit;
			}
			else {
				request_dispatch_retint();
			}
		}
		ercd = E_OK;
	}
	release_glock();
  unlock_and_exit:
	unlock_cpu();

  error_exit:
	LOG_FSND_DTQ_LEAVE(ercd);
	return(ercd);
}

#endif /* TOPPERS_fsnd_dtq */

/*
 *  データキューからの受信
 */
#ifdef TOPPERS_rcv_dtq

ER
rcv_dtq(ID dtqid, intptr_t *p_data)
{
	DTQCB	*p_dtqcb;
	ER		ercd;
	TCB		*p_selftsk;
	PCB		*p_my_pcb;

	LOG_RCV_DTQ_ENTER(dtqid, p_data);
	CHECK_DISPATCH_MYSTATE(&p_selftsk);
	CHECK_ID(VALID_DTQID(dtqid));
	p_dtqcb = get_dtqcb(dtqid);

	lock_cpu_dsp();
	acquire_glock();
	p_my_pcb = get_my_pcb();
	if (p_dtqcb->p_dtqinib->dtqatr == TA_NOEXS) {
		ercd = E_NOEXS;
	}
	else if (p_selftsk->raster) {
		ercd = E_RASTER;
	}
	else if (receive_data(p_my_pcb, p_dtqcb, p_data)) {
		if (p_selftsk != p_my_pcb->p_schedtsk) {
			release_glock();
			dispatch();
			ercd = E_OK;
			goto unlock_and_exit;
		}
		ercd = E_OK;
	}
	else {
		make_wait(p_my_pcb, TS_WAITING_RDTQ, p_selftsk);
		queue_insert_prev(&(p_dtqcb->rwait_queue), &(p_selftsk->task_queue));
		p_selftsk->p_wobjcb = (WOBJCB *) p_dtqcb;
		LOG_TSKSTAT(p_selftsk);
		release_glock();
		dispatch();
		ercd = p_selftsk->winfo.wercd;
		if (ercd == E_OK) {
			*p_data = p_selftsk->winfo_obj.rdtq.data;
		}
		goto unlock_and_exit;
	}
	release_glock();
  unlock_and_exit:
	unlock_cpu_dsp();

  error_exit:
	LOG_RCV_DTQ_LEAVE(ercd, p_data);
	return(ercd);
}

#endif /* TOPPERS_rcv_dtq */

/*
 *  データキューからの受信（ポーリング）
 */
#ifdef TOPPERS_prcv_dtq

ER
prcv_dtq(ID dtqid, intptr_t *p_data)
{
	DTQCB	*p_dtqcb;
	ER		ercd;
	TCB		*p_selftsk;
	PCB		*p_my_pcb;

	LOG_PRCV_DTQ_ENTER(dtqid, p_data);
	CHECK_TSKCTX_UNL_MYSTATE(&p_selftsk);
	CHECK_ID(VALID_DTQID(dtqid));
	p_dtqcb = get_dtqcb(dtqid);

	lock_cpu();
	acquire_glock();
	p_my_pcb = get_my_pcb();
	if (p_dtqcb->p_dtqinib->dtqatr == TA_NOEXS) {
		ercd = E_NOEXS;
	}
	else if (receive_data(p_my_pcb, p_dtqcb, p_data)) {
		if (p_selftsk != p_my_pcb->p_schedtsk) {
			release_glock();
			dispatch();
			ercd = E_OK;
			goto unlock_and_exit;
		}
		ercd = E_OK;
	}
	else {
		ercd = E_TMOUT;
	}
	release_glock();
  unlock_and_exit:
	unlock_cpu();

  error_exit:
	LOG_PRCV_DTQ_LEAVE(ercd, p_data);
	return(ercd);
}

#endif /* TOPPERS_prcv_dtq */

/*
 *  データキューからの受信（タイムアウトあり）
 */
#ifdef TOPPERS_trcv_dtq

ER
trcv_dtq(ID dtqid, intptr_t *p_data, TMO tmout)
{
	DTQCB	*p_dtqcb;
	ER		ercd;
	TCB		*p_selftsk;
	PCB		*p_my_pcb;

	LOG_TRCV_DTQ_ENTER(dtqid, p_data, tmout);
	CHECK_DISPATCH_MYSTATE(&p_selftsk);
	CHECK_ID(VALID_DTQID(dtqid));
	CHECK_PAR(VALID_TMOUT(tmout));
	p_dtqcb = get_dtqcb(dtqid);

	lock_cpu_dsp();
	acquire_glock();
	p_my_pcb = get_my_pcb();
	if (p_dtqcb->p_dtqinib->dtqatr == TA_NOEXS) {
		ercd = E_NOEXS;
	}
	else if (p_selftsk->raster) {
		ercd = E_RASTER;
	}
	else if (receive_data(p_my_pcb, p_dtqcb, p_data)) {
		if (p_selftsk != p_my_pcb->p_schedtsk) {
			release_glock();
			dispatch();
			ercd = E_OK;
			goto unlock_and_exit;
		}
		ercd = E_OK;
	}
	else if (tmout == TMO_POL) {
		ercd = E_TMOUT;
	}
	else {
		make_wait_tmout(p_my_pcb, TS_WAITING_RDTQ, p_selftsk, tmout);
		queue_insert_prev(&(p_dtqcb->rwait_queue), &(p_selftsk->task_queue));
		p_selftsk->p_wobjcb = (WOBJCB *) p_dtqcb;
		LOG_TSKSTAT(p_selftsk);
		release_glock();
		dispatch();
		ercd = p_selftsk->winfo.wercd;
		if (ercd == E_OK) {
			*p_data = p_selftsk->winfo_obj.rdtq.data;
		}
		goto unlock_and_exit;
	}
	release_glock();
  unlock_and_exit:
	unlock_cpu_dsp();

  error_exit:
	LOG_TRCV_DTQ_LEAVE(ercd, p_data);
	return(ercd);
}

#endif /* TOPPERS_trcv_dtq */

/*
 *  データキューの再初期化
 */
#ifdef TOPPERS_ini_dtq

ER
ini_dtq(ID dtqid)
{
	DTQCB	*p_dtqcb;
	ER		ercd;
	TCB		*p_selftsk;
	PCB		*p_my_pcb;

	LOG_INI_DTQ_ENTER(dtqid);
	CHECK_TSKCTX_UNL_MYSTATE(&p_selftsk);
	CHECK_ID(VALID_DTQID(dtqid));
	p_dtqcb = get_dtqcb(dtqid);

	lock_cpu();
	acquire_glock();
	p_my_pcb = get_my_pcb();
	if (p_dtqcb->p_dtqinib->dtqatr == TA_NOEXS) {
		ercd = E_NOEXS;
	}
	else {
		init_wait_queue(p_my_pcb, &(p_dtqcb->swait_queue));
		init_wait_queue(p_my_pcb, &(p_dtqcb->rwait_queue));
		p_dtqcb->count = 0U;
		p_dtqcb->head = 0U;
		p_dtqcb->tail = 0U;
		if (p_selftsk != p_my_pcb->p_schedtsk) {
			release_glock();
			dispatch();
			ercd = E_OK;
			goto unlock_and_exit;
		}
		ercd = E_OK;
	}
	release_glock();
  unlock_and_exit:
	unlock_cpu();

  error_exit:
	LOG_INI_DTQ_LEAVE(ercd);
	return(ercd);
}

#endif /* TOPPERS_ini_dtq */

/*
 *  データキューの状態参照
 */
#ifdef TOPPERS_ref_dtq

ER
ref_dtq(ID dtqid, T_RDTQ *pk_rdtq)
{
	DTQCB	*p_dtqcb;
	ER		ercd;

	LOG_REF_DTQ_ENTER(dtqid, pk_rdtq);
	CHECK_TSKCTX_UNL();
	CHECK_ID(VALID_DTQID(dtqid));
	p_dtqcb = get_dtqcb(dtqid);

	lock_cpu();
	acquire_glock();
	if (p_dtqcb->p_dtqinib->dtqatr == TA_NOEXS) {
		ercd = E_NOEXS;
	}
	else {
		pk_rdtq->stskid = wait_tskid(&(p_dtqcb->swait_queue));
		pk_rdtq->rtskid = wait_tskid(&(p_dtqcb->rwait_queue));
		pk_rdtq->sdtqcnt = p_dtqcb->count;
		ercd = E_OK;
	}
	release_glock();
	unlock_cpu();

  error_exit:
	LOG_REF_DTQ_LEAVE(ercd, pk_rdtq);
	return(ercd);
}

#endif /* TOPPERS_ref_dtq */
