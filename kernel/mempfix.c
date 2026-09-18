/*
 *  TOPPERS/FMP Kernel
 *      Toyohashi Open Platform for Embedded Real-Time Systems/
 *      Flexible MultiProcessor Kernel
 * 
 *  Copyright (C) 2000-2003 by Embedded and Real-Time Systems Laboratory
 *                              Toyohashi Univ. of Technology, JAPAN
 *  Copyright (C) 2005-2023 by Embedded and Real-Time Systems Laboratory
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
 *  $Id: mempfix.c 335 2023-04-18 10:50:40Z ertl-honda $
 */

/*
 *		固定長メモリプール機能
 */

#include "kernel_impl.h"
#include "check.h"
#include "task.h"
#include "wait.h"
#include "mempfix.h"

/*
 *  トレースログマクロのデフォルト定義
 */
#ifndef LOG_GET_MPF_ENTER
#define LOG_GET_MPF_ENTER(mpfid, p_blk)
#endif /* LOG_GET_MPF_ENTER */

#ifndef LOG_GET_MPF_LEAVE
#define LOG_GET_MPF_LEAVE(ercd, p_blk)
#endif /* LOG_GET_MPF_LEAVE */

#ifndef LOG_PGET_MPF_ENTER
#define LOG_PGET_MPF_ENTER(mpfid, p_blk)
#endif /* LOG_PGET_MPF_ENTER */

#ifndef LOG_PGET_MPF_LEAVE
#define LOG_PGET_MPF_LEAVE(ercd, p_blk)
#endif /* LOG_PGET_MPF_LEAVE */

#ifndef LOG_TGET_MPF_ENTER
#define LOG_TGET_MPF_ENTER(mpfid, p_blk, tmout)
#endif /* LOG_TGET_MPF_ENTER */

#ifndef LOG_TGET_MPF_LEAVE
#define LOG_TGET_MPF_LEAVE(ercd, p_blk)
#endif /* LOG_TGET_MPF_LEAVE */

#ifndef LOG_REL_MPF_ENTER
#define LOG_REL_MPF_ENTER(mpfid, blk)
#endif /* LOG_REL_MPF_ENTER */

#ifndef LOG_REL_MPF_LEAVE
#define LOG_REL_MPF_LEAVE(ercd)
#endif /* LOG_REL_MPF_LEAVE */

#ifndef LOG_INI_MPF_ENTER
#define LOG_INI_MPF_ENTER(mpfid)
#endif /* LOG_INI_MPF_ENTER */

#ifndef LOG_INI_MPF_LEAVE
#define LOG_INI_MPF_LEAVE(ercd)
#endif /* LOG_INI_MPF_LEAVE */

#ifndef LOG_REF_MPF_ENTER
#define LOG_REF_MPF_ENTER(mpfid, pk_rmpf)
#endif /* LOG_REF_MPF_ENTER */

#ifndef LOG_REF_MPF_LEAVE
#define LOG_REF_MPF_LEAVE(ercd, pk_rmpf)
#endif /* LOG_REF_MPF_LEAVE */

/*
 *  固定長メモリプールIDから固定長メモリプール管理ブロックを取り出すた
 *  めのマクロ
 */
#define INDEX_MPF(mpfid)	((uint_t)((mpfid) - TMIN_MPFID))
#define get_mpfcb(mpfid)	(p_mpfcb_table[INDEX_MPF(mpfid)])

/*
 *  固定長メモリプール機能の初期化
 */
#ifdef TOPPERS_mpfini

/*
 *  使用していない固定長メモリプール管理ブロックのリスト
 *
 *  MPFCBの先頭フィールドがQUEUE（wait_queue）なので，そのまま
 *  free-listのリンクに流用する．acre_mpfは取り出した直後に
 *  queue_initializeで作り直すため，TA_TPRI（優先度順待ちキュー）と
 *  干渉しない．
 */
QUEUE	free_mpfcb;

