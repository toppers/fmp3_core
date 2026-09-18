/*
 *  TOPPERS/FMP Kernel
 *      Toyohashi Open Platform for Embedded Real-Time Systems/
 *      Flexible MultiProcessor Kernel
 * 
 *  Copyright (C) 2000-2003 by Embedded and Real-Time Systems Laboratory
 *                              Toyohashi Univ. of Technology, JAPAN
 *  Copyright (C) 2005-2025 by Embedded and Real-Time Systems Laboratory
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
 *  $Id: startup.c 464 2026-05-27 11:42:34Z ertl-honda $
 */

/*
 *		カーネルの初期化と終了処理
 */

#include "kernel_impl.h"
#include "time_event.h"
#include "spin_lock.h"
#include "target_ipi.h"
#include <sil.h>

/*
 *  トレースログマクロのデフォルト定義
 */
#ifndef LOG_KER_ENTER
#define LOG_KER_ENTER()
#endif /* LOG_KER_ENTER */

#ifndef LOG_KER_LEAVE
#define LOG_KER_LEAVE()
#endif /* LOG_KER_LEAVE */

#ifndef LOG_EXT_KER_ENTER
#define LOG_EXT_KER_ENTER()
#endif /* LOG_EXT_KER_ENTER */

#ifndef LOG_EXT_KER_LEAVE
#define LOG_EXT_KER_LEAVE(ercd)
#endif /* LOG_EXT_KER_LEAVE */

#ifdef TOPPERS_barsync
#ifndef OMIT_BARRIER_SYNC

/*
 *  バリア同期用変数
 */
static volatile uint_t	prc_phase[TNUM_PRCID];
static volatile uint_t	sys_phase;

/*
 *  カーネルの起動／終了に用いるバリア同期
 */
void
barrier_sync(uint_t phase)
{
	uint_t	prcidx;
	uint_t	count;
	uint_t	i;

	prcidx = get_my_prcidx();
	prc_phase[prcidx] = phase;

	if (ID_PRC(prcidx) == TOPPERS_MASTER_PRCID) {
		/* マスタプロセッサの処理 */
		do {
			sil_dly_nse(100);
			count = 0;
			for (i = 0; i < TNUM_PRCID; i++) {
				if (prc_phase[i] == phase) {
					count++;
				}
			}
		} while (count < TNUM_PRCID);
		sys_phase = phase;
	}
	else {
		/* 他のプロセッサの処理 */
		while (sys_phase != phase) {
			sil_dly_nse(100);
		}
	}
}

#endif /* OMIT_BARRIER_SYNC */
#endif /* TOPPERS_barsync */

#ifdef TOPPERS_sta_ker

/*
 *  カーネルメモリプール領域有効フラグ
 */
bool_t	mpk_valid;

/*
 *  初期化ルーチンの呼び出し
 */
static void
call_inirtn(const INIRTNBB *p_inirtnbb)
{
	uint_t			i;
	const INIRTNB	*inirtnb_table;

	inirtnb_table = p_inirtnbb->inirtnb_table;
	for (i = 0; i < p_inirtnbb->tnum_inirtn; i++) {
		(*(inirtnb_table[i].inirtn))(inirtnb_table[i].exinf);
	}
}

/*
 *  カーネルの起動
 */
