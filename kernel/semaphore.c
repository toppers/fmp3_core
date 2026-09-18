/*
 *  TOPPERS/FMP Kernel
 *      Toyohashi Open Platform for Embedded Real-Time Systems/
 *      Flexible MultiProcessor Kernel
 * 
 *  Copyright (C) 2000-2003 by Embedded and Real-Time Systems Laboratory
 *                              Toyohashi Univ. of Technology, JAPAN
 *  Copyright (C) 2005-2019 by Embedded and Real-Time Systems Laboratory
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
 *  $Id: semaphore.c 335 2023-04-18 10:50:40Z ertl-honda $
 */

/*
 *		セマフォ機能
 */

#include "kernel_impl.h"
#include "check.h"
#include "task.h"
#include "wait.h"
#include "semaphore.h"

/*
 *  トレースログマクロのデフォルト定義
 */
#ifndef LOG_SIG_SEM_ENTER
#define LOG_SIG_SEM_ENTER(semid)
#endif /* LOG_SIG_SEM_ENTER */

#ifndef LOG_SIG_SEM_LEAVE
#define LOG_SIG_SEM_LEAVE(ercd)
#endif /* LOG_SIG_SEM_LEAVE */

#ifndef LOG_WAI_SEM_ENTER
#define LOG_WAI_SEM_ENTER(semid)
#endif /* LOG_WAI_SEM_ENTER */

#ifndef LOG_WAI_SEM_LEAVE
#define LOG_WAI_SEM_LEAVE(ercd)
#endif /* LOG_WAI_SEM_LEAVE */

#ifndef LOG_POL_SEM_ENTER
#define LOG_POL_SEM_ENTER(semid)
#endif /* LOG_POL_SEM_ENTER */

#ifndef LOG_POL_SEM_LEAVE
#define LOG_POL_SEM_LEAVE(ercd)
#endif /* LOG_POL_SEM_LEAVE */

#ifndef LOG_TWAI_SEM_ENTER
#define LOG_TWAI_SEM_ENTER(semid, tmout)
#endif /* LOG_TWAI_SEM_ENTER */

#ifndef LOG_TWAI_SEM_LEAVE
#define LOG_TWAI_SEM_LEAVE(ercd)
#endif /* LOG_TWAI_SEM_LEAVE */

#ifndef LOG_INI_SEM_ENTER
#define LOG_INI_SEM_ENTER(semid)
#endif /* LOG_INI_SEM_ENTER */

#ifndef LOG_INI_SEM_LEAVE
#define LOG_INI_SEM_LEAVE(ercd)
#endif /* LOG_INI_SEM_LEAVE */

#ifndef LOG_REF_SEM_ENTER
#define LOG_REF_SEM_ENTER(semid, pk_rsem)
#endif /* LOG_REF_SEM_ENTER */

#ifndef LOG_REF_SEM_LEAVE
#define LOG_REF_SEM_LEAVE(ercd, pk_rsem)
#endif /* LOG_REF_SEM_LEAVE */

/*
 *  セマフォIDからセマフォ管理ブロックを取り出すためのマクロ
 */
#define INDEX_SEM(semid)	((uint_t)((semid) - TMIN_SEMID))
#define get_semcb(semid)	(p_semcb_table[INDEX_SEM(semid)])

/* 
 *  セマフォ機能の初期化
 */
#ifdef TOPPERS_semini

/*
 *  使用していないセマフォ管理ブロックのリスト
 *
 *  SEMCBの先頭フィールドがQUEUE（wait_queue）なので，そのまま
 *  free-listのリンクに流用する．acre_semは取り出した直後に
 *  queue_initializeで作り直すため，TA_TPRI（優先度順待ちキュー）と
 *  干渉しない．
 */
QUEUE	free_semcb;

void
initialize_semaphore(PCB *p_my_pcb)
{
	uint_t	i, j;
	SEMCB	*p_semcb;
	SEMINIB	*p_seminib;

	if (p_my_pcb->prcid == TOPPERS_MASTER_PRCID) {
		for (i = 0; i < tnum_ssem; i++) {
			p_semcb = p_semcb_table[i];
			queue_initialize(&(p_semcb->wait_queue));
			p_semcb->p_seminib = &(seminib_table[i]);
			p_semcb->semcnt = p_semcb->p_seminib->isemcnt;
		}

		/*
		 *  動的生成用スロットの初期化
		 *
		 *  セマフォはプロセッサ親和を持たない（SEMINIBにiprcid/affinity
		 *  が無く，SEMCBにp_pcbが無い）ため，段階2のcyc/almのような
		 *  プロセッサ判定や充填は一切不要である．本関数は元から
		 *  マスタプロセッサ限定なので，そのブロックの中で続けて初期化
		 *  する．他プロセッサへの可視性は，本関数の呼出し後の
		 *  barrier_syncが保証する（段階1のfree_tcbと同じ論証）．
		 */
		queue_initialize(&free_semcb);
		for (j = 0; i < tnum_sem; i++, j++) {
			p_semcb = p_semcb_table[i];
			p_seminib = &(aseminib_table[j]);
			p_seminib->sematr = TA_NOEXS;
			p_semcb->p_seminib = ((const SEMINIB *) p_seminib);
			queue_insert_prev(&free_semcb, &(p_semcb->wait_queue));
		}
	}
}