void
initialize_mempfix(PCB *p_my_pcb)
{
	uint_t	i, j;
	MPFCB	*p_mpfcb;
	MPFINIB	*p_mpfinib;

	if (p_my_pcb->prcid == TOPPERS_MASTER_PRCID) {
		for (i = 0; i < tnum_smpf; i++) {
			p_mpfcb = p_mpfcb_table[i];
			queue_initialize(&(p_mpfcb->wait_queue));
			p_mpfcb->p_mpfinib = &(mpfinib_table[i]);
			p_mpfcb->fblkcnt = p_mpfcb->p_mpfinib->blkcnt;
			p_mpfcb->unused = 0U;
			p_mpfcb->freelist = INDEX_NULL;
		}

		/*
		 *  動的生成用スロットの初期化
		 *
		 *  固定長メモリプールはプロセッサ親和を持たない（MPFINIBに
		 *  iprcid/affinityが無く，MPFCBにp_pcbが無い）ため，段階2の
		 *  cyc/almのようなプロセッサ判定や充填は一切不要である．本関数は
		 *  元からマスタプロセッサ限定なので，そのブロックの中で続けて
		 *  初期化する．他プロセッサへの可視性は，本関数の呼出し後の
		 *  barrier_syncが保証する（段階1のfree_tcbと同じ論証）．
		 */
		queue_initialize(&free_mpfcb);
		for (j = 0; i < tnum_mpf; i++, j++) {
			p_mpfcb = p_mpfcb_table[i];
			p_mpfinib = &(ampfinib_table[j]);
			p_mpfinib->mpfatr = TA_NOEXS;
			p_mpfcb->p_mpfinib = ((const MPFINIB *) p_mpfinib);
			queue_insert_prev(&free_mpfcb, &(p_mpfcb->wait_queue));
		}
	}
}

#endif /* TOPPERS_mpfini */

/*
 *  固定長メモリプールからブロックを獲得
 */
#ifdef TOPPERS_mpfget

void
get_mpf_block(MPFCB *p_mpfcb, void **p_blk)
{
	uint_t	blkidx;

	if (p_mpfcb->freelist != INDEX_NULL) {
		blkidx = p_mpfcb->freelist;
		p_mpfcb->freelist = p_mpfcb->p_mpfinib->p_mpfmb[blkidx].next;
	}
	else {
		blkidx = p_mpfcb->unused;
		p_mpfcb->unused++;
	}
	*p_blk = (void *)((char *)(p_mpfcb->p_mpfinib->mpf)
								+ p_mpfcb->p_mpfinib->blksz * blkidx);
	p_mpfcb->fblkcnt--;
	p_mpfcb->p_mpfinib->p_mpfmb[blkidx].next = INDEX_ALLOC;
}

#endif /* TOPPERS_mpfget */

/*
 *  固定長メモリプールの生成
 */
#ifdef TOPPERS_acre_mpf

#ifndef LOG_ACRE_MPF_ENTER
#define LOG_ACRE_MPF_ENTER(pk_cmpf)
#endif /* LOG_ACRE_MPF_ENTER */

#ifndef LOG_ACRE_MPF_LEAVE
#define LOG_ACRE_MPF_LEAVE(ercd)
#endif /* LOG_ACRE_MPF_LEAVE */

