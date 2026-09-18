/*
 *  TOPPERS/FMP Kernel
 *      Toyohashi Open Platform for Embedded Real-Time Systems/
 *      Flexible MultiProcessor Kernel
 * 
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
 *  $Id: mutex.h 335 2023-04-18 10:50:40Z ertl-honda $
 */

/*
 *		ミューテックス機能
 */

#ifndef TOPPERS_MUTEX_H
#define TOPPERS_MUTEX_H

#include "kernel_impl.h"
#include <queue.h>
#include "wait.h"

/*
 *  ミューテックス初期化ブロック
 *
 *  この構造体は，同期・通信オブジェクトの初期化ブロックの共通部分
 *  （WOBJINIB）を拡張（オブジェクト指向言語の継承に相当）したもので，
 *  最初のフィールドが共通になっている．
 */
typedef struct mutex_initialization_block {
	ATR			mtxatr;			/* ミューテックス属性 */
	uint_t		ceilpri;		/* ミューテックスの上限優先度（内部表現）*/
} MTXINIB;

/*
 *  ミューテックス管理ブロック
 *
 *  この構造体は，同期・通信オブジェクトの管理ブロックの共通部分（WOBJCB）
 *  を拡張（オブジェクト指向言語の継承に相当）したもので，最初の2つの
 *  フィールドが共通になっている．
 */
typedef struct mutex_control_block MTXCB;

typedef struct mutex_control_block {
	QUEUE		wait_queue;		/* ミューテックス待ちキュー */
	const MTXINIB *p_mtxinib;	/* 初期化ブロックへのポインタ */
	TCB			*p_loctsk;		/* ミューテックスをロックしているタスク */
	MTXCB		*p_prevmtx;		/* この前にロックしたミューテックス */
} MTXCB;

/*
 *  ミューテックスIDの最大値（kernel_cfg.c）
 */
extern const ID	tmax_mtxid;
extern const ID	tmax_smtxid;		/* 静的生成ミューテックスのID番号の最大値 */

/*
 *  使用していないミューテックス管理ブロックのリスト（mutex.c）
 *
 *  MTXCBの先頭フィールドがQUEUE（wait_queue）なので，そのまま
 *  free-listのリンクに流用する（dcre mutex.c:164-171 と同一）．
 *  段階2のcyc/almで用いたtmevtb領域のオーバレイは不要である．
 */
extern QUEUE	free_mtxcb;

/*
 *  ミューテックス初期化ブロックのエリア（kernel_cfg.c）
 */
extern const MTXINIB	mtxinib_table[];

/*
 *  動的生成ミューテックスの初期化ブロックのエリア（kernel_cfg.c・RAM）
 */
extern MTXINIB	amtxinib_table[];

/*
 *  ミューテックス管理ブロックへのポインタテーブル（kernel_cfg.c）
 */
extern MTXCB *const	p_mtxcb_table[];

/*
 *  ミューテックスの数
 *
 *  MTXIDマクロから参照するためmutex.cから移設した．
 */
#define tnum_mtx	((uint_t)(tmax_mtxid - TMIN_MTXID + 1))
#define tnum_smtx	((uint_t)(tmax_smtxid - TMIN_MTXID + 1))

/*
 *  ミューテックス管理ブロックからミューテックスIDを取り出すためのマクロ
 *
 *  FMP3のMTXCBはポインタ表（p_mtxcb_table）経由で参照される個別の
 *  named staticであり，MTXCB自身の配列位置から番号を引けない．元から
 *  MTXINIBへのポインタ差分で求めていた式を，動的生成ミューテックス
 *  （p_mtxinibがamtxinib_tableを指す）と静的生成ミューテックス
 *  （p_mtxinibがmtxinib_tableを指す）の2レンジに拡張する
 *  （段階2のCYCIDと同型）．AID_MTXが無い構成ではtnum_mtx == tnum_smtx
 *  となり第1項が常に偽＝従来と同一の式に落ちる．
 */
#define	MTXID(p_mtxcb) \
	((((p_mtxcb)->p_mtxinib >= amtxinib_table) \
		&& ((p_mtxcb)->p_mtxinib < &amtxinib_table[tnum_mtx - tnum_smtx])) \
	  ? ((ID)(((p_mtxcb)->p_mtxinib - amtxinib_table) + TMIN_MTXID + tnum_smtx)) \
	  : ((ID)(((p_mtxcb)->p_mtxinib - mtxinib_table) + TMIN_MTXID)))

/*
 *  ミューテックス機能の初期化
 */
extern void	initialize_mutex(PCB *p_my_pcb);

/*
 *  上限優先度違反のチェック
 *
 *  chg_priの中で上限優先度違反のチェックを行うために用いる関数であり，
 *  p_tcbで指定されるタスクがロックしている優先度上限ミューテックスと，
 *  ロックを待っている優先度上限ミューテックスの中で，上限優先度が
 *  bpriorityよりも低いものがあればfalseを，そうでなければtrueを返す．
 */
extern bool_t	mutex_check_ceilpri(TCB *p_tcb, uint_t bpriority);

/*
 *  ミューテックスのロック
 *
 *  p_tcbで指定されるタスクに，p_mtxcbで指定されるミューテックスをロッ
 *  クさせる．
 */
extern void	mutex_acquire(PCB *p_my_pcb, TCB *p_selftsk, MTXCB *p_mtxcb);

/*
 *  ミューテックスのロック解除
 *
 *  p_mtxcbで指定されるミューテックスをロック解除する．ロック解除した
 *  ミューテックスに，ロック待ち状態のタスクがある場合には，そのタスク
 *  にミューテックスをロックさせる．
 */
extern void	mutex_release(PCB *p_my_pcb, MTXCB *p_mtxcb);

/*
 *  タスクがロックしているすべてのミューテックスのロック解除
 *
 *  p_tcbで指定されるタスクに，それがロックしているすべてのミューテック
 *  スをロック解除させる．ロック解除したミューテックスに，ロック待ち状
 *  態のタスクがある場合には，そのタスクにミューテックスをロックさせる．
 *
 *  この関数は，タスクの終了時に使われるものであるため，p_tcbで指定され
 *  るタスクの優先度を変更する処理は行わない．ただし，この関数の中で他
 *  のタスクの優先度が変化し，実行すべきタスクが変わることがある．その
 *  ため，この関数から戻った後に，ディスパッチが必要か判別して，必要な
 *  場合にはディスパッチを行わなければならない．
 */
extern void	mutex_release_all(PCB *p_my_pcb, TCB *p_tcb);

#endif /* TOPPERS_MUTEX_H */