#endif /* TOPPERS_semini */

/*
 *  セマフォの生成［NGKI1453］
 */
#ifdef TOPPERS_acre_sem

#ifndef LOG_ACRE_SEM_ENTER
#define LOG_ACRE_SEM_ENTER(pk_csem)
#endif /* LOG_ACRE_SEM_ENTER */

#ifndef LOG_ACRE_SEM_LEAVE
#define LOG_ACRE_SEM_LEAVE(ercd)
#endif /* LOG_ACRE_SEM_LEAVE */

ER_ID
acre_sem(const T_CSEM *pk_csem)
{
	SEMCB	*p_semcb;
	SEMINIB	*p_seminib;
	ATR		sematr;
	uint_t	isemcnt, maxsem;
	ER		ercd;

	LOG_ACRE_SEM_ENTER(pk_csem);
	CHECK_TSKCTX_UNL();							/*［NGKI1454］［NGKI1455］*/

	sematr = pk_csem->sematr;
	isemcnt = pk_csem->isemcnt;
	maxsem = pk_csem->maxsem;

	CHECK_VALIDATR(sematr, TA_TPRI);			/*［NGKI1456］*/
	CHECK_PAR(isemcnt <= maxsem);				/*［NGKI1466］*/
	CHECK_PAR(1 <= maxsem && maxsem <= TMAX_MAXSEM);	/*［NGKI1468］*/

	lock_cpu();
	acquire_glock();
	if (tnum_sem == tnum_ssem || queue_empty(&free_semcb)) {
		ercd = E_NOID;							/*［NGKI1462］*/
	}
	else {										/*［NGKI5189］*/
		p_semcb = ((SEMCB *) queue_delete_next(&free_semcb));
		p_seminib = (SEMINIB *)(p_semcb->p_seminib);
		p_seminib->sematr = sematr;
		p_seminib->isemcnt = isemcnt;
		p_seminib->maxsem = maxsem;

		queue_initialize(&(p_semcb->wait_queue));	/*［NGKI1464］*/
		p_semcb->semcnt = p_semcb->p_seminib->isemcnt;
		ercd = SEMID(p_semcb);
	}
	release_glock();
	unlock_cpu();

  error_exit:
	LOG_ACRE_SEM_LEAVE(ercd);
	return(ercd);
}

#endif /* TOPPERS_acre_sem */

/*
 *  セマフォの削除［NGKI1487］
 */
#ifdef TOPPERS_del_sem

#ifndef LOG_DEL_SEM_ENTER
#define LOG_DEL_SEM_ENTER(semid)
#endif /* LOG_DEL_SEM_ENTER */

#ifndef LOG_DEL_SEM_LEAVE
#define LOG_DEL_SEM_LEAVE(ercd)
#endif /* LOG_DEL_SEM_LEAVE */

