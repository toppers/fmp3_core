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
 *  $Id: dataqueue.h 178 2019-10-08 13:55:00Z ertl-honda $
 */

/*
 *		データキュー機能
 */

#ifndef TOPPERS_DATAQUEUE_H
#define TOPPERS_DATAQUEUE_H

#include "kernel_impl.h"
#include <queue.h>
#include "wait.h"

/*
 *  データ管理ブロック
 */
typedef struct data_management_block {
	intptr_t	data;			/* データ本体 */
} DTQMB;

/*
 *  データキュー初期化ブロック
 *
 *  この構造体は，同期・通信オブジェクトの初期化ブロックの共通部分
 *  （WOBJINIB）を拡張（オブジェクト指向言語の継承に相当）したもので，
 *  最初のフィールドが共通になっている．
 */
typedef struct dataqueue_initialization_block {
	ATR			dtqatr;			/* データキュー属性 */
	uint_t		dtqcnt;			/* データキューの容量 */
	DTQMB		*p_dtqmb;		/* データキュー管理領域の先頭番地 */
} DTQINIB;

/*
 *  データキュー管理ブロック
 *
 *  この構造体は，同期・通信オブジェクトの管理ブロックの共通部分（WOBJCB）
 *  を拡張（オブジェクト指向言語の継承に相当）したもので，最初の2つの
 *  フィールドが共通になっている．
 */
typedef struct dataqueue_control_block {
	QUEUE		swait_queue;	/* データキュー送信待ちキュー */
	const DTQINIB *p_dtqinib;	/* 初期化ブロックへのポインタ */
	QUEUE		rwait_queue;	/* データキュー受信待ちキュー */
	uint_t		count;			/* データキュー中のデータの数 */
	uint_t		head;			/* 最初のデータの格納場所 */
	uint_t		tail;			/* 最後のデータの格納場所の次 */
} DTQCB;

/*
 *  データキューIDの最大値（kernel_cfg.c）
 */
extern const ID	tmax_dtqid;
extern const ID	tmax_sdtqid;		/* 静的生成データキューのID番号の最大値 */

/*
 *  使用していないデータキュー管理ブロックのリスト（dataqueue.c）
 *
 *  DTQCBの先頭フィールドがQUEUE（swait_queue）なので，そのまま
 *  free-listのリンクに流用する（dcre dataqueue.c:183-189 と同一）．
 *  段階2のcyc/almで用いたtmevtb領域のオーバレイは不要である．
 */
extern QUEUE	free_dtqcb;

/*
 *  データキュー初期化ブロックのエリア（kernel_cfg.c）
 */
extern const DTQINIB	dtqinib_table[];

/*
 *  動的生成データキューの初期化ブロックのエリア（kernel_cfg.c・RAM）
 */
extern DTQINIB	adtqinib_table[];

/*
 *  データキュー管理ブロックのエリアへのポインタテーブル（kernel_cfg.c）
 */
extern DTQCB *const	p_dtqcb_table[];

/*
 *  データキューの数
 *
 *  DTQIDマクロから参照するためdataqueue.cから移設した．
 */
#define tnum_dtq	((uint_t)(tmax_dtqid - TMIN_DTQID + 1))
#define tnum_sdtq	((uint_t)(tmax_sdtqid - TMIN_DTQID + 1))

/*
 *  データキュー管理ブロックからデータキューIDを取り出すためのマクロ
 *
 *  FMP3のDTQCBはポインタ表（p_dtqcb_table）経由で参照される個別の
 *  named staticであり，DTQCB自身の配列位置から番号を引けない．元から
 *  DTQINIBへのポインタ差分で求めていた式を，動的生成データキュー
 *  （p_dtqinibがadtqinib_tableを指す）と静的生成データキュー
 *  （p_dtqinibがdtqinib_tableを指す）の2レンジに拡張する
 *  （段階2のCYCID・段階3aのSEMIDと同型）．AID_DTQが無い構成では
 *  tnum_dtq == tnum_sdtqとなり第1項が常に偽＝従来と同一の式に落ちる．
 */
#define	DTQID(p_dtqcb) \
	((((p_dtqcb)->p_dtqinib >= adtqinib_table) \
		&& ((p_dtqcb)->p_dtqinib < &adtqinib_table[tnum_dtq - tnum_sdtq])) \
	  ? ((ID)(((p_dtqcb)->p_dtqinib - adtqinib_table) + TMIN_DTQID + tnum_sdtq)) \
	  : ((ID)(((p_dtqcb)->p_dtqinib - dtqinib_table) + TMIN_DTQID)))

/*
 *  データキュー機能の初期化
 */
extern void	initialize_dataqueue(PCB *p_my_pcb);

/*
 *  データキュー管理領域へのデータの格納
 */
extern void	enqueue_data(DTQCB *p_dtqcb, intptr_t data);

/*
 *  データキュー管理領域へのデータの強制格納
 */
extern void	force_enqueue_data(DTQCB *p_dtqcb, intptr_t data);

/*
 *  データキュー管理領域からのデータの取出し
 */
extern void	dequeue_data(DTQCB *p_dtqcb, intptr_t *p_data);

/*
 *  データキューへのデータ送信
 */
extern bool_t	send_data(PCB *p_my_pcb, DTQCB *p_dtqcb, intptr_t data);

/*
 *  データキューへのデータ強制送信
 */
extern void	force_send_data(PCB *p_my_pcb, DTQCB *p_dtqcb, intptr_t data);

/*
 *  データキューからのデータ受信
 */
extern bool_t	receive_data(PCB *p_my_pcb, DTQCB *p_dtqcb, intptr_t *p_data);

#endif /* TOPPERS_DATAQUEUE_H */
