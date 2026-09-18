/*
 *  TOPPERS/FMP Kernel
 *      Toyohashi Open Platform for Embedded Real-Time Systems/
 *      Flexible MultiProcessor Kernel
 * 
 *  Copyright (C) 2000 by Embedded and Real-Time Systems Laboratory
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
 *  $Id: interrupt.h 335 2023-04-18 10:50:40Z ertl-honda $
 */

/*
 *		割込み管理機能
 */

#ifndef TOPPERS_INTERRUPT_H
#define TOPPERS_INTERRUPT_H

#include "kernel_impl.h"
#include <queue.h>

/*
 *  割込みサービスルーチン呼出しキュー管理ブロック
 *
 *  ★dcreのisr_queue_table[]は素のQUEUE配列（dcre interrupt.h:93）だが，
 *  FMP3はキューごとにenqueue世代番号（isrseq）のカウンタを持つ必要がある
 *  ため専用の型を新設した．isr_queueが先頭メンバなので，(ISRQCB *)への
 *  キャストとqueue_*操作はISRCBと同じ技法で書ける．
 *
 *  isrseqは「次にenqueueするISRCBへ与える世代番号」であり，キューの
 *  生存期間を通じて単調増加する（リセットしない）．これにより走査中の
 *  キュー一時空→再enqueueでも，走査位置（cur）と同じかそれより後の
 *  isrpri位置に入る新エントリのキーは必ず走査位置より大きく，同一起動内
 *  で取りこぼされない（del→再acreの同一isrpriケースを含む）．curより
 *  高優先（isrpriが小さい）位置に入る新エントリは走査が既に通過した
 *  位置のため本起動では実行されず，次回の割込み発生から有効になる
 *  （ISR_KEY_GTがisrpri第一キーの辞書式比較であるため．静的インライン
 *  連鎖の「起動開始時点のISR集合を処理する」意味論と同じであり仕様．
 *  詳細はinterrupt.cのenqueue_isrコメント参照）．u32のラップには単一
 *  キューへの2^32回のenqueueが必要であり実用上到達しないものとして
 *  受容する．
 */
typedef struct isr_queue_control_block {
	QUEUE		isr_queue;		/* 割込みサービスルーチン呼出しキュー */
	uint32_t	isrseq;			/* 次に採番するenqueue世代番号 */
} ISRQCB;

/*
 *  割込みサービスルーチン初期化ブロック
 */
typedef struct isr_initialization_block {
	ATR			isratr;			/* 割込みサービスルーチン属性 */
	EXINF		exinf;			/* 割込みサービスルーチンの拡張情報 */
	ISRQCB		*p_isr_queue;	/* 登録先割込みサービスルーチン呼出しキュー */
	ISR			isr;			/* 割込みサービスルーチンの先頭番地 */
	PRI			isrpri;			/* 割込みサービスルーチン優先度 */
} ISRINIB;

/*
 *  割込みサービスルーチン管理ブロック
 *
 *  isr_queueは，キューへの登録エントリと，使用していないISRCBのリスト
 *  （free_isrcb）のリンクを兼ねる（dcre interrupt.c:337,384と同一）．
 *
 *  isrseqとrunningはFMP3の追加である．
 *  ・isrseq: enqueueされたときにISRQCBのカウンタから採番される世代番号．
 *    call_isrの走査が「次に呼ぶISR」を(isrpri, isrseq)の辞書式順序で
 *    再決定するための安定キーである．同一isrpriのISRが複数あるとき，
 *    isrpriだけでは走査の再開位置を決められない（削除と再生成で
 *    スロットが再利用されるとポインタやIDでも決められない）．
 *  ・running: 当該ISRの本体を実行中のプロセッサのビットマップ．
 *    del_isrは，キューから外した後この値が0になるまで待つ（quiesce）．
 *    ビットマップなので複数コアが同時に実行していても互いに壊さない．
 */
typedef struct isr_control_block {
	QUEUE		isr_queue;		/* 割込みサービスルーチン呼出しキュー */
	const ISRINIB *p_isrinib;	/* 初期化ブロックへのポインタ */
	uint32_t	isrseq;			/* enqueue世代番号 */
	uint_t		running;		/* 実行中プロセッサのビットマップ */
} ISRCB;

/*
 *  割込みサービスルーチン呼出しキューを検索するためのデータ構造
 *
 *  cfgが生成するこの表が，動的ISR生成の対象として適格な割込み番号の
 *  グローバルな一覧である（ENA_DYNISRされたintnoだけが載る）．intnoの
 *  昇順にソートされており，acre_isrが二分探索で引く．per-coreのビット
 *  マップ（check_intno_cfg）を使わないので，判定が呼出しコアに依存しない．
 */
typedef struct {
	INTNO		intno;			/* 割込み番号 */
	ISRQCB		*p_isr_queue;	/* 割込みサービスルーチン呼出しキュー */
} ISR_ENTRY;

/*
 *  割込みサービスルーチン呼出しキューのエントリ数（kernel_cfg.c）
 */
extern const uint_t	tnum_isr_queue;

/*
 *  割込みサービスルーチン呼出しキューリスト（kernel_cfg.c）
 */