ER
del_sem(ID semid)
{
	SEMCB	*p_semcb;
	SEMINIB	*p_seminib;
	ER		ercd;
	TCB		*p_selftsk;
	PCB		*p_my_pcb;

	LOG_DEL_SEM_ENTER(semid);
	CHECK_TSKCTX_UNL_MYSTATE(&p_selftsk);		/*［NGKI1488］［NGKI1489］*/
	CHECK_ID(VALID_SEMID(semid));				/*［NGKI1490］*/
	p_semcb = get_semcb(semid);

	lock_cpu();
	acquire_glock();
	p_my_pcb = get_my_pcb();
	if (p_semcb->p_seminib->sematr == TA_NOEXS) {
		ercd = E_NOEXS;							/*［NGKI1491］*/
	}
	else if (semid <= tmax_ssemid) {
		ercd = E_OBJ;							/*［NGKI1493］*/
	}
	else {
		/*
		 *  待ちタスクがいても削除は成功し，待ちタスクはE_DLTで強制
		 *  解除される［NGKI1495］［NGKI1496］．init_wait_queueは
		 *  MP対応済み（wait.c:215-228）で，既存のini_semと同一の機構
		 *  である．新規の解除機構は書かない．
		 */
		init_wait_queue(p_my_pcb, &(p_semcb->wait_queue));
		p_seminib = (SEMINIB *)(p_semcb->p_seminib);
		p_seminib->sematr = TA_NOEXS;
		queue_insert_prev(&free_semcb, &(p_semcb->wait_queue));	/*［NGKI1494］*/
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
	LOG_DEL_SEM_LEAVE(ercd);
	return(ercd);
}

#endif /* TOPPERS_del_sem */

/*
 *  セマフォ資源の返却［NGKI3533］
 */
#ifdef TOPPERS_sig_sem

ER
sig_sem(ID semid)
{
	SEMCB	*p_semcb;
	TCB		*p_tcb;
	ER		ercd;
	TCB		*p_selftsk;
	bool_t	context;
	PCB		*p_my_pcb;

	LOG_SIG_SEM_ENTER(semid);
	CHECK_UNL_MYSTATE(&p_selftsk, &context);	/*［NGKI1501］*/
	CHECK_ID(VALID_SEMID(semid));	/*［NGKI1502］*/
	p_semcb = get_semcb(semid);

	lock_cpu();
	acquire_glock();
	p_my_pcb = get_my_pcb();
	if (p_semcb->p_seminib->sematr == TA_NOEXS) {
		ercd = E_NOEXS;
	}
	else if (!queue_empty(&(p_semcb->wait_queue))) {
		p_tcb = (TCB *) queue_delete_next(&(p_semcb->wait_queue));
		wait_complete(p_my_pcb, p_tcb);	/*［NGKI1505］［NGKI1506］［NGKI1507］*/
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
	else if (p_semcb->semcnt < p_semcb->p_seminib->maxsem) {
		p_semcb->semcnt += 1;					/*［NGKI1508］*/
		ercd = E_OK;
	}
	else {
		ercd = E_QOVR;							/*［NGKI1509］*/
	}
	release_glock();
  unlock_and_exit:
	unlock_cpu();

  error_exit:
	LOG_SIG_SEM_LEAVE(ercd);
	return(ercd);
}

#endif /* TOPPERS_sig_sem */

/*
 *  セマフォ資源の獲得［NGKI1510］
 */
#ifdef TOPPERS_wai_sem

ER
wai_sem(ID semid)
{
	SEMCB	*p_semcb;
	ER		ercd;
	TCB		*p_selftsk;
	PCB		*p_my_pcb;

	LOG_WAI_SEM_ENTER(semid);
	CHECK_DISPATCH_MYSTATE(&p_selftsk);	/*［NGKI1515］*/
	CHECK_ID(VALID_SEMID(semid));				/*［NGKI1517］*/
	p_semcb = get_semcb(semid);

	lock_cpu_dsp();
	acquire_glock();
	p_my_pcb = get_my_pcb();
	if (p_semcb->p_seminib->sematr == TA_NOEXS) {
		ercd = E_NOEXS;
	}
	else if (p_selftsk->raster) {
		ercd = E_RASTER;						/*［NGKI3457］*/
	}
	else if (p_semcb->semcnt >= 1) {
		p_semcb->semcnt -= 1;					/*［NGKI1524］*/
		ercd = E_OK;
	}
	else {										/*［NGKI1525］*/
		wobj_make_wait(p_my_pcb, (WOBJCB *) p_semcb, TS_WAITING_SEM, p_selftsk);
		release_glock();
		dispatch();
		ercd = p_selftsk->winfo.wercd;
		goto unlock_and_exit;
	}
	release_glock();
  unlock_and_exit:
	unlock_cpu_dsp();

  error_exit:
	LOG_WAI_SEM_LEAVE(ercd);
	return(ercd);
}

#endif /* TOPPERS_wai_sem */

/*
 *  セマフォ資源の獲得（ポーリング）［NGKI1511］
 */
#ifdef TOPPERS_pol_sem

ER
pol_sem(ID semid)
{
	SEMCB	*p_semcb;
	ER		ercd;

	LOG_POL_SEM_ENTER(semid);
	CHECK_TSKCTX_UNL();	/*［NGKI1513］［NGKI1514］*/
	CHECK_ID(VALID_SEMID(semid));				/*［NGKI1517］*/
	p_semcb = get_semcb(semid);

	lock_cpu();
	acquire_glock();
	if (p_semcb->p_seminib->sematr == TA_NOEXS) {
		ercd = E_NOEXS;
	}
	else if (p_semcb->semcnt >= 1) {
		p_semcb->semcnt -= 1;					/*［NGKI1524］*/
		ercd = E_OK;
	}
	else {
		ercd = E_TMOUT;							/*［NGKI1521］*/
	}
	release_glock();
	unlock_cpu();

  error_exit:
	LOG_POL_SEM_LEAVE(ercd);
	return(ercd);
}

#endif /* TOPPERS_pol_sem */

/*
 *  セマフォ資源の獲得（タイムアウトあり）［NGKI1512］
 */
#ifdef TOPPERS_twai_sem

ER
twai_sem(ID semid, TMO tmout)
{
	SEMCB	*p_semcb;
	ER		ercd;
	TCB		*p_selftsk;
	PCB		*p_my_pcb;

	LOG_TWAI_SEM_ENTER(semid, tmout);
	CHECK_DISPATCH_MYSTATE(&p_selftsk);	/*［NGKI1515］*/
	CHECK_ID(VALID_SEMID(semid));		/*［NGKI1517］*/
	CHECK_PAR(VALID_TMOUT(tmout));		/*［NGKI1518］*/
	p_semcb = get_semcb(semid);

	lock_cpu_dsp();
	acquire_glock();
	p_my_pcb = get_my_pcb();
	if (p_semcb->p_seminib->sematr == TA_NOEXS) {
		ercd = E_NOEXS;
	}
	else if (p_selftsk->raster) {
		ercd = E_RASTER;						/*［NGKI3457］*/
	}
	else if (p_semcb->semcnt >= 1) {
		p_semcb->semcnt -= 1;					/*［NGKI1524］*/
		ercd = E_OK;
	}
	else if (tmout == TMO_POL) {
		ercd = E_TMOUT;							/*［NGKI1521］*/
	}
	else {										/*［NGKI1525］*/
		wobj_make_wait_tmout(p_my_pcb, (WOBJCB *) p_semcb, TS_WAITING_SEM,
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
	LOG_TWAI_SEM_LEAVE(ercd);
	return(ercd);
}

#endif /* TOPPERS_twai_sem */

/*
 *  セマフォの再初期化［NGKI1526］
 */
#ifdef TOPPERS_ini_sem

ER
ini_sem(ID semid)
{
	SEMCB	*p_semcb;
	ER		ercd;
	PCB		*p_my_pcb;

	LOG_INI_SEM_ENTER(semid);
	CHECK_TSKCTX_UNL();	/*［NGKI1527］［NGKI1528］*/
	CHECK_ID(VALID_SEMID(semid));				/*［NGKI1529］*/
	p_semcb = get_semcb(semid);

	lock_cpu();
	acquire_glock();
	p_my_pcb = get_my_pcb();
	if (p_semcb->p_seminib->sematr == TA_NOEXS) {
		ercd = E_NOEXS;
	}
	else {
		init_wait_queue(p_my_pcb, &(p_semcb->wait_queue));
													/*［NGKI1533］［NGKI1534］*/
		p_semcb->semcnt = p_semcb->p_seminib->isemcnt;	/*［NGKI1532］*/
		if (p_my_pcb->p_runtsk != p_my_pcb->p_schedtsk) {
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
	LOG_INI_SEM_LEAVE(ercd);
	return(ercd);
}

#endif /* TOPPERS_ini_sem */

/*
 *  セマフォの状態参照［NGKI1535］
 */
#ifdef TOPPERS_ref_sem

ER
ref_sem(ID semid, T_RSEM *pk_rsem)
{
	SEMCB	*p_semcb;
	ER		ercd;

	LOG_REF_SEM_ENTER(semid, pk_rsem);
	CHECK_TSKCTX_UNL();	/*［NGKI1536］［NGKI1537］*/
	CHECK_ID(VALID_SEMID(semid));				/*［NGKI1538］*/
	p_semcb = get_semcb(semid);

	lock_cpu();
	acquire_glock();
	if (p_semcb->p_seminib->sematr == TA_NOEXS) {
		ercd = E_NOEXS;
	}
	else {
		pk_rsem->wtskid = wait_tskid(&(p_semcb->wait_queue));
												/*［NGKI1542］［NGKI1543］*/
		pk_rsem->semcnt = p_semcb->semcnt;			/*［NGKI1542］*/
		ercd = E_OK;
	}
	release_glock();
	unlock_cpu();

  error_exit:
	LOG_REF_SEM_LEAVE(ercd, pk_rsem);
	return(ercd);
}

#endif /* TOPPERS_ref_sem */