ER_ID
acre_mpf(const T_CMPF *pk_cmpf)
{
	MPFCB	*p_mpfcb;
	MPFINIB	*p_mpfinib;
	ATR		mpfatr;
	uint_t	blkcnt;
	uint_t	blksz;
	MPF_T	*mpf;
	MPFMB	*p_mpfmb;
	ER		ercd;

	LOG_ACRE_MPF_ENTER(pk_cmpf);
	CHECK_TSKCTX_UNL();

	mpfatr = pk_cmpf->mpfatr;
	blkcnt = pk_cmpf->blkcnt;
	blksz = pk_cmpf->blksz;
	mpf = pk_cmpf->mpf;
	p_mpfmb = pk_cmpf->mpfmb;

	CHECK_VALIDATR(mpfatr, TA_TPRI);
	CHECK_PAR(blkcnt != 0);
	CHECK_PAR(blksz != 0);

	/*
	 *  ブロック領域と管理領域のサイズ計算があふれないこと（3件）
	 *
	 *  ★dcre（extension/dcre/kernel/mempfix.c）にこれらの検査は無い．
	 *  acre_mpfは2段確保（①ブロック領域 ②管理領域）であり，あふれうる
	 *  箇所が3つある．
	 *
	 *  (a) ROUND_MPF_T(blksz) の丸めそのもの
	 *      TOPPERS_ROUND_SZ(sz, unit) は ((sz) + (unit) - 1) & ~((unit) - 1)
	 *      であり，blkszがsize_tの最大値に近いと加算があふれて丸め結果が
	 *      0（またはblkszより小さい値）になる．結果，malloc_mpk(0)が成功し，
	 *      p_mpfinib->blkszに0が入った固定長メモリプールができあがる．
	 *      get_mpfは同じ番地を何度も配ることになる．
	 *  (b) ROUND_MPF_T(blksz) * blkcnt（①ブロック領域）
	 *  (c) sizeof(MPFMB) * blkcnt（②管理領域）
	 *      いずれもacre_dtqと同じ理由（32bitで積があふれる）．
	 *      ②があふれた場合は①の巻き戻し経路（下の pk_cmpf->mpf 判定）を
	 *      通るが，そもそも「小さすぎる管理領域の確保に成功する」ので
	 *      巻き戻しは起こらず，MPFMB配列の外を触るプールが生まれる．
	 *
	 *  (a)を先に置くのは，(b)の除数 ROUND_MPF_T(blksz) が壊れていない
	 *  ことを(b)より前に保証するためである．blksz != 0 は直前で検査済み
	 *  なので，(a)を通れば ROUND_MPF_T(blksz) >= sizeof(MPF_T) > 0 であり，
	 *  (b)の除算は0除算にならない．
	 */
	CHECK_PAR(blksz <= (SIZE_MAX - (sizeof(MPF_T) - 1)));
	CHECK_PAR(blkcnt <= (SIZE_MAX / ROUND_MPF_T(blksz)));
	CHECK_PAR(blkcnt <= (SIZE_MAX / sizeof(MPFMB)));

	if (mpf != NULL) {
		CHECK_PAR(MPF_ALIGN(mpf));
	}
	if (p_mpfmb != NULL) {
		CHECK_PAR(MB_ALIGN(p_mpfmb));
	}

	lock_cpu();
	acquire_glock();
	if (tnum_mpf == tnum_smpf || queue_empty(&free_mpfcb)) {
		ercd = E_NOID;
	}
	else {
		/*
		 *  ①固定長メモリプール領域の確保
		 *
		 *  ユーザがmpfを与えた場合はそれを使い，TA_MEMALLOCを立てない．
		 */
		if (mpf == NULL) {
			mpf = malloc_mpk(ROUND_MPF_T(blksz) * blkcnt);
			mpfatr |= TA_MEMALLOC;
		}
		if (mpf == NULL) {
			ercd = E_NOMEM;
		}
		else {
			/*
			 *  ②管理領域の確保
			 *
			 *  ★②に失敗したときは①でカーネルが確保した分だけを
			 *  巻き戻す．判定にローカル変数mpfを使うと①で上書き
			 *  されているため区別できないので，パケットの元の値
			 *  pk_cmpf->mpfを見る（dcre mempfix.c:250と同一）．
			 */
			if (p_mpfmb == NULL) {
				p_mpfmb = malloc_mpk(sizeof(MPFMB) * blkcnt);
				mpfatr |= TA_MBALLOC;
			}
			if (p_mpfmb == NULL) {
				if (pk_cmpf->mpf == NULL) {
					free_mpk(mpf);
				}
				ercd = E_NOMEM;
			}
			else {
				/*
				 *  ★E_NOMEMのときfree-listからCBを取り出していない
				 *  ことが重要である（2段とも確保に成功してから初めて
				 *  queue_delete_nextする）．段階1のacre_tskと同じ順序．
				 */
				p_mpfcb = ((MPFCB *) queue_delete_next(&free_mpfcb));
				p_mpfinib = (MPFINIB *)(p_mpfcb->p_mpfinib);
				p_mpfinib->mpfatr = mpfatr;
				p_mpfinib->blkcnt = blkcnt;
				p_mpfinib->blksz = ROUND_MPF_T(blksz);
				p_mpfinib->mpf = mpf;
				p_mpfinib->p_mpfmb = p_mpfmb;

				queue_initialize(&(p_mpfcb->wait_queue));
				p_mpfcb->fblkcnt = p_mpfcb->p_mpfinib->blkcnt;
				p_mpfcb->unused = 0U;
				p_mpfcb->freelist = INDEX_NULL;
				ercd = MPFID(p_mpfcb);
			}
		}
	}
	release_glock();
	unlock_cpu();

  error_exit:
	LOG_ACRE_MPF_LEAVE(ercd);
	return(ercd);
}

#endif /* TOPPERS_acre_mpf */

/*
 *  固定長メモリプールの削除
 */
#ifdef TOPPERS_del_mpf