void
sta_ker(void)
{
	uint_t	my_prcidx;
	PCB		*p_my_pcb;

	/*
	 *  TECSの初期化
	 */
#ifndef TOPPERS_OMIT_TECS
	initialize_tecs();
#endif /* TOPPERS_OMIT_TECS */

	/*
	 *  p_my_pcbとプロセッサIDの初期化
	 */
	my_prcidx = get_my_prcidx();
	p_my_pcb = p_pcb_table[my_prcidx];
	p_my_pcb->prcid = ID_PRC(my_prcidx);

	/*
	 *  取得しているスピンロックの初期化
	 */
	p_my_pcb->p_locspn = NULL;

	/*
	 *  ターゲット依存の初期開始のバリア同期
	 */    
	barrier_sync(1);

	/*
	 *  ターゲット依存の初期化
	 */
	target_initialize(p_my_pcb);

	/*
	 *  ターゲット依存の初期化待ちのバリア同期
	 */
	barrier_sync(2);

	/*
	 *  カーネルメモリプール領域の初期化（マスタプロセッサのみ）
	 */
	if (p_my_pcb->prcid == TOPPERS_MASTER_PRCID) {
		if (mpksz > 0U && mpk != NULL) {
			mpk_valid = initialize_mempool(mpk, mpksz);
		}
		else {
			mpk_valid = false;
		}
	}

	/*
	 *  各モジュールの初期化
	 *
	 *  タイムイベント管理モジュールは他のモジュールより先に初期化
	 *  する必要がある．
	 */
	initialize_tmevt(p_my_pcb);								/*［ASPD1061］*/
	initialize_object(p_my_pcb);

	/*
	 *  ジャイアントロックの初期化（マスタプロセッサのみ）
	 */
	if (p_my_pcb->prcid == TOPPERS_MASTER_PRCID) {
		initialize_glock();
	}

	/*
	 *  初期化ルーチンの実行
	 */
	barrier_sync(3);

	if (p_my_pcb->prcid == TOPPERS_MASTER_PRCID) {
		call_inirtn(&(inirtnbb_table[0]));
	}

	barrier_sync(4);

	call_inirtn(&(inirtnbb_table[p_my_pcb->prcid]));

	/*
	 *  オブジェクトの初期化待ちのバリア同期
	 */
	barrier_sync(5);

	/*
	 *  高分解能タイマの設定
	 */
	if (p_my_pcb->prcid == TOPPERS_TMASTER_PRCID) {
		current_hrtcnt = target_hrt_get_current();		/*［ASPD1063］*/
	}

	barrier_sync(6);

	if (p_my_pcb->p_tevtcb != NULL) {
		/*
		 *  acquire_glockは，CPUロック状態を解除する場合があるが，ここ
		 *  でCPUロック状態を解除してはならないため，try_glockを用いて
		 *  ジャイアントロックを取得する．
		 */
		while (try_glock());
		set_hrt_event(p_my_pcb);						/*［ASPD1064］*/
		release_glock();
	}

	/*
	 *  カーネル動作の開始
	 */
	kerflg_table[my_prcidx] = true;
	LOG_KER_ENTER();
	start_dispatch();
	assert(0);
}

#endif /* TOPPERS_sta_ker */

/*
 *  カーネルの終了
 */
#ifdef TOPPERS_ext_ker

ER
ext_ker(void)
{
	PCB		*p_my_pcb;
	ID		prcid;
	ER		ercd;
	SIL_PRE_LOC;

	LOG_EXT_KER_ENTER();

	/*
	 *  割込みロック状態に移行
	 */
	SIL_LOC_INT();

	p_my_pcb = get_my_pcb();

	/*
	 *  スピンロックを取得している場合は，スピンロックを解放する
	 */
	force_unlock_spin(p_my_pcb);

	/*
	 *  カーネル動作の終了
	 */
	LOG_KER_LEAVE();
	kerflg_table[INDEX_PRC(p_my_pcb->prcid)] = false;

	/*
	 *  他のプロセッサへ終了処理を要求する
	 */
	for (prcid = TMIN_PRCID; prcid <= TMAX_PRCID; prcid++) {
		if (prcid != p_my_pcb->prcid) {
			request_ext_ker(prcid);
		}
	}

	/*
	 *  カーネルの終了処理の呼出し
	 *
	 *  非タスクコンテキストに切り換えて，exit_kernelを呼び出す．
	 */
	call_exit_kernel(p_my_pcb);

	/*
	 *  コンパイラの警告対策（ここへ来ることはないはず）
	 */
	SIL_UNL_INT();
	ercd = E_SYS;

	LOG_EXT_KER_LEAVE(ercd);
	return(ercd);
}

/*
 *  終了処理ルーチンの呼び出し
 */
static void
call_terrtn(const TERRTNBB *p_terrtnbb)
{
	uint_t			i;
	const TERRTNB	*terrtnb_table;

	terrtnb_table = p_terrtnbb->terrtnb_table;
	for (i = 0; i < p_terrtnbb->tnum_terrtn; i++) {
		(*(terrtnb_table[i].terrtn))(terrtnb_table[i].exinf);
	}
}

/*
 *  カーネルの終了処理
 */
