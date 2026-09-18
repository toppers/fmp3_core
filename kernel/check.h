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
 *  $Id: check.h 335 2023-04-18 10:50:40Z ertl-honda $
 */

/*
 *		エラーチェック用マクロ
 */

#ifndef TOPPERS_CHECK_H
#define TOPPERS_CHECK_H

#include "kernel_impl.h"
#include <sil.h>

/*
 *  オブジェクトIDの範囲の判定
 */
#define VALID_TSKID(tskid)	(TMIN_TSKID <= (tskid) && (tskid) <= tmax_tskid)
#define VALID_SEMID(semid)	(TMIN_SEMID <= (semid) && (semid) <= tmax_semid)
#define VALID_FLGID(flgid)	(TMIN_FLGID <= (flgid) && (flgid) <= tmax_flgid)
#define VALID_DTQID(dtqid)	(TMIN_DTQID <= (dtqid) && (dtqid) <= tmax_dtqid)
#define VALID_PDQID(pdqid)	(TMIN_PDQID <= (pdqid) && (pdqid) <= tmax_pdqid)
#define VALID_MTXID(mtxid)	(TMIN_MTXID <= (mtxid) && (mtxid) <= tmax_mtxid)
#define VALID_MPFID(mpfid)	(TMIN_MPFID <= (mpfid) && (mpfid) <= tmax_mpfid)
#define VALID_CYCID(cycid)	(TMIN_CYCID <= (cycid) && (cycid) <= tmax_cycid)
#define VALID_ALMID(almid)	(TMIN_ALMID <= (almid) && (almid) <= tmax_almid)
#define VALID_SPNID(spnid)	(TMIN_SPNID <= (spnid) && (spnid) <= tmax_spnid)
#define VALID_ISRID(isrid)	(TMIN_ISRID <= (isrid) && (isrid) <= tmax_isrid)

/*
 *  優先度の範囲の判定
 */
#define VALID_TPRI(tpri)	(TMIN_TPRI <= (tpri) && (tpri) <= TMAX_TPRI)

/*
 *  データ優先度の範囲の判定
 */
#define VALID_DPRI(dpri)	(TMIN_DPRI <= (dpri) && (dpri) <= TMAX_DPRI)

/*
 *  割込みサービスルーチン優先度の範囲の判定
 */
#define VALID_ISRPRI(isrpri) \
				(TMIN_ISRPRI <= (isrpri) && (isrpri) <= TMAX_ISRPRI)

/*
 *  相対時間の範囲の判定
 */
#define VALID_RELTIM(reltim)	((reltim) <= TMAX_RELTIM)

/*
 *  タイムアウト指定値の範囲の判定
 */
#define VALID_TMOUT(tmout)	((tmout) <= TMAX_RELTIM || (tmout) == TMO_FEVR)

/*
 *  呼出しコンテキストのチェック（E_CTX）
 */
#ifndef OMIT_CHECK_TSKCTX

Inline bool_t
check_tskctx(void)
{
	bool_t	state;
	SIL_PRE_LOC;

	SIL_LOC_INT();
	state = sense_context(get_my_pcb());
	SIL_UNL_INT();
	return(state);
}

#endif /* OMIT_CHECK_TSKCTX */

#define CHECK_TSKCTX() do {									\
	if (check_tskctx()) {									\
		ercd = E_CTX;										\
		goto error_exit;									\
	}														\
} while (false)


/*
 *  CPUロック状態のチェック（E_CTX）
 */
#define CHECK_UNL() do {									\
	if (sense_lock()) {										\
		ercd = E_CTX;										\
		goto error_exit;									\
	}														\
} while (false)

/*
 *  CPUロック状態のチェック（E_CTX）
 */
#ifndef OMIT_CHECK_UNL_MYSTATE

Inline bool_t
check_unl_mystate(TCB **pp_selftsk, bool_t *p_context)
{
	PCB		*p_my_pcb;
	SIL_PRE_LOC;

	SIL_LOC_INT();
	p_my_pcb = get_my_pcb();
	*p_context = sense_context(p_my_pcb);
	*pp_selftsk = p_my_pcb->p_runtsk;
	SIL_UNL_INT();
	return(sense_lock());
}

#endif /* OMIT_CHECK_UNL_MYSTATE */

#define CHECK_UNL_MYSTATE(pp_selftsk, p_context) do {		\
	if (check_unl_mystate(pp_selftsk, p_context)) {			\
		ercd = E_CTX;										\
		goto error_exit;									\
	}														\
} while (false)

/*
 *  呼出しコンテキストとCPUロック状態のチェック（E_CTX）
 */
#ifndef OMIT_CHECK_TSKCTX_UNL