extern const ISR_ENTRY	isr_queue_list[];

/*
 *  割込みサービスルーチン呼出しキューのエリア（kernel_cfg.c）
 */
extern ISRQCB	isr_queue_table[];

/*
 *  使用していない割込みサービスルーチン管理ブロックのリスト（interrupt.c）
 */
extern QUEUE	free_isrcb;

/*
 *  割込みサービスルーチンIDの最大値（kernel_cfg.c）
 */
extern const ID	tmax_isrid;
extern const ID	tmax_sisrid;	/* 静的生成ISRのID番号の最大値 */

/*
 *  割込みサービスルーチン初期化ブロックのエリア（kernel_cfg.c）
 */
extern const ISRINIB	isrinib_table[];

/*
 *  動的生成割込みサービスルーチンの初期化ブロックのエリア（kernel_cfg.c・RAM）
 */
extern ISRINIB			aisrinib_table[];

/*
 *  割込みサービスルーチン生成順序テーブル（kernel_cfg.c）
 */
extern const ID	isrorder_table[];

/*
 *  割込みサービスルーチン管理ブロックのエリアへのポインタテーブル（kernel_cfg.c）
 */
extern ISRCB *const	p_isrcb_table[];

/*
 *  割込みサービスルーチンの数
 */
#define tnum_isr	((uint_t)(tmax_isrid - TMIN_ISRID + 1))
#define tnum_sisr	((uint_t)(tmax_sisrid - TMIN_ISRID + 1))

/*
 *  割込みサービスルーチン管理ブロックから割込みサービスルーチンIDを取り
 *  出すためのマクロ
 *
 *  dcre（interrupt.h:126）はISRCBの実体配列からの差分で求めるが，FMP3の
 *  ISRCBはポインタ表（p_isrcb_table）経由で参照される個別のnamed static
 *  であり，ISRCB自身の配列位置から番号を引けない．そのためISRINIBへの
 *  ポインタ差分で求め，動的生成ISR（p_isrinibがaisrinib_tableを指す）と
 *  静的生成ISR（p_isrinibがisrinib_tableを指す）の2レンジに分ける
 *  （段階2のCYCID・段階3aのSEMID・段階3bのDTQIDと同型）．AID_ISRが無い
 *  構成ではtnum_isr == tnum_sisrとなり第1項が常に偽＝静的レンジの式に落ちる．
 */
#define	ISRID(p_isrcb) \
	((((p_isrcb)->p_isrinib >= aisrinib_table) \
		&& ((p_isrcb)->p_isrinib < &aisrinib_table[tnum_isr - tnum_sisr])) \
	  ? ((ID)(((p_isrcb)->p_isrinib - aisrinib_table) + TMIN_ISRID + tnum_sisr)) \
	  : ((ID)(((p_isrcb)->p_isrinib - isrinib_table) + TMIN_ISRID)))

/*
 *  割込みサービスルーチン機能の初期化
 */
extern void	initialize_isr(PCB *p_my_pcb);

/*
 *  割込みサービスルーチンの呼出し
 */
extern void	call_isr(ISRQCB *p_isr_queue);

#if !defined(OMIT_INITIALIZE_INTERRUPT) || defined(USE_INHINIB_TABLE)

/*
 *  割込みハンドラ初期化ブロック
 */
typedef struct interrupt_handler_initialization_block {
	INHNO		inhno;			/* 割込みハンドラ番号 */
	ATR			inhatr;			/* 割込みハンドラ属性 */
	FP			int_entry;		/* 割込みハンドラの出入口処理の番地 */
	ID			prcid;			/* 割込みハンドラの割付けプロセッサ */
} INHINIB;

/*
 *  定義する割込みハンドラ番号の数（kernel_cfg.c）
 */
extern const uint_t	tnum_def_inhno;

/*
 *  割込みハンドラ初期化ブロックのエリア（kernel_cfg.c）
 */
extern const INHINIB	inhinib_table[];

#endif /* !defined(OMIT_INITIALIZE_INTERRUPT) || defined(USE_INHINIB_TABLE) */

#if !defined(OMIT_INITIALIZE_INTERRUPT) || defined(USE_INTINIB_TABLE)

/*
 *  割込み要求ライン初期化ブロック
 */
typedef struct interrupt_request_initialization_block {
	INTNO		intno;			/* 割込み番号 */
	ATR			intatr;			/* 割込み属性 */
	PRI			intpri;			/* 割込み優先度 */
	ID			iprcid;			/* 割込みの初期割付けプロセッサ */
	uint_t		affinity;		/* 割込みの割付け可能プロセッサ */
} INTINIB;

/*
 *  設定する割込み要求ラインの数（kernel_cfg.c）
 */
extern const uint_t	tnum_cfg_intno;

/*
 *  割込み要求ライン初期化ブロックのエリア（kernel_cfg.c）
 */
extern const INTINIB	intinib_table[];

#endif /* !defined(OMIT_INITIALIZE_INTERRUPT) || defined(USE_INTINIB_TABLE) */

/*
 *  割込み管理機能の初期化
 */
extern void	initialize_interrupt(PCB *p_my_pcb);

#endif /* TOPPERS_INTERRUPT_H */