void
exit_kernel(PCB *p_my_pcb)
{
	/*
	 *  SILスピンロックの強制解放
	 */
	TOPPERS_sil_force_unl_spn();

	barrier_sync(5);

	/*
	 *  終了処理ルーチンの実行
	 */
	call_terrtn(&(terrtnbb_table[p_my_pcb->prcid]));

	barrier_sync(6);

	if (p_my_pcb->prcid == TOPPERS_MASTER_PRCID) {
		call_terrtn(&(terrtnbb_table[0]));
	}

	barrier_sync(7);

	/*
	 *  ターゲット依存の終了処理
	 */
	target_exit();
	assert(0);
}

#endif /* TOPPERS_ext_ker */

/*
 *  ディスパッチハンドラ
 *
 *  非タスクコンテキストで実行されるため，他のプロセッサへマイグレート
 *  することはなく，CPUロック状態にせずに自プロセッサのPCBにアクセスし
 *  てよい．
 */
#ifdef TOPPERS_dsphdr
#ifndef OMIT_DISPATCH_HANDLER

void
dispatch_handler(void)
{
	assert(sense_context(get_my_pcb()));
	assert(!sense_lock());
}

#endif /* OMIT_DISPATCH_HANDLER */
#endif /* TOPPERS_dsphdr */

/*
 *  カーネル終了ハンドラ
 *
 *  非タスクコンテキストで実行されるため，他のプロセッサへマイグレート
 *  することはなく，CPUロック状態にせずに自プロセッサのPCBにアクセスし
 *  てよい．
 */
#ifdef TOPPERS_extkerhdr

void
ext_ker_handler(void)
{
	PCB		*p_my_pcb = get_my_pcb();
	SIL_PRE_LOC;

	assert(sense_context(p_my_pcb));
	assert(!sense_lock());

	/*
	 *  割込みロック状態に移行
	 */
	SIL_LOC_INT();

	/*
	 *  スピンロックを取得している場合は，スピンロックを解放する
	 */
	force_unlock_spin(p_my_pcb);

	/*
	 *  カーネル動作の終了
	 */
	LOG_KER_LEAVE();
	kerflg_table[INDEX_PRC(p_my_pcb->prcid)] = false;

	/*
	 *  カーネルの終了処理の呼出し
	 *
	 *  非タスクコンテキストに切り換えて，exit_kernelを呼び出す．
	 */
	call_exit_kernel(p_my_pcb);

	/*
	 *  コンパイラの警告対策（ここへ来ることはないはず）
	 */
	SIL_UNL_INT();
}

#endif /* TOPPERS_extkerhdr */

/*
 *  デフォルトのメモリプール管理機能
 *
 *  基本はメモリプール領域の先頭から順に割り当てるバンプ方式だが，解放
 *  されたメモリ領域を単方向リスト（freelist）に記録しておき，要求サイ
 *  ズが解放時の要求サイズと完全に一致し，かつ番地が要求アラインメント
 *  に適合する場合に限ってそれを再利用するメモリプール管理機能．サイズ
 *  が一致しない要求は従来どおりバンプで割り当てる（ブロックの分割・結
 *  合は行わない）．すべてのメモリ領域が解放されたとき（count == 0）は，
 *  従来どおり未使用領域の先頭番地をプール先頭へ戻し，freelist も空に
 *  する（backstop）．
 *
 *  ★dcreからの意図的な逸脱（機能拡張）：dcre の原形（extension/dcre/
 *  kernel/startup.c）は「すべてのメモリ領域が解放されるまで解放された
 *  メモリ領域を再利用しない」単純バンプであり，長寿命の割付けが1つで
 *  も生存している限り create/delete の反復でプールが単調に痩せて
 *  E_NOMEM に到達する．本実装は完全一致サイズの再利用でこの病理を除去
 *  する（DIVERGENCE_MAP.md 参照）．
 */
#ifdef TOPPERS_kermem
#ifndef OMIT_MEMPOOL_DEFAULT

/*
 *  割付けヘッダ
 *
 *  各割付けのユーザ領域の直前に置き，割付け時の要求サイズ（無加工）を
 *  記録する．解放後も上書きされずに残り，freelist 走査時のサイズ完全
 *  一致判定に使う．
 *
 *  レイアウト： [padding][MPHDR][ユーザ領域 size バイト]
 *
 *  ユーザ領域側を要求アラインメントに合わせ，ヘッダはその直前
 *  （ユーザ領域先頭 - sizeof(MPHDR)）に置く．ヘッダ自身のアラインメン
 *  トは，alignment が2の巾乗（呼出し側の責任）かつ sizeof(size_t) 以
 *  上であることに依存して成立する（alloc_mempool 冒頭の assert と
 *  下の mphdr_size_check を参照）．
 */