#ifndef LOG_DEL_MPF_ENTER
#define LOG_DEL_MPF_ENTER(mpfid)
#endif /* LOG_DEL_MPF_ENTER */

#ifndef LOG_DEL_MPF_LEAVE
#define LOG_DEL_MPF_LEAVE(ercd)
#endif /* LOG_DEL_MPF_LEAVE */

ER
del_mpf(ID mpfid)
{
	MPFCB	*p_mpfcb;
	MPFINIB	*p_mpfinib;
	ER		ercd;
	TCB		*p_selftsk;
	PCB		*p_my_pcb;

	LOG_DEL_MPF_ENTER(mpfid);
	CHECK_TSKCTX_UNL_MYSTATE(&p_selftsk);
	CHECK_ID(VALID_MPFID(mpfid));
	p_mpfcb = get_mpfcb(mpfid);

	lock_cpu();
	acquire_glock();
	p_my_pcb = get_my_pcb();
	if (p_mpfcb->p_mpfinib->mpfatr == TA_NOEXS) {
		ercd = E_NOEXS;
	}
	else if (mpfid <= tmax_smpfid) {
		ercd = E_OBJ;
	}
	else {
		/*
		 *  待ちタスクがいても削除は成功し，待ちタスクはE_DLTで強制
		 *  解除される．init_wait_queueはMP対応済み（wait.c:215-228）で，
		 *  既存のini_mpfと同一の機構である．獲得済みのメモリブロックは
		 *  返却されないまま領域ごと解放される（dcre意味論）．
		 */
		init_wait_queue(p_my_pcb, &(p_mpfcb->wait_queue));
		p_mpfinib = (MPFINIB *)(p_mpfcb->p_mpfinib);
		/*
		 *  ★順序制約：属性の読み（ビット検査2件）はTA_NOEXSの書込みより
		 *  前で行う．TA_NOEXSは((ATR)(-1))＝全ビットが1であるため，
		 *  TA_NOEXSを書いた後では(mpfatr & TA_MEMALLOC) != 0Uも
		 *  (mpfatr & TA_MBALLOC) != 0Uも必ず真になり，ユーザ供給の
		 *  領域まで解放してしまう（プールの破壊）．
		 *  dcre mempfix.c:308-314 も同じ順序．
		 */
		if ((p_mpfinib->mpfatr & TA_MEMALLOC) != 0U) {
			free_mpk(p_mpfinib->mpf);
		}
		if ((p_mpfinib->mpfatr & TA_MBALLOC) != 0U) {
			free_mpk(p_mpfinib->p_mpfmb);
		}
		p_mpfinib->mpfatr = TA_NOEXS;
		queue_insert_prev(&free_mpfcb, &(p_mpfcb->wait_queue));
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
	LOG_DEL_MPF_LEAVE(ercd);
	return(ercd);
}

#endif /* TOPPERS_del_mpf */

/*
 *  固定長メモリブロックの獲得
 */
#ifdef TOPPERS_get_mpf

ER
get_mpf(ID mpfid, void **p_blk)
{
	MPFCB		*p_mpfcb;
	ER			ercd;
	TCB			*p_selftsk;
	PCB			*p_my_pcb;

	LOG_GET_MPF_ENTER(mpfid, p_blk);
	CHECK_DISPATCH_MYSTATE(&p_selftsk);
	CHECK_ID(VALID_MPFID(mpfid));
	p_mpfcb = get_mpfcb(mpfid);

	lock_cpu_dsp();
	acquire_glock();
	p_my_pcb = get_my_pcb();
	if (p_mpfcb->p_mpfinib->mpfatr == TA_NOEXS) {
		ercd = E_NOEXS;
	}
	else if (p_selftsk->raster) {
		ercd = E_RASTER;
	}
	else if (p_mpfcb->fblkcnt > 0) {
		get_mpf_block(p_mpfcb, p_blk);
		ercd = E_OK;
	}
	else {
		wobj_make_wait(p_my_pcb, (WOBJCB *) p_mpfcb, TS_WAITING_MPF, p_selftsk);
		release_glock();
		dispatch();
		ercd = p_selftsk->winfo.wercd;
		if (ercd == E_OK) {
			*p_blk = p_selftsk->winfo_obj.mpf.blk;
		}
		goto unlock_and_exit;
	}
	release_glock();
  unlock_and_exit:
	unlock_cpu_dsp();

  error_exit:
	LOG_GET_MPF_LEAVE(ercd, p_blk);
	return(ercd);
}

#endif /* TOPPERS_get_mpf */

/*
 *  固定長メモリブロックの獲得（ポーリング）
 */
#ifdef TOPPERS_pget_mpf

ER
pget_mpf(ID mpfid, void **p_blk)
{
	MPFCB	*p_mpfcb;
	ER		ercd;

	LOG_PGET_MPF_ENTER(mpfid, p_blk);
	CHECK_TSKCTX_UNL();
	CHECK_ID(VALID_MPFID(mpfid));
	p_mpfcb = get_mpfcb(mpfid);

	lock_cpu();
	acquire_glock();
	if (p_mpfcb->p_mpfinib->mpfatr == TA_NOEXS) {
		ercd = E_NOEXS;
	}
	else if (p_mpfcb->fblkcnt > 0) {
		get_mpf_block(p_mpfcb, p_blk);
		ercd = E_OK;
	}
	else {
		ercd = E_TMOUT;
	}
	release_glock();
	unlock_cpu();

  error_exit:
	LOG_PGET_MPF_LEAVE(ercd, p_blk);
	return(ercd);
}

#endif /* TOPPERS_pget_mpf */

/*
 *  固定長メモリブロックの獲得（タイムアウトあり）
 */
#ifdef TOPPERS_tget_mpf

ER
tget_mpf(ID mpfid, void **p_blk, TMO tmout)
{
	MPFCB		*p_mpfcb;
	ER			ercd;
	TCB			*p_selftsk;
	PCB			*p_my_pcb;

	LOG_TGET_MPF_ENTER(mpfid, p_blk, tmout);
	CHECK_DISPATCH_MYSTATE(&p_selftsk);
	CHECK_ID(VALID_MPFID(mpfid));
	CHECK_PAR(VALID_TMOUT(tmout));
	p_mpfcb = get_mpfcb(mpfid);

	lock_cpu_dsp();
	acquire_glock();
	p_my_pcb = get_my_pcb();
	if (p_mpfcb->p_mpfinib->mpfatr == TA_NOEXS) {
		ercd = E_NOEXS;
	}
	else if (p_selftsk->raster) {
		ercd = E_RASTER;
	}
	else if (p_mpfcb->fblkcnt > 0) {
		get_mpf_block(p_mpfcb, p_blk);
		ercd = E_OK;
	}
	else if (tmout == TMO_POL) {
		ercd = E_TMOUT;
	}
	else {
		wobj_make_wait_tmout(p_my_pcb, (WOBJCB *) p_mpfcb, TS_WAITING_MPF,
														p_selftsk, tmout);
		release_glock();
		dispatch();
		ercd = p_selftsk->winfo.wercd;
		if (ercd == E_OK) {
			*p_blk = p_selftsk->winfo_obj.mpf.blk;
		}
		goto unlock_and_exit;
	}
	release_glock();
  unlock_and_exit:
	unlock_cpu_dsp();

  error_exit:
	LOG_TGET_MPF_LEAVE(ercd, p_blk);
	return(ercd);
}

#endif /* TOPPERS_tget_mpf */

/*
 *  固定長メモリブロックの返却
 */
#ifdef TOPPERS_rel_mpf

ER
rel_mpf(ID mpfid, void *blk)
{
	MPFCB	*p_mpfcb;
	size_t	blkoffset;
	uint_t	blkidx;
	TCB		*p_tcb;
	ER		ercd;
	TCB		*p_selftsk;
	PCB		*p_my_pcb;

	LOG_REL_MPF_ENTER(mpfid, blk);
	CHECK_TSKCTX_UNL_MYSTATE(&p_selftsk);
	CHECK_ID(VALID_MPFID(mpfid));
	p_mpfcb = get_mpfcb(mpfid);

	lock_cpu();
	acquire_glock();
	p_my_pcb = get_my_pcb();
	if (p_mpfcb->p_mpfinib->mpfatr == TA_NOEXS) {
		ercd = E_NOEXS;
	}
	else {
		/*
		 *  ★dcreに倣い，ブロック番地の妥当性検査をロック取得前の
		 *  CHECK_PAR 4件からロック内のこの位置へ移した
		 *  （dcre mempfix.c:487-495）．削除済み（TA_NOEXS）の
		 *  固定長メモリプールに対しては，p_mpfinib->mpfも
		 *  p_mpfinib->p_mpfmbもfree_mpk済みの番地であり，
		 *  E_NOEXSゲートより前にp_mpfmb[blkidx]をデリファレンス
		 *  すると解放済み領域を読むことになるため．
		 *  ★4条件の評価順は変えないこと．blkidxが範囲外のときは
		 *  第3条件（blkoffset / blksz < unused）で短絡し，
		 *  第4条件のp_mpfmb[blkidx]は評価されない．
		 */
		blkoffset = ((char *) blk) - (char *)(p_mpfcb->p_mpfinib->mpf);
		blkidx = (uint_t)(blkoffset / p_mpfcb->p_mpfinib->blksz);
		if (!(p_mpfcb->p_mpfinib->mpf <= blk)
				|| !(blkoffset % p_mpfcb->p_mpfinib->blksz == 0U)
				|| !(blkoffset / p_mpfcb->p_mpfinib->blksz < p_mpfcb->unused)
				|| !(p_mpfcb->p_mpfinib->p_mpfmb[blkidx].next == INDEX_ALLOC)) {
			ercd = E_PAR;
		}
		else if (!queue_empty(&(p_mpfcb->wait_queue))) {
			p_tcb = (TCB *) queue_delete_next(&(p_mpfcb->wait_queue));
			p_tcb->winfo_obj.mpf.blk = blk;
			wait_complete(p_my_pcb, p_tcb);
			if (p_selftsk != p_my_pcb->p_schedtsk) {
				release_glock();
				dispatch();
				ercd = E_OK;
				goto unlock_and_exit;
			}
			ercd = E_OK;
		}
		else {
			p_mpfcb->fblkcnt++;
			p_mpfcb->p_mpfinib->p_mpfmb[blkidx].next = p_mpfcb->freelist;
			p_mpfcb->freelist = blkidx;
			ercd = E_OK;
		}
	}
	release_glock();
  unlock_and_exit:
	unlock_cpu();

  error_exit:
	LOG_REL_MPF_LEAVE(ercd);
	return(ercd);
}

#endif /* TOPPERS_rel_mpf */

/*
 *  固定長メモリプールの再初期化
 */
#ifdef TOPPERS_ini_mpf

ER
ini_mpf(ID mpfid)
{
	MPFCB	*p_mpfcb;
	ER		ercd;
	TCB		*p_selftsk;
	PCB		*p_my_pcb;

	LOG_INI_MPF_ENTER(mpfid);
	CHECK_TSKCTX_UNL_MYSTATE(&p_selftsk);
	CHECK_ID(VALID_MPFID(mpfid));
	p_mpfcb = get_mpfcb(mpfid);

	lock_cpu();
	acquire_glock();
	p_my_pcb = get_my_pcb();
	if (p_mpfcb->p_mpfinib->mpfatr == TA_NOEXS) {
		ercd = E_NOEXS;
	}
	else {
		init_wait_queue(p_my_pcb, &(p_mpfcb->wait_queue));
		p_mpfcb->fblkcnt = p_mpfcb->p_mpfinib->blkcnt;
		p_mpfcb->unused = 0U;
		p_mpfcb->freelist = INDEX_NULL;
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
	LOG_INI_MPF_LEAVE(ercd);
	return(ercd);
}

#endif /* TOPPERS_ini_mpf */

/*
 *  固定長メモリプールの状態参照
 */
#ifdef TOPPERS_ref_mpf

ER
ref_mpf(ID mpfid, T_RMPF *pk_rmpf)
{
	MPFCB	*p_mpfcb;
	ER		ercd;

	LOG_REF_MPF_ENTER(mpfid, pk_rmpf);
	CHECK_TSKCTX_UNL();
	CHECK_ID(VALID_MPFID(mpfid));
	p_mpfcb = get_mpfcb(mpfid);

	lock_cpu();
	acquire_glock();
	if (p_mpfcb->p_mpfinib->mpfatr == TA_NOEXS) {
		ercd = E_NOEXS;
	}
	else {
		pk_rmpf->wtskid = wait_tskid(&(p_mpfcb->wait_queue));
		pk_rmpf->fblkcnt = p_mpfcb->fblkcnt;
		ercd = E_OK;
	}
	release_glock();
	unlock_cpu();

  error_exit:
	LOG_REF_MPF_LEAVE(ercd, pk_rmpf);
	return(ercd);
}

#endif /* TOPPERS_ref_mpf */
