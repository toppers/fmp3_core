/*
 *		カーネルメモリプールの完全一致フリーリスト再利用のテスト
 */

#include <kernel.h>
#include "target_test.h"

#define HIGH_PRIORITY	9
#define MID_PRIORITY	10
#define LOW_PRIORITY	11

#ifndef STACK_SIZE
#define	STACK_SIZE		4096
#endif /* STACK_SIZE */

/*
 *  カーネルメモリプールのサイズと机上算術の前提（32bit・musca_b1 専用）
 *
 *  本テストの数値はすべて 32bit ターゲット（sizeof(size_t) == 4）かつ
 *  freelist 実装（sizeof(MEMPOOLCB) == 16・割付けヘッダ 4B・要求サイズ
 *  無加工）を前提に机上計算してある．実行時に
 *  check_assert(sizeof(size_t) == 4U) で前提を固定する．
 *
 *    プール実効容量 = MPK_SIZE(2048) - sizeof(MEMPOOLCB)(16) = 2032
 *    割付けフットプリント = 4(ヘッダ) + 要求サイズ
 *    （全要求が 4 の倍数なのでパディング無し）
 *
 *  【フェーズA: 枯渇しないことの実証】
 *    長寿命 dtq（LONG_DTQCNT=1 → 4B，フットプリント 8B，brk=24）を保持
 *    したまま acre_mpf/del_mpf を MPF_CYCLES(=16) 周まわす．1周の消費は
 *      ① ブロック領域 ROUND_MPF_T(64)*4 = 256B → フットプリント 260B
 *      ② 管理領域 sizeof(MPFMB)*4 = 16B → フットプリント 20B
 *    の計 280B．旧実装（bump のみ）では count が 0 に戻らないため brk が
 *    単調に前進し，8周目の①（brk=24+280*7=1984，1984+260 > 2048）で
 *    E_NOMEM になる．新実装では2周目以降①②とも freelist の完全一致で
 *    再利用され，brk は 304 で頭打ちになる．
 *    ★この「8周目」は変異control（freelist 走査の無効化）の予測値でもある．
 *
 *  【フェーズC: サイズ不一致は再利用されない（bump 前進の観測）】
 *    フェーズB終了時点で brk=304・freelist={256B, 16B}．
 *    probe（PROBE_DTQCNT=2 → 8B，どちらとも不一致）→ bump で brk=316．
 *    fill（FILL_DTQCNT=432 → 1728B）のフットプリント 1732B は残量
 *    2048-316=1732 にちょうど収まる（成功＝brk が 316 以下だった証拠）．
 *    直後の境界 dtq（BOUND_DTQCNT=1 → 4B，フットプリント 8B）は残量 0
 *    で E_NOMEM（＝brk が 2048 に到達した証拠）．もし probe が 16B の
 *    ブロックを（不一致にもかかわらず）再利用していれば brk=304+1732=2036
 *    で残量 12B が残り，この 4B 要求は成功してしまう——E_NOMEM である
 *    ことが「不一致の要求で bump が前進した」ことの決定的な観測である．
 *
 *  【フェーズD: count==0 backstop の全域リセット】
 *    全 del ののち BIG_DTQCNT=500 → 2000B（フットプリント 2004 ≤ 2032）
 *    が入る＝brk リセットと freelist クリアの実証．
 */
#ifndef MPK_SIZE
#define MPK_SIZE		2048
#endif /* MPK_SIZE */

/*  反復生成する固定長メモリプールの諸元  */
#define MPF_BLKCNT		4
#define MPF_BLKSZ		64
#define MPF_CYCLES		16

/*  各 dtq の容量（上の机上算術の前提）  */
#define LONG_DTQCNT		1		/*  4B（長寿命保持）  */
#define PROBE_DTQCNT	2		/*  8B（freelist の 256B/16B と不一致）  */
#define FILL_DTQCNT		432		/*  1728B（残量 1732B にちょうど収まる）  */
#define BOUND_DTQCNT	1		/*  4B（残量 0 で E_NOMEM になる）  */
#define BIG_DTQCNT		500		/*  2000B（リセット後の 2032B に入る）  */

#ifndef TOPPERS_MACRO_ONLY
extern void	task1(EXINF exinf);
#endif /* TOPPERS_MACRO_ONLY */