typedef struct {
	size_t	size;		/* 割付け時の要求サイズ（無加工） */
} MPHDR;

/*
 *  解放済みブロック
 *
 *  解放されたユーザ領域の先頭を転用して格納する（単方向 LIFO）．
 *
 *  ユーザ領域が sizeof(FREEBLK)（ポインタ1個）より小さい割付け（現行
 *  カーネルでは 64bit ターゲットの sizeof(MPFMB) * 1 == 4B が該当）で
 *  は，この書込みがユーザ領域の末尾を越える．ただし越える範囲は
 *  [user + size, user + sizeof(FREEBLK)) である．
 *
 *  ★次の割付けのヘッダの最速到達番地の導出（2026-08-04 レビューで訂正：
 *  当初「user + size + sizeof(MPHDR)」と書いたが誤りだった。反例：64bit
 *  （sizeof(MPHDR)==8），本割付け size=4，次の割付け alignment=8，
 *  user ≡ 0 (mod 8) のとき，brk（本割付け後）= user+4 → +sizeof(MPHDR)
 *  = user+12 → align_up(user+12, 8) = user+16 → header2 = user+8。
 *  これは「最速でも user+size+sizeof(MPHDR) = user+12」という当初の
 *  主張より小さい。正しい下界は以下のとおり：
 *
 *  alloc_mempool が生成する「ユーザ領域先頭（aligned）」は常に
 *  alignment の倍数であり，alignment は常に2の巾乗かつ sizeof(size_t)
 *  以上（冒頭の assert）——2の巾乗どうしのこの大小関係から alignment
 *  は sizeof(size_t)（== sizeof(MPHDR)，mphdr_size_check）の倍数になる。
 *  よって aligned も sizeof(MPHDR) の倍数，ヘッダ番地（aligned -
 *  sizeof(MPHDR)）も sizeof(MPHDR) の倍数——これはプール先頭の最初の
 *  割付けを含め全世代で帰納的に成立する。したがって次のヘッダ番地
 *  header2 も sizeof(MPHDR) の倍数であり，かつ brk は前進のみ（後退
 *  しない）なので header2 >= user + size。user 自身も sizeof(MPHDR)
 *  の倍数なので，header2 - user は「sizeof(MPHDR) の倍数」かつ
 *  「size 以上」——0 < size < sizeof(MPHDR) のときこれを満たす最小の
 *  倍数はちょうど sizeof(MPHDR) 自身。すなわち header2 の最速到達番地
 *  は user + sizeof(MPHDR)（上の反例が正しくこの値 user+8 を実現して
 *  いる。タイトな下界であり，これより早く到達することはない）。
 *
 *  sizeof(FREEBLK) == sizeof(void *) == sizeof(size_t) == sizeof(MPHDR)
 *  なので，この最速到達番地 user + sizeof(MPHDR) は，はみ出し範囲の
 *  上端 user + sizeof(FREEBLK) にちょうど一致する（半開区間なので
 *  この番地自体ははみ出し範囲の外）．よってはみ出しは必ず「自ユーザ
 *  領域末尾〜次ヘッダ直前の死領域（誰も読み書きしないパディング）」に
 *  収まり，他の生きたデータを壊さない．
 *
 *  ★プール末尾（次の割付けが存在しない場合）の扱い（2026-08-04 最終
 *  レビューで発見・修正）：上の論証は「次の割付けのヘッダが存在する」
 *  ことを前提にしており，このブロックがプール内で最後の（末尾に最も
 *  近い）生存割付けである場合，はみ出しがプール自体の外（limit の外）
 *  へ出ないことは別に保証しなければならない．これは「はみ出しの
 *  ドキュメント上の論証」ではなく「initialize_mempool が limit を
 *  sizeof(MPHDR) の倍数へ切り下げる」という**構造的な**保証で閉じて
 *  いる（`initialize_mempool` 冒頭のコメント参照）．limit が
 *  sizeof(MPHDR) の倍数で user（sizeof(MPHDR) の倍数）が limit 未満
 *  なら limit - user >= sizeof(MPHDR)，すなわち user + sizeof(MPHDR)
 *  <= limit が常に成り立つため，末尾ケースでも上と同じ結論（はみ出し
 *  は limit の内側に収まる）が成立する．
 *  size == 0 の割付けは非対応（現行の全呼出し側は CHECK_PAR とあふれ
 *  検査により size > 0 を保証している．将来呼出し側を追加する場合は
 *  この不変を維持すること）．
 */