Inline bool_t
check_tskctx_unl(void)
{
	bool_t	context;
	SIL_PRE_LOC;

	SIL_LOC_INT();
	context = sense_context(get_my_pcb());
	SIL_UNL_INT();
	return(context || sense_lock());
}

#endif /* OMIT_CHECK_TSKCTX_UNL */

#define CHECK_TSKCTX_UNL() do {								\
	if (check_tskctx_unl()) {								\
		ercd = E_CTX;										\
		goto error_exit;									\
	}														\
} while (false)

/*
 *  呼出しコンテキストとCPUロック状態のチェック（E_CTX）
 */
#ifndef OMIT_CHECK_TSKCTX_UNL_MYSTATE

Inline bool_t
check_tskctx_unl_mystate(TCB **pp_selftsk)
{
	bool_t	context;
	PCB		*p_my_pcb;
	SIL_PRE_LOC;

	SIL_LOC_INT();
	p_my_pcb = get_my_pcb();
	context = sense_context(p_my_pcb);
	*pp_selftsk = p_my_pcb->p_runtsk;
	SIL_UNL_INT();
	return(context || sense_lock());
}

#endif /* OMIT_CHECK_TSKCTX_UNL_MYSTATE */

#define CHECK_TSKCTX_UNL_MYSTATE(pp_selftsk) do {			\
	if (check_tskctx_unl_mystate(pp_selftsk)) {				\
		ercd = E_CTX;										\
		goto error_exit;									\
	}														\
} while (false)

/*
 *  ディスパッチ保留状態でないかのチェック（E_CTX）
 */
#ifndef OMIT_CHECK_DISPATCH

Inline bool_t
check_dispatch(void)
{
	bool_t	state;
	PCB		*p_my_pcb;
	SIL_PRE_LOC;

	SIL_LOC_INT();
	p_my_pcb = get_my_pcb();
	state = (sense_context(p_my_pcb) || !(p_my_pcb->dspflg));
	SIL_UNL_INT();
	return(state || sense_lock());
}

#endif /* OMIT_CHECK_DISPATCH */

#define CHECK_DISPATCH() do {								\
	if (check_dispatch()) {									\
		ercd = E_CTX;										\
		goto error_exit;									\
	}														\
} while (false)

/*
 *  ディスパッチ保留状態でないかのチェック（E_CTX）
 */
#ifndef OMIT_CHECK_DISPATCH_MYSTATE

Inline bool_t
check_dispatch_mystate(TCB **pp_selftsk)
{
	bool_t	state;
	PCB		*p_my_pcb;
	SIL_PRE_LOC;

	SIL_LOC_INT();
	p_my_pcb = get_my_pcb();
	state = (sense_context(p_my_pcb) || !(p_my_pcb->dspflg));
	*pp_selftsk = p_my_pcb->p_runtsk;
	SIL_UNL_INT();
	return(state || sense_lock());
}

#endif /* OMIT_CHECK_DISPATCH_MYSTATE */

#define CHECK_DISPATCH_MYSTATE(pp_selftsk) do {	\
	if (check_dispatch_mystate(pp_selftsk)) {	\
		ercd = E_CTX;										\
		goto error_exit;									\
	}														\
} while (false)

/*
 *  その他のコンテキストエラーのチェック（E_CTX）
 */
#define CHECK_CTX(exp) do {									\
	if (!(exp)) {											\
		ercd = E_CTX;										\
		goto error_exit;									\
	}														\
} while (false)

/*
 *  不正ID番号のチェック（E_ID）
 */
#define CHECK_ID(exp) do {									\
	if (!(exp)) {											\
		ercd = E_ID;										\
		goto error_exit;									\
	}														\
} while (false)

/*
 *  属性が無効なビットが立っていないかのチェック（E_RSATR）
 */
#define CHECK_VALIDATR(atr, valid_atr) do {					\
	if (((atr) & ~(valid_atr)) != 0U) {						\
		ercd = E_RSATR;										\
		goto error_exit;									\
	}														\
} while (false)

/*
 *  パラメータエラーのチェック（E_PAR）
 */
#define CHECK_PAR(exp) do {									\
	if (!(exp)) {											\
		ercd = E_PAR;										\
		goto error_exit;									\
	}														\
} while (false)

/*
 *  サービスコール不正使用のチェック（E_ILUSE）
 */
#define CHECK_ILUSE(exp) do {								\
	if (!(exp)) {											\
		ercd = E_ILUSE;										\
		goto error_exit;									\
	}														\
} while (false)

/*
 *  オブジェクト状態エラーのチェック（E_OBJ）
 */