typedef struct free_block {
	struct free_block	*next;	/* 次の解放済みブロック */
} FREEBLK;

typedef struct {
	void	*brk;		/* メモリプール領域の未使用領域の先頭番地 */
	void	*limit;		/* メモリプール領域の上限 */
	uint_t	count;		/* 割り当てたメモリ領域の数 */
	FREEBLK	*freelist;	/* 解放済みブロックのリストの先頭 */
} MEMPOOLCB;

/*
 *  sizeof(MPHDR) == sizeof(size_t) のコンパイル時検査（ヘッダ直前配置
 *  のアラインメント成立性と FREEBLK 書込みの安全性論証の前提）．
 *  成立しない構成ではこの typedef が負サイズ配列になりコンパイルエラー．
 */
typedef char	mphdr_size_check[(sizeof(MPHDR) == sizeof(size_t)) ? 1 : -1];

bool_t
initialize_mempool(MB_T *mempool, size_t size)
{
	MEMPOOLCB	*p_mempoolcb = ((MEMPOOLCB *) mempool);
	uintptr_t	limit;

	/*
	 *  ★2026-08-04 最終レビュー：limit を sizeof(MPHDR) の倍数へ切り下げ
	 *  る（FREEBLK コメントの「はみ出しは死領域に収まる」という論証の
	 *  前提を構造的に保証する）．自動生成プール（cfg の ROUND_MB_T）は
	 *  すでに sizeof(MB_T) の倍数なのでこの丸めは無害だが，ユーザ供給
	 *  mpk（`DEF_MPK` の mpk 引数）は CHECK_MB_ALIGN（4）にしか丸められ
	 *  ておらず，64bit ターゲット（sizeof(MPHDR)==8）では mpksz ≡ 4
	 *  (mod 8) になりうる．その場合，丸め無しでは limit ちょうどに
	 *  4B 割付け（sizeof(MPFMB)*1）が着地し，解放時の FREEBLK 書込み
	 *  （8B）が limit を 4B 越えてプール外を破壊しうる（実装計画レビュー
	 *  で発見）．limit を切り下げることで，どのユーザ領域先頭も
	 *  sizeof(MPHDR) の倍数（下の alloc_mempool 冒頭のコメント参照）
	 *  であり limit も sizeof(MPHDR) の倍数になるため，
	 *  user < limit ⟹ limit - user >= sizeof(MPHDR) ⟹
	 *  user + sizeof(MPHDR) <= limit が常に成り立ち，はみ出しは必ず
	 *  プール内に収まる．在来の挙動は不変：既存の全 MPK_SIZE 値・自動
	 *  生成プールはすでに整列済みのため，この丸めで何も変わらない．
	 */
	limit = (((uintptr_t) mempool) + ((uintptr_t) size))
			& ~((uintptr_t)(sizeof(MPHDR) - 1U));

	/*
	 *  丸め後の limit が MEMPOOLCB を保持できるかで検査する（丸め前の
	 *  size でなく，丸め後の実効容量で判定を一致させる）．
	 */
	if (limit >= (((uintptr_t) mempool) + sizeof(MEMPOOLCB))) {
		p_mempoolcb->brk = ((char *) mempool) + sizeof(MEMPOOLCB);
		p_mempoolcb->limit = ((void *) limit);
		p_mempoolcb->count = 0;
		p_mempoolcb->freelist = NULL;
		return(true);
	}
	else {
		return(false);
	}
}

/*
 *  メモリプール領域からの割当て（malloc_mempool / aligned_alloc_mempool の共通部）
 *
 *  ★dcreからの意図的な逸脱：符号混在比較とあふれの安全化（上流報告候補c）
 *
 *  dcre（extension/dcre/kernel/startup.c）の原形は
 *
 *      brk = align_pointer(p_mempoolcb->brk, alignment);
 *      if (((char *)(p_mempoolcb->limit)) - ((char *) brk) >= size) { … }
 *
 *  であり，2つの欠陥がある．
 *
 *  (1) 左辺のポインタ差はptrdiff_t（符号つき），右辺のsizeはsize_t（符号
 *      なし）である．通常の算術変換により左辺が符号なしへ変換されるので，
 *      アラインメント調整でbrkがlimitを越えた場合（差が負）に，その負の値が
 *      巨大な符号なし値になり，比較が真＝「入る」と誤判定する．その結果，
 *      メモリプール領域の外側の番地を成功として返す．
 *  (2) align_pointerの加算 ((uintptr_t) ptr + alignment - 1) 自体があふれ
 *      うる．あふれるとbrkが小さな値へ巻き戻り，(1)と同じ誤判定を招く．
 *
 *  そこで本実装は，すべての算術を符号なし（uintptr_t）で行い，
 *
 *      ・ヘッダ分（sizeof(MPHDR)）の前進の加算があふれないこと
 *      ・調整の加算があふれないこと
 *      ・調整後のbrkがlimitを越えていないこと
 *
 *  をこの順に確かめてから初めて残量（limit - aligned）を計算する．負の差が
 *  生じうる箇所が式の上に存在しないため，(1)の誤判定は構造的に起こらない．
 *  ★freelist化（機能拡張）で加算が1つ増えた（brk + sizeof(MPHDR)）ため，
 *  そのあふれ検査を既存のガード列と同じ流儀（加算の前に検査）で追加して
 *  いる．size側にヘッダを加算する式は存在しない（ヘッダはbrk側に折り込ま
 *  れ，sizeは残量比較にのみ使う）．
 *
 *  ★この修正だけでは穴は塞がらない．呼出し側（acre_dtq/acre_pdq/acre_mpf/
 *  acre_tsk）が渡すsize自体が乗算・丸めであふれていると，「小さくなったsize」
 *  は正当に「入る」と判定されて成功してしまう．両者は同じ穴の両端であり，
 *  一体で修正している（各acre_*のCHECK_PARを参照）．
 *
 *  alignmentは2の巾乗であること（呼出し側の責任．alignof(MB_T)ないし
 *  aligned_alloc_mpkの引数）．加えてヘッダ直前配置のアラインメント成立性
 *  のため alignment >= sizeof(size_t) であること（現行の全呼出し側で成立
 *  することをコード読解で確認済み．冒頭のassertはこの前提の防御である）．
 */
Inline void *
alloc_mempool(MEMPOOLCB *p_mempoolcb, size_t alignment, size_t size)
{
	uintptr_t	brk = ((uintptr_t)(p_mempoolcb->brk));
	uintptr_t	limit = ((uintptr_t)(p_mempoolcb->limit));
	uintptr_t	adjust = (((uintptr_t) alignment) - 1U);
	uintptr_t	aligned;
	FREEBLK		**pp_freeblk;
	FREEBLK		*p_freeblk;
	MPHDR		*p_mphdr;

	assert(alignment >= sizeof(size_t)
			&& (alignment & (alignment - 1U)) == 0U);

	/*
	 *  解放済みリストの走査（完全一致再利用）
	 *
	 *  割付け時の要求サイズが今回の要求サイズと完全に一致し，かつ番地
	 *  が要求アラインメントに適合する最初のブロックを unlink して返す．
	 *  ヘッダ（MPHDR.size）は割付け時のまま残っているので書換え不要．
	 */
	for (pp_freeblk = &(p_mempoolcb->freelist); (*pp_freeblk) != NULL;
					pp_freeblk = &((*pp_freeblk)->next)) {
		p_freeblk = *pp_freeblk;
		p_mphdr = ((MPHDR *)(((char *) p_freeblk) - sizeof(MPHDR)));
		if (p_mphdr->size == size
				&& (((uintptr_t) p_freeblk) & adjust) == 0U) {
			*pp_freeblk = p_freeblk->next;
			p_mempoolcb->count += 1;
			return((void *) p_freeblk);
		}
	}

	/*
	 *  ヘッダ分の前進の加算があふれる場合は割り当てられない．
	 */
	if (brk > ((~((uintptr_t) 0U)) - ((uintptr_t) sizeof(MPHDR)))) {
		return(NULL);
	}
	brk += ((uintptr_t) sizeof(MPHDR));

	/*
	 *  アラインメント調整の加算があふれる場合は割り当てられない．
	 */
	if (brk > ((~((uintptr_t) 0U)) - adjust)) {
		return(NULL);
	}
	aligned = ((brk + adjust) & ~adjust);

	/*
	 *  調整の結果が上限を越えた場合は割り当てられない．
	 *  ★この検査を済ませるまで limit - aligned を計算してはならない．
	 */
	if (aligned > limit) {
		return(NULL);
	}
	if (((uintptr_t) size) > (limit - aligned)) {
		return(NULL);
	}

	/*
	 *  ヘッダをユーザ領域の直前に書き，要求サイズを記録する．
	 *  aligned は alignment の倍数，alignment は sizeof(size_t) 以上
	 *  の2の巾乗（冒頭の assert），sizeof(MPHDR) == sizeof(size_t)
	 *  （mphdr_size_check）なので，aligned - sizeof(MPHDR) も
	 *  sizeof(size_t) の倍数であり，この書込みは常にアラインする．
	 */
	p_mphdr = ((MPHDR *)(aligned - ((uintptr_t) sizeof(MPHDR))));
	p_mphdr->size = size;
	p_mempoolcb->brk = ((void *)(aligned + ((uintptr_t) size)));
	p_mempoolcb->count += 1;
	return((void *) aligned);
}