#define CHECK_OBJ(exp) do {									\
	if (!(exp)) {											\
		ercd = E_OBJ;										\
		goto error_exit;									\
	}														\
} while (false)


/*
 *  プロセッサIDの範囲の判定
 */
#define VALID_PRCID(prcid)	(TMIN_PRCID <= (prcid) && (prcid) <= TMAX_PRCID)

/*
 *  プロセッサIDのチェック
 */
#define CHECK_PRCID(prcid) do {								\
	if (!VALID_PRCID(prcid)) {								\
		ercd = E_ID;										\
		goto error_exit;									\
	}														\
} while (false)

/*
 *  マイグレーション可能の判定
 */
#define VALID_MIG(affinity, prcid) \
					((affinity & (1U << INDEX_PRC(prcid))) != 0U)

/*
 *  マイグレーション可能チェック・ロック解除（E_PAR）
 */
#define CHECK_MIG(affinity, prcid) do {						\
	if (!VALID_MIG(affinity, prcid)) {						\
		ercd = E_PAR;										\
		goto error_exit;									\
	}														\
} while (false)

/*
 *  ディスパッチ禁止状態のチェック
 */
#ifndef OMIT_CHECK_DISDSP

Inline bool_t
check_disdsp(void)
{
	bool_t	disdsp;
	SIL_PRE_LOC;

	SIL_LOC_INT();
	disdsp = !(get_my_pcb()->enadsp);
	SIL_UNL_INT();
	return(disdsp);
}

#endif /* OMIT_CHECK_DISDSP */

/*
 *  自タスクのTCBへのポインタの取得
 */
#ifndef OMIT_GET_P_SELFTSK

Inline TCB *
get_p_selftsk(void)
{
	TCB		*p_selftsk;
	SIL_PRE_LOC;

	SIL_LOC_INT();
	p_selftsk = get_my_pcb()->p_runtsk;
	SIL_UNL_INT();
	return(p_selftsk);
}

#endif /* OMIT_GET_P_SELFTSK */

/*
 *  アラインしているかの判定
 */
#define ALIGNED(val, align)		((((uintptr_t)(val)) & ((align) - 1U)) == 0U)

#ifdef CHECK_STKSZ_ALIGN
#define STKSZ_ALIGN(stksz)		ALIGNED(stksz, CHECK_STKSZ_ALIGN)
#else /* CHECK_STKSZ_ALIGN */
#define STKSZ_ALIGN(stksz)		true
#endif /* CHECK_STKSZ_ALIGN */

#ifdef CHECK_INTPTR_ALIGN
#define INTPTR_ALIGN(p_var)		ALIGNED(p_var, CHECK_INTPTR_ALIGN)
#else /* CHECK_INTPTR_ALIGN */
#define INTPTR_ALIGN(p_var)		true
#endif /* CHECK_INTPTR_ALIGN */

#ifdef CHECK_FUNC_ALIGN
#define FUNC_ALIGN(func)		ALIGNED(func, CHECK_FUNC_ALIGN)
#else /* CHECK_FUNC_ALIGN */
#define FUNC_ALIGN(func)		true
#endif /* CHECK_FUNC_ALIGN */

#ifdef CHECK_STACK_ALIGN
#define STACK_ALIGN(stack)		ALIGNED(stack, CHECK_STACK_ALIGN)
#else /* CHECK_STACK_ALIGN */
#define STACK_ALIGN(stack)		true
#endif /* CHECK_STACK_ALIGN */

#ifdef CHECK_MPF_ALIGN
#define MPF_ALIGN(mpf)			ALIGNED(mpf, CHECK_MPF_ALIGN)
#else /* CHECK_MPF_ALIGN */
#define MPF_ALIGN(mpf)			true
#endif /* CHECK_MPF_ALIGN */

#ifdef CHECK_MB_ALIGN
#define MB_ALIGN(mb)			ALIGNED(mb, CHECK_MB_ALIGN)
#else /* CHECK_MB_ALIGN */
#define MB_ALIGN(mb)			true
#endif /* CHECK_MB_ALIGN */

/*
 *  NULLでないことの判定
 */
#ifdef CHECK_FUNC_NONNULL
#define FUNC_NONNULL(func)		((func) != NULL)
#else /* CHECK_FUNC_NONNULL */
#define FUNC_NONNULL(func)		true
#endif /* CHECK_FUNC_NONNULL */

#ifdef CHECK_INTPTR_NONNULL
#define INTPTR_NONNULL(p_var)	((p_var) != NULL)
#else /* CHECK_INTPTR_NONNULL */
#define INTPTR_NONNULL(p_var)	true
#endif /* CHECK_INTPTR_NONNULL */

#endif /* TOPPERS_CHECK_H */