void *
malloc_mempool(MB_T *mempool, size_t size)
{
	return(alloc_mempool(((MEMPOOLCB *) mempool), alignof(MB_T), size));
}

void *
aligned_alloc_mempool(MB_T *mempool, size_t alignment, size_t size)
{
	return(alloc_mempool(((MEMPOOLCB *) mempool), alignment, size));
}

void
free_mempool(MB_T *mempool, void *ptr)
{
	MEMPOOLCB	*p_mempoolcb = ((MEMPOOLCB *) mempool);
	FREEBLK		*p_freeblk = ((FREEBLK *) ptr);

	/*
	 *  ★契約：同一番地を，再割付されるまでの間に二重に free_mempool へ
	 *  渡してはならない（呼出し側＝カーネル内部コードの責務．外部から
	 *  渡された ptr ではなく，カーネル自身が管理する DTQMB/PDQMB/MPFMB/
	 *  STK_T 領域のみが対象）．
	 *
	 *  違反した場合の失敗モード（意図的に記録．ガードは入れない）：
	 *  二重解放された番地を next が自分自身を指す形で freelist へ push
	 *  してしまい，自己循環ができる．以後の alloc_mempool の走査は
	 *  ・サイズが一致する要求では，まだ freelist 上に残っている（かつ
	 *    別の呼出し元が生きたポインタとして保持している）番地をもう
	 *    一度払い出してしまう（多重払い出し＝エイリアシング）．
	 *  ・サイズが一致しない要求では，走査が自己循環から抜けられず
	 *    無限ループしてロックを保持し続ける（カーネルロックアップ）．
	 *  旧実装（bump のみ）の二重解放は count の帳簿を狂わせるだけで
	 *  ハングしなかったため，本実装はこの点で質的に悪い失敗モードを
	 *  持つ．★ガードを入れない理由：現行の呼出し側（mempfix.c・
	 *  dataqueue.c・pridataq.c・task_manage.c）を全数確認し，正常系・
	 *  ロールバック系のいずれも同一番地を二重解放しないことを確認済み
	 *  （grep によるコード読解での確認．潜在＝将来の呼出し側追加時の
	 *  リスクであり，現行コードで到達する経路は無い）．O(freelist長) の
	 *  メンバーシップ走査は現行の呼出し側に対して無駄なコスト，先頭
	 *  ノードのみの簡易検査は多重 push を捕まえられない偽の安心にしか
	 *  ならないため，どちらも実装しない（実装計画レビューでの裁定）．
	 */
	p_freeblk->next = p_mempoolcb->freelist;
	p_mempoolcb->freelist = p_freeblk;
	p_mempoolcb->count -= 1;
	if (p_mempoolcb->count == 0) {
		/*
		 *  backstop：全解放されたら brk を先頭へ戻し，freelist も空に
		 *  する（直前に push した1件も含めて消える）．
		 */
		p_mempoolcb->brk = ((char *) mempool) + sizeof(MEMPOOLCB);
		p_mempoolcb->freelist = NULL;
	}
}

#endif /* OMIT_MEMPOOL_DEFAULT */
#endif /* TOPPERS_kermem */
