# FMP3 動的生成API 段階3b（acre_dtq/del_dtq・acre_pdq/del_pdq・acre_mpf/del_mpf）実装計画

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `feature/dynamic-creation` ブランチ上で `acre_dtq`/`del_dtq`・`acre_pdq`/`del_pdq`・
`acre_mpf`/`del_mpf` と `AID_DTQ`/`AID_PDQ`/`AID_MPF` を、cfg 両エンジン（Ruby オラクル +
Python 製品）同時対応・QEMU 回帰テスト付きで動かす。これで dcre 標準の動的生成オブジェクトのうち
ISR を除く全てが揃う。

**Architecture:** ASP3 dcre の機構（cfg 予約スロットの free-list + RAM `adtqinib_table[]` 等）を
忠実移植し、FMP3 固有の3点（ジャイアントロック・named-static CB + const ポインタ表・
MP 対応済みの `init_wait_queue`）を局所適応する。段階1が一般化し段階2（cyc/alm）・
段階3a（sem/flg/mtx）で実証した cfg 共通枠組み（`@aidapi`/`inibList`/`inibSizeToken`/
予約 CB/訂正E ガード）は**そのまま再利用**でき、dtq/pdq/mpf 側の per-object テンプレート
（`dataqueue.py`/`.trb` 等）の変更は**ゼロ**である（Task 1 Step 7 で裏取りする）。

★段階3a との最大の相違は **3オブジェクトが「管理領域」を持つ**ことである。
`acre_*` は `malloc_mpk()` でカーネルメモリプールから管理領域を確保し、`del_*` は
`free_mpk()` で返す。したがって段階3b では**段階2・段階3a では1度も通らなかった
E_NOMEM 経路と TA_MBALLOC/TA_MEMALLOC のビット検査が初めて生きる**。
段階3a と同じく **3オブジェクトとも非親和**であり（INIB に `iprcid`/`affinity` が無く、
CB に `p_pcb` が無い）、段階2 Constraint 4 に相当する充填コードは**1行も書いてはならない**。

**Tech Stack:** C（カーネル）、Python/Ruby（cfg テンプレート）、CMake、QEMU（musca_b1・kria_r5 ほか）。

---

## Global Constraints（spec から転記。実装中に再解釈しない）

1. ブランチ `feature/dynamic-creation`（段階3a の続き、HEAD = `958c1f4`）。**main へはマージしない。**
   pristine への改変は `DIVERGENCE_MAP.md` に記録する（種別 `mod (dcre-port)`、上流報告欄 `-`）。
2. 段階3b = dtq/pdq/mpf の `acre_*`/`del_*` + `AID_DTQ`/`AID_PDQ`/`AID_MPF` + `TA_MBALLOC` 定義のみ。
   **ISR（別計画・案B ハイブリッド）・段階4的な何かを含めない。**
3. API 面は dcre 標準のみ：`T_CDTQ`/`T_CPDQ`/`T_CMPF` は dcre `include/kernel.h:246-252,258-266,285-291` と
   同一定義（バイト照合すること）。独自 API なし。`AID_*` はクラス外専用（クラス内は E_RSATR）。
4. ★★**プロセッサ親和なし。** dtq/pdq/mpf の INIB には `iprcid`/`affinity` が**存在しない**
   （現物確認済み: `kernel/dataqueue.h:68-72`・`pridataq.h:72-77`・`mempfix.h:78-84`）。
   CB にも `p_pcb` は無い（`dataqueue.h:81-88`・`pridataq.h:86-94`・`mempfix.h:93-99`）。
   **段階2 Constraint 4 の類推で `p_dtqinib->iprcid = TOPPERS_MASTER_PRCID;` や
   `->affinity = TOPPERS_TEPP_PRC;` や `p_dtqcb->p_pcb = ...;` を書いたら、それは
   コンパイルエラーになるか、なったとしてもバグである。書かないこと。**
   `initialize_dataqueue`/`initialize_pridataq`/`initialize_mempfix` は
   **現行が既にマスタプロセッサのみのループ**（`dataqueue.c:142-163`・`pridataq.c:135-158`・
   `mempfix.c:119-142`）であり、**プロセッサ別フィルタは存在しないし追加もしない**。
   変えるのは**静的ループの境界（`tnum_dtq`→`tnum_sdtq` 等）と、その直後に足す
   動的スロット節だけ**である。
5. 検証 = F-1：Ruby `.trb`（オラクル）にも同時移植し `tools/cfg_equivalence.sh`
   （exit 0=一致 / 1=不一致 / **2=前提未充足であり合格ではない**）を主検査に維持。
6. CB はヒープ確保しない。予約 CB（named static + ポインタ表末尾）+ RAM inib 配列。
   free-list のリンクには **CB 先頭の QUEUE フィールドを直接流用**する
   （dtq/pdq = `swait_queue`、mpf = `wait_queue`。dcre `dataqueue.c:183-189`・
   `pridataq.c:177-183`・`mempfix.c:159-165` と同一）。
   **段階2 の tmevtb オーバーレイ技法は使わない**＝段階2 訂正D（64bit で `callback` が
   上書きされる問題）は**構造的に発生しない**。
7. **free-list は FIFO**（`del_*` = `queue_insert_prev` で末尾へ / `acre_*` =
   `queue_delete_next` で先頭から）。これは段階1で**裁定済み**の設計であり、
   実装者・レビュアーとも再議しない。テストは FIFO/LIFO 不問で決定的になる形
   （「空きが1個だけの状態で del → 再 acre」）に組む。
8. **管理領域はカーネルメモリプール（`malloc_mpk`）から確保**し `TA_MBALLOC` を立てる
   （dcre 忠実）。mpf のブロック領域は `mpf == NULL` のとき自動確保で `TA_MEMALLOC`。
9. **プール裁定は済んでいる（spec §1 — ユーザ承認済み 2026-08-04）。再議しない。**
   「3b の管理領域は構造的にウィンドウ・フリー（glock 下参照のみ + E_NOEXS ゲート）」
   という論証への**反証は最終レビューで試みる**（それが spec §1-1 の指示）が、
   実装タスク中に設計を蒸し返さないこと。
10. 汎用層 `CMakeLists.txt`・`cmake/fmp3_core.cmake`・`cfg_py/pass1.py`・`cfg_py/pass2.py` は
    **変更しない**。`KERNEL_FCSRCS`（`kernel/Makefile.kernel:51-56` の22個）も**不変**
    （`acre_*`/`del_*` はいずれも既存 `.c` に入る。新規 `.c` を作らない）。
11. 訂正C（段階3a）を踏襲：`del_*` の呼出しコンテキスト検査は
    `CHECK_TSKCTX_UNL_MYSTATE(&p_selftsk)` を使い、ディスパッチ判断は
    `if (p_selftsk != p_my_pcb->p_schedtsk)` で行う。
12. `rc=124` 単独を成功判定に使わない（期待出力の実在を `grep` で確認する）。
13. **`cmd | tail` / `cmd | grep` で成否判定しない。** パイプラインの `$?` は最後の要素のもの。
    ファイルへリダイレクトしてから `grep` するか、`${PIPESTATUS[0]}` を見る。
14. `tools/cfg_equivalence.sh` の **exit 2 は合格ではない**（前提未充足）。exit 0 のみ合格。
15. QEMU 実行は**プリセットごとに個別コマンド**で行い、ログを別ファイルに落とす。
    `for` ループで全構成を1コマンドに詰めると Bash ツールの 2 分タイムアウトに当たり、
    **qemu が孤児化する**（段階1 Task 7 の実害）。各実行後に `pgrep -a qemu` で残存 0 を確認する。
16. `tools/cfg_error_tests/run.sh` の呼出しは
    `run.sh <builddir> <cfg> <期待ercd> [EXTRA_CFLAGS]` の**4引数形**である。
    cfg 内で `#include "test_int2.h"` 等を使うケースは**第4引数
    `EXTRA_CFLAGS="-I<repo>/test"` が必須**で、付けないと `rc=2` になる。
    **本計画では全ケースに引数を明記する。**

---

## ★★段階3a 最終レビューからの申し送り（本段階の中核リスク）

**`TA_NOEXS` は `((ATR)(-1))`＝全ビットが 1 である**（現物確認済み: `kernel/kernel_impl.h:199`）。
したがって TA_NOEXS を書き込んだ後の初期化ブロックに対して

- **ビット検査** `(atr & TA_MBALLOC) != 0U` は**必ず真**になる、
- **マスク比較** `(atr & 0x03U) == TA_CEILING` のような式も**誤って真**になりうる

（段階3a 最終レビュー Important #1。3a では `MTX_CEILING` がこれに該当し、
コメントの根拠が誤っていたことが指摘された）。

★**段階3b はこの罠が「コメントの正しさ」ではなく「機能の正しさ」に直結する唯一の段階である。**
`del_dtq`/`del_pdq`/`del_mpf` は「`TA_MBALLOC` が立っていれば `free_mpk`」という
**ビット検査による解放条件**を持つ。もし `dtqatr = TA_NOEXS;` を `free_mpk` の条件判定より
**前**に置いたら、TA_NOEXS の全ビット 1 により条件が常に真となり、
**ユーザ供給の管理領域（TA_MBALLOC が立っていないもの）まで `free_mpk` に渡される**。
これは「ercd を間違える」ではなく**プールの破壊**である。

→ **不変量（3つの `del_*` すべてで必須）:**
> **属性の読み（`TA_MBALLOC`/`TA_MEMALLOC` のビット検査を含む）は、すべて
> `TA_NOEXS` の書込みより前に完了していなければならない。**

dcre の現物もこの順序である（`dataqueue.c:427-430`・`pridataq.c:409-412`・
`mempfix.c:308-314`：`free_mpk` 群 → `xxxatr = TA_NOEXS;`）。
**Task 1 Step 3 でこの順序を現物から再確認し、Task 3/4/5 の各 `del_*` に
「TA_NOEXS は全ビット 1 なのでビット検査は必ず真になる」という真の根拠つきコメントを書く。**
Task 6 の変異 control でこの経路が生きていることを実演する。

---

## ★spec からの訂正8件（Task 1 で spec に反映してから実装に入ること）

計画作成時に dcre 現物と FMP3 現物を確認した結果、spec の記述と現物が食い違う点、
および spec が「実装前確認で決める」としていて**計画作成時に決着がついた**点が8件ある。
いずれも **Task 1 で spec 本文を直してから** 後続タスクに入る。

**訂正A：`T_CDTQ`/`T_CPDQ`/`T_CMPF` の spec §2.1 は「要旨」であって dcre 原文ではない。**
dcre 現物（`include/kernel.h:246-252` / `258-266` / `285-291`）と spec §2.1 の相違は3点：
(1) コメント文言が違う（例：`dtqcnt` は「データキューの容量」ではなく
**「データキュー管理領域に格納できるデータ数」**）、
(2) `T_CPDQ.maxdpri` のコメントは**2行に折り返されている**（`\t\t\t\t\t\t   大値 */`）、
(3) spec は `}T_CMPF;` と書くが dcre は **`} T_CMPF;`（空白あり）**。
→ **Task 2 では dcre 現物からバイト単位で転写する**（spec §2.1 の要旨を写さない）。
spec §2.1 を dcre 原文で置き換える。

**訂正B：`acre_dtq` に dtqcnt の範囲検査は存在しない。ユーザ供給 `dtqmb` は受理される。**
spec §2.3 は「検査（dtqatr・dtqcnt 範囲は dcre の式）」「dtqcnt > 0 なら
`p_dtqmb = malloc_mpk(...)`」と書くが、dcre `dataqueue.c:358-361, 368-374` の現物は：

```c
	CHECK_VALIDATR(dtqatr, TA_TPRI);
	if (p_dtqmb != NULL) {
		CHECK_PAR(MB_ALIGN(p_dtqmb));
	}
	...
		if (dtqcnt != 0 && p_dtqmb == NULL) {
			p_dtqmb = malloc_mpk(sizeof(DTQMB) * dtqcnt);
			dtqatr |= TA_MBALLOC;
		}
		if (dtqcnt != 0 && p_dtqmb == NULL) {
			ercd = E_NOMEM;
		}
```

すなわち **(a) `dtqcnt` の範囲検査は無い**（0 も許される＝管理領域を持たないデータキュー）、
**(b) ユーザが `dtqmb` を渡した場合は受理し、`TA_MBALLOC` を立てない**（アラインだけ検査する）、
**(c) `dtqcnt == 0` なら管理領域を確保しない**（`p_dtqmb` は NULL のまま INIB に入る）。
`acre_pdq` も同型（＋`CHECK_PAR(VALID_DPRI(maxdpri))`）。
→ spec §2.3 をこの3点で書き直す。**`E_NOSPT` は使わない**（cfg 側の静的生成だけが
`dtqmb != NULL` を E_NOSPT で弾く。動的生成は受理する — 非対称だが dcre 忠実）。

**訂正C：`VALID_DPRI` は FMP3 に存在しない。`kernel/check.h` へ追加する。**
dcre `acre_pdq`（`pridataq.c:338`）は `CHECK_PAR(VALID_DPRI(maxdpri));` を使い、
`VALID_DPRI` は dcre `check.h:71` で定義されている。
**FMP3 の `kernel/check.h` には `VALID_DPRI` が無い**（`VALID_TPRI`＝`:70` はある。
`TMIN_DPRI`=1 / `TMAX_DPRI`=16 は `include/kernel.h:624-625` に**ある**）。
→ **Task 4 で `kernel/check.h` の `VALID_TPRI` の直後に**

```c
#define VALID_DPRI(dpri)	(TMIN_DPRI <= (dpri) && (dpri) <= TMAX_DPRI)
```

**を追加する**（dcre `check.h:71` と同一）。段階1 で `CHECK_VALIDATR` を追加したのと同じ前例。
spec §4 の「追加の移植は不要」という前提はここだけ崩れる。

**訂正D：`acre_mpf` の2段確保には巻き戻しが**ある**。条件はローカル `mpf` ではなく
`pk_cmpf->mpf` を見る。**
dcre `mempfix.c:237-254` の現物：

```c
		if (mpf == NULL) {
			mpf = malloc_mpk(ROUND_MPF_T(blksz) * blkcnt);
			mpfatr |= TA_MEMALLOC;
		}
		if (mpf == NULL) {
			ercd = E_NOMEM;
		}
		else {
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
```

★**巻き戻しの条件が `pk_cmpf->mpf == NULL`（パケットの元の値）である**ことが要点。
ローカル `mpf` は①で上書きされているので、ローカルを見ると判定できない
（＝「カーネルが確保した分だけを返す」を実現するための書き方）。
`mpfatr & TA_MEMALLOC` を見ても等価だが、**dcre のとおり `pk_cmpf->mpf` を見る**。
また `acre_mpf` には `CHECK_PAR(blkcnt != 0)` / `CHECK_PAR(blksz != 0)` /
`MPF_ALIGN(mpf)` / `MB_ALIGN(p_mpfmb)` の検査が**ある**（spec は書いていない）。
`MPFINIB.blksz` には **`ROUND_MPF_T(blksz)`（丸めた値）**を入れる。
→ spec §2.3 の acre_mpf 節をこの現物で置き換える（「巻き戻しの有無は現物照合」を実測で確定）。

**訂正E：★E_NOEXS 挿入23関数のうち5関数は「字下げのみ」では済まない。dcre は
ロック前の検査をロック内へ移している。**
spec §4 は「E_NOEXS 23関数 — すべて 3a の型どおり」と書くが、**5関数は違う**。
FMP3 現物が `p_xxxinib` を**ロック取得前に読んでいる**ためで、E_NOEXS ゲートより前に
削除済みスロットの INIB を読むことになる（`rel_mpf` に至っては**解放済みの管理領域を
デリファレンスする**）。dcre は同じ問題を「ロック内へ移す」形で解決している。

| 関数 | FMP3 現物（ロック前） | dcre の形（ロック内・E_NOEXS の次） |
|---|---|---|
| `fsnd_dtq`（`dataqueue.c:485`） | `CHECK_ILUSE(p_dtqcb->p_dtqinib->dtqcnt > 0U);` | `else if (!(p_dtqcb->p_dtqinib->dtqcnt > 0U)) { ercd = E_ILUSE; }`（`dataqueue.c:604-606`） |
| `snd_pdq`（`pridataq.c:301`） | `CHECK_PAR(TMIN_DPRI <= datapri && datapri <= p_pdqcb->p_pdqinib->maxdpri);` | ロック前は `CHECK_PAR(TMIN_DPRI <= datapri);` のみ、ロック内に `else if (datapri > p_pdqcb->p_pdqinib->maxdpri) { ercd = E_PAR; }`（`pridataq.c:444, 450-452`） |
| `psnd_pdq`（`pridataq.c:357`） | 同上 | 同上（`pridataq.c:494, 500-502`） |
| `tsnd_pdq`（`pridataq.c:408`） | 同上 | 同上（`pridataq.c:544, 550-552`） |
| `rel_mpf`（`mempfix.c:324-330`） | `CHECK_PAR` ×4（`mpf` 比較・`blkoffset % blksz`・`blkoffset / blksz < unused`・`p_mpfmb[blkidx].next == INDEX_ALLOC`） | ロック内で `blkoffset`/`blkidx` を計算し、4条件を **1つの `if (!(...) \|\| !(...) \|\| !(...) \|\| !(...)) { ercd = E_PAR; }`** にまとめる（`mempfix.c:487-495`） |

→ **この5関数は「既存ロジックのバイト保存」の例外である。**dcre の形へ**構造変更**し、
DIVERGENCE_MAP に「dcre と同じ構造変更を行った・理由は E_NOEXS ゲートより前に
INIB／解放済み管理領域を読まないため」と明記する。**残る18関数は 3a どおり字下げのみ。**
★`rel_mpf` は**メモリ安全性の修正**である（削除済みプールに対して
`p_mpfinib->p_mpfmb[blkidx]` を読むと `free_mpk` 済み領域へのアクセスになる）。

**訂正F：dcre `del_dtq` の ID 検査は `CHECK_PAR`（E_PAR）＝dcre 側の不整合。FMP3 は `CHECK_ID` に揃える。**
dcre `del_pdq`（`pridataq.c:395`）と `del_mpf`（`mempfix.c:295`）は `CHECK_ID(VALID_*ID(...))`（E_ID）だが、
`del_dtq`（`dataqueue.c:413`）だけ `CHECK_PAR(VALID_DTQID(dtqid))`（**E_PAR**）になっている。
FMP3 の既存 dtq 系サービスコールは**全て `CHECK_ID(VALID_DTQID(dtqid))`**
（`dataqueue.c:322,376,426,483,530,587,631,692,735`）。
→ **FMP3 の `del_dtq` は `CHECK_ID` を使う**（dcre からの意図的な逸脱・台帳記録）。
★これは段階3a の訂正D（dcre `del_flg` の `CHECK_PAR`）と**同型の2件目**である。
Task 7 で **上流報告候補 d を「`del_flg` と `del_dtq` の2件」へ拡張する**。

**訂正G：カーネルメモリプールは bump allocator であり、`free_mpk` は
「全割当てが解放された（count==0）」ときにしか brk を戻さない。**
`kernel/startup.c` の現物（`malloc_mempool`/`free_mempool`）：
`malloc_mempool` は `brk` を進めて `count += 1`、`free_mempool` は `count -= 1` し
**`count == 0` になったときだけ `brk` を先頭へ戻す**。
→ **事実として3点が導かれる。spec §1・§5・§6 に明記する。**
1. 【MP 安全性の補強】解放した管理領域のアドレスは、**プール上の全割当てが解放されるまで
   再利用されない**。spec §1 の「ウィンドウ・フリー」論証をさらに強くする事実である
   （ただし論証の根拠は「glock 下参照のみ + E_NOEXS ゲート」であって、これはその補強にすぎない）。
2. 【テスト設計の拘束】`acre`→`del` を繰り返すテストは、**各サイクルで count が 0 に戻る形**
   （他の割当てを抱えていない状態）でなければプールを食い潰す。Task 6 のテストは
   これを踏まえて組む（そして**この性質そのものを変異 control の梃子に使う**）。
3. 【既知の上流報告候補 c と同じ場所】`malloc_mempool` の
   `((char *)limit - (char *)brk) >= size` は `ptrdiff_t >= size_t` の符号混在比較である
   （既存の上流報告候補 c）。**Task 6 のテストで `size` を巨大にするときは
   `sizeof(DTQMB) * dtqcnt` が `size_t` で桁あふれしない値を選ぶ**（あふれると 0 に化けて
   確保が成功し E_NOMEM が出ない）。

**訂正H：`TA_MBALLOC` は cfg の出力トークンではない。段階1 COUNT_MB_T 型の案件ではない。**
spec §4 は `TA_MBALLOC` の追加を求めるが、**cfg は `TA_MBALLOC` を1度も出力しない**
（現物確認：`kernel/dataqueue.py`・`pridataq.py`・`mempfix.py` は静的オブジェクトの
管理領域を `static DTQMB _kernel_dtqmb_<id>[n]` 等として**実体で**生成し、属性には
`params['dtqatr']` をそのまま入れる）。`DTQMB`/`PDQMB`/`MPFMB`/`MPF_T`/`COUNT_MPF_T`/
`ROUND_MPF_T` も**すべて既存**で、**今日すでに cfg が出力してコンパイルが通っている**。
→ 段階1 の COUNT_MB_T 型の欠陥（cfg が未定義トークンを出す）は**構造的に起こりえない**。
**それでも `TA_MBALLOC` の定義は Task 2 に置く**。理由は cfg の教訓ではなく**依存の衛生**：
`TA_MBALLOC` は Task 3/4/5 の3つが等しく使う共有定義なので、どれか1つに置くと
他の2つが兄弟タスクのカーネル編集に依存してしまう。Task 2 の「管理された差分」検査で
**生成物が1バイトも変わらないこと**（＝`#define` の追加が cfg 出力に影響しないこと）を
同時に実証できる利点もある。spec §4 にこの判断と根拠を書く。

---

## 変更ファイル一覧（全体像）

| 層 | ファイル | 種別 |
|---|---|---|
| spec | `docs/superpowers/specs/2026-08-04-fmp3-dcre-stage3b-dtq-pdq-mpf-design.md` | 派生・修正 |
| cfg 定義 | `kernel/kernel_api.def` | **pristine・台帳** |
| API | `include/kernel.h`（T_CDTQ/T_CPDQ/T_CMPF + 6宣言） | **pristine・台帳** |
| 共通定義 | `kernel/kernel_impl.h`（`TA_MBALLOC`） | **pristine・台帳** |
| 共通検査 | `kernel/check.h`（`VALID_DPRI`） | **pristine・台帳** |
| カーネル dtq | `kernel/dataqueue.h` `kernel/dataqueue.c` | **pristine・台帳** |
| カーネル pdq | `kernel/pridataq.h` `kernel/pridataq.c` | **pristine・台帳** |
| カーネル mpf | `kernel/mempfix.h` `kernel/mempfix.c` | **pristine・台帳** |
| 配線 | `kernel/allfunc.h` `kernel/Makefile.kernel` | **pristine・台帳** |
| rename | `kernel/kernel_rename.def` →（再生成）`kernel_rename.h` `kernel_unrename.h` | **pristine・台帳** |
| テスト | `test/test_dcre4.{c,cfg,h}` `test/test_dcre_mix.cfg` `test/MANIFEST` `test/testexec.rb` | **pristine・台帳** |
| エラー回帰 | `tools/cfg_error_tests/dcre_aid_{dtq,pdq,mpf}_in_class.cfg`（3） `dcre_aid_{dtq,pdq,mpf}_no_static.cfg`（3） | 派生 |
| 台帳 | `DIVERGENCE_MAP.md` | 派生 |
| 記録 | `.superpowers/sdd/progress.md` | 派生 |

**READ ONLY（読むが変更しない）:**
`kernel/wait.c` `kernel/wait.h`（`init_wait_queue` は**再利用するだけ**。MP 対応済み＝`wait.c:215-228`、`wait.h:249`）。
`kernel/startup.c`（`malloc_mempool`/`free_mempool`。訂正G の根拠を読むだけ）。
`include/kernel_fncode.h`（6コードとも既存＝Task 1 Step 1）。
`kernel/kernel.py` `kernel/kernel.trb`（**共通枠組みは無変更**）。
`kernel/dataqueue.py` `kernel/dataqueue.trb` `kernel/pridataq.py` `kernel/pridataq.trb`
`kernel/mempfix.py` `kernel/mempfix.trb`（**per-object テンプレートの変更はゼロ** — spec §3）。

---

### Task 1: 実装前確認（spec §8 の8項目）と spec の訂正8件反映

**推奨モデル:** 最安価（現物確認と機械的な文書修正。判断は本計画が既に与えている）

**★★このタスクへの特別な注意（段階3a Task 1 の実害から）:**
段階3a の同タスクでは、最安価モデルが**現物確認と証拠収集は正確**だったにもかかわらず、
**計画から spec への転記の段階で内容を創作・すり替えた**（訂正Hの内容が §7 の別の
コントロールに置き換わる、訂正Eで落としてはいけない条件まで落として根拠を捏造する、
など4件。レビュー1回目で全部差し戻し）。
→ **本タスクの転記は「本計画の訂正A〜H の文面を、要約せず・言い換えず・逐語で」spec へ移すこと。**
自分の言葉で言い直したくなったら、それは誤りの兆候である。
→ **レビュアーへ：本タスクのレビューは「計画原文 vs spec 反映後の文面」を直接
diff で突き合わせること。** 「もっともらしいか」で判断しない。

**Files:**
- Modify: `docs/superpowers/specs/2026-08-04-fmp3-dcre-stage3b-dtq-pdq-mpf-design.md`
  （§2.1・§2.3・§3・§4・§5・§6・§7・末尾に §9 を新設）
- Create: なし

**Interfaces（後続 Task が参照する記録）:**
- Produces: 本 Task の**確認結果表**（spec 末尾 `## 9. 実装前確認の結果` として追記）。
  後続 Task は「Task 1 の記録」としてこれを参照する。特に
  (a) `TFN_*` 6件の既存値、(b) `DTQID`/`PDQID`/`MPFID` の現行実装（＝置換対象）、
  (c) dcre 転写元の行範囲9組、(d) `acre_*` の「E_NOID → 確保 → CB pop」順序、
  (e) `del_*` の「属性読み → TA_NOEXS 書込み」順序、
  (f) E_NOEXS 23関数の内訳と**構造変更が必要な5関数**、
  (g) `VALID_DPRI` の不在と追加先、(h) no-static 回帰 cfg の include 構成。

**★ゲート条件（BLOCKED 判定）:** 下記のいずれかが成り立ったら、**以降のステップに進まず
発見内容を報告して停止する**（設計が無効になるため）。
- dtq/pdq/mpf の INIB または CB に `iprcid`/`affinity`/`p_pcb` が**存在した**
  （Global Constraint 4 の前提が崩れる）。
- `initialize_dataqueue`/`initialize_pridataq`/`initialize_mempfix` が
  **マスタプロセッサ限定でなかった**（動的スロット初期化の置き場所が変わる）。
- CB の**先頭フィールドが QUEUE でなかった**
  （`(DTQCB *) queue_delete_next(&free_dtqcb)` の型 punning が成立しない）。
- dcre の `del_*` が **`TA_NOEXS` 書込みを `free_mpk` の条件判定より前に**行っていた
  （その場合は dcre 側のバグ。**dcre に従わず順序を入れ替えて実装し、上流報告候補に追加する**
  — 本計画の現物確認では「free_mpk が先」だったので、これは起こらないはずである）。

- [ ] **Step 1: §8-5 機能コードの既存有無**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
grep -n "TFN_ACRE_DTQ\|TFN_DEL_DTQ\|TFN_ACRE_PDQ\|TFN_DEL_PDQ\|TFN_ACRE_MPF\|TFN_DEL_MPF" \
     include/kernel_fncode.h > /tmp/dcre4-t1-fncode.txt
cat /tmp/dcre4-t1-fncode.txt
grep -c . /tmp/dcre4-t1-fncode.txt      # 期待: 6
```
期待（計画作成時の実測）:
`TFN_ACRE_DTQ (-196)`:137 / `TFN_ACRE_PDQ (-197)`:138 / `TFN_ACRE_MPF (-201)`:141 /
`TFN_DEL_DTQ (-212)`:149 / `TFN_DEL_PDQ (-213)`:150 / `TFN_DEL_MPF (-217)`:153。
→ `include/kernel_fncode.h` は**変更不要**。6値を確認結果表に転記する。

- [ ] **Step 2: §8-6 `DTQID`/`PDQID`/`MPFID` の現行実装（2レンジ化の形を確定）**

```bash
grep -n "define	DTQID\|define	PDQID\|define	MPFID" -A2 \
     kernel/dataqueue.h kernel/pridataq.h kernel/mempfix.h
grep -n "p_dtqcb_table\|p_pdqcb_table\|p_mpfcb_table" \
     kernel/dataqueue.h kernel/pridataq.h kernel/mempfix.h
grep -n "define tnum_dtq\|define tnum_pdq\|define tnum_mpf" \
     kernel/dataqueue.c kernel/pridataq.c kernel/mempfix.c
grep -n "include <queue.h>" kernel/dataqueue.h kernel/pridataq.h kernel/mempfix.h
```
期待（実測）:
- 3マクロとも**既に存在**し、既に **inib ポインタ差分式**（`dataqueue.h:108-109` /
  `pridataq.h:115-116` / `mempfix.h:120-121`）。CB は `DTQCB *const p_dtqcb_table[]`
  （`dataqueue.h:103`）＝**ポインタ表**（dcre の実体配列 `dtqcb_table[]` とは別物）。
- `tnum_dtq`/`tnum_pdq`/`tnum_mpf` は `.c` 側（`dataqueue.c:131` / `pridataq.c:123` /
  `mempfix.c:107`）にあり、`.h` には無い。
- `#include <queue.h>` は3つの `.h` の **`:51` に既にある**（追加不要）。
→ **判断: 段階3a と同じ「既存マクロの2レンジ化置換」。** `tnum_*` は `.h` へ移設が必要
（マクロから参照するため）。この判断と根拠を確認結果表に明記する。

- [ ] **Step 3: §8-2/§8-3/§8-4 dcre の `acre_*`/`del_*` 現物（本段階の設計の核）**

```bash
D=/home/honda/TOPPERS/ASP3/asp3_3.7/extension/dcre/kernel
sed -n '339,397p' $D/dataqueue.c      # acre_dtq
sed -n '402,444p' $D/dataqueue.c      # del_dtq
sed -n '316,379p' $D/pridataq.c       # acre_pdq
sed -n '384,426p' $D/pridataq.c       # del_pdq
sed -n '199,279p' $D/mempfix.c        # acre_mpf
sed -n '284,328p' $D/mempfix.c        # del_mpf
grep -n "define VALID_DPRI" $D/check.h
```
確認して確認結果表へ記録すること（**食い違ったら現行ソースを正とし、実測値を書く**）：

1. **`acre_*` の順序（訂正B/D の根拠・§8-2/§8-3/§8-4）**
   - 検査（`CHECK_VALIDATR` ほか）→ `lock_cpu()` → **`E_NOID`（`tnum_* == 0 || queue_empty`）が最初**
     → 管理領域の `malloc_mpk` → NULL なら `E_NOMEM` → **成功して初めて `queue_delete_next` で CB を pop**。
   - ★したがって **`E_NOMEM` のとき free-list は 1 要素も減っていない**（spec §5 の要求どおり）。
     段階1 `acre_tsk` と同じ順序である。
   - `acre_dtq`: **dtqcnt の範囲検査は無い**。`p_dtqmb != NULL` なら `CHECK_PAR(MB_ALIGN(p_dtqmb))` のみ。
     確保条件は `dtqcnt != 0 && p_dtqmb == NULL`。→ **訂正B**。
   - `acre_pdq`: 加えて `CHECK_PAR(VALID_DPRI(maxdpri))`。→ **訂正C**。
   - `acre_mpf`: `CHECK_PAR(blkcnt != 0)` / `CHECK_PAR(blksz != 0)` / `MPF_ALIGN(mpf)` /
     `MB_ALIGN(p_mpfmb)`、2段確保、②失敗時の巻き戻し条件は **`pk_cmpf->mpf == NULL`**、
     `MPFINIB.blksz` には `ROUND_MPF_T(blksz)` を入れる。→ **訂正D**。

2. **★`del_*` の順序（本段階の中核・上の「申し送り」）**
   - `E_NOEXS` → `E_OBJ` → `init_wait_queue`（dtq/pdq は swait+rwait の**2回**、mpf は1回）
     → **`TA_MBALLOC`/`TA_MEMALLOC` のビット検査と `free_mpk`** → **`xxxatr = TA_NOEXS;`**
     → `queue_insert_prev(&free_xxxcb, ...)` → ディスパッチ判断。
   - ★**属性の読みが `TA_NOEXS` の書込みより前にあること**を行番号つきで記録する
     （`dataqueue.c:427-430` / `pridataq.c:409-412` / `mempfix.c:308-314`）。
   - `del_dtq` だけ `CHECK_PAR(VALID_DTQID(...))`（E_PAR）、`del_pdq`/`del_mpf` は
     `CHECK_ID`（E_ID）。→ **訂正F**。

3. `VALID_DPRI` が dcre `check.h:71` にあり **FMP3 の `kernel/check.h` には無い**こと。→ **訂正C**。

- [ ] **Step 4: §8-1 `T_CDTQ`/`T_CPDQ`/`T_CMPF` のバイト照合（訂正A）**

```bash
D=/home/honda/TOPPERS/ASP3/asp3_3.7/extension/dcre
sed -n '246,252p;258,266p;285,291p' $D/include/kernel.h | cat -A | sed 's/\$$//'
```
確認すること：
- フィールド区切りが**タブ**であること（`^I`）と、その個数。
  実測: `\tATR\t\tdtqatr;\t\t/* ... */` / `\tuint_t\tdtqcnt;\t\t/* ... */` /
  `\tvoid\t*dtqmb;\t\t/* ... */`。
- `T_CPDQ.maxdpri` のコメントが **2行に折り返されている**こと
  （2行目は `\t\t\t\t\t\t   大値 */`）。
- 閉じ括弧は **`} T_CDTQ;` / `} T_CPDQ;` / `} T_CMPF;`（空白あり）**であること。
- `T_CMPF.mpf` の型が **`MPF_T *`**（`void *` ではない）であること。
→ **訂正A**：spec §2.1 の要旨を dcre 原文で置き換える。
★あわせて `include/kernel.h` の**隣接する既存ブロック**（`t_rdtq`＝`:248`、`t_rpdq`＝`:254`、
`t_rmpf`＝`:273`）が**同じタブ幅（`\tID\t\tstskid;`）**であることを確認する。
段階3a で追加した `T_CSEM`/`T_CFLG`/`T_CMTX`（`:227,238,263`）は
**タブが1個多い**（3a Task 3 の deferred minor）。
→ **段階3b は dcre 原文＝隣接 pristine ブロックのタブ幅に揃える**（3a の逸脱を再現しない）。
**3a の3ブロックは触らない**（本段階のスコープ外）。この判断を確認結果表に書く。

- [ ] **Step 5: §8-7 型・マクロの FMP3 既存有無（COUNT_MB_T の教訓／訂正H）**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
grep -n "} DTQMB;\|} PDQMB;\|} MPFMB;" kernel/dataqueue.h kernel/pridataq.h kernel/mempfix.h
grep -n "define COUNT_MPF_T\|define ROUND_MPF_T\|typedef	TOPPERS_MPF_T" include/kernel.h
grep -n "define MPF_ALIGN\|define MB_ALIGN\|define INDEX_NULL\|define INDEX_ALLOC" \
     kernel/check.h kernel/mempfix.h
grep -n "define TA_NOEXS\|define TA_MEMALLOC\|TA_MBALLOC" kernel/kernel_impl.h
grep -rn "TA_MBALLOC" kernel/*.py kernel/*.trb cfg_py/ | wc -l     # 期待: 0
grep -n "DTQMB\|MPFMB\|MPF_T" kernel/dataqueue.py kernel/mempfix.py
```
期待（実測）:
- `DTQMB`（`dataqueue.h:59`）/ `PDQMB`（`pridataq.h:63`）/ `MPFMB`（`mempfix.h:69`）は**既存**。
- `MPF_T`（`include/kernel.h:131`）/ `COUNT_MPF_T`（`:672`）/ `ROUND_MPF_T`（`:673`）は**既存**。
- `MPF_ALIGN`/`MB_ALIGN`（`check.h:408-418`）・`INDEX_NULL`/`INDEX_ALLOC`（`mempfix.h:57-58`）は**既存**。
- `TA_NOEXS`＝`((ATR)(-1))`（`kernel_impl.h:199`）、`TA_MEMALLOC`＝`UINT_C(0x8000)`
  （`kernel_impl.h:201-203`、`#ifndef` ガード付き）、**`TA_MBALLOC` は無い**。
- **cfg（両エンジン）は `TA_MBALLOC` を1度も出力しない**（`grep` 結果 0）。
  一方 `DTQMB`/`MPFMB`/`MPF_T`/`COUNT_MPF_T` は cfg が**今日すでに出力していて
  コンパイルが通っている**（`dataqueue.py:59-66`・`mempfix.py:75-96`）。
→ **訂正H**：段階1 COUNT_MB_T 型の欠陥は構造的に起こりえない。`TA_MBALLOC` を Task 2 に
置くのは「依存の衛生」が理由であることを spec §4 に明記する。

- [ ] **Step 6: §8-8 no-static 回帰 cfg の include 構成（3a serial.cfg 教訓の適用）**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
grep -rn "CRE_DTQ\|CRE_PDQ\|CRE_MPF" --include=*.cfg . | sort
cat test/test_common1.cfg
cat tools/cfg_error_tests/dcre_aid_sem_no_static.cfg
```
期待（実測）: 静的 `CRE_DTQ`/`CRE_PDQ`/`CRE_MPF` を含む `.cfg` は
`sample/sample1.cfg`（CRE_DTQ のみ）・`target/polarfire_soc_kit_gcc/softconsole/sample1/sample1.cfg`・
`test/perf2.cfg`・`test/test_dtq1.cfg`・`test/test_mpf1.cfg`・`test/test_notify1.cfg`・
`test/test_pdq1.cfg` の7本で、**`syssvc/*.cfg`・`target/*/*.cfg`・`test/test_common1.cfg` には
1件も無い**。
→ **段階3a の `dcre_aid_sem_no_static.cfg` が必要とした回避（`serial.cfg` の静的 `CRE_SEM`×2 を
避けるため `syslog.cfg`/`banner.cfg` を個別 INCLUDE する）は、dtq/pdq/mpf では不要**である。
3件とも **`INCLUDE("test/test_common1.cfg")` のままでよい**（`flg`/`mtx` 版と同じ形）。
★ただし **Task 2 Step 12 で実際に E_OBJ が出ることを実測で確かめる**こと
（出なければ隠れた静的インスタンスがある＝この結論が誤り。そのときは
`dcre_aid_sem_no_static.cfg` の形に切り替え、切り替えた事実を記録する）。

- [ ] **Step 7: §8-1 補足 cfg 共通枠組みが per-object 変更ゼロで通ること**

```bash
sed -n '108,145p' kernel/kernel.py         # KernelObject.__init__ / inibList
sed -n '145,275p' kernel/kernel.py         # generate（has_aid / 訂正E ガード / 予約CB）
grep -n "class DataqueueObject\|class PridataqObject\|class MempfixObject" -A3 \
     kernel/dataqueue.py kernel/pridataq.py kernel/mempfix.py
grep -n "inibList\|generateData\|omit_cb" kernel/dataqueue.py kernel/pridataq.py kernel/mempfix.py
```
期待: 3クラスとも `super().__init__("dtq", "dataqueue")` の形で共通枠組みを継承し、
`inibList`/`omit_cb`/`generateData` の**カスタマイズが無い**（`prepare` と `generateInib` のみ）。
`self.inibList` の既定は `{f"{OBJ_S}INIB": f"a{obj_s}inib_table"}`（`kernel.py:123`）なので
`DTQINIB _kernel_adtqinib_table[N];` 等が自動で出る。
→ **per-object テンプレートの変更はゼロ**（spec §3）であることを確定させ、
「編集したくなったら共通枠組みの理解が誤っている合図」と確認結果表に書く。
★静的オブジェクトの管理領域（`static DTQMB _kernel_dtqmb_<id>[n]` 等）は
`prepare` が生成するが、**動的スロット用の管理領域は cfg では生成しない**
（実行時に `malloc_mpk` する）ことも明記する。

- [ ] **Step 8: `initialize_*` の現行形（マスタ限定であること・★ゲート対象）**

```bash
sed -n '138,165p' kernel/dataqueue.c
sed -n '131,160p' kernel/pridataq.c
sed -n '115,143p' kernel/mempfix.c
grep -n "iprcid\|affinity\|p_pcb" kernel/dataqueue.h kernel/pridataq.h kernel/mempfix.h
grep -n "init_wait_queue" kernel/wait.h kernel/wait.c
sed -n '211,228p' kernel/wait.c           # init_wait_queue 本体（E_DLT）
```
期待（実測）: 3つとも `if (p_my_pcb->prcid == TOPPERS_MASTER_PRCID) { ... }` で
**既に全体がマスタ限定**。段階2 の `initialize_cyclic` にあった
`if (p_my_pcb->p_tevtcb == NULL) { return; }` のような**時間イベント処理プロセッサ判定は無い**。
3つの `.h` に `iprcid`/`affinity`/`p_pcb` は **1件も出ない**。
`init_wait_queue(PCB *p_my_pcb, QUEUE *p_wait_queue)`（`wait.h:249`、本体 `wait.c:215-228`）は
**MP 対応済み**で、待ちタスクを `wait_dequeue_tmevtb` → `winfo.wercd = E_DLT` →
`make_non_wait(p_my_pcb, p_tcb)` で解除する。**新規の解除機構は書かない。**
→ **変えるのは静的ループの境界と、その直後に足す動的スロット節だけ**であることを確定させる。
**プロセッサフィルタを新設しない**ことを確認結果表に太字で書く（Constraint 4）。

- [ ] **Step 9: E_NOEXS 挿入対象23関数の棚卸し（★訂正E の根拠）**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
grep -n "^snd_dtq\|^psnd_dtq\|^tsnd_dtq\|^fsnd_dtq\|^rcv_dtq\|^prcv_dtq\|^trcv_dtq\|^ini_dtq\|^ref_dtq" kernel/dataqueue.c
grep -n "^snd_pdq\|^psnd_pdq\|^tsnd_pdq\|^rcv_pdq\|^prcv_pdq\|^trcv_pdq\|^ini_pdq\|^ref_pdq" kernel/pridataq.c
grep -n "^get_mpf\|^pget_mpf\|^tget_mpf\|^rel_mpf\|^ini_mpf\|^ref_mpf" kernel/mempfix.c
# ★ロック取得前に p_xxxinib を読んでいる関数を洗い出す
grep -n "CHECK_ILUSE\|CHECK_PAR" kernel/dataqueue.c kernel/pridataq.c kernel/mempfix.c
```
期待（実測）: 対象は **dataqueue.c 9 / pridataq.c 8 / mempfix.c 6 = 23関数**。
定義行は dataqueue.c `313,367,417,474,521,578,622,683,727` /
pridataq.c `290,345,396,455,513,557,619,663` / mempfix.c `173,222,257,311,370,413`。
★このうち **5関数が `lock_cpu*()` より前に `p_xxxinib` を読んでいる**：
`fsnd_dtq`（`dataqueue.c:485`）・`snd_pdq`（`pridataq.c:301`）・`psnd_pdq`（`:357`）・
`tsnd_pdq`（`:408`）・`rel_mpf`（`mempfix.c:324-330`）。
→ **訂正E** の表を確認結果表へそのまま転記する。
**FMP3 固有関数（段階2 の `msta_cyc`/`msta_alm` に相当するもの）は dtq/pdq/mpf には無い**
ことも記録する（＝上流に先例が無い類推適用は本段階でも 0 件）。

- [ ] **Step 10: spec への反映（訂正8件 + §9 新設）**

★**本計画の訂正A〜H の文面を逐語で移す。要約・言い換えをしない。**

  - §2.1 の `T_CDTQ`/`T_CPDQ`/`T_CMPF` を **訂正A**（dcre 原文＝Step 4 の実測）で置き換える。
    「調査時点の要旨」という但し書きを削除する。
  - §2.3 の `acre_dtq` 節を **訂正B** で置き換える（dtqcnt 範囲検査は無い／ユーザ供給 dtqmb は
    受理し TA_MBALLOC を立てない／dtqcnt==0 は管理領域を確保しない）。
  - §2.3 の `acre_pdq` 節に **訂正C**（`VALID_DPRI` は FMP3 に無く `kernel/check.h` へ追加する）を追記する。
  - §2.3 の `acre_mpf` 節を **訂正D** で置き換える（巻き戻しは**ある**／条件は `pk_cmpf->mpf == NULL`／
    `blkcnt != 0`・`blksz != 0`・`MPF_ALIGN`・`MB_ALIGN` の検査がある／`blksz` は `ROUND_MPF_T` で丸める）。
  - §2.3 の `del_*` 節に **訂正F**（`del_dtq` も `CHECK_ID`＝E_ID。dcre の `CHECK_PAR` は
    dcre 側の不整合で、段階3a 訂正D の同型2件目）と、★**「属性の読みは TA_NOEXS 書込みより前」
    という順序制約**（根拠：`TA_NOEXS` は `((ATR)(-1))` で全ビットが 1 なので
    `(atr & TA_MBALLOC) != 0U` は TA_NOEXS に対して必ず真になる）を追記する。
  - §4 の「E_NOEXS 23関数 — すべて 3a の型どおり」を **訂正E** で置き換える
    （5関数は dcre と同じ構造変更が要る・表つき）。
  - §4 に **訂正H**（`TA_MBALLOC` は cfg の出力トークンではない。Task 2 に置くのは依存の衛生が理由）を追記する。
  - §1 と §5 と §6 に **訂正G**（`malloc_mempool`/`free_mempool` は bump allocator で
    count==0 のときだけ brk を戻す。3点の帰結）を追記する。
  - §6 に、テストで使う静的オブジェクト（`DTQ1`/`PDQ1`/`MPF1`）・タスク編成
    （`TASK1`=MID/PRC1、`TASK2`=HIGH/PRC1、`TASK3`=HIGH/PRC2）・
    `MPK_SIZE` の値と根拠を確定として書く（Task 6 Step 2-4 の内容）。
  - §7 の「8タスク構成 …（7タスク構成でも可 — 計画で確定）」を **7タスク構成に確定**させる
    （段階2 hardening のような別枠が無いため）。
  - spec 末尾に `## 9. 実装前確認の結果（2026-08-04 実測）` を新設し、
    Step 1-9 の確認結果を**表**で記録する（後続 Task はここを参照する）。
    Step 3 の dcre 行範囲と Step 9 の23関数一覧・5関数の構造変更表は**そのまま**転記する。

- [ ] **Step 11: コミット**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
git add docs/superpowers/specs/2026-08-04-fmp3-dcre-stage3b-dtq-pdq-mpf-design.md
git commit -m "docs(spec): dcre段階3bの実装前確認と訂正8件（T_C*バイト照合・dtqcnt無検査とユーザ供給mb・VALID_DPRI追加・acre_mpfの巻き戻し・E_NOEXS5関数の構造変更・del_dtqのE_ID・プールのbump特性・TA_MBALLOCの置き場）"
```

---
### Task 2: cfg 両エンジン — AID_DTQ / AID_PDQ / AID_MPF と T_CDTQ/T_CPDQ/T_CMPF と TA_MBALLOC

**推奨モデル:** 中位（sonnet）。許容差分リストの厳密判定と、実コンパイル検査の設計が要る。

**Files:**
- Modify: `kernel/kernel_api.def`（3行追加・pristine）
- Modify: `include/kernel.h`（`T_CDTQ`/`T_CPDQ`/`T_CMPF` + 6宣言・pristine）
- Modify: `kernel/kernel_impl.h`（`TA_MBALLOC` の定義・pristine）
- Modify: `test/test_dcre_mix.cfg`（静的 `CRE_DTQ`/`CRE_PDQ`/`CRE_MPF` と
  `AID_DTQ(2)`/`AID_PDQ(1)`/`AID_MPF(1)` を追記・pristine）
- Create: `tools/cfg_error_tests/dcre_aid_dtq_in_class.cfg`
  `tools/cfg_error_tests/dcre_aid_pdq_in_class.cfg`
  `tools/cfg_error_tests/dcre_aid_mpf_in_class.cfg`
  `tools/cfg_error_tests/dcre_aid_dtq_no_static.cfg`
  `tools/cfg_error_tests/dcre_aid_pdq_no_static.cfg`
  `tools/cfg_error_tests/dcre_aid_mpf_no_static.cfg`
- Modify: `DIVERGENCE_MAP.md`

**★この Task で `kernel/dataqueue.py` `dataqueue.trb` `pridataq.py` `pridataq.trb`
`mempfix.py` `mempfix.trb` および `kernel/kernel.py` `kernel/kernel.trb` を
編集してはならない**（Task 1 Step 7 の結論：per-object 変更ゼロ・共通枠組みも無変更）。
編集したくなったら、それは共通枠組みの理解が誤っている合図である。

**Interfaces（後続 Task が依存する生成物）:**
- Consumes: Task 1 の確認結果表（訂正A/H、`T_C*` のバイト像、タブ幅の判断）。
- Produces（`kernel_cfg.c`/`kernel_cfg.h` に恒常出力）:
  `#define TNUM_SDTQID <n>` / `const ID _kernel_tmax_sdtqid`、
  `DTQINIB _kernel_adtqinib_table[N]`（N=0 時
  `TOPPERS_EMPTY_LABEL(DTQINIB, _kernel_adtqinib_table);`）、
  予約 DTQCB `static DTQCB _kernel_adtqcb_<i>;`（i=1..N）と `_kernel_p_dtqcb_table` 末尾への追加。
  pdq 側は `TNUM_SPDQID` / `_kernel_tmax_spdqid` / `_kernel_apdqinib_table` / `_kernel_apdqcb_<i>`、
  mpf 側は `TNUM_SMPFID` / `_kernel_tmax_smpfid` / `_kernel_ampfinib_table` / `_kernel_ampfcb_<i>`。
  `TNUM_DTQID`/`TNUM_PDQID`/`TNUM_MPFID` は**総数**（静的+AID）へ意味変更。
- Produces（`include/kernel.h`）: `T_CDTQ` `T_CPDQ` `T_CMPF` と6つの `extern` 宣言。
- Produces（`kernel/kernel_impl.h`）: `TA_MBALLOC`（Task 3/4/5 が使う）。
- Produces（`test/test_dcre_mix.cfg`）: 3家族の AID を含む実構成（Task 3/4/5 のリンク検査の媒体）。

- [ ] **Step 1: 基準生成物の保存（管理された差分の比較元）**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
cmake --build build/musca_b1-2core > /tmp/dcre4-t2-base-build.log 2>&1; echo "rc=$?"
rm -rf /tmp/dcre4-base-generated
cp -r build/musca_b1-2core/generated /tmp/dcre4-base-generated
cmake --build build/musca_b1-2core-tmix > /tmp/dcre4-t2-base-tmix.log 2>&1; echo "rc=$?"
rm -rf /tmp/dcre4-base-tmix-generated
cp -r build/musca_b1-2core-tmix/generated /tmp/dcre4-base-tmix-generated
```
（`build/musca_b1-2core-tmix` が無ければ段階3a Task 2 Step 8 のコマンドで作る。
無かった事実を記録すること。）

- [ ] **Step 2: `kernel/kernel_api.def` に dcre と同一の3行を追加**

ファイル末尾（`AID_MTX .nomtx` の次）に：

```
AID_DTQ .nodtq
AID_PDQ .nopdq
AID_MPF .nompf
```

（`AID_TSK .notsk` / `AID_CYC .nocyc` / `AID_ALM .noalm` / `AID_SEM .nosem` /
`AID_FLG .noflg` / `AID_MTX .nomtx` と同じ形。共通枠組みが `self.noobj = "no" + obj` で
参照するため、パラメータ名は `nodtq`/`nopdq`/`nompf` でなければならない＝`kernel.py:121`。）
★`kernel_api.def` は **Ruby / Python 両エンジンの共通入力**なので、
この1ファイルの変更で「両エンジン同時変更」（Constraint 5）を満たす。

- [ ] **Step 3: `include/kernel.h` にパケット型3つを追加（★dcre からバイト転写）**

`typedef struct t_rdtq {`（`include/kernel.h:248` 付近）の**直前**に `T_CDTQ`、
`typedef struct t_rpdq {`（`:254` 付近）の**直前**に `T_CPDQ`、
`typedef struct t_rmpf {`（`:273` 付近）の**直前**に `T_CMPF` を置く
（＝既存の `T_CSEM`/`T_RSEM`・`T_CCYC`/`T_RCYC` と同じく「生成パケットを参照パケットの
直前に置く」流儀。実際の行番号は動いている可能性があるので、**行番号ではなく
`typedef struct t_rdtq {` 等の文字列を目印にする**）。

dcre `include/kernel.h:246-252` / `258-266` / `285-291` からそのまま転記する
（**インデントはタブ。下記の空白はタブに置き換えること**）：

```c
typedef struct t_cdtq {
	ATR		dtqatr;		/* データキュー属性 */
	uint_t	dtqcnt;		/* データキュー管理領域に格納できるデータ数 */
	void	*dtqmb;		/* データキュー管理領域の先頭番地 */
} T_CDTQ;
```

```c
typedef struct t_cpdq {
	ATR		pdqatr;		/* 優先度データキュー属性 */
	uint_t	pdqcnt;		/* 優先度データキュー管理領域に格納できるデータ数 */
	PRI		maxdpri;	/* 優先度データキューに送信できるデータ優先度の最
						   大値 */
	void	*pdqmb;		/* 優先度データキュー管理領域の先頭番地 */
} T_CPDQ;
```

```c
typedef struct t_cmpf {
	ATR		mpfatr;		/* 固定長メモリプール属性 */
	uint_t	blkcnt;		/* 獲得できる固定長メモリブロックの数 */
	uint_t	blksz;		/* 固定長メモリブロックのサイズ */
	MPF_T	*mpf;		/* 固定長メモリプール領域の先頭番地 */
	void	*mpfmb;		/* 固定長メモリプール管理領域の先頭番地 */
} T_CMPF;
```

★**タブ幅の決定（Task 1 Step 4 の記録に従う）:** フィールド名の前は
`ATR` / `uint_t` / `void` / `PRI` / `MPF_T` の後に**タブ**を置き、
`\tATR\t\tdtqatr;` のように**短い型名（`ATR`・`PRI`）だけタブ2個**、
`uint_t`・`MPF_T`・`void` はタブ1個にする（＝dcre 原文そのまま）。
これは**隣接する既存 pristine ブロック（`t_rdtq`/`t_rpdq`/`t_rmpf`）と同一の流儀**である。
段階3a が追加した `T_CSEM`/`T_CFLG`/`T_CMTX`（`:227,238,263`）は**タブが1個多い**が、
それは 3a の deferred minor であり、**本段階では再現しない／3a のブロックも直さない**
（スコープ外）。この判断は Task 1 の確認結果表に記録済みである。

**検証（転写が正しいこと）:**
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
D=/home/honda/TOPPERS/ASP3/asp3_3.7/extension/dcre
diff <(sed -n '/^typedef struct t_cdtq {/,/^} T_CDTQ;/p' $D/include/kernel.h) \
     <(sed -n '/^typedef struct t_cdtq {/,/^} T_CDTQ;/p' include/kernel.h); echo "cdtq diff rc=$?"
diff <(sed -n '/^typedef struct t_cpdq {/,/^} T_CPDQ;/p' $D/include/kernel.h) \
     <(sed -n '/^typedef struct t_cpdq {/,/^} T_CPDQ;/p' include/kernel.h); echo "cpdq diff rc=$?"
diff <(sed -n '/^typedef struct t_cmpf {/,/^} T_CMPF;/p' $D/include/kernel.h) \
     <(sed -n '/^typedef struct t_cmpf {/,/^} T_CMPF;/p' include/kernel.h); echo "cmpf diff rc=$?"
```
期待: **3件とも rc=0（バイト一致）**。1行でも差が出たら転写ミスである。

（`ATR`/`uint_t`/`PRI`/`MPF_T` はいずれも既存。追加の include は不要。
★`T_CMPF.mpf` は **`MPF_T *`** であって `MPFINIB.mpf` の `void *` ではない。
`acre_mpf` の中で `p_mpfinib->mpf = mpf;` と代入するとき `MPF_T *` → `void *` の
暗黙変換が働く＝キャスト不要。）

- [ ] **Step 4: `include/kernel.h` に6つのサービスコール宣言を追加**

`extern ER snd_dtq(ID dtqid, intptr_t data) throw();`（`:375` 付近）の**直前**に：
```c
extern ER_ID	acre_dtq(const T_CDTQ *pk_cdtq) throw();
extern ER		del_dtq(ID dtqid) throw();
```
`extern ER snd_pdq(ID pdqid, intptr_t data, PRI datapri) throw();`（`:385` 付近）の**直前**に：
```c
extern ER_ID	acre_pdq(const T_CPDQ *pk_cpdq) throw();
extern ER		del_pdq(ID pdqid) throw();
```
`extern ER get_mpf(ID mpfid, void **p_blk) throw();`（`:413` 付近）の**直前**に：
```c
extern ER_ID	acre_mpf(const T_CMPF *pk_cmpf) throw();
extern ER		del_mpf(ID mpfid) throw();
```

（機能コードは Task 1 Step 1 の記録どおり6件とも `include/kernel_fncode.h` に**既存**＝
`kernel_fncode.h` は**変更しない**。
返値型は段階1 の `acre_tsk`・段階2/3a の `acre_cyc`/`acre_sem` 等と揃えて **`ER_ID`** にする。
dcre の `.c` は `ER_UINT` を使うが、どちらも `int_t` の別名であり宣言と定義で揃っていれば問題ない
— **段階1/2/3a で確立済みの意図的な逸脱**として台帳に1文書く。）

- [ ] **Step 5: `kernel/kernel_impl.h` に `TA_MBALLOC` を追加（★訂正H）**

`TA_MEMALLOC` のブロック（`kernel_impl.h:201-203`）の**直後**に、同じ形で追加する：

```c
#ifndef TA_MBALLOC
#define TA_MBALLOC		UINT_C(0x4000)		/* 管理領域をカーネルで確保 */
#endif /* TA_MBALLOC */
```

結果として `kernel_impl.h:196-207` 付近が次の並びになる：

```c
/*
 *  カーネル内部で使用する属性の定義
 */
#define TA_NOEXS		((ATR)(-1))			/* 未登録状態 */

#ifndef TA_MEMALLOC
#define TA_MEMALLOC		UINT_C(0x8000)		/* メモリ領域をカーネルで確保 */
#endif /* TA_MEMALLOC */

#ifndef TA_MBALLOC
#define TA_MBALLOC		UINT_C(0x4000)		/* 管理領域をカーネルで確保 */
#endif /* TA_MBALLOC */
```

★**この Task に置く理由**（訂正H）：`TA_MBALLOC` は Task 3/4/5 の3つが等しく使う共有定義で、
どれか1つのタスクに置くと他の2つが兄弟タスクのカーネル編集に依存する。
**cfg は `TA_MBALLOC` を出力しない**ので段階1 COUNT_MB_T 型の案件ではない
（Task 1 Step 5 で実測済み）。この `#define` の追加は**生成物を1バイトも変えてはならない**
＝Step 7 の管理された差分検査がそれを実証する。

★**`TA_NOEXS` との関係を必ず理解しておくこと**（本段階の中核リスク）：
`TA_NOEXS = ((ATR)(-1))` は**全ビットが 1** なので、`TA_NOEXS & TA_MBALLOC` も
`TA_NOEXS & TA_MEMALLOC` も**非 0（真）**である。Task 3/4/5 の `del_*` では
**必ず `TA_NOEXS` を書く前にビット検査と `free_mpk` を済ませる**こと。

- [ ] **Step 6: 全8構成のビルドと等価性**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
for p in musca_b1 musca_b1-2core rp2350_pico2 polarfire_soc_kit-qemu \
         kria_arm64 kria_arm64-1core kria_r5 kria_r5-2core; do
  cmake --preset $p > /tmp/dcre4-t2-conf-$p.log 2>&1 || { echo "$p CONF FAIL"; continue; }
  cmake --build build/$p > /tmp/dcre4-t2-build-$p.log 2>&1; echo "$p build rc=$?"
  tools/cfg_equivalence.sh build/$p > /tmp/dcre4-t2-eq-$p.log 2>&1; echo "$p eq rc=$?"
done
```
期待: すべて rc=0（`RESULT = MATCH`）。**exit=2 は不合格**として原因を調べる（Constraint 14）。

- [ ] **Step 7: ★管理された差分の検査（AID 無し構成の生成物）**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
diff -u /tmp/dcre4-base-generated/kernel_cfg.h build/musca_b1-2core/generated/kernel_cfg.h
echo "h diff rc=$?"
diff -u /tmp/dcre4-base-generated/kernel_cfg.c build/musca_b1-2core/generated/kernel_cfg.c \
     > /tmp/dcre4-t2-managed-diff.txt; cat /tmp/dcre4-t2-managed-diff.txt
```
期待: `kernel_cfg.h` は**差分なし（rc=0）**。`kernel_cfg.c` の差分が次の
**許容リストと完全一致**すること（**1件でも余分な差分があれば不合格**）：

1. Dataqueue ブロック: `#define TNUM_SDTQID <静的個数>` 行と
   `const ID _kernel_tmax_sdtqid = (TMIN_DTQID + TNUM_SDTQID - 1);` 行の追加
   （`_kernel_tmax_dtqid` 行の直後。空行位置の変化を含む）
2. `_kernel_dtqinib_table` のサイズトークン `[TNUM_DTQID]` → `[TNUM_SDTQID]`
3. `TOPPERS_EMPTY_LABEL(DTQINIB, _kernel_adtqinib_table);` の追加
4. Pridataq ブロック: 1〜3 の pdq 版
   （`TNUM_SPDQID` / `_kernel_tmax_spdqid` / `[TNUM_SPDQID]` /
   `TOPPERS_EMPTY_LABEL(PDQINIB, _kernel_apdqinib_table);`）
5. Mempfix ブロック: 1〜3 の mpf 版
   （`TNUM_SMPFID` / `_kernel_tmax_smpfid` / `[TNUM_SMPFID]` /
   `TOPPERS_EMPTY_LABEL(MPFINIB, _kernel_ampfinib_table);`）

**★次のものが差分に出たら不合格である**（設計の誤りを示す）：
- `_kernel_p_dtqcb_table` の**サイズトークン**が変わる
  （`[TNUM_DTQID]` のまま＝総数であるべき。`TNUM_SDTQID` になっていたら共通枠組みの誤読）
- `static DTQMB _kernel_dtqmb_<id>[n]` / `static MPF_T _kernel_mpf_<id>[...]` /
  `static MPFMB _kernel_mpfmb_<id>[n]` の**個数や中身**の変化
  （静的オブジェクトの管理領域は cfg 生成のままで変わってはならない）
- task / cyclic / alarm / semaphore / eventflag / mutex ブロックの変化
  （段階3b は dtq/pdq/mpf にしか触っていない）
- `TA_MBALLOC` の文字列（cfg は出力しない＝訂正H の裏取り）

```bash
grep -c "TA_MBALLOC" build/musca_b1-2core/generated/kernel_cfg.c   # 期待: 0
```

- [ ] **Step 8: `test/test_dcre_mix.cfg` を dtq/pdq/mpf まで拡張**

★**sample1.cfg は本段階の positive control には使えない。**
`sample/sample1.cfg` は `CRE_DTQ` を6個持つが **`CRE_PDQ`/`CRE_MPF` を1個も持たない**ため、
`AID_PDQ`/`AID_MPF` を足すと訂正E ガード（「静的オブジェクトが1個以上必要」）で
**E_OBJ になる**（Task 1 Step 6 で実測済み）。
→ **positive control と実コンパイル検査は `test/test_dcre_mix` を媒体にする**
（段階3a Task 3 Step 7 が sample1 を使えたのとの相違。この事実を記録すること）。

`test/test_dcre_mix.cfg` の `CLASS(CLS_PRC1)` ブロックの末尾（`CRE_MTX(MTX1, ...)` の次）に
静的オブジェクト3つを足し、ファイル末尾に AID 3行を足す：

```c
	CRE_DTQ(DTQ1, { TA_TPRI, 2, NULL });
	CRE_PDQ(PDQ1, { TA_TPRI, 2, 4, NULL });
	CRE_MPF(MPF1, { TA_TPRI, 2, 32, NULL, NULL });
```

```c
AID_DTQ(2);
AID_PDQ(1);
AID_MPF(1);
```

そして混在の説明コメント（`★混在の実体：…` のブロック）を次に置き換える：

```c
/*
 *  ★混在の実体：AID_TSK / AID_CYC / AID_SEM / AID_MTX / AID_DTQ / AID_PDQ /
 *  AID_MPF は書き，AID_ALM と AID_FLG は書かない．同一構成の中に has_aid が
 *  真のオブジェクトと偽のオブジェクトが共存することを両エンジンで検査する
 *  （段階2 最終レビュー Minor 3 の hardening）．
 *  段階3b では加えて，この cfg が dtq/pdq/mpf の acre_*/del_* を実際に
 *  リンクさせる唯一の非テスト構成として働く（sample1.cfg は静的 CRE_PDQ/
 *  CRE_MPF を持たないため AID_PDQ/AID_MPF を足せない）．
 */
```

★`CRE_PDQ` の第3引数 `maxdpri` は `4`（`TMIN_DPRI=1 <= 4 <= TMAX_DPRI=16`）。
`CRE_MPF` は `{ mpfatr, blkcnt, blksz, mpf, mpfmb }` の5要素で、末尾2つは `NULL`
（cfg が自動確保する。`mpf`/`mpfmb` に非 NULL を書くと **E_NOSPT** になる＝
`mempfix.py:78-96`。動的生成側が受理するのとは非対称だが、これは既存 pristine の仕様である）。
実際の引数名と省略可否は `kernel/kernel_api.def:4,5,8` で確認すること。

- [ ] **Step 9: positive control — AID 有り構成で出力が実際に変わり、両エンジンでバイト一致**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
cmake --build build/musca_b1-2core-tmix > /tmp/dcre4-t2-mix-build.log 2>&1; echo "build rc=$?"
tools/cfg_equivalence.sh build/musca_b1-2core-tmix > /tmp/dcre4-t2-mix-eq.log 2>&1; echo "eq rc=$?"
G=build/musca_b1-2core-tmix/generated/kernel_cfg.c
grep -n "TNUM_SDTQID\|_kernel_adtqinib_table\|_kernel_adtqcb_" $G
grep -n "TNUM_SPDQID\|_kernel_apdqinib_table\|_kernel_apdqcb_" $G
grep -n "TNUM_SMPFID\|_kernel_ampfinib_table\|_kernel_ampfcb_" $G
grep -n "define TNUM_DTQID\|define TNUM_PDQID\|define TNUM_MPFID" \
     build/musca_b1-2core-tmix/generated/kernel_cfg.h
grep -n "EMPTY_LABEL(FLGINIB, _kernel_aflginib_table)\|EMPTY_LABEL(ALMINIB, _kernel_aalminib_table)" $G
```
期待:
- `DTQINIB _kernel_adtqinib_table[2];`（`TOPPERS_EMPTY_LABEL` ではなく**実体**）、
  `PDQINIB _kernel_apdqinib_table[1];`、`MPFINIB _kernel_ampfinib_table[1];`。
- `static DTQCB _kernel_adtqcb_1;` `_kernel_adtqcb_2;` と
  `_kernel_p_dtqcb_table` 末尾への `&_kernel_adtqcb_1` `&_kernel_adtqcb_2` 追加。pdq/mpf も同様。
- `TNUM_DTQID` = 静的1個 + 2 = 3、`TNUM_PDQID` = 1 + 1 = 2、`TNUM_MPFID` = 1 + 1 = 2。
- **`EMPTY_LABEL(FLGINIB, ...)` と `EMPTY_LABEL(ALMINIB, ...)` が同じファイルに同居**
  （＝混在構成が保たれている証拠）。
- `cfg_equivalence.sh` rc=0（**Ruby/Python がバイト一致**）。**rc=2 は不合格。**

- [ ] **Step 10: ★★compile-through control（段階1 COUNT_MB_T ／ 段階2 訂正B の再発防止）**

Step 9 の `build rc=0` は**単なる cfg 生成の成功ではなく、`kernel_cfg.c` の
実コンパイルとリンクの成功**でなければならない。`cfg_equivalence.sh` は両エンジンの
**生成文字列を diff するだけでコンパイルしない**ため、
「両エンジンが同じように未定義の型/マクロを出力する」欠陥を**構造的に検出できない**。

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
touch /tmp/dcre4-t2-marker
cmake --build build/musca_b1-2core-tmix --target clean > /dev/null 2>&1
cmake --build build/musca_b1-2core-tmix > /tmp/dcre4-t2-mix-rebuild.log 2>&1; echo "rc=$?"
find build/musca_b1-2core-tmix -name 'kernel_cfg.c.obj' -newer /tmp/dcre4-t2-marker -print
find build/musca_b1-2core-tmix -name '*.elf' -newer /tmp/dcre4-t2-marker -print
```
期待: `kernel_cfg.c.obj` と `.elf` が **marker より新しいタイムスタンプで実在**すること
（＝`DTQINIB _kernel_adtqinib_table[2];` 等を含む `kernel_cfg.c` が実際にコンパイル・
リンクされた）。`.obj` 名が違う環境なら `find build/musca_b1-2core-tmix -name 'kernel_cfg*.o*'` で探す。

さらに**64bit ターゲットでも**同じ構成をコンパイル・リンクする（新規ビルドディレクトリ）：

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
cmake --preset kria_arm64 -B build/kria_arm64-tmix \
  -DFMP3_APPLNAME=test_dcre_mix -DFMP3_APPLDIR=test \
  -DFMP3_SYSSVC_TARGET_C_FILES=$PWD/syssvc/test_svc.c > /tmp/dcre4-t2-a64mix-conf.log 2>&1
echo "conf rc=$?"
cmake --build build/kria_arm64-tmix > /tmp/dcre4-t2-a64mix-build.log 2>&1; echo "build rc=$?"
tools/cfg_equivalence.sh build/kria_arm64-tmix > /tmp/dcre4-t2-a64mix-eq.log 2>&1; echo "eq rc=$?"
find build/kria_arm64-tmix -name 'kernel_cfg.c.obj' -o -name 'kernel_cfg.c.o' | head
```
期待: conf/build/eq とも rc=0、`kernel_cfg.c` のオブジェクトが実在。
（★64bit では `MPF_T = intptr_t` が 8 バイト・`ROUND_MPF_T(32)` の丸めが 32 のまま、
`sizeof(DTQMB) = 8` になる。**RAM inib 配列（`DTQINIB _kernel_adtqinib_table[2];` 等）が
リンクできること**の確認としても意味がある。段階2 訂正D の 64bit 問題は本段階では
**構造的に発生しない**〔Constraint 6〕。）

- [ ] **Step 11: negative control（等価性検査が空虚でないことの実演）**

`kernel/kernel_api.def` を壊しても両エンジン共通の入力なので差が出ない。
**Python 側だけを壊す**：`kernel/kernel.py` の

```python
            kernelCfgC.add(f"#define TNUM_S{self.OBJ}ID\t"
                           f"{len(cfgData[self.api])}")
```

を一時的に `f"{len(cfgData[self.api]) + 1}"` に書き換えて：

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
cmake --build build/musca_b1-2core-tmix > /dev/null 2>&1
tools/cfg_equivalence.sh build/musca_b1-2core-tmix > /tmp/dcre4-t2-neg.log 2>&1; echo "rc=$?"
grep -n "MISMATCH\|RESULT" /tmp/dcre4-t2-neg.log
```
期待: **rc=1（MISMATCH）**。rc=0 なら等価性検査が空虚である。**rc=2 も不合格**（前提未充足）。
確認後、書き換えを**復元**して rc=0 に戻ることを再確認する
（`git diff kernel/kernel.py` が空になることを見る）。

★**この control を「実行したことにして飛ばさない」こと。** 段階3b は cfg 側の変更が
`kernel_api.def` の3行だけであり、等価性検査が本当に効いているかを確かめる機会が
ここしかない。

- [ ] **Step 12: エラー回帰ケース6件の追加（in-class E_RSATR ×3 + no-static E_OBJ ×3）**

★Task 1 Step 6 の結論により、**no-static 版は `INCLUDE("test/test_common1.cfg")` のままでよい**
（`syssvc/*.cfg` に静的 `CRE_DTQ`/`CRE_PDQ`/`CRE_MPF` が無い。段階3a の
`dcre_aid_sem_no_static.cfg` が `serial.cfg` を避けるために取った回避は不要）。
**ただし実測で E_OBJ が出ることを確かめる**（出なければ隠れた静的インスタンスがある）。

`tools/cfg_error_tests/dcre_aid_dtq_in_class.cfg`：
```c
/*
 *  AID_DTQ をクラスの囲みの中に書くと E_RSATR（クラス外専用 API）
 *
 *  dcre_aid_in_class.cfg と同じ理由（cfg_py/cfg.py:search_file_path が
 *  cwd相対でしか解決できず、run.shは常にスクラッチディレクトリへcdする）
 *  で、「test/test_common1.cfg」と書き、実行時に EXTRA_CFLAGS として
 *  「-I<repo>/test」（第4引数）を渡す。
 */
INCLUDE("test/test_common1.cfg");
#include "test_int2.h"
CLASS(CLS_PRC1) {
	CRE_TSK(TASK1, { TA_ACT, 1, task1, MID_PRIORITY, STACK_SIZE, NULL });
	CRE_DTQ(DTQ1, { TA_TPRI, 2, NULL });
	AID_DTQ(1);
}
```

`tools/cfg_error_tests/dcre_aid_pdq_in_class.cfg`（同じヘッダコメント。`DTQ`→`PDQ`）：
```c
INCLUDE("test/test_common1.cfg");
#include "test_int2.h"
CLASS(CLS_PRC1) {
	CRE_TSK(TASK1, { TA_ACT, 1, task1, MID_PRIORITY, STACK_SIZE, NULL });
	CRE_PDQ(PDQ1, { TA_TPRI, 2, 4, NULL });
	AID_PDQ(1);
}
```

`tools/cfg_error_tests/dcre_aid_mpf_in_class.cfg`（同。`DTQ`→`MPF`）：
```c
INCLUDE("test/test_common1.cfg");
#include "test_int2.h"
CLASS(CLS_PRC1) {
	CRE_TSK(TASK1, { TA_ACT, 1, task1, MID_PRIORITY, STACK_SIZE, NULL });
	CRE_MPF(MPF1, { TA_TPRI, 2, 32, NULL, NULL });
	AID_MPF(1);
}
```

`tools/cfg_error_tests/dcre_aid_dtq_no_static.cfg`（訂正E ガードの dtq 版）：
```c
/*
 *  静的な CRE_DTQ が1個も無いのに AID_DTQ を書くと E_OBJ（訂正E ガード）
 *
 *  ★段階3a の dcre_aid_sem_no_static.cfg と異なり、test_common1.cfg を
 *  そのまま INCLUDE してよい。syssvc/*.cfg・target/*/*.cfg のいずれにも
 *  静的な CRE_DTQ/CRE_PDQ/CRE_MPF が存在しないことを現物で確認済み
 *  （段階3b Task 1 Step 6）。sem だけが serial.cfg の CRE_SEM×2 という
 *  罠を持っていた。
 *
 *  実行時に EXTRA_CFLAGS として「-I<repo>/test」（第4引数）を渡すこと。
 */
INCLUDE("test/test_common1.cfg");
#include "test_int2.h"
CLASS(CLS_PRC1) {
	CRE_TSK(TASK1, { TA_ACT, 1, task1, MID_PRIORITY, STACK_SIZE, NULL });
}
AID_DTQ(2);
```

`tools/cfg_error_tests/dcre_aid_pdq_no_static.cfg` / `dcre_aid_mpf_no_static.cfg` は
同じ内容で最終行を `AID_PDQ(2);` / `AID_MPF(2);` に替えたもの
（コメントの `CRE_DTQ`/`AID_DTQ` も読み替える）。

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
R=tools/cfg_error_tests/run.sh
X="-I$PWD/test"
M=build/musca_b1-2core
$R $M tools/cfg_error_tests/dcre_aid_dtq_in_class.cfg   E_RSATR "$X"; echo "1:$?"
$R $M tools/cfg_error_tests/dcre_aid_pdq_in_class.cfg   E_RSATR "$X"; echo "2:$?"
$R $M tools/cfg_error_tests/dcre_aid_mpf_in_class.cfg   E_RSATR "$X"; echo "3:$?"
$R $M tools/cfg_error_tests/dcre_aid_dtq_no_static.cfg  E_OBJ   "$X"; echo "4:$?"
$R $M tools/cfg_error_tests/dcre_aid_pdq_no_static.cfg  E_OBJ   "$X"; echo "5:$?"
$R $M tools/cfg_error_tests/dcre_aid_mpf_no_static.cfg  E_OBJ   "$X"; echo "6:$?"
```
期待: 6件とも rc=0（両エンジンが同じ ercd を同じ文言で検出）。
- **rc=2** が出たら**前提未充足**＝第4引数を渡し忘れているか、cfg が別のエラーで先に落ちている。
- **no-static の3件で E_OBJ が出ない**（＝素通りする）なら、どこかに隠れた静的
  `CRE_DTQ`/`CRE_PDQ`/`CRE_MPF` がある。そのときは
  `dcre_aid_sem_no_static.cfg` と同じく `INCLUDE("syssvc/syslog.cfg"); INCLUDE("syssvc/banner.cfg");`
  へ切り替え、**切り替えた事実と発見した静的インスタンスの所在を記録する**。

- [ ] **Step 13: 台帳とコミット**

`DIVERGENCE_MAP.md` に次を追記する：
- `kernel/kernel_api.def`（既存行の理由欄に「段階3b Task 2 で `AID_DTQ`/`AID_PDQ`/`AID_MPF` の
  3行を追加」を追記）
- `include/kernel.h`（既存行の理由欄に「段階3b Task 2 で `T_CDTQ`/`T_CPDQ`/`T_CMPF` と
  `acre_dtq`/`del_dtq`/`acre_pdq`/`del_pdq`/`acre_mpf`/`del_mpf` の6宣言を追加。
  dcre `include/kernel.h:246-252,258-266,285-291` とバイト一致（タブ幅を含む）。
  返値型のみ dcre の `ER_UINT` から `ER_ID` へ揃えた（段階1/2/3a と同じ意図的逸脱、
  `int_t` の別名で実体は同じ）」を追記）
- `kernel/kernel_impl.h`（既存行の理由欄に「段階3b Task 2 で `TA_MBALLOC`
  （`UINT_C(0x4000)`・`#ifndef` ガード付き）を `TA_MEMALLOC` の直後に追加。
  dcre の同名定義と同一。cfg は本マクロを出力しないため生成物は不変」を追記。
  既存行が無ければ新設し、種別 `mod (dcre-port)`）
- `test/test_dcre_mix.cfg`（既存行の理由欄に「段階3b Task 2 で静的
  `CRE_DTQ`/`CRE_PDQ`/`CRE_MPF` と `AID_DTQ(2)`/`AID_PDQ(1)`/`AID_MPF(1)` を追記。
  `AID_FLG`/`AID_ALM` は意図的に書かないまま。`sample/sample1.cfg` は静的
  `CRE_PDQ`/`CRE_MPF` を持たないため本 cfg が段階3b の positive control 兼
  実コンパイル検査の媒体になる」を追記）

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
git add -A && git commit -m "feat(cfg): AID_DTQ/AID_PDQ/AID_MPF と T_CDTQ/T_CPDQ/T_CMPF・TA_MBALLOC を両エンジンへ追加（dcre段階3b）"
```

---
### Task 3: dataqueue 層 — free_dtqcb・initialize・2レンジ DTQID・acre_dtq/del_dtq・E_NOEXS ×9

**推奨モデル:** 中位（sonnet）

**Files:**
- Modify: `kernel/dataqueue.h`（externs / `tnum_dtq`・`tnum_sdtq` 移設 / `DTQID` 置換）
- Modify: `kernel/dataqueue.c`（`tnum_dtq` 重複削除 / `free_dtqcb` / `initialize_dataqueue` /
  `acre_dtq` / `del_dtq` / E_NOEXS ×9）
- Modify: `kernel/allfunc.h`（`/* dataqueue.c */` 節に2行）
- Modify: `kernel/Makefile.kernel`（`dataqueue =` 行に .o 2個）
- Modify: `kernel/kernel_rename.def` →（再生成）`kernel_rename.h` `kernel_unrename.h`
- Modify: `DIVERGENCE_MAP.md`

**Interfaces:**
- Consumes: Task 2 の `_kernel_tmax_sdtqid` / `_kernel_adtqinib_table` / 予約 DTQCB /
  `T_CDTQ` / `TA_MBALLOC` / `acre_dtq`・`del_dtq` の宣言。
  Task 1 の確認結果表（訂正B/E/F、dcre 転写元行範囲、del の順序制約）。
- Produces: `QUEUE free_dtqcb` / `tmax_sdtqid` / `tnum_sdtq` / `adtqinib_table[]` /
  2レンジ `DTQID(p_dtqcb)` / `ER_ID acre_dtq(const T_CDTQ *)` / `ER del_dtq(ID)`。Task 6 が使う。

**AID 無し構成の挙動は不変である**（`tnum_sdtq == tnum_dtq` となり動的スロットのループは
空振りし、`DTQID` は既存式に落ち、既存 API は `dtqatr != TA_NOEXS` なので E_NOEXS 分岐を通らない）。
★ただし **`fsnd_dtq` だけは AID 無し構成でも挙動が変わる**：ロック前の `CHECK_ILUSE` を
ロック内へ移すため、`E_ILUSE` を返すタイミングが「`lock_cpu` 前」から「`lock_cpu` 後」になる。
返値は同じ `E_ILUSE` であり、`LOG_FSND_DTQ_LEAVE` も同じく通る。**意味論は不変**。

- [ ] **Step 1: `kernel/dataqueue.h` — extern 追加・`tnum_*` 移設・`DTQID` の2レンジ置換**

`extern const ID tmax_dtqid;` のブロック（`dataqueue.h:90-93`）を次に置換：

```c
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
```

`extern const DTQINIB dtqinib_table[];` のブロック（`dataqueue.h:95-98`）の**直後**に追加：

```c
/*
 *  動的生成データキューの初期化ブロックのエリア（kernel_cfg.c・RAM）
 */
extern DTQINIB	adtqinib_table[];
```

`extern DTQCB *const p_dtqcb_table[];`（`dataqueue.h:103`）の**直後**、
既存の `DTQID` マクロブロック（`dataqueue.h:105-109`）を次で**置換**する
（★Task 1 Step 2 の記録に従う。**新設ではなく置換**である）：

```c
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
```

★`#include <queue.h>` は `dataqueue.h:51` に**既にある**ので追加不要（Task 1 Step 2 で確認済み）。

- [ ] **Step 2: `kernel/dataqueue.c` — `tnum_dtq` の重複削除と `initialize_dataqueue` の改造**

`dataqueue.c:128-131` の

```c
/*
 *  データキューの数
 */
#define tnum_dtq	((uint_t)(tmax_dtqid - TMIN_DTQID + 1))
```

を**削除**する（Step 1 で `dataqueue.h` に移した）。
`INDEX_DTQ`/`get_dtqcb`（`:133-137`）は**そのまま残す**。

`#ifdef TOPPERS_dtqini` ブロック（`dataqueue.c:142-163`）を次で置換する：

```c
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
```

（**FIFO**＝`queue_insert_prev` で末尾へ。Constraint 7 のとおり LIFO 化しない。
`i` は静的ループから引き継ぐ＝dcre `dataqueue.c:173,183` と同じ書き方。
`count`/`head`/`tail`/`p_dtqmb` は動的スロットでは設定しない — `acre_dtq` が設定する。dcre も同じ。）

- [ ] **Step 3: `kernel/dataqueue.c` に `acre_dtq` を追加**（`TOPPERS_dtqrcv` 区画の直後、
  `TOPPERS_snd_dtq` 区画の直前。dcre の配置と同じ）

```c
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
```

dcre（`dataqueue.c:339-397`）からの適応点は**2つだけ**：
1. `lock_cpu()` の直後に `acquire_glock()`／末尾に `release_glock()`（FMP3 の giant lock 規約）。
2. 空判定を `tnum_dtq == 0` → `tnum_dtq == tnum_sdtq`（FMP3 は静的分が別レンジのため）。

**★書いてはいけないもの**（Constraint 4）：`p_dtqinib->iprcid = ...` / `->affinity = ...` /
`p_dtqcb->p_pcb = ...`。DTQINIB にも DTQCB にもこれらのフィールドは存在しない。

**★書き足してはいけないもの**（訂正B）：`CHECK_PAR(dtqcnt <= ...)` のような
dtqcnt の範囲検査。dcre には無い。`p_dtqmb != NULL` を E_NOSPT で弾く分岐も**書かない**
（それは cfg 側の静的生成だけの制約である）。

★`dtqatr |= TA_MBALLOC;` を `malloc_mpk` の**成否判定より前**に書いているのは dcre と同じ。
確保に失敗した場合 `dtqatr` は INIB に書き込まれないまま E_NOMEM で戻るので実害は無い
（この理由をコード中のコメントに書かないこと — dcre 原文にも無い。台帳に1文書けばよい）。

- [ ] **Step 4: `kernel/dataqueue.c` に `del_dtq` を追加**（`acre_dtq` の直後）

```c
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
```

★**順序が重要（2箇所）**：
1. `init_wait_queue` で**両方**の待ちキューを空にした**後**に
   `queue_insert_prev(&free_dtqcb, &(p_dtqcb->swait_queue))` で `swait_queue` を
   free-list へ繋ぐ。逆順にすると free-list のリンクを `init_wait_queue` が壊す。
2. **属性の読み（`dtqatr & TA_MBALLOC`）と `free_mpk` は `dtqatr = TA_NOEXS;` の前**。
   根拠は上のコメントのとおり（TA_NOEXS は全ビット 1）。dcre も同じ順序。

dcre（`dataqueue.c:402-444`）からの適応点は**4つ**：(1) glock の対化、
(2) ★訂正C（段階3a 継承）＝`CHECK_TSKCTX_UNL()` + `p_runtsk` を
`CHECK_TSKCTX_UNL_MYSTATE(&p_selftsk)` + `p_selftsk != p_my_pcb->p_schedtsk` に、
(3) `init_wait_queue` に `p_my_pcb` 引数が付く、
(4) ★訂正F＝dcre `dataqueue.c:413` の **`CHECK_PAR(VALID_DTQID(dtqid))`（E_PAR）を
`CHECK_ID(VALID_DTQID(dtqid))`（E_ID）に直す**。dcre 自身の不整合であり
（`del_pdq`/`del_mpf` は `CHECK_ID`）、FMP3 の dtq 系サービスコールは全て `CHECK_ID` である。

- [ ] **Step 5: E_NOEXS 検査の挿入（dataqueue.c の9関数）**

段階1/2/3a と同じ **existence-before-state** 規約：`acquire_glock()`（および直後の
`p_my_pcb = get_my_pcb();` / `p_selftsk = p_my_pcb->p_runtsk;` があればその後）の**直後・
既存の状態判定の最初の分岐**として挿入し、既存本体は `else if` / `else` 連鎖へ
**字下げのみ**で繰り込む（★既存ロジックの**バイト保存**。式や順序を変えない）。

判定式はすべて `p_dtqcb->p_dtqinib->dtqatr == TA_NOEXS`。

| 関数 | 定義行 | 既存本体の形 | 挿入後 |
|---|---|---|---|
| `snd_dtq` | `:313` | `if (raster) … else if (send_data…) … else {…wobj_make_wait…}` | 先頭に `if (TA_NOEXS)`、既存 `if` を `else if` に |
| `psnd_dtq` | `:367` | `if (send_data…) … else { E_TMOUT }` | 同上 |
| `tsnd_dtq` | `:417` | `if (raster) … else if (send_data…) … else if (tmout==TMO_POL) … else {…}` | 同上 |
| **`fsnd_dtq`** | `:474` | **ロック前 `CHECK_ILUSE`**＋ロック内は単文列 | **★構造変更（訂正E）。下記参照** |
| `rcv_dtq` | `:521` | `if (raster) … else if (receive_data…) … else {…make_wait…}` | 先頭に `if (TA_NOEXS)`、既存 `if` を `else if` に |
| `prcv_dtq` | `:578` | `if (receive_data…) … else { E_TMOUT }` | 同上 |
| `trcv_dtq` | `:622` | `if (raster) … else if … else if (tmout==TMO_POL) … else {…}` | 同上 |
| `ini_dtq` | `:683` | 単文列（`init_wait_queue`×2／count/head/tail／dispatch 判定） | `else { … }` で丸ごと包む |
| `ref_dtq` | `:727` | 単文列（`wait_tskid`×2／`sdtqcnt`／`ercd = E_OK`） | `else { … }` で丸ごと包む |

★具体例1（`psnd_dtq`。`acquire_glock(); p_my_pcb = get_my_pcb();` の直後を置換）：

```c
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
```
（＝既存の `if` を `else if` に変えて先頭に E_NOEXS 分岐を足しただけ。**本体は無改変**。）

★具体例2（`ref_dtq`。既存本体は `if` 連鎖でないため `else { … }` で丸ごと包む）：

```c
	if (p_dtqcb->p_dtqinib->dtqatr == TA_NOEXS) {
		ercd = E_NOEXS;
	}
	else {
		pk_rdtq->stskid = wait_tskid(&(p_dtqcb->swait_queue));
		pk_rdtq->rtskid = wait_tskid(&(p_dtqcb->rwait_queue));
		pk_rdtq->sdtqcnt = p_dtqcb->count;
		ercd = E_OK;
	}
```

★★具体例3（**`fsnd_dtq` — 唯一の構造変更**。訂正E）：

**変更前**（現行 `dataqueue.c:481-494` 付近）：
```c
	CHECK_UNL_MYSTATE(&p_selftsk, &context);
	CHECK_ID(VALID_DTQID(dtqid));
	p_dtqcb = get_dtqcb(dtqid);
	CHECK_ILUSE(p_dtqcb->p_dtqinib->dtqcnt > 0U);

	lock_cpu();
	acquire_glock();
	p_my_pcb = get_my_pcb();
	force_send_data(p_my_pcb, p_dtqcb, data);
	if (p_selftsk != p_my_pcb->p_schedtsk) {
		...
	}
	ercd = E_OK;
	release_glock();
```

**変更後**（dcre `dataqueue.c:596-620` と同じ構造）：
```c
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
```
★`CHECK_ILUSE` の行を**削除**すること（残すと二重検査になり、しかも危険な方が残る）。
★`error_exit:` ラベルは `CHECK_UNL_MYSTATE`/`CHECK_ID` がまだ使うので**残す**。
コンパイラが「未使用ラベル」を言わないことを確認する。

**除外**: `initialize_dataqueue`、`enqueue_data`/`force_enqueue_data`/`dequeue_data`/
`send_data`/`force_send_data`/`receive_data`（`TOPPERS_dtqenq` 他。ID を取らない内部関数）、
`acre_dtq`（ID を取らない）、`del_dtq`（自前で E_NOEXS を持つ）。
→ **dataqueue.c で E_NOEXS を挿入するのは上表の9関数だけ。FMP3 固有関数は無い**
（段階2 の `msta_cyc`/`msta_alm` のような「上流に先例が無い類推適用」は本 Task では発生しない）。

- [ ] **Step 6: 配線（allfunc.h / Makefile.kernel）**

`kernel/allfunc.h` の `/* dataqueue.c */` 節（`:158-174`）の `#define TOPPERS_dtqrcv`（`:165`）の
直後に2行：
```c
#define TOPPERS_acre_dtq
#define TOPPERS_del_dtq
```
（ALLFUNC ビルド＝`CMakeLists.txt:562` で全区画を有効化するため、**ここに書かないと
関数が1つもコンパイルされない**。）

`kernel/Makefile.kernel:92-94` を次に変更（上流形式の維持。CMake は参照しない）：
```
dataqueue = dtqini.o dtqenq.o dtqfenq.o dtqdeq.o dtqsnd.o dtqfsnd.o dtqrcv.o \
		acre_dtq.o del_dtq.o \
		snd_dtq.o psnd_dtq.o tsnd_dtq.o fsnd_dtq.o \
		rcv_dtq.o prcv_dtq.o trcv_dtq.o ini_dtq.o ref_dtq.o
```
**`KERNEL_FCSRCS`（`Makefile.kernel:51-56`）は触らない**（22個のまま）。

- [ ] **Step 7: rename 追加・再生成**

`kernel/kernel_rename.def` の `# dataqueue.c` 節（`:77-84`。★段階3a で他の節が伸びているので
**行番号ではなく `# dataqueue.c` の文字列を目印にする**）を次に置換する：

```
# dataqueue.c
initialize_dataqueue
enqueue_data
force_enqueue_data
dequeue_data
send_data
force_send_data
receive_data
free_dtqcb
tmax_sdtqid
adtqinib_table

```
（`acre_dtq`/`del_dtq` は**公開名なのでリネームしない**＝段階2/3a の `acre_cyc`/`acre_sem` と同じ。
`tmax_sdtqid`/`adtqinib_table` は cfg が `_kernel_tmax_sdtqid`/`_kernel_adtqinib_table` として
出力する＝`kernel.py:187,272` の `_kernel_tmax_s{obj}id` / `_kernel_{array}`。）

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core/kernel && ruby ../utils/genrename.rb kernel && cd ..
cd /home/honda/TOPPERS/FMP3/fmp3_core
git diff --stat kernel/kernel_rename.h kernel/kernel_unrename.h   # 期待: 各 +3 行・削除0
grep -n "free_dtqcb\|tmax_sdtqid\|adtqinib_table" kernel/kernel_rename.h kernel/kernel_unrename.h
```
期待: `_kernel_free_dtqcb` / `_kernel_tmax_sdtqid` / `_kernel_adtqinib_table` が実在。
**削除行が1行でもあれば不合格**（genrename の実行位置ミス等を疑う）。

- [ ] **Step 8: 全8構成ビルド + 等価性 + 混在サンプル**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
for p in musca_b1 musca_b1-2core rp2350_pico2 polarfire_soc_kit-qemu \
         kria_arm64 kria_arm64-1core kria_r5 kria_r5-2core; do
  cmake --build build/$p > /tmp/dcre4-t3-build-$p.log 2>&1; echo "$p build rc=$?"
  tools/cfg_equivalence.sh build/$p > /tmp/dcre4-t3-eq-$p.log 2>&1; echo "$p eq rc=$?"
done
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
for d in musca_b1-2core-tmix kria_arm64-tmix; do
  cmake --build build/$d > /tmp/dcre4-t3-build-$d.log 2>&1; echo "$d build rc=$?"
  tools/cfg_equivalence.sh build/$d > /tmp/dcre4-t3-eq-$d.log 2>&1; echo "$d eq rc=$?"
done
```
期待: 全て rc=0。**exit=2 は不合格**。
`*-tmix` は `AID_DTQ(2)` を含む実構成なので、
ここで `acre_dtq`/`del_dtq` が**実際にリンクされる**ことも同時に検査している。
★`build/musca_b1-2core` は `sample1`（静的 `CRE_DTQ`×6）を使うので、
`initialize_dataqueue` の静的ループ境界変更と E_NOEXS 挿入9箇所が
**AID 無し構成の静的データキューを壊していない**ことの検査になっている。

- [ ] **Step 9: QEMU 起動（非退行）— musca_b1-2core と kria_arm64**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 25 cmake --build build/musca_b1-2core --target run > /tmp/dcre4-t3-run-musca.log 2>&1
grep -c 'Processor [12] start\.' /tmp/dcre4-t3-run-musca.log     # 期待: 2
grep -c 'Sample program starts' /tmp/dcre4-t3-run-musca.log      # 参考: サンプル走行の証拠
pgrep -a qemu                                                    # 期待: 何も出ない
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 25 cmake --build build/kria_arm64 --target run > /tmp/dcre4-t3-run-arm64.log 2>&1
grep -c 'Processor [1-4] start\.' /tmp/dcre4-t3-run-arm64.log    # 期待: 4
pgrep -a qemu                                                    # 期待: 何も出ない
```
★`sample1` は**静的データキューを実際に使って通信する**（`SERVER_DTQ1..6`）ので、
これは「E_NOEXS 挿入と 2レンジ `DTQID` が静的データキューの動作を壊していない」ことの
最も直接的な実証である。kria_arm64 は 64bit ＝ ポインタ差分による2レンジ `DTQID` が
最も壊れやすい構成なので、必ず両方走らせる。
`pgrep` が qemu を出したら `pkill -f qemu-system` で掃除してから次へ進む。

- [ ] **Step 10: 台帳とコミット**

`DIVERGENCE_MAP.md` に次の行を追記する（種別はすべて `mod (dcre-port)`）：
- `kernel/dataqueue.h（dcre段階3b Task 3）` — 理由：「`tmax_sdtqid`／`QUEUE free_dtqcb`／
  `adtqinib_table[]` の extern を追加。`tnum_dtq` を `dataqueue.c` から移設し `tnum_sdtq` を新設。
  既存の `DTQID(p_dtqcb)` マクロを段階2 の `CYCID`・段階3a の `SEMID` と同型の
  **2レンジ版へ置換**（新設ではない）。AID 無し構成では `tnum_dtq == tnum_sdtq` となり
  従来と同一の式に落ちる＝挙動不変。`#include <queue.h>` は元から `:51` にあるため追加不要」
- `kernel/dataqueue.c（dcre段階3b Task 3）` — 理由：「`#define tnum_dtq` を削除（`.h` へ移設）。
  `QUEUE free_dtqcb;` の定義を追加。`initialize_dataqueue` の静的ループ境界を
  `tnum_dtq` → `tnum_sdtq` に変更し、**既存のマスタ限定ブロックの中に**動的スロット初期化
  （`queue_initialize(&free_dtqcb)`、`adtqinib_table[j].dtqatr = TA_NOEXS`、
  `queue_insert_prev` で FIFO 挿入）を追加。**データキューは非親和オブジェクトのため
  `iprcid`/`affinity`/`p_pcb` の充填やプロセッサ判定は一切行っていない**。
  `acre_dtq`/`del_dtq` を追加（dcre `dataqueue.c:339-397`/`:402-444` の転写）。
  dcre からの意図的な逸脱4件：(1) glock の対化、(2) 空判定 `tnum_dtq == 0` →
  `tnum_dtq == tnum_sdtq`、(3) `del_dtq` の `CHECK_TSKCTX_UNL()`＋`p_runtsk` を
  `CHECK_TSKCTX_UNL_MYSTATE(&p_selftsk)`＋`p_selftsk != p_my_pcb->p_schedtsk` へ、
  (4) ★dcre `dataqueue.c:413` の `CHECK_PAR(VALID_DTQID(dtqid))`（E_PAR）を
  `CHECK_ID(VALID_DTQID(dtqid))`（E_ID）へ — dcre 自身の不整合（`del_pdq`/`del_mpf` は
  `CHECK_ID`、FMP3 の dtq 系サービスコールも全て `CHECK_ID`）。段階3a 訂正D（`del_flg`）の
  同型2件目であり、上流報告候補 d を拡張する。
  ★`del_dtq` は **`TA_MBALLOC` のビット検査と `free_mpk` を `dtqatr = TA_NOEXS` の書込みより
  前**に置く順序制約がある（`TA_NOEXS` は `((ATR)(-1))`＝全ビット 1 なので、
  後に置くと `(dtqatr & TA_MBALLOC) != 0U` が必ず真になり、ユーザ供給の管理領域まで
  `free_mpk` に渡してしまう）。dcre `dataqueue.c:427-430` も同じ順序。
  ★`acre_dtq` はユーザ供給の `dtqmb` を**受理**する（`TA_MBALLOC` を立てない）。
  cfg 側の静的生成が `dtqmb != NULL` を E_NOSPT で弾くのとは非対称だが dcre 忠実である。
  `dtqcnt` の範囲検査は dcre に無いので**書いていない**。
  `snd_dtq`/`psnd_dtq`/`tsnd_dtq`/`fsnd_dtq`/`rcv_dtq`/`prcv_dtq`/`trcv_dtq`/`ini_dtq`/`ref_dtq`
  の9関数に existence-before-state 規約の E_NOEXS 分岐を挿入。うち8関数は既存ロジックを
  字下げのみで byte-preserve したが、**`fsnd_dtq` だけは dcre に倣って構造変更した**：
  ロック取得前の `CHECK_ILUSE(p_dtqcb->p_dtqinib->dtqcnt > 0U)` を削除し、ロック内の
  E_NOEXS の次の分岐 `else if (!(p_dtqcb->p_dtqinib->dtqcnt > 0U)) { ercd = E_ILUSE; }` へ移した
  （dcre `dataqueue.c:604-606`）。理由は E_NOEXS ゲートより前に削除済みスロットの INIB を
  読まないため。返値・意味論は不変」
- `kernel/allfunc.h（dcre段階3b Task 3）`（既存行の理由欄に「`/* dataqueue.c */` 節に
  `TOPPERS_acre_dtq`/`TOPPERS_del_dtq` を追加」を追記）
- `kernel/Makefile.kernel（dcre段階3b Task 3）`（同、「`dataqueue =` 行に
  `acre_dtq.o`/`del_dtq.o` を追加。`KERNEL_FCSRCS` は不変」）
- `kernel/kernel_rename.def（dcre段階3b Task 3）` — 理由：「`# dataqueue.c` 節に
  `free_dtqcb`/`tmax_sdtqid`/`adtqinib_table` の3エントリを追加」
- `kernel/kernel_rename.h・kernel_unrename.h（dcre段階3b Task 3）` — 種別
  `mod (dcre-port, 生成物)`、理由：「`utils/genrename.rb kernel` で再生成（手編集ではない）。
  各+3行・削除0行」

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
git add -A && git commit -m "feat(kernel): acre_dtq/del_dtq（管理領域のmalloc_mpkとTA_MBALLOC）・free_dtqcb・2レンジDTQID とE_NOEXS検査9箇所（dcre段階3b）"
```

---
### Task 4: pridataq 層 — VALID_DPRI・free_pdqcb・initialize・2レンジ PDQID・acre_pdq/del_pdq・E_NOEXS ×8

**推奨モデル:** 中位（sonnet）

**Files:**
- Modify: `kernel/check.h`（★訂正C：`VALID_DPRI` を新設）
- Modify: `kernel/pridataq.h`（externs / `tnum_pdq`・`tnum_spdq` 移設 / `PDQID` 置換）
- Modify: `kernel/pridataq.c`（`tnum_pdq` 重複削除 / `free_pdqcb` / `initialize_pridataq` /
  `acre_pdq` / `del_pdq` / E_NOEXS ×8）
- Modify: `kernel/allfunc.h`（`/* pridataq.c */` 節に2行）
- Modify: `kernel/Makefile.kernel`（`pridataq =` 行に .o 2個）
- Modify: `kernel/kernel_rename.def` →（再生成）`kernel_rename.h` `kernel_unrename.h`
- Modify: `DIVERGENCE_MAP.md`

**Interfaces:**
- Consumes: Task 2 の `_kernel_tmax_spdqid` / `_kernel_apdqinib_table` / 予約 PDQCB /
  `T_CPDQ` / `TA_MBALLOC` / `acre_pdq`・`del_pdq` の宣言。
  Task 1 の確認結果表（訂正C/E、dcre 転写元行範囲、del の順序制約）。
- Produces: `VALID_DPRI` / `QUEUE free_pdqcb` / `tmax_spdqid` / `tnum_spdq` / `apdqinib_table[]` /
  2レンジ `PDQID(p_pdqcb)` / `ER_ID acre_pdq(const T_CPDQ *)` / `ER del_pdq(ID)`。Task 6 が使う。

**★本 Task は Task 3 と同型だが、「dtq と同様に」で済ませない。** pdq 固有の相違が4点ある：
(a) `acre_pdq` には `CHECK_PAR(VALID_DPRI(maxdpri))` があり、**`VALID_DPRI` は FMP3 に存在しない**
（訂正C）、(b) E_NOEXS の対象は**8関数**（dtq にある `fsnd_dtq` に相当する強制送信が pdq には無い）、
(c) **構造変更が3関数**（`snd_pdq`/`psnd_pdq`/`tsnd_pdq` の `datapri <= maxdpri` 検査を
ロック内へ移す＝訂正E）、(d) PDQCB は `count`/`p_head`/`unused`/`p_freelist` の4つを初期化する。

**AID 無し構成の挙動は不変である**（`tnum_spdq == tnum_pdq`）。
★ただし `snd_pdq`/`psnd_pdq`/`tsnd_pdq` は AID 無し構成でも
「`datapri > maxdpri` の E_PAR を返す位置がロック前からロック内へ移る」。返値は同じ。

- [ ] **Step 1: ★訂正C — `kernel/check.h` に `VALID_DPRI` を追加**

`kernel/check.h` の `VALID_TPRI`（`:70`）の**直後**、`/* 相対時間の範囲の判定 */` の**手前**に：

```c
/*
 *  データ優先度の範囲の判定
 */
#define VALID_DPRI(dpri)	(TMIN_DPRI <= (dpri) && (dpri) <= TMAX_DPRI)
```

（dcre `check.h:71` と同一。`TMIN_DPRI`=1 / `TMAX_DPRI`=16 は `include/kernel.h:624-625` に既存。
FMP3 の pristine には `VALID_DPRI` が無く、`snd_pdq` 等は `TMIN_DPRI <= datapri && datapri <= maxdpri`
と直書きしている。**既存の直書きは書き換えない** — 新設マクロは `acre_pdq` からのみ使う。）

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
grep -n "define VALID_DPRI" kernel/check.h \
     /home/honda/TOPPERS/ASP3/asp3_3.7/extension/dcre/kernel/check.h
```
期待: 2行出て、`#define` 以降が**同一の文字列**であること。

- [ ] **Step 2: `kernel/pridataq.h` — extern 追加・`tnum_*` 移設・`PDQID` の2レンジ置換**

`extern const ID tmax_pdqid;` のブロック（`pridataq.h:96-99`）を次に置換：

```c
/*
 *  優先度データキューIDの最大値（kernel_cfg.c）
 */
extern const ID	tmax_pdqid;
extern const ID	tmax_spdqid;		/* 静的生成優先度データキューのID番号の最大値 */

/*
 *  使用していない優先度データキュー管理ブロックのリスト（pridataq.c）
 *
 *  PDQCBの先頭フィールドがQUEUE（swait_queue）なので，そのまま
 *  free-listのリンクに流用する（dcre pridataq.c:177-183 と同一）．
 *  段階2のcyc/almで用いたtmevtb領域のオーバレイは不要である．
 */
extern QUEUE	free_pdqcb;
```

`extern const PDQINIB pdqinib_table[];` のブロック（`pridataq.h:101-104`）の**直後**に追加：

```c
/*
 *  動的生成優先度データキューの初期化ブロックのエリア（kernel_cfg.c・RAM）
 */
extern PDQINIB	apdqinib_table[];
```

`extern PDQCB *const p_pdqcb_table[];`（`pridataq.h:109`）の**直後**、
既存の `PDQID` マクロブロック（`pridataq.h:111-116`）を次で**置換**する：

```c
/*
 *  優先度データキューの数
 *
 *  PDQIDマクロから参照するためpridataq.cから移設した．
 */
#define tnum_pdq	((uint_t)(tmax_pdqid - TMIN_PDQID + 1))
#define tnum_spdq	((uint_t)(tmax_spdqid - TMIN_PDQID + 1))

/*
 *  優先度データキュー管理ブロックから優先度データキューIDを取り出すた
 *  めのマクロ
 *
 *  FMP3のPDQCBはポインタ表（p_pdqcb_table）経由で参照される個別の
 *  named staticであり，PDQCB自身の配列位置から番号を引けない．元から
 *  PDQINIBへのポインタ差分で求めていた式を，動的生成優先度データキュー
 *  （p_pdqinibがapdqinib_tableを指す）と静的生成優先度データキュー
 *  （p_pdqinibがpdqinib_tableを指す）の2レンジに拡張する
 *  （段階2のCYCID・段階3aのSEMIDと同型）．AID_PDQが無い構成では
 *  tnum_pdq == tnum_spdqとなり第1項が常に偽＝従来と同一の式に落ちる．
 */
#define	PDQID(p_pdqcb) \
	((((p_pdqcb)->p_pdqinib >= apdqinib_table) \
		&& ((p_pdqcb)->p_pdqinib < &apdqinib_table[tnum_pdq - tnum_spdq])) \
	  ? ((ID)(((p_pdqcb)->p_pdqinib - apdqinib_table) + TMIN_PDQID + tnum_spdq)) \
	  : ((ID)(((p_pdqcb)->p_pdqinib - pdqinib_table) + TMIN_PDQID)))
```

★`#include <queue.h>` は `pridataq.h:51` に**既にある**ので追加不要。

- [ ] **Step 3: `kernel/pridataq.c` — `tnum_pdq` の重複削除と `initialize_pridataq` の改造**

`pridataq.c:120-123` の

```c
/*
 *  優先度データキューの数
 */
#define tnum_pdq	((uint_t)(tmax_pdqid - TMIN_PDQID + 1))
```

を**削除**する（Step 2 で `pridataq.h` に移した）。
`INDEX_PDQ`/`get_pdqcb`（`:125-130`）は**そのまま残す**。

`#ifdef TOPPERS_pdqini` ブロック（`pridataq.c:135-158`）を次で置換する：

```c
#ifdef TOPPERS_pdqini

/*
 *  使用していない優先度データキュー管理ブロックのリスト
 *
 *  PDQCBの先頭フィールドがQUEUE（swait_queue）なので，そのまま
 *  free-listのリンクに流用する．acre_pdqは取り出した直後に
 *  queue_initializeで作り直すため，TA_TPRI（優先度順待ちキュー）と
 *  干渉しない．
 */
QUEUE	free_pdqcb;

void
initialize_pridataq(PCB *p_my_pcb)
{
	uint_t	i, j;
	PDQCB	*p_pdqcb;
	PDQINIB	*p_pdqinib;

	if (p_my_pcb->prcid == TOPPERS_MASTER_PRCID) {
		for (i = 0; i < tnum_spdq; i++) {
			p_pdqcb = p_pdqcb_table[i];
			queue_initialize(&(p_pdqcb->swait_queue));
			p_pdqcb->p_pdqinib = &(pdqinib_table[i]);
			queue_initialize(&(p_pdqcb->rwait_queue));
			p_pdqcb->count = 0U;
			p_pdqcb->p_head = NULL;
			p_pdqcb->unused = 0U;
			p_pdqcb->p_freelist = NULL;
		}

		/*
		 *  動的生成用スロットの初期化
		 *
		 *  優先度データキューはプロセッサ親和を持たない（PDQINIBに
		 *  iprcid/affinityが無く，PDQCBにp_pcbが無い）ため，段階2の
		 *  cyc/almのようなプロセッサ判定や充填は一切不要である．本関数は
		 *  元からマスタプロセッサ限定なので，そのブロックの中で続けて
		 *  初期化する．他プロセッサへの可視性は，本関数の呼出し後の
		 *  barrier_syncが保証する（段階1のfree_tcbと同じ論証）．
		 */
		queue_initialize(&free_pdqcb);
		for (j = 0; i < tnum_pdq; i++, j++) {
			p_pdqcb = p_pdqcb_table[i];
			p_pdqinib = &(apdqinib_table[j]);
			p_pdqinib->pdqatr = TA_NOEXS;
			p_pdqcb->p_pdqinib = ((const PDQINIB *) p_pdqinib);
			queue_insert_prev(&free_pdqcb, &(p_pdqcb->swait_queue));
		}
	}
}

#endif /* TOPPERS_pdqini */
```

（**FIFO**＝`queue_insert_prev` で末尾へ。`i` は静的ループから引き継ぐ＝dcre `pridataq.c:166,177`。
`count`/`p_head`/`unused`/`p_freelist`/`p_pdqmb` は動的スロットでは設定しない — `acre_pdq` が設定する。）

- [ ] **Step 4: `kernel/pridataq.c` に `acre_pdq` を追加**（`TOPPERS_pdqrcv` 区画の直後、
  `TOPPERS_snd_pdq` 区画の直前。dcre の配置と同じ）

```c
/*
 *  優先度データキューの生成
 */
#ifdef TOPPERS_acre_pdq

#ifndef LOG_ACRE_PDQ_ENTER
#define LOG_ACRE_PDQ_ENTER(pk_cpdq)
#endif /* LOG_ACRE_PDQ_ENTER */

#ifndef LOG_ACRE_PDQ_LEAVE
#define LOG_ACRE_PDQ_LEAVE(ercd)
#endif /* LOG_ACRE_PDQ_LEAVE */

ER_ID
acre_pdq(const T_CPDQ *pk_cpdq)
{
	PDQCB	*p_pdqcb;
	PDQINIB	*p_pdqinib;
	ATR		pdqatr;
	uint_t	pdqcnt;
	PRI		maxdpri;
	PDQMB	*p_pdqmb;
	ER		ercd;

	LOG_ACRE_PDQ_ENTER(pk_cpdq);
	CHECK_TSKCTX_UNL();

	pdqatr = pk_cpdq->pdqatr;
	pdqcnt = pk_cpdq->pdqcnt;
	maxdpri = pk_cpdq->maxdpri;
	p_pdqmb = pk_cpdq->pdqmb;

	CHECK_VALIDATR(pdqatr, TA_TPRI);
	CHECK_PAR(VALID_DPRI(maxdpri));
	if (p_pdqmb != NULL) {
		CHECK_PAR(MB_ALIGN(p_pdqmb));
	}

	lock_cpu();
	acquire_glock();
	if (tnum_pdq == tnum_spdq || queue_empty(&free_pdqcb)) {
		ercd = E_NOID;
	}
	else {
		/*
		 *  管理領域の確保
		 *
		 *  pdqcntが0の優先度データキューは管理領域を必要としない．
		 *  ユーザがpdqmbを与えた場合はそれを使い，TA_MBALLOCを立てない
		 *  （del_pdqがfree_mpkしてはならないため）．
		 *
		 *  ★E_NOMEMのときfree-listからCBを取り出していないことが重要
		 *  である（確保に成功してから初めてqueue_delete_nextする）．
		 *  段階1のacre_tskと同じ順序．
		 */
		if (pdqcnt != 0 && p_pdqmb == NULL) {
			p_pdqmb = malloc_mpk(sizeof(PDQMB) * pdqcnt);
			pdqatr |= TA_MBALLOC;
		}
		if (pdqcnt != 0 && p_pdqmb == NULL) {
			ercd = E_NOMEM;
		}
		else {
			p_pdqcb = ((PDQCB *) queue_delete_next(&free_pdqcb));
			p_pdqinib = (PDQINIB *)(p_pdqcb->p_pdqinib);
			p_pdqinib->pdqatr = pdqatr;
			p_pdqinib->pdqcnt = pdqcnt;
			p_pdqinib->maxdpri = maxdpri;
			p_pdqinib->p_pdqmb = p_pdqmb;

			queue_initialize(&(p_pdqcb->swait_queue));
			queue_initialize(&(p_pdqcb->rwait_queue));
			p_pdqcb->count = 0U;
			p_pdqcb->p_head = NULL;
			p_pdqcb->unused = 0U;
			p_pdqcb->p_freelist = NULL;
			ercd = PDQID(p_pdqcb);
		}
	}
	release_glock();
	unlock_cpu();

  error_exit:
	LOG_ACRE_PDQ_LEAVE(ercd);
	return(ercd);
}

#endif /* TOPPERS_acre_pdq */
```

dcre（`pridataq.c:316-379`）からの適応点は**2つだけ**：
1. `lock_cpu()` の直後に `acquire_glock()`／末尾に `release_glock()`。
2. 空判定を `tnum_pdq == 0` → `tnum_pdq == tnum_spdq`。

**★書いてはいけないもの**（Constraint 4）：`p_pdqinib->iprcid` / `->affinity` / `p_pdqcb->p_pcb`。
**★書き足してはいけないもの**（訂正B と同型）：`pdqcnt` の範囲検査。dcre には無い。

- [ ] **Step 5: `kernel/pridataq.c` に `del_pdq` を追加**（`acre_pdq` の直後）

```c
/*
 *  優先度データキューの削除
 */
#ifdef TOPPERS_del_pdq

#ifndef LOG_DEL_PDQ_ENTER
#define LOG_DEL_PDQ_ENTER(pdqid)
#endif /* LOG_DEL_PDQ_ENTER */

#ifndef LOG_DEL_PDQ_LEAVE
#define LOG_DEL_PDQ_LEAVE(ercd)
#endif /* LOG_DEL_PDQ_LEAVE */

ER
del_pdq(ID pdqid)
{
	PDQCB	*p_pdqcb;
	PDQINIB	*p_pdqinib;
	ER		ercd;
	TCB		*p_selftsk;
	PCB		*p_my_pcb;

	LOG_DEL_PDQ_ENTER(pdqid);
	CHECK_TSKCTX_UNL_MYSTATE(&p_selftsk);
	CHECK_ID(VALID_PDQID(pdqid));
	p_pdqcb = get_pdqcb(pdqid);

	lock_cpu();
	acquire_glock();
	p_my_pcb = get_my_pcb();
	if (p_pdqcb->p_pdqinib->pdqatr == TA_NOEXS) {
		ercd = E_NOEXS;
	}
	else if (pdqid <= tmax_spdqid) {
		ercd = E_OBJ;
	}
	else {
		/*
		 *  待ちタスクがいても削除は成功し，送信待ち・受信待ちの両方の
		 *  タスクがE_DLTで強制解除される．init_wait_queueはMP対応済み
		 *  （wait.c:215-228）で，既存のini_pdqと同一の機構である．
		 *  滞留データは破棄される（acre_pdqが取り出し時にcount/p_head/
		 *  unused/p_freelistを初期化する）．
		 */
		init_wait_queue(p_my_pcb, &(p_pdqcb->swait_queue));
		init_wait_queue(p_my_pcb, &(p_pdqcb->rwait_queue));
		p_pdqinib = (PDQINIB *)(p_pdqcb->p_pdqinib);
		/*
		 *  ★順序制約：属性の読みはTA_NOEXSの書込みより前で行う．
		 *  TA_NOEXSは((ATR)(-1))＝全ビットが1であるため，
		 *  TA_NOEXSを書いた後では(pdqatr & TA_MBALLOC) != 0Uが
		 *  必ず真になり，ユーザ供給の管理領域まで解放してしまう
		 *  （プールの破壊）．dcre pridataq.c:409-412 も同じ順序．
		 */
		if ((p_pdqinib->pdqatr & TA_MBALLOC) != 0U) {
			free_mpk(p_pdqinib->p_pdqmb);
		}
		p_pdqinib->pdqatr = TA_NOEXS;
		queue_insert_prev(&free_pdqcb, &(p_pdqcb->swait_queue));
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
	LOG_DEL_PDQ_LEAVE(ercd);
	return(ercd);
}

#endif /* TOPPERS_del_pdq */
```

★**順序が重要（2箇所）**：Task 3 Step 4 と同一（両待ちキューの `init_wait_queue` →
属性読み・`free_mpk` → `TA_NOEXS` 書込み → `queue_insert_prev`）。

dcre（`pridataq.c:384-426`）からの適応点は**3つ**：(1) glock の対化、
(2) 訂正C（段階3a 継承）＝`CHECK_TSKCTX_UNL_MYSTATE`＋`p_selftsk != p_my_pcb->p_schedtsk`、
(3) `init_wait_queue` に `p_my_pcb` 引数が付く。
★**`del_pdq` の ID 検査は dcre も `CHECK_ID`（`pridataq.c:395`）なので訂正F の対象外**である。

- [ ] **Step 6: E_NOEXS 検査の挿入（pridataq.c の8関数）**

判定式はすべて `p_pdqcb->p_pdqinib->pdqatr == TA_NOEXS`。
挿入位置は `acquire_glock()`（および直後の `p_my_pcb = get_my_pcb();` /
`p_selftsk = p_my_pcb->p_runtsk;` があればその後）の**直後**、既存の状態判定の**最初の分岐**。

| 関数 | 定義行 | 既存本体の形 | 挿入後 |
|---|---|---|---|
| **`snd_pdq`** | `:290` | **ロック前 `CHECK_PAR(TMIN_DPRI <= datapri && datapri <= maxdpri)`**＋`if (raster) … else if (send_pridata…) … else {…}` | **★構造変更（訂正E）。下記参照** |
| **`psnd_pdq`** | `:345` | 同上＋`if (send_pridata…) … else { E_TMOUT }` | **★構造変更** |
| **`tsnd_pdq`** | `:396` | 同上＋`if (raster) … else if … else if (tmout==TMO_POL) … else {…}` | **★構造変更** |
| `rcv_pdq` | `:455` | `if (raster) … else if (receive_pridata…) … else {…make_wait…}` | 先頭に `if (TA_NOEXS)`、既存 `if` を `else if` に |
| `prcv_pdq` | `:513` | `if (receive_pridata…) … else { E_TMOUT }` | 同上 |
| `trcv_pdq` | `:557` | `if (raster) … else if … else if (tmout==TMO_POL) … else {…}` | 同上 |
| `ini_pdq` | `:619` | 単文列（`init_wait_queue`×2／count/p_head/unused/p_freelist／`ercd = E_OK`／dispatch 判定） | `else { … }` で丸ごと包む |
| `ref_pdq` | `:663` | 単文列（`wait_tskid`×2／`spdqcnt`／`ercd = E_OK`） | `else { … }` で丸ごと包む |

★具体例1（`prcv_pdq`。字下げのみ）：

```c
	if (p_pdqcb->p_pdqinib->pdqatr == TA_NOEXS) {
		ercd = E_NOEXS;
	}
	else if (receive_pridata(p_my_pcb, p_pdqcb, p_data, p_datapri)) {
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
```

★具体例2（`ini_pdq`。★既存コードは `ercd = E_OK;` が `if (p_selftsk != …)` の**前**にあり、
dispatch する側では `ercd` を代入し直さない。**この形をそのまま保つ**）：

```c
	if (p_pdqcb->p_pdqinib->pdqatr == TA_NOEXS) {
		ercd = E_NOEXS;
	}
	else {
		init_wait_queue(p_my_pcb, &(p_pdqcb->swait_queue));
		init_wait_queue(p_my_pcb, &(p_pdqcb->rwait_queue));
		p_pdqcb->count = 0U;
		p_pdqcb->p_head = NULL;
		p_pdqcb->unused = 0U;
		p_pdqcb->p_freelist = NULL;
		ercd = E_OK;
		if (p_selftsk != p_my_pcb->p_schedtsk) {
			release_glock();
			dispatch();
			goto unlock_and_exit;
		}
	}
	release_glock();
  unlock_and_exit:
	unlock_cpu();
```
（★`ini_dtq`（Task 3）は `ercd = E_OK;` が dispatch 判定の**後ろ**にあり、
`ini_pdq` は**前**にある。**現物どおりに包むこと**。「揃える」誘惑に負けないこと。
現物を `sed -n '619,660p' kernel/pridataq.c` で確かめてから書く。）

★★具体例3（**`snd_pdq`/`psnd_pdq`/`tsnd_pdq` — 構造変更**。訂正E）：

**変更前**（`snd_pdq`。`pridataq.c:298-312` 付近）：
```c
	CHECK_DISPATCH_MYSTATE(&p_selftsk);
	CHECK_ID(VALID_PDQID(pdqid));
	p_pdqcb = get_pdqcb(pdqid);
	CHECK_PAR(TMIN_DPRI <= datapri && datapri <= p_pdqcb->p_pdqinib->maxdpri);

	lock_cpu_dsp();
	acquire_glock();
	p_my_pcb = get_my_pcb();
	if (p_selftsk->raster) {
		ercd = E_RASTER;
	}
	else if (send_pridata(p_my_pcb, p_pdqcb, data, datapri)) {
```

**変更後**（dcre `pridataq.c:444-456` と同じ構造）：
```c
	CHECK_DISPATCH_MYSTATE(&p_selftsk);
	CHECK_ID(VALID_PDQID(pdqid));
	p_pdqcb = get_pdqcb(pdqid);
	CHECK_PAR(TMIN_DPRI <= datapri);

	lock_cpu_dsp();
	acquire_glock();
	p_my_pcb = get_my_pcb();
	if (p_pdqcb->p_pdqinib->pdqatr == TA_NOEXS) {
		ercd = E_NOEXS;
	}
	else if (datapri > p_pdqcb->p_pdqinib->maxdpri) {
		/*
		 *  ★dcreに倣い，maxdpriとの比較をロック取得前のCHECK_PARから
		 *  ロック内のこの位置へ移した（dcre pridataq.c:450-452）．
		 *  E_NOEXSゲートより前にp_pdqinibを読むと，削除済み
		 *  （TA_NOEXS）スロットの残留maxdpriを読むことになるため．
		 *  ロック前に残すのはTMIN_DPRIとの比較だけ（INIBを読まない）．
		 */
		ercd = E_PAR;
	}
	else if (p_selftsk->raster) {
		ercd = E_RASTER;
	}
	else if (send_pridata(p_my_pcb, p_pdqcb, data, datapri)) {
```
`psnd_pdq` は `if (p_selftsk->raster)` の節が無いので、E_NOEXS → `datapri > maxdpri` →
既存の `if (send_pridata…)`（→`else if` へ）という並びになる。
`tsnd_pdq` は `snd_pdq` と同じ並び（E_NOEXS → E_PAR → raster → send_pridata → tmout==TMO_POL → wait）。
★3関数とも **ロック前の `CHECK_PAR` は `CHECK_PAR(TMIN_DPRI <= datapri);` に短縮**すること
（丸ごと削除しない。dcre もロック前に残している）。
★コメントは3関数のうち**先頭の `snd_pdq` にだけ**書き、`psnd_pdq`/`tsnd_pdq` には
`/*  dcreに倣いロック内へ移した（snd_pdq のコメント参照）．  */` の1行を置く
（同じ長文を3回貼らない）。

**除外**: `initialize_pridataq`、`enqueue_pridata`/`dequeue_pridata`/`send_pridata`/
`receive_pridata`（`TOPPERS_pdqenq` 他。ID を取らない内部関数）、
`acre_pdq`（ID を取らない）、`del_pdq`（自前で E_NOEXS を持つ）。
→ **pridataq.c で E_NOEXS を挿入するのは上表の8関数だけ。FMP3 固有関数は無い。**

- [ ] **Step 7: 配線（allfunc.h / Makefile.kernel）**

`kernel/allfunc.h` の `/* pridataq.c */` 節（`:176-189`）の `#define TOPPERS_pdqrcv`（`:181`）の
直後に2行：
```c
#define TOPPERS_acre_pdq
#define TOPPERS_del_pdq
```

`kernel/Makefile.kernel:96-98` を次に変更：
```
pridataq = pdqini.o pdqenq.o pdqdeq.o pdqsnd.o pdqrcv.o \
		acre_pdq.o del_pdq.o \
		snd_pdq.o psnd_pdq.o tsnd_pdq.o \
		rcv_pdq.o prcv_pdq.o trcv_pdq.o ini_pdq.o ref_pdq.o
```
**`KERNEL_FCSRCS` は触らない**（22個のまま）。

- [ ] **Step 8: rename 追加・再生成**

`kernel/kernel_rename.def` の `# pridataq.c` 節（★Task 3 で `# dataqueue.c` 節が3行増えて
いるので**行番号は動いている**。`# pridataq.c` の文字列を目印にする）を次に置換する：

```
# pridataq.c
initialize_pridataq
enqueue_pridata
dequeue_pridata
send_pridata
receive_pridata
free_pdqcb
tmax_spdqid
apdqinib_table

```

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core/kernel && ruby ../utils/genrename.rb kernel && cd ..
cd /home/honda/TOPPERS/FMP3/fmp3_core
git diff --stat kernel/kernel_rename.h kernel/kernel_unrename.h   # 期待: 各 +3 行・削除0
grep -n "free_pdqcb\|tmax_spdqid\|apdqinib_table" kernel/kernel_rename.h kernel/kernel_unrename.h
```
期待: `_kernel_free_pdqcb` / `_kernel_tmax_spdqid` / `_kernel_apdqinib_table` が実在。
**削除行があれば不合格。**
（★Task 3 のコミット後に走らせるので、diff は本 Task 分の +3 行だけになるはず。
+6 行になったら Task 3 の再生成が commit に入っていない。）

- [ ] **Step 9: 全8構成ビルド + 等価性 + 混在サンプル**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
for p in musca_b1 musca_b1-2core rp2350_pico2 polarfire_soc_kit-qemu \
         kria_arm64 kria_arm64-1core kria_r5 kria_r5-2core; do
  cmake --build build/$p > /tmp/dcre4-t4-build-$p.log 2>&1; echo "$p build rc=$?"
  tools/cfg_equivalence.sh build/$p > /tmp/dcre4-t4-eq-$p.log 2>&1; echo "$p eq rc=$?"
done
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
for d in musca_b1-2core-tmix kria_arm64-tmix; do
  cmake --build build/$d > /tmp/dcre4-t4-build-$d.log 2>&1; echo "$d build rc=$?"
  tools/cfg_equivalence.sh build/$d > /tmp/dcre4-t4-eq-$d.log 2>&1; echo "$d eq rc=$?"
done
```
期待: 全て rc=0。**exit=2 は不合格**。
`*-tmix` は `AID_PDQ(1)` を含むので `acre_pdq`/`del_pdq` が実際にリンクされる。
★`build/musca_b1-2core`（sample1）は静的 `CRE_PDQ` を**持たない**ので、
この構成では「`tnum_pdq == tnum_spdq == 0` かつ `pdqinib_table` が `TOPPERS_EMPTY_LABEL`」
という縮退ケースが通ることの検査になる。**`tnum_pdq - tnum_spdq == 0` のとき
`&apdqinib_table[0]` を取る式が UB にならない**ことを確認する
（`apdqinib_table` は `TOPPERS_EMPTY_LABEL` で実体が無いが、`PDQID` の第1項は
`p_pdqinib >= apdqinib_table && p_pdqinib < &apdqinib_table[0]` ＝常に偽で短絡する）。

- [ ] **Step 10: QEMU 起動（非退行）— musca_b1-2core と kria_r5-2core**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 25 cmake --build build/musca_b1-2core --target run > /tmp/dcre4-t4-run-musca.log 2>&1
grep -c 'Processor [12] start\.' /tmp/dcre4-t4-run-musca.log     # 期待: 2
pgrep -a qemu                                                    # 期待: 何も出ない
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 25 cmake --build build/kria_r5-2core --target run > /tmp/dcre4-t4-run-r5.log 2>&1
grep -c 'Processor [12] start\.' /tmp/dcre4-t4-run-r5.log        # 期待: 2
pgrep -a qemu                                                    # 期待: 何も出ない
```
（`kria_r5-2core` は既知どおり rc=124 になるが、**両 Processor start 行が出ていれば合格**
＝rc 単独で判定しない（Constraint 12）。）
★`sample1` は優先度データキューを使わない。したがって本 Step は**起動の非退行しか見ていない**。
**pdq の実動作の検査は Task 6 の test_dcre4 が唯一の砦である**ことを記録する。

- [ ] **Step 11: 台帳とコミット**

`DIVERGENCE_MAP.md` に次の行を追記する（種別はすべて `mod (dcre-port)`）：
- `kernel/check.h（dcre段階3b Task 4）` — 理由：「`VALID_TPRI` の直後に
  `VALID_DPRI(dpri)`（dcre `check.h:71` と同一）を追加。`acre_pdq` の `maxdpri` 検査に必要で、
  FMP3 の pristine には存在しなかった（既存の `snd_pdq` 等は直書きしており、それは
  書き換えていない）。段階1 で `CHECK_VALIDATR` を追加したのと同じ前例」
  （`kernel/check.h` の既存行があればその理由欄に追記する）
- `kernel/pridataq.h（dcre段階3b Task 4）` — 理由：Task 3 の `dataqueue.h` 行と同型
  （`tmax_spdqid`／`QUEUE free_pdqcb`／`apdqinib_table[]` の extern 追加、`tnum_pdq` の移設と
  `tnum_spdq` 新設、既存 `PDQID` の2レンジ版への**置換**、`#include <queue.h>` は既存で追加不要）
- `kernel/pridataq.c（dcre段階3b Task 4）` — 理由：「`#define tnum_pdq` を削除（`.h` へ移設）。
  `QUEUE free_pdqcb;` の定義を追加。`initialize_pridataq` の静的ループ境界を
  `tnum_pdq` → `tnum_spdq` に変更し、既存のマスタ限定ブロックの中に動的スロット初期化を追加。
  **優先度データキューは非親和オブジェクトのため `iprcid`/`affinity`/`p_pcb` の充填や
  プロセッサ判定は一切行っていない**。`acre_pdq`/`del_pdq` を追加
  （dcre `pridataq.c:316-379`/`:384-426` の転写）。dcre からの意図的な逸脱2件：
  (1) glock の対化、(2) 空判定 `tnum_pdq == 0` → `tnum_pdq == tnum_spdq`
  （`del_pdq` の ID 検査は dcre も `CHECK_ID` なので `del_dtq` のような訂正は不要）。
  ★`del_pdq` は **`TA_MBALLOC` のビット検査と `free_mpk` を `pdqatr = TA_NOEXS` の書込みより
  前**に置く順序制約がある（`TA_NOEXS` は全ビット 1 なので後に置くと必ず真になる）。
  `snd_pdq`/`psnd_pdq`/`tsnd_pdq`/`rcv_pdq`/`prcv_pdq`/`trcv_pdq`/`ini_pdq`/`ref_pdq` の
  8関数に existence-before-state 規約の E_NOEXS 分岐を挿入。うち5関数は字下げのみで
  byte-preserve したが、**`snd_pdq`/`psnd_pdq`/`tsnd_pdq` は dcre に倣って構造変更した**：
  ロック取得前の `CHECK_PAR(TMIN_DPRI <= datapri && datapri <= p_pdqcb->p_pdqinib->maxdpri)` を
  `CHECK_PAR(TMIN_DPRI <= datapri)` に短縮し、`maxdpri` との比較をロック内の
  `else if (datapri > p_pdqcb->p_pdqinib->maxdpri) { ercd = E_PAR; }` へ移した
  （dcre `pridataq.c:444,450-452` ほか）。理由は E_NOEXS ゲートより前に削除済みスロットの
  INIB を読まないため。返値・意味論は不変」
- `kernel/allfunc.h（dcre段階3b Task 4）`（既存行の理由欄に「`/* pridataq.c */` 節に
  `TOPPERS_acre_pdq`/`TOPPERS_del_pdq` を追加」を追記）
- `kernel/Makefile.kernel（dcre段階3b Task 4）`（同、「`pridataq =` 行に
  `acre_pdq.o`/`del_pdq.o` を追加」）
- `kernel/kernel_rename.def（dcre段階3b Task 4）` — 理由：「`# pridataq.c` 節に
  `free_pdqcb`/`tmax_spdqid`/`apdqinib_table` の3エントリを追加」
- `kernel/kernel_rename.h・kernel_unrename.h（dcre段階3b Task 4）` — 種別
  `mod (dcre-port, 生成物)`、理由：「再生成。各+3行・削除0行」

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
git add -A && git commit -m "feat(kernel): acre_pdq/del_pdq・VALID_DPRI追加・free_pdqcb・2レンジPDQID とE_NOEXS検査8箇所（datapri検査のロック内移動を含む・dcre段階3b）"
```

---
### Task 5: mempfix 層 — free_mpfcb・initialize・2レンジ MPFID・acre_mpf（2段確保）/del_mpf・E_NOEXS ×6

**推奨モデル:** 中位（sonnet）。段階3b で**最も難しい**タスク（`acre_mpf` の2段確保と巻き戻し、
`del_mpf` の二重解放条件、`rel_mpf` の構造変更＝メモリ安全性の修正）。

**Files:**
- Modify: `kernel/mempfix.h`（externs / `tnum_mpf`・`tnum_smpf` 移設 / `MPFID` 置換）
- Modify: `kernel/mempfix.c`（`tnum_mpf` 重複削除 / `free_mpfcb` / `initialize_mempfix` /
  `acre_mpf` / `del_mpf` / E_NOEXS ×6）
- Modify: `kernel/allfunc.h`（`/* mempfix.c */` 節に2行）
- Modify: `kernel/Makefile.kernel`（`mempfix =` 行に .o 2個）
- Modify: `kernel/kernel_rename.def` →（再生成）`kernel_rename.h` `kernel_unrename.h`
- Modify: `DIVERGENCE_MAP.md`

**Interfaces:**
- Consumes: Task 2 の `_kernel_tmax_smpfid` / `_kernel_ampfinib_table` / 予約 MPFCB /
  `T_CMPF` / `TA_MBALLOC` / `acre_mpf`・`del_mpf` の宣言。
  Task 1 の確認結果表（訂正D/E、dcre 転写元行範囲、del の順序制約）。
- Produces: `QUEUE free_mpfcb` / `tmax_smpfid` / `tnum_smpf` / `ampfinib_table[]` /
  2レンジ `MPFID(p_mpfcb)` / `ER_ID acre_mpf(const T_CMPF *)` / `ER del_mpf(ID)`。Task 6 が使う。

**★mpf 固有の相違（dtq/pdq と「同様」ではない点）:**
1. free-list のリンクは **`wait_queue`**（`swait_queue` ではない）。MPFCB の待ちキューは1本だけ。
   `del_mpf` の `init_wait_queue` も**1回**。
2. `acre_mpf` は **2段確保**（①ブロック領域 ②管理領域）で、②が失敗したときに
   **①を巻き戻す**。巻き戻しの条件は `pk_cmpf->mpf == NULL`（**ローカル `mpf` ではない**）。
3. `del_mpf` は **`TA_MEMALLOC` と `TA_MBALLOC` の2つのビット検査**で最大2回 `free_mpk` する。
   **どちらも `mpfatr = TA_NOEXS` の書込みより前**でなければならない。
4. `acre_mpf` には `CHECK_PAR(blkcnt != 0)` / `CHECK_PAR(blksz != 0)` /
   `MPF_ALIGN(mpf)` / `MB_ALIGN(p_mpfmb)` の検査がある（dtq/pdq より多い）。
5. `MPFINIB.blksz` には **`ROUND_MPF_T(blksz)`（丸めた値）**を入れる（生の `blksz` ではない）。
6. `rel_mpf` の構造変更は**メモリ安全性の修正**である（訂正E）。現行はロック取得前に
   `p_mpfcb->p_mpfinib->p_mpfmb[blkidx]` をデリファレンスしており、削除済みプールに対しては
   **`free_mpk` 済み領域への読出し**になる。

**AID 無し構成の挙動は不変である**（`tnum_smpf == tnum_mpf`）。
★ただし `rel_mpf` は AID 無し構成でも「E_PAR を返す位置がロック前からロック内へ移る」。返値は同じ。

- [ ] **Step 1: `kernel/mempfix.h` — extern 追加・`tnum_*` 移設・`MPFID` の2レンジ置換**

`extern const ID tmax_mpfid;` のブロック（`mempfix.h:101-104`）を次に置換：

```c
/*
 *  固定長メモリプールIDの最大値（kernel_cfg.c）
 */
extern const ID	tmax_mpfid;
extern const ID	tmax_smpfid;		/* 静的生成固定長メモリプールのID番号の最大値 */

/*
 *  使用していない固定長メモリプール管理ブロックのリスト（mempfix.c）
 *
 *  MPFCBの先頭フィールドがQUEUE（wait_queue）なので，そのまま
 *  free-listのリンクに流用する（dcre mempfix.c:159-165 と同一）．
 *  段階2のcyc/almで用いたtmevtb領域のオーバレイは不要である．
 */
extern QUEUE	free_mpfcb;
```

`extern const MPFINIB mpfinib_table[];` のブロック（`mempfix.h:106-109`）の**直後**に追加：

```c
/*
 *  動的生成固定長メモリプールの初期化ブロックのエリア（kernel_cfg.c・RAM）
 */
extern MPFINIB	ampfinib_table[];
```

`extern MPFCB *const p_mpfcb_table[];`（`mempfix.h:114`）の**直後**、
既存の `MPFID` マクロブロック（`mempfix.h:116-121`）を次で**置換**する：

```c
/*
 *  固定長メモリプールの数
 *
 *  MPFIDマクロから参照するためmempfix.cから移設した．
 */
#define tnum_mpf	((uint_t)(tmax_mpfid - TMIN_MPFID + 1))
#define tnum_smpf	((uint_t)(tmax_smpfid - TMIN_MPFID + 1))

/*
 *  固定長メモリプール管理ブロックから固定長メモリプールIDを取り出すた
 *  めのマクロ
 *
 *  FMP3のMPFCBはポインタ表（p_mpfcb_table）経由で参照される個別の
 *  named staticであり，MPFCB自身の配列位置から番号を引けない．元から
 *  MPFINIBへのポインタ差分で求めていた式を，動的生成固定長メモリプール
 *  （p_mpfinibがampfinib_tableを指す）と静的生成固定長メモリプール
 *  （p_mpfinibがmpfinib_tableを指す）の2レンジに拡張する
 *  （段階2のCYCID・段階3aのSEMIDと同型）．AID_MPFが無い構成では
 *  tnum_mpf == tnum_smpfとなり第1項が常に偽＝従来と同一の式に落ちる．
 */
#define	MPFID(p_mpfcb) \
	((((p_mpfcb)->p_mpfinib >= ampfinib_table) \
		&& ((p_mpfcb)->p_mpfinib < &ampfinib_table[tnum_mpf - tnum_smpf])) \
	  ? ((ID)(((p_mpfcb)->p_mpfinib - ampfinib_table) + TMIN_MPFID + tnum_smpf)) \
	  : ((ID)(((p_mpfcb)->p_mpfinib - mpfinib_table) + TMIN_MPFID)))
```

★`#include <queue.h>` は `mempfix.h:51` に**既にある**ので追加不要。

- [ ] **Step 2: `kernel/mempfix.c` — `tnum_mpf` の重複削除と `initialize_mempfix` の改造**

`mempfix.c:104-107` の

```c
/*
 *  固定長メモリプールの数
 */
#define tnum_mpf	((uint_t)(tmax_mpfid - TMIN_MPFID + 1))
```

を**削除**する（Step 1 で `mempfix.h` に移した）。
`INDEX_MPF`/`get_mpfcb`（`:109-114`）は**そのまま残す**。

`#ifdef TOPPERS_mpfini` ブロック（`mempfix.c:119-142`）を次で置換する：

```c
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
```

（**FIFO**＝`queue_insert_prev` で末尾へ。`i` は静的ループから引き継ぐ＝dcre `mempfix.c:150,159`。
★free-list のリンクは **`wait_queue`**。`fblkcnt`/`unused`/`freelist`/`mpf`/`p_mpfmb` は
動的スロットでは設定しない — `acre_mpf` が設定する。）

- [ ] **Step 3: `kernel/mempfix.c` に `acre_mpf` を追加**（`TOPPERS_mpfget` 区画の直後、
  `TOPPERS_get_mpf` 区画の直前。dcre の配置と同じ）

```c
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
```

dcre（`mempfix.c:199-279`）からの適応点は**2つだけ**：
1. `lock_cpu()` の直後に `acquire_glock()`／末尾に `release_glock()`。
2. 空判定を `tnum_mpf == 0` → `tnum_mpf == tnum_smpf`。

★**型について**：`MPFINIB.mpf` は FMP3 では `void *`（`mempfix.h:82`）、
`T_CMPF.mpf` は `MPF_T *`。`p_mpfinib->mpf = mpf;` は `MPF_T *` → `void *` の
暗黙変換で通る（キャスト不要）。`mpf = malloc_mpk(...)` は `void *` → `MPF_T *` の
暗黙変換で通る。**キャストを足さないこと**（dcre 原文にも無い）。

**★書いてはいけないもの**（Constraint 4）：`p_mpfinib->iprcid` / `->affinity` / `p_mpfcb->p_pcb`。

★**巻き戻しが dcre に無かった場合の指示**（Task 1 Step 3 で現物確認する）：
本計画の実測では**巻き戻しは存在する**（`mempfix.c:249-254`）。もし現物に無ければ
それは dcre 側のリークバグなので、**上の形（巻き戻しあり）で実装し、
上流報告候補として台帳に記録する**。無かったことにして dcre をそのまま写さないこと。

- [ ] **Step 4: `kernel/mempfix.c` に `del_mpf` を追加**（`acre_mpf` の直後）

```c
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
```

★**順序が重要（3箇所）**：
1. `init_wait_queue` で待ちキューを空にした**後**に `queue_insert_prev(&free_mpfcb,
   &(p_mpfcb->wait_queue))`（**同じ `wait_queue`** を free-list のリンクに使う）。
2. **2つのビット検査と `free_mpk` は `mpfatr = TA_NOEXS;` の前**。
3. `TA_MEMALLOC`（ブロック領域）→ `TA_MBALLOC`（管理領域）の順。dcre と同じ
   （順序自体に依存性は無いが、現物どおりにする）。

dcre（`mempfix.c:284-328`）からの適応点は**3つ**：(1) glock の対化、
(2) 訂正C（段階3a 継承）＝`CHECK_TSKCTX_UNL_MYSTATE`＋`p_selftsk != p_my_pcb->p_schedtsk`、
(3) `init_wait_queue` に `p_my_pcb` 引数が付く。
★**`del_mpf` の ID 検査は dcre も `CHECK_ID`（`mempfix.c:295`）なので訂正F の対象外**である。

- [ ] **Step 5: E_NOEXS 検査の挿入（mempfix.c の6関数）**

判定式はすべて `p_mpfcb->p_mpfinib->mpfatr == TA_NOEXS`。

| 関数 | 定義行 | 既存本体の形 | 挿入後 |
|---|---|---|---|
| `get_mpf` | `:173` | `if (raster) … else if (fblkcnt>0) … else {…wobj_make_wait…}` | 先頭に `if (TA_NOEXS)`、既存 `if` を `else if` に |
| `pget_mpf` | `:222` | `if (fblkcnt>0) … else { E_TMOUT }` | 同上 |
| `tget_mpf` | `:257` | `if (raster) … else if (fblkcnt>0) … else if (tmout==TMO_POL) … else {…}` | 同上 |
| **`rel_mpf`** | `:311` | **ロック前 `CHECK_PAR` ×4**＋`if (!queue_empty…) … else {…}` | **★構造変更（訂正E）。下記参照** |
| `ini_mpf` | `:370` | 単文列（`init_wait_queue`／fblkcnt/unused/freelist／dispatch 判定） | `else { … }` で丸ごと包む |
| `ref_mpf` | `:413` | 単文列（`wait_tskid`／`fblkcnt`／`ercd = E_OK`） | `else { … }` で丸ごと包む |

★具体例1（`pget_mpf`。★この関数は `p_my_pcb` も `p_selftsk` も持たず
`acquire_glock();` の直後がいきなり `if` である）：

```c
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
```

★具体例2（`ref_mpf`）：

```c
	if (p_mpfcb->p_mpfinib->mpfatr == TA_NOEXS) {
		ercd = E_NOEXS;
	}
	else {
		pk_rmpf->wtskid = wait_tskid(&(p_mpfcb->wait_queue));
		pk_rmpf->fblkcnt = p_mpfcb->fblkcnt;
		ercd = E_OK;
	}
```

★★具体例3（**`rel_mpf` — メモリ安全性の修正を伴う構造変更**。訂正E）：

**変更前**（現行 `mempfix.c:320-352` 付近）：
```c
	LOG_REL_MPF_ENTER(mpfid, blk);
	CHECK_TSKCTX_UNL_MYSTATE(&p_selftsk);
	CHECK_ID(VALID_MPFID(mpfid));
	p_mpfcb = get_mpfcb(mpfid);
	CHECK_PAR(p_mpfcb->p_mpfinib->mpf <= blk);
	blkoffset = ((char *) blk) - (char *)(p_mpfcb->p_mpfinib->mpf);
	CHECK_PAR(blkoffset % p_mpfcb->p_mpfinib->blksz == 0U);
	CHECK_PAR(blkoffset / p_mpfcb->p_mpfinib->blksz < p_mpfcb->unused);
	blkidx = (uint_t)(blkoffset / p_mpfcb->p_mpfinib->blksz);
	CHECK_PAR(p_mpfcb->p_mpfinib->p_mpfmb[blkidx].next == INDEX_ALLOC);

	lock_cpu();
	acquire_glock();
	p_my_pcb = get_my_pcb();
	if (!queue_empty(&(p_mpfcb->wait_queue))) {
		...
	}
	else {
		...
	}
	release_glock();
  unlock_and_exit:
	unlock_cpu();
```

**変更後**（dcre `mempfix.c:479-506` と同じ構造）：
```c
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
```
★`CHECK_PAR` 4行を**削除**すること（残すと二重検査になり、しかも危険な方が残る）。
★`error_exit:` ラベルは `CHECK_TSKCTX_UNL_MYSTATE`/`CHECK_ID` がまだ使うので**残す**。
★`blkoffset` は `size_t`（符号なし）なので `blk < mpf` のとき巨大値に化けるが、
**第1条件 `!(mpf <= blk)` が先に真になって短絡する**ので実害は無い（dcre と同一の並び）。
`blkidx` の計算だけは条件の前にあり、範囲外の値になりうるが、**使われない**。

**除外**: `initialize_mempfix`、`get_mpf_block`（`TOPPERS_mpfget`。ID を取らない内部関数）、
`acre_mpf`（ID を取らない）、`del_mpf`（自前で E_NOEXS を持つ）。
→ **mempfix.c で E_NOEXS を挿入するのは上表の6関数だけ。FMP3 固有関数は無い。**

- [ ] **Step 6: 配線（allfunc.h / Makefile.kernel）**

`kernel/allfunc.h` の `/* mempfix.c */` 節（`:208-216`）の `#define TOPPERS_mpfget`（`:210`）の
直後に2行：
```c
#define TOPPERS_acre_mpf
#define TOPPERS_del_mpf
```

`kernel/Makefile.kernel:103-104` を次に変更：
```
mempfix = mpfini.o mpfget.o acre_mpf.o del_mpf.o \
		get_mpf.o pget_mpf.o tget_mpf.o \
		rel_mpf.o ini_mpf.o ref_mpf.o
```
**`KERNEL_FCSRCS` は触らない**（22個のまま）。

- [ ] **Step 7: rename 追加・再生成**

`kernel/kernel_rename.def` の `# mempfix.c` 節（★文字列を目印にする）を次に置換する：

```
# mempfix.c
initialize_mempfix
get_mpf_block
free_mpfcb
tmax_smpfid
ampfinib_table

```

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core/kernel && ruby ../utils/genrename.rb kernel && cd ..
cd /home/honda/TOPPERS/FMP3/fmp3_core
git diff --stat kernel/kernel_rename.h kernel/kernel_unrename.h   # 期待: 各 +3 行・削除0
grep -n "free_mpfcb\|tmax_smpfid\|ampfinib_table" kernel/kernel_rename.h kernel/kernel_unrename.h
```
期待: `_kernel_free_mpfcb` / `_kernel_tmax_smpfid` / `_kernel_ampfinib_table` が実在。
**削除行があれば不合格。**

- [ ] **Step 8: 全8構成ビルド + 等価性 + 混在サンプル（32/64bit 両方）**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
for p in musca_b1 musca_b1-2core rp2350_pico2 polarfire_soc_kit-qemu \
         kria_arm64 kria_arm64-1core kria_r5 kria_r5-2core; do
  cmake --build build/$p > /tmp/dcre4-t5-build-$p.log 2>&1; echo "$p build rc=$?"
  tools/cfg_equivalence.sh build/$p > /tmp/dcre4-t5-eq-$p.log 2>&1; echo "$p eq rc=$?"
done
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
for d in musca_b1-2core-tmix kria_arm64-tmix; do
  cmake --build build/$d > /tmp/dcre4-t5-build-$d.log 2>&1; echo "$d build rc=$?"
  tools/cfg_equivalence.sh build/$d > /tmp/dcre4-t5-eq-$d.log 2>&1; echo "$d eq rc=$?"
done
```
期待: 全て rc=0。**exit=2 は不合格**。
★`kria_arm64-tmix` は 64bit（`MPF_T = intptr_t` が 8 バイト・`sizeof(MPFMB) = 4`）で、
`ROUND_MPF_T(blksz) * blkcnt` と `sizeof(MPFMB) * blkcnt` の型が異なることの
コンパイル確認になる。**警告が出ていないことも `grep -i "warning" /tmp/dcre4-t5-build-kria_arm64-tmix.log`
で確かめる**（符号／サイズの暗黙変換警告が出たら報告する）。

- [ ] **Step 9: QEMU 起動（非退行）— musca_b1-2core と kria_arm64**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 25 cmake --build build/musca_b1-2core --target run > /tmp/dcre4-t5-run-musca.log 2>&1
grep -c 'Processor [12] start\.' /tmp/dcre4-t5-run-musca.log     # 期待: 2
pgrep -a qemu                                                    # 期待: 何も出ない
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 25 cmake --build build/kria_arm64 --target run > /tmp/dcre4-t5-run-arm64.log 2>&1
grep -c 'Processor [1-4] start\.' /tmp/dcre4-t5-run-arm64.log    # 期待: 4
pgrep -a qemu                                                    # 期待: 何も出ない
```
★`sample1` は固定長メモリプールを使わない。したがって本 Step は**起動の非退行しか見ていない**。
**mpf の実動作（特に `rel_mpf` の構造変更）の検査は Task 6 の test_dcre4 が唯一の砦である**
ことを記録する。

- [ ] **Step 10: 台帳とコミット**

`DIVERGENCE_MAP.md` に次の行を追記する（種別はすべて `mod (dcre-port)`）：
- `kernel/mempfix.h（dcre段階3b Task 5）` — 理由：Task 3/4 と同型
  （`tmax_smpfid`／`QUEUE free_mpfcb`／`ampfinib_table[]` の extern 追加、`tnum_mpf` の移設と
  `tnum_smpf` 新設、既存 `MPFID` の2レンジ版への**置換**）
- `kernel/mempfix.c（dcre段階3b Task 5）` — 理由：「`#define tnum_mpf` を削除（`.h` へ移設）。
  `QUEUE free_mpfcb;` の定義を追加。`initialize_mempfix` の静的ループ境界を
  `tnum_mpf` → `tnum_smpf` に変更し、既存のマスタ限定ブロックの中に動的スロット初期化を追加
  （free-list のリンクは `wait_queue`）。**固定長メモリプールは非親和オブジェクトのため
  `iprcid`/`affinity`/`p_pcb` の充填やプロセッサ判定は一切行っていない**。
  `acre_mpf`/`del_mpf` を追加（dcre `mempfix.c:199-279`/`:284-328` の転写）。
  dcre からの意図的な逸脱2件：(1) glock の対化、(2) 空判定 `tnum_mpf == 0` →
  `tnum_mpf == tnum_smpf`（`del_mpf` の ID 検査は dcre も `CHECK_ID` なので訂正不要）。
  ★`acre_mpf` は**2段確保**（①`mpf == NULL` なら `malloc_mpk(ROUND_MPF_T(blksz) * blkcnt)` +
  `TA_MEMALLOC`、②`mpfmb == NULL` なら `malloc_mpk(sizeof(MPFMB) * blkcnt)` + `TA_MBALLOC`）で、
  **②失敗時は `pk_cmpf->mpf == NULL` を条件に①を `free_mpk` で巻き戻す**
  （判定にローカル `mpf` を使うと①で上書きされていて区別できない。dcre `mempfix.c:250` と同一）。
  `MPFINIB.blksz` には `ROUND_MPF_T(blksz)`（丸めた値）を入れる。
  E_NOMEM のとき free-list から CB を取り出していない（2段とも成功してから `queue_delete_next`）。
  ★`del_mpf` は **`TA_MEMALLOC`/`TA_MBALLOC` の2つのビット検査と `free_mpk` を
  `mpfatr = TA_NOEXS` の書込みより前**に置く順序制約がある（`TA_NOEXS` は `((ATR)(-1))`＝
  全ビット 1 なので、後に置くと2条件とも必ず真になり、ユーザ供給の領域まで解放してしまう）。
  dcre `mempfix.c:308-314` も同じ順序。
  `get_mpf`/`pget_mpf`/`tget_mpf`/`rel_mpf`/`ini_mpf`/`ref_mpf` の6関数に
  existence-before-state 規約の E_NOEXS 分岐を挿入。うち5関数は字下げのみで byte-preserve
  したが、**`rel_mpf` は dcre に倣って構造変更した**：ロック取得前の `CHECK_PAR` 4件を削除し、
  ロック内の E_NOEXS の次に `blkoffset`/`blkidx` の計算と4条件の `||` 結合による
  `E_PAR` 分岐を置いた（dcre `mempfix.c:487-495`）。これは **ercd の問題ではなくメモリ
  安全性の修正**である（削除済みプールでは `p_mpfinib->p_mpfmb` が `free_mpk` 済みの番地で、
  ロック前にデリファレンスすると解放済み領域を読む）。4条件の評価順は dcre のままで、
  範囲外 `blkidx` は第3条件の短絡により第4条件へ到達しない」
- `kernel/allfunc.h（dcre段階3b Task 5）`（既存行の理由欄に「`/* mempfix.c */` 節に
  `TOPPERS_acre_mpf`/`TOPPERS_del_mpf` を追加」を追記）
- `kernel/Makefile.kernel（dcre段階3b Task 5）`（同、「`mempfix =` 行に
  `acre_mpf.o`/`del_mpf.o` を追加」）
- `kernel/kernel_rename.def（dcre段階3b Task 5）` — 理由：「`# mempfix.c` 節に
  `free_mpfcb`/`tmax_smpfid`/`ampfinib_table` の3エントリを追加」
- `kernel/kernel_rename.h・kernel_unrename.h（dcre段階3b Task 5）` — 種別
  `mod (dcre-port, 生成物)`、理由：「再生成。各+3行・削除0行」

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
git add -A && git commit -m "feat(kernel): acre_mpf（2段確保と巻き戻し）/del_mpf（TA_MEMALLOC+TA_MBALLOCの二重解放）・free_mpfcb・2レンジMPFID とE_NOEXS検査6箇所（rel_mpfの解放済み領域参照を解消・dcre段階3b）"
```

---
### Task 6: QEMU 回帰テスト test_dcre4

**推奨モデル:** 中位（sonnet）

**Files:**
- Create: `test/test_dcre4.c` `test/test_dcre4.cfg` `test/test_dcre4.h`
- Modify: `test/MANIFEST`（`test_dcre3.h` の直後に3行）・`test/testexec.rb`（`"dcre3"` の直後に1行）
- Modify: `DIVERGENCE_MAP.md`

**Interfaces:** Consumes Task 2〜5 の全成果。`syssvc/test_svc.h` の
`test_start` / `check_point`（= `check_point_prc(count, 0)`）/ `check_point_prc` /
`check_ercd` / `check_assert` / `check_finish`。

**★check_point の意味論（現物確認済み。段階3a で数え間違いが3回続いた箇所なので厳密に書く）:**
`syssvc/test_svc.c:110-176` の実装より：
- `check_point_prc(count, prcid)` は **`0 < prcid && prcid <= TNUM_PRCID` のとき
  `check_count[prcid - 1]`** を、**それ以外（`prcid == 0`）のとき `check_count[0]`** を使う。
  すなわち **PRC1 の `check_point()` と PRC2 の `check_point_prc(n,2)` は独立したカウンタ**であり、
  **PRC2 側の最初のチェックポイントは `check_point_prc(1, 2)`** になる。
- 出力形式は `prcid > 0` のとき `syslog_2(LOG_NOTICE, "Check point %d-%d passed.", prcid, count)`
  ＝**`prcid` が先**。`check_point_prc(1, 2)` は **`Check point 2-1 passed.`** と出る
  （`Check point 1-2` **ではない**）。`prcid == 0` のときは `Check point %d passed.`。
- ★**`check_finish(count)` は内部で `check_point_prc(count, 0)` を呼ぶ**（`test_svc.c:183-190`）ので、
  **`check_finish` 自身が "Check point <count> passed." の行を1本出す**。
  その後に `All check points passed.` と `TTSP_RESULT: PASS` が続く。
- ★**同じ PRC1 上の TASK1 と TASK2 は同じカウンタ `check_count[0]` を共有する**ので、
  2タスクにまたがる番号列が**通し**で単調増加になるよう振り付ける。

**★本テストの `Check point` 行数は 15 である**（後段の grep の期待値）：
- PRC1（`check_count[0]`、TASK1 と TASK2 が共有）: TASK1 が 1,2,3,5,7,8,9,10,11,12／
  TASK2 が 4,6 ＝ **12 本** + `check_finish(13)` 自身の 1 本 = **13 本**
- PRC2（`check_count[1]`、TASK3）: `Check point 2-1 passed.` と `Check point 2-2 passed.` = **2 本**
- 合計 **15 本**

- [ ] **Step 1: 前提の現物確認（存在するものだけで書く）**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
sed -n '55,115p' syssvc/test_svc.h
sed -n '105,215p' syssvc/test_svc.c
grep -n "^CRE_DTQ\|^CRE_PDQ\|^CRE_MPF\|^CRE_TSK\|^AID_DTQ\|^AID_PDQ\|^AID_MPF\|^DEF_MPK" kernel/kernel_api.def
grep -n "define TSK_NONE\|define TMO_POL\|define TMIN_DPRI\|define TMAX_DPRI" include/kernel.h
grep -n "ext_tsk" test/test_dcre3.c test/test_dcre1.c
grep -rn "CLS_PRC2" test/test_mmutex1.cfg | head
grep -n "MPK_SIZE" test/test_dcre1.h test/test_dcre_mix.h
sed -n '440,500p' kernel/startup.c        # MEMPOOLCB / malloc_mempool / free_mempool
```
確認すること：
- `test_start` / `check_point_prc` / `check_finish` / `check_assert` / `check_ercd` /
  `check_point` が存在する（無いものは使わない）。
- `CRE_DTQ #dtqid* { .dtqatr .dtqcnt &dtqmb? }`（`kernel_api.def:4`）/
  `CRE_PDQ #pdqid* { .pdqatr .pdqcnt +maxdpri &pdqmb? }`（`:5`）/
  `CRE_MPF #mpfid* { .mpfatr .blkcnt .blksz &mpf? &mpfmb? }`（`:8`）。
- `TSK_NONE = 0`（`include/kernel.h:604`）、`TMO_POL`、`TMIN_DPRI = 1`・`TMAX_DPRI = 16`（`:624-625`）。
- `CLS_PRC2` が既存テスト（`test/test_mmutex1.cfg:18` 等）で使われている＝
  `test_common1.cfg` 経由で使える。
- `ext_tsk` の書き方は **`ext_tsk();`（返値を見ない）**（`test/test_dcre3.c:104,119`）。
- ★**`free_mempool` は `count == 0` になったときだけ `brk` を先頭へ戻す**こと（訂正G）。
  本テストの `MPK_SIZE` の設計根拠であり、変異 control の梃子でもある。
★食い違ったら**現物に合わせてテストを直し、直した事実を記録する**。

- [ ] **Step 2: `test/test_dcre4.h`**

```c
/*
 *		動的生成API（acre_dtq/del_dtq・acre_pdq/del_pdq・acre_mpf/del_mpf）
 *		のテスト
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
 *  カーネルメモリプールのサイズ
 *
 *  ★この値は変異controlの成立条件でもある．kernel/startup.cのプールは
 *  bump allocatorで，free_mempoolはcountが0になったときにだけbrkを
 *  先頭へ戻す（段階3b 訂正G）．したがって
 *
 *    ・正常時：acre_mpf/del_mpfを1周するとcountが0に戻りbrkがリセット
 *              されるので，MPF_CYCLES回まわしても消費は1周分（272B前後）
 *              で頭打ちになる．
 *    ・del_mpfのTA_MEMALLOC解放を落とすと：1周ごとにブロック領域
 *              （ROUND_MPF_T(64)*4 = 256B）が返らずbrkが単調に進み，
 *              MPK_SIZEを7〜8周で使い切ってacre_mpfがE_NOMEMになる．
 *
 *  MPK_SIZE = 2048 は「1周分（272B前後）は十分入るが，MPF_CYCLES(=16)周
 *  ぶんの累積（4KB超）は入らない」ように選んである．
 */
#ifndef MPK_SIZE
#define MPK_SIZE		2048
#endif /* MPK_SIZE */

/*  動的生成する固定長メモリプールの諸元  */
#define MPF_BLKCNT		4
#define MPF_BLKSZ		64
#define MPF_CYCLES		16

/*  ユーザ供給の管理領域を持つデータキューの容量  */
#define USER_DTQCNT		2

/*  他プロセッサのタスクが待ちに入るのを待つ時間（単位: マイクロ秒）  */
#ifndef TEST_TIME_PROC
#define TEST_TIME_PROC	200000U		/* 200ms */
#endif /* TEST_TIME_PROC */

#ifndef TOPPERS_MACRO_ONLY
extern void	task1(EXINF exinf);
extern void	task2(EXINF exinf);
extern void	task3(EXINF exinf);
#endif /* TOPPERS_MACRO_ONLY */
```

- [ ] **Step 3: `test/test_dcre4.cfg`**

```c
/*
 *		動的生成API（dtq/pdq/mpf）のテストの
 *		システムコンフィギュレーションファイル
 *
 *  $Id$
 */
INCLUDE("test_common1.cfg");

#include "test_dcre4.h"

CLASS(CLS_PRC1) {
	CRE_TSK(TASK1, { TA_ACT, 1, task1, MID_PRIORITY, STACK_SIZE, NULL });
	/*  TASK2: PRC1 上で送信待ちの E_DLT を受け取る待ちタスク  */
	CRE_TSK(TASK2, { TA_NULL, 2, task2, HIGH_PRIORITY, STACK_SIZE, NULL });
	/*
	 *  静的な dtq/pdq/mpf．del_dtq/del_pdq/del_mpf の E_OBJ 対象であると
	 *  同時に，AID_DTQ/AID_PDQ/AID_MPF が「静的オブジェクトが1個以上ある
	 *  こと」を要求する（訂正E ガード）ため必須．
	 */
	CRE_DTQ(DTQ1, { TA_TPRI, 2, NULL });
	CRE_PDQ(PDQ1, { TA_TPRI, 2, 4, NULL });
	CRE_MPF(MPF1, { TA_TPRI, 2, 32, NULL, NULL });
}

CLASS(CLS_PRC2) {
	/*  TASK3: PRC2 上で受信待ちの E_DLT を受け取る待ちタスク（MP 経路の実証）  */
	CRE_TSK(TASK3, { TA_NULL, 3, task3, HIGH_PRIORITY, STACK_SIZE, NULL });
}

/*  AID_DTQ/AID_PDQ/AID_MPF と DEF_MPK はクラス外専用（Task 2 の E_RSATR 検査対象）  */
AID_DTQ(2);
AID_PDQ(1);
AID_MPF(1);
DEF_MPK({ MPK_SIZE, NULL });
```
（★段階3a の `test_dcre3.cfg` と異なり **`DEF_MPK` が必須**である。
dtq/pdq/mpf は管理領域を `malloc_mpk` から取るため、`DEF_MPK` が無いと
`mpk_valid` が偽で `malloc_mpk` が常に NULL を返し、`dtqcnt > 0` の `acre_dtq` が
**すべて E_NOMEM になる**。）

- [ ] **Step 4: `test/test_dcre4.c`**（著作権ヘッダは `test/test_dcre3.c` と同形式で付ける）

```c
/*
 *		動的生成API（acre_dtq/del_dtq・acre_pdq/del_pdq・acre_mpf/del_mpf）
 *		のテスト
 *
 * 【テストの目的】
 *
 *	(A) acre_dtq → psnd/prcv/fsnd の実通信（データ整合）→ del_dtq →
 *	    E_NOEXS ×9．dtqcnt==0 のデータキュー（管理領域なし）も覆う．
 *	(B) E_NOMEM の実証と E_NOID との順序：同じ過大な dtqcnt が
 *	    「スロットが空いていれば E_NOMEM」「枯渇していれば E_NOID」に
 *	    なることを実測し，E_NOMEM のとき free-list が減っていないことを
 *	    直後の acre_dtq ×2 の成功で示す．
 *	(C) 送信待ちの E_DLT（満杯 dtq に snd_dtq で待つ TASK2・同一プロセッサ）と
 *	    受信待ちの E_DLT（空 dtq に rcv_dtq で待つ TASK3・別プロセッサ）の両方．
 *	    del_dtq が swait_queue と rwait_queue の両方を解放することの実証．
 *	(D) 滞留データの破棄：データを入れたまま del → 再 acre で空であること．
 *	(E) pdq: acre → 優先度順配送 → maxdpri 検査（ロック内へ移した分岐）→
 *	    del → E_NOEXS ×8．VALID_DPRI による acre_pdq の E_PAR．
 *	(F) dtq のスロット枯渇 E_NOID／静的オブジェクトへの del が E_OBJ／
 *	    「空きが1個だけの状態で del → 再 acre」による決定形の同一 ID／
 *	    ★ユーザ供給 dtqmb（TA_MBALLOC を立てない経路）の生成・通信・削除．
 *	(G) mpf: acre（mpf=NULL 自動確保）→ pget/get/rel → ★ブロック内容の
 *	    書込み読出し → 未返却ブロックを持ったままの del → E_NOEXS ×6 →
 *	    ブロック領域が入らない blksz*blkcnt での E_NOMEM →
 *	    ★acre/del の反復（MPF_CYCLES 回）でプールが実際に返っていること．
 *
 * 【使用リソース】
 *
 *	TASK1: 中優先度タスク，TA_ACT属性（静的・PRC1）
 *	TASK2: 高優先度タスク，TA_NULL属性（静的・PRC1．送信待ちの E_DLT）
 *	TASK3: 高優先度タスク，TA_NULL属性（静的・PRC2．受信待ちの E_DLT）
 *	DTQ1/PDQ1/MPF1: 静的なデータキュー／優先度データキュー／固定長メモリプール
 *	AID_DTQ(2)/AID_PDQ(1)/AID_MPF(1): 動的スロット
 *	DEF_MPK({ MPK_SIZE, NULL }): 管理領域の確保元（必須）
 *
 * 【チェックポイント】
 *
 *	PRC1（check_count[0]，TASK1 と TASK2 が共有）: 1..12 + check_finish(13)
 *	  TASK1 が 1,2,3,5,7,8,9,10,11,12／TASK2 が 4,6
 *	PRC2（check_count[1]，TASK3）: 1,2（出力は "Check point 2-1/2-2 passed."）
 *	  ＝ログ中の "Check point" 行は合計 15 本（check_finish 自身の 1 本を含む）
 */

#include <kernel.h>
#include <t_syslog.h>
#include "syssvc/test_svc.h"
#include "kernel_cfg.h"
#include "test_dcre4.h"

/* DO NOT DELETE THIS LINE -- gentest depends on it. */

/*
 *  TASK2 / TASK3 が待つ動的データキューの ID（TASK1 が設定してから act_tsk する）
 */
static volatile ID		dlt_dtqid;
static volatile bool_t	prc2_done;

/*
 *  ユーザ供給のデータキュー管理領域
 *
 *  acre_dtq に dtqmb を渡す経路（TA_MBALLOC を立てない経路）の検査に使う．
 *  del_dtq がこれを free_mpk に渡してはならない．
 */
static DTQMB			user_dtqmb[USER_DTQCNT];

/*
 *  PRC1 上で送信待ちの E_DLT を受け取るタスク
 */
void
task2(EXINF exinf)
{
	check_point(4);
	/*  データキューが満杯なので送信待ちに入る．TASK1 の del_dtq で E_DLT．  */
	check_ercd(snd_dtq(dlt_dtqid, 0xB1), E_DLT);
	check_point(6);
	ext_tsk();
}

/*
 *  PRC2 上で受信待ちの E_DLT を受け取るタスク
 *
 *  PRC2 のチェックポイントカウンタは PRC1 と独立なので 1 から始まる．
 */
void
task3(EXINF exinf)
{
	intptr_t	data;

	check_point_prc(1, 2);
	/*  データキューが空なので受信待ちに入る．TASK1 の del_dtq で E_DLT．  */
	check_ercd(rcv_dtq(dlt_dtqid, &data), E_DLT);
	check_point_prc(2, 2);
	prc2_done = true;
	ext_tsk();
}

void
task1(EXINF exinf)
{
	T_CDTQ		cdtq;
	T_CPDQ		cpdq;
	T_CMPF		cmpf;
	T_RDTQ		rdtq;
	T_RPDQ		rpdq;
	T_RMPF		rmpf;
	ER_ID		erid;
	ID			dtqid1, dtqid2, pdqid1, mpfid1;
	intptr_t	data;
	PRI			datapri;
	void		*blk1;
	void		*blk2;
	uint_t		i;

	test_start(__FILE__);
	check_point(1);

	/*
	 *  1) acre_dtq → 実通信 → del_dtq → E_NOEXS ×9
	 */
	cdtq.dtqatr = TA_TPRI;
	cdtq.dtqcnt = 2U;
	cdtq.dtqmb = NULL;
	erid = acre_dtq(&cdtq);
	check_assert(erid > DTQ1);		/*  動的IDは静的レンジの外＝2レンジDTQIDの直接検証  */
	dtqid1 = (ID) erid;

	check_ercd(ref_dtq(dtqid1, &rdtq), E_OK);
	check_assert(rdtq.sdtqcnt == 0U);
	check_assert(rdtq.stskid == TSK_NONE);
	check_assert(rdtq.rtskid == TSK_NONE);
	check_ercd(psnd_dtq(dtqid1, 0x11), E_OK);
	check_ercd(psnd_dtq(dtqid1, 0x22), E_OK);
	check_ercd(psnd_dtq(dtqid1, 0x33), E_TMOUT);	/*  満杯（dtqcnt=2）  */
	check_ercd(ref_dtq(dtqid1, &rdtq), E_OK);
	check_assert(rdtq.sdtqcnt == 2U);
	check_ercd(prcv_dtq(dtqid1, &data), E_OK);
	check_assert(data == 0x11);						/*  FIFO 順で取り出される  */
	check_ercd(prcv_dtq(dtqid1, &data), E_OK);
	check_assert(data == 0x22);
	check_ercd(prcv_dtq(dtqid1, &data), E_TMOUT);	/*  空  */
	check_ercd(fsnd_dtq(dtqid1, 0x44), E_OK);
	check_ercd(prcv_dtq(dtqid1, &data), E_OK);
	check_assert(data == 0x44);
	check_ercd(ini_dtq(dtqid1), E_OK);
	check_ercd(del_dtq(dtqid1), E_OK);

	check_ercd(snd_dtq(dtqid1, 0x11), E_NOEXS);
	check_ercd(psnd_dtq(dtqid1, 0x11), E_NOEXS);
	check_ercd(tsnd_dtq(dtqid1, 0x11, TMO_POL), E_NOEXS);
	check_ercd(fsnd_dtq(dtqid1, 0x11), E_NOEXS);
	check_ercd(rcv_dtq(dtqid1, &data), E_NOEXS);
	check_ercd(prcv_dtq(dtqid1, &data), E_NOEXS);
	check_ercd(trcv_dtq(dtqid1, &data, TMO_POL), E_NOEXS);
	check_ercd(ini_dtq(dtqid1), E_NOEXS);
	check_ercd(ref_dtq(dtqid1, &rdtq), E_NOEXS);
	check_ercd(del_dtq(dtqid1), E_NOEXS);

	/*
	 *  dtqcnt == 0 のデータキュー（管理領域を持たない）
	 *
	 *  ★fsnd_dtq の E_ILUSE をロック内へ移した効果（訂正E）の直接検証：
	 *    生存中は E_ILUSE，削除後は（残留 dtqcnt が 0 でも）E_NOEXS になる．
	 *    ロック前の CHECK_ILUSE が残っていると削除後も E_ILUSE が返る．
	 */
	cdtq.dtqcnt = 0U;
	erid = acre_dtq(&cdtq);
	check_assert(erid > DTQ1);
	dtqid2 = (ID) erid;
	check_ercd(fsnd_dtq(dtqid2, 0x55), E_ILUSE);
	check_ercd(psnd_dtq(dtqid2, 0x55), E_TMOUT);	/*  容量0・受信待ち無し  */
	check_ercd(del_dtq(dtqid2), E_OK);
	check_ercd(fsnd_dtq(dtqid2, 0x55), E_NOEXS);	/*  ★E_ILUSE ではない  */
	check_point(2);

	/*
	 *  2) E_NOMEM の実証と E_NOID との順序
	 *
	 *  sizeof(DTQMB) は 4（32bit）または 8（64bit）なので
	 *  sizeof(DTQMB) * MPK_SIZE は MPK_SIZE の 4〜8 倍＝プールに入らない．
	 *  size_t の桁あふれも起きない大きさである（訂正G-3）．
	 */
	cdtq.dtqcnt = MPK_SIZE;
	check_assert(acre_dtq(&cdtq) == E_NOMEM);

	/*  E_NOMEM で free-list が減っていないこと＝直後に2個とも取れる  */
	cdtq.dtqcnt = 1U;
	erid = acre_dtq(&cdtq);
	check_assert(erid > DTQ1);
	dtqid1 = (ID) erid;
	erid = acre_dtq(&cdtq);
	check_assert(erid > DTQ1);
	dtqid2 = (ID) erid;
	check_assert(dtqid1 != dtqid2);

	/*  ★同じ過大な dtqcnt でも，枯渇していれば E_NOMEM ではなく E_NOID  */
	cdtq.dtqcnt = MPK_SIZE;
	check_assert(acre_dtq(&cdtq) == E_NOID);
	cdtq.dtqcnt = 1U;
	check_ercd(del_dtq(dtqid2), E_OK);
	check_ercd(del_dtq(dtqid1), E_OK);
	check_point(3);

	/*
	 *  3) 送信待ちの E_DLT（同一プロセッサ PRC1）
	 *
	 *  TASK2 は TASK1 より高優先度なので act_tsk で即座に走り，
	 *  満杯のデータキューへの snd_dtq で待ちに入ったところで TASK1 へ戻る．
	 */
	cdtq.dtqcnt = 1U;
	erid = acre_dtq(&cdtq);
	check_assert(erid > DTQ1);
	dtqid1 = (ID) erid;
	check_ercd(psnd_dtq(dtqid1, 0xA1), E_OK);	/*  満杯にする  */
	dlt_dtqid = dtqid1;

	check_ercd(act_tsk(TASK2), E_OK);			/*  → TASK2 が cp(4) を打って待つ  */
	check_point(5);
	check_ercd(ref_dtq(dtqid1, &rdtq), E_OK);
	check_assert(rdtq.stskid == TASK2);			/*  送信待ちタスクが実在する  */
	check_ercd(del_dtq(dtqid1), E_OK);			/*  → TASK2 が E_DLT で起き cp(6) → ext_tsk  */
	check_point(7);
	check_ercd(del_dtq(dtqid1), E_NOEXS);

	/*
	 *  4) 受信待ちの E_DLT（別プロセッサ PRC2）＝ init_wait_queue の MP 経路
	 *
	 *  TASK3 は PRC2 で並行に走るため，dly_tsk で待ちに入るのを待つ．
	 */
	erid = acre_dtq(&cdtq);
	check_assert(erid > DTQ1);
	dtqid1 = (ID) erid;
	dlt_dtqid = dtqid1;
	prc2_done = false;

	check_ercd(act_tsk(TASK3), E_OK);
	check_ercd(dly_tsk(TEST_TIME_PROC), E_OK);	/*  TASK3 が rcv_dtq に入るのを待つ  */
	check_ercd(ref_dtq(dtqid1, &rdtq), E_OK);
	check_assert(rdtq.rtskid == TASK3);			/*  受信待ちタスクが実在する  */
	check_ercd(del_dtq(dtqid1), E_OK);
	check_ercd(dly_tsk(TEST_TIME_PROC), E_OK);	/*  TASK3 が cp(2,2) を打つのを待つ  */
	check_assert(prc2_done);
	check_point(8);

	/*
	 *  5) 滞留データの破棄
	 */
	cdtq.dtqcnt = 2U;
	erid = acre_dtq(&cdtq);
	check_assert(erid > DTQ1);
	dtqid1 = (ID) erid;
	check_ercd(psnd_dtq(dtqid1, 0xD1), E_OK);
	check_ercd(psnd_dtq(dtqid1, 0xD2), E_OK);
	check_ercd(ref_dtq(dtqid1, &rdtq), E_OK);
	check_assert(rdtq.sdtqcnt == 2U);
	check_ercd(del_dtq(dtqid1), E_OK);			/*  滞留データごと削除  */

	erid = acre_dtq(&cdtq);
	check_assert(erid > DTQ1);
	dtqid2 = (ID) erid;
	check_ercd(ref_dtq(dtqid2, &rdtq), E_OK);
	check_assert(rdtq.sdtqcnt == 0U);			/*  ★滞留データは破棄されている  */
	check_ercd(prcv_dtq(dtqid2, &data), E_TMOUT);
	check_ercd(del_dtq(dtqid2), E_OK);
	check_point(9);

	/*
	 *  6) pdq: 優先度順配送・maxdpri 検査・del → E_NOEXS ×8
	 */
	cpdq.pdqatr = TA_TPRI;
	cpdq.pdqcnt = 3U;
	cpdq.maxdpri = 3;
	cpdq.pdqmb = NULL;
	erid = acre_pdq(&cpdq);
	check_assert(erid > PDQ1);					/*  2レンジPDQIDの直接検証  */
	pdqid1 = (ID) erid;

	check_ercd(psnd_pdq(pdqid1, 0x30, 3), E_OK);
	check_ercd(psnd_pdq(pdqid1, 0x10, 1), E_OK);
	check_ercd(psnd_pdq(pdqid1, 0x20, 2), E_OK);
	check_ercd(ref_pdq(pdqid1, &rpdq), E_OK);
	check_assert(rpdq.spdqcnt == 3U);
	check_ercd(prcv_pdq(pdqid1, &data, &datapri), E_OK);
	check_assert(data == 0x10 && datapri == 1);	/*  ★優先度順に配送される  */
	check_ercd(prcv_pdq(pdqid1, &data, &datapri), E_OK);
	check_assert(data == 0x20 && datapri == 2);
	check_ercd(prcv_pdq(pdqid1, &data, &datapri), E_OK);
	check_assert(data == 0x30 && datapri == 3);
	check_ercd(prcv_pdq(pdqid1, &data, &datapri), E_TMOUT);

	check_ercd(psnd_pdq(pdqid1, 0x40, 4), E_PAR);	/*  maxdpri=3 を超える（ロック内の検査）  */
	check_ercd(psnd_pdq(pdqid1, 0x40, 0), E_PAR);	/*  TMIN_DPRI 未満（ロック前の検査）  */
	check_ercd(ini_pdq(pdqid1), E_OK);

	check_assert(acre_pdq(&cpdq) == E_NOID);	/*  AID_PDQ(1) を使い切っている  */
	check_ercd(del_pdq(PDQ1), E_OBJ);			/*  静的生成オブジェクト  */
	check_ercd(del_pdq(pdqid1), E_OK);

	check_ercd(snd_pdq(pdqid1, 0x10, 1), E_NOEXS);
	check_ercd(psnd_pdq(pdqid1, 0x10, 1), E_NOEXS);
	check_ercd(tsnd_pdq(pdqid1, 0x10, 1, TMO_POL), E_NOEXS);
	check_ercd(rcv_pdq(pdqid1, &data, &datapri), E_NOEXS);
	check_ercd(prcv_pdq(pdqid1, &data, &datapri), E_NOEXS);
	check_ercd(trcv_pdq(pdqid1, &data, &datapri, TMO_POL), E_NOEXS);
	check_ercd(ini_pdq(pdqid1), E_NOEXS);
	check_ercd(ref_pdq(pdqid1, &rpdq), E_NOEXS);
	check_ercd(del_pdq(pdqid1), E_NOEXS);
	/*
	 *  ★maxdpri 検査をロック内へ移した効果（訂正E）の直接検証：
	 *    削除済みスロットの残留 maxdpri（=3）を超える datapri でも
	 *    E_PAR ではなく E_NOEXS が返る．
	 */
	check_ercd(psnd_pdq(pdqid1, 0x40, 4), E_NOEXS);

	cpdq.pdqcnt = MPK_SIZE;
	check_assert(acre_pdq(&cpdq) == E_NOMEM);
	cpdq.pdqcnt = 3U;
	cpdq.maxdpri = TMAX_DPRI + 1;				/*  VALID_DPRI を破る（上限外）  */
	check_assert(acre_pdq(&cpdq) == E_PAR);
	cpdq.maxdpri = TMIN_DPRI - 1;				/*  VALID_DPRI を破る（下限外）  */
	check_assert(acre_pdq(&cpdq) == E_PAR);
	cpdq.maxdpri = 3;
	cpdq.pdqatr = TA_TPRI | 0x04U;				/*  未定義ビット → E_RSATR  */
	check_assert(acre_pdq(&cpdq) == E_RSATR);
	cpdq.pdqatr = TA_TPRI;

	erid = acre_pdq(&cpdq);
	check_assert(erid == pdqid1);				/*  空きが1個だけ＝決定形  */
	check_ercd(del_pdq(pdqid1), E_OK);
	check_point(10);

	/*
	 *  7) dtq の枯渇 E_NOID／静的 E_OBJ／決定形の再 acre／ユーザ供給 dtqmb
	 */
	cdtq.dtqatr = TA_TPRI;
	cdtq.dtqcnt = 1U;
	cdtq.dtqmb = NULL;
	erid = acre_dtq(&cdtq);
	check_assert(erid > DTQ1);
	dtqid1 = (ID) erid;
	erid = acre_dtq(&cdtq);
	check_assert(erid > DTQ1);
	dtqid2 = (ID) erid;
	check_assert(dtqid1 != dtqid2);
	check_assert(acre_dtq(&cdtq) == E_NOID);	/*  スロット2個を使い切った  */

	check_ercd(del_dtq(DTQ1), E_OBJ);			/*  静的生成オブジェクト  */

	check_ercd(del_dtq(dtqid2), E_OK);			/*  空きが1個だけの状態を作る  */
	erid = acre_dtq(&cdtq);
	check_assert(erid == dtqid2);				/*  FIFO/LIFO 不問で決定的  */
	check_ercd(del_dtq(dtqid2), E_OK);
	check_ercd(del_dtq(dtqid1), E_OK);

	cdtq.dtqatr = TA_TPRI | 0x04U;				/*  未定義ビット → E_RSATR  */
	check_assert(acre_dtq(&cdtq) == E_RSATR);
	cdtq.dtqatr = TA_TPRI;

	/*
	 *  ★ユーザ供給の管理領域（TA_MBALLOC を立てない経路）
	 *
	 *  del_dtq がこの静的配列を free_mpk に渡してはならない．渡すと
	 *  プールの count が壊れ，以後の acre がプールを使い切って
	 *  E_NOMEM になる（手順8のループが検出する）．
	 */
	cdtq.dtqcnt = USER_DTQCNT;
	cdtq.dtqmb = (void *) user_dtqmb;
	erid = acre_dtq(&cdtq);
	check_assert(erid > DTQ1);
	dtqid1 = (ID) erid;
	check_ercd(psnd_dtq(dtqid1, 0xE1), E_OK);
	check_ercd(psnd_dtq(dtqid1, 0xE2), E_OK);
	check_ercd(prcv_dtq(dtqid1, &data), E_OK);
	check_assert(data == 0xE1);
	check_ercd(prcv_dtq(dtqid1, &data), E_OK);
	check_assert(data == 0xE2);
	check_ercd(del_dtq(dtqid1), E_OK);
	cdtq.dtqmb = NULL;
	check_point(11);

	/*
	 *  8) mpf: 自動確保・ブロックの読み書き・削除・E_NOEXS ×6・
	 *     E_NOMEM・プール解放の反復実証
	 */
	cmpf.mpfatr = TA_TPRI;
	cmpf.blkcnt = MPF_BLKCNT;
	cmpf.blksz = MPF_BLKSZ;
	cmpf.mpf = NULL;							/*  ブロック領域を自動確保  */
	cmpf.mpfmb = NULL;							/*  管理領域を自動確保  */
	erid = acre_mpf(&cmpf);
	check_assert(erid > MPF1);					/*  2レンジMPFIDの直接検証  */
	mpfid1 = (ID) erid;

	check_ercd(ref_mpf(mpfid1, &rmpf), E_OK);
	check_assert(rmpf.fblkcnt == MPF_BLKCNT);
	check_assert(rmpf.wtskid == TSK_NONE);
	check_ercd(pget_mpf(mpfid1, &blk1), E_OK);
	check_ercd(pget_mpf(mpfid1, &blk2), E_OK);
	check_assert(blk1 != blk2);

	/*  ★確保したブロック領域が本当に読み書きできること  */
	for (i = 0U; i < MPF_BLKSZ; i++) {
		((char *) blk1)[i] = (char)(i + 1U);
		((char *) blk2)[i] = (char)(0x80U - i);
	}
	for (i = 0U; i < MPF_BLKSZ; i++) {
		check_assert(((char *) blk1)[i] == (char)(i + 1U));
		check_assert(((char *) blk2)[i] == (char)(0x80U - i));
	}

	check_ercd(ref_mpf(mpfid1, &rmpf), E_OK);
	check_assert(rmpf.fblkcnt == MPF_BLKCNT - 2);
	check_ercd(rel_mpf(mpfid1, blk2), E_OK);
	check_ercd(rel_mpf(mpfid1, blk1), E_OK);
	check_ercd(ref_mpf(mpfid1, &rmpf), E_OK);
	check_assert(rmpf.fblkcnt == MPF_BLKCNT);
	check_ercd(get_mpf(mpfid1, &blk1), E_OK);	/*  空きがあるので待たない  */
	check_ercd(rel_mpf(mpfid1, blk1), E_OK);
	check_ercd(ini_mpf(mpfid1), E_OK);

	check_assert(acre_mpf(&cmpf) == E_NOID);	/*  AID_MPF(1) を使い切っている  */
	check_ercd(del_mpf(MPF1), E_OBJ);			/*  静的生成オブジェクト  */

	check_ercd(pget_mpf(mpfid1, &blk1), E_OK);	/*  未返却のブロックを持ったまま削除する  */
	check_ercd(del_mpf(mpfid1), E_OK);

	check_ercd(get_mpf(mpfid1, &blk2), E_NOEXS);
	check_ercd(pget_mpf(mpfid1, &blk2), E_NOEXS);
	check_ercd(tget_mpf(mpfid1, &blk2, TMO_POL), E_NOEXS);
	/*
	 *  ★rel_mpf の検査をロック内へ移した効果（訂正E）の直接検証：
	 *    削除済みプールでは p_mpfinib->mpf も p_mpfmb も free_mpk 済みの
	 *    番地である．ロック前に p_mpfmb[blkidx] を読む実装では未定義動作
	 *    になるが，正しい実装は何も触らずに E_NOEXS を返す．
	 */
	check_ercd(rel_mpf(mpfid1, blk1), E_NOEXS);
	check_ercd(ini_mpf(mpfid1), E_NOEXS);
	check_ercd(ref_mpf(mpfid1, &rmpf), E_NOEXS);
	check_ercd(del_mpf(mpfid1), E_NOEXS);

	/*  ブロック領域が入らない大きさ → E_NOMEM  */
	cmpf.blkcnt = MPK_SIZE;
	cmpf.blksz = 32U;
	check_assert(acre_mpf(&cmpf) == E_NOMEM);

	/*  E_NOMEM で free-list が減っていないこと＝同じスロットが取れる  */
	cmpf.blkcnt = MPF_BLKCNT;
	cmpf.blksz = MPF_BLKSZ;
	erid = acre_mpf(&cmpf);
	check_assert(erid == mpfid1);
	check_ercd(del_mpf(mpfid1), E_OK);

	cmpf.blkcnt = 0U;							/*  blkcnt != 0 を破る  */
	check_assert(acre_mpf(&cmpf) == E_PAR);
	cmpf.blkcnt = MPF_BLKCNT;
	cmpf.blksz = 0U;							/*  blksz != 0 を破る  */
	check_assert(acre_mpf(&cmpf) == E_PAR);
	cmpf.blksz = MPF_BLKSZ;
	cmpf.mpfatr = TA_TPRI | 0x04U;				/*  未定義ビット → E_RSATR  */
	check_assert(acre_mpf(&cmpf) == E_RSATR);
	cmpf.mpfatr = TA_TPRI;

	/*
	 *  ★プールが実際に返っていることの実証
	 *
	 *  del_mpf が TA_MEMALLOC のブロック領域を free_mpk しなければ，
	 *  bump allocator の brk が周回ごとに進み（count が 0 に戻らない），
	 *  MPK_SIZE を数周で使い切って acre_mpf が E_NOMEM になる．
	 *  正常な実装では1周ごとに count が 0 に戻り brk がリセットされる
	 *  ので，MPF_CYCLES 周まわしても消費は頭打ちである．
	 */
	for (i = 0U; i < MPF_CYCLES; i++) {
		erid = acre_mpf(&cmpf);
		check_assert(erid > MPF1);
		mpfid1 = (ID) erid;
		check_ercd(pget_mpf(mpfid1, &blk1), E_OK);
		check_ercd(rel_mpf(mpfid1, blk1), E_OK);
		check_ercd(del_mpf(mpfid1), E_OK);
	}
	check_point(12);

	check_finish(13);
}
```

**注意（実装者へ）:**
- `MPF_BLKSZ` は `uint_t` との比較でループを回すので、`i` は `uint_t` にしてある。
  `((char *) blk1)[i]` の添字は `uint_t` でよい。
- `check_assert(data == 0x10 && datapri == 1)` のように `&&` を1行に書くと
  失敗時にどちらが原因か分からない。**既存テストの流儀に合わせて分けてもよい**
  （`test/test_pdq1.c` を見て決め、決めた事実を記録する）。
- `TSK_NONE`（`ref_dtq`/`ref_mpf` の `stskid`/`rtskid`/`wtskid` が待ちタスク無しのとき返す値）が
  `include/kernel.h:604` に存在することを Step 1 で確認済み。
- `E_RSATR` の検査は `CHECK_VALIDATR` が使う属性ビットに依存する。
  ターゲット固有の追加属性ビットがある場合は使うビットを変える
  （`grep -rn "TARGET_DTQATR\|TARGET_PDQATR\|TARGET_MPFATR" kernel/ include/ target/`）。
- `dly_tsk(TEST_TIME_PROC)` は 200ms。QEMU で PRC2 のタスクが起動して待ちに入るのに十分だが、
  ホストが遅い場合は `TEST_TIME_PROC` を大きくする（回数を固定していないので値を増やしても壊れない）。
- **チェックポイント番号は PRC1 が 1..12 + `check_finish(13)`、PRC2 が `(1,2)`・`(2,2)` の2個。
  各プロセッサ内で単調増加であることを QEMU 出力で確認する。**
- ★**`user_dtqmb` は `static DTQMB user_dtqmb[USER_DTQCNT];`** である。`DTQMB` 型は
  `kernel/dataqueue.h` にあり、アプリからは `<kernel.h>` 経由では**見えない可能性がある**。
  Step 6 でコンパイルエラーになったら **`static intptr_t user_dtqmb_area[USER_DTQCNT];` を
  用意して `cdtq.dtqmb = (void *) user_dtqmb_area;` とする**（`DTQMB` は
  `intptr_t data;` 1個の構造体なのでサイズ・アラインとも等価）。
  どちらにしたかを記録すること。

- [ ] **Step 5: 登録**
  - `test/MANIFEST` の `test_dcre3.h`（`:46` 付近）の直後に、アルファベット順で
    `test_dcre4.c` `test_dcre4.cfg` `test_dcre4.h` の3行
    （`test_dcre3` < `test_dcre4` < `test_dcre_mix` の並び）。
  - `test/testexec.rb` の `"dcre3"    => { SRC: "test_dcre3" },`（`:92` 付近）の直後に
    `"dcre4"    => { SRC: "test_dcre4" },`。

- [ ] **Step 6: ビルド・実行**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
cmake --preset musca_b1-2core -B build/musca_b1-2core-tdcre4 \
  -DFMP3_APPLNAME=test_dcre4 -DFMP3_APPLDIR=test \
  -DFMP3_SYSSVC_TARGET_C_FILES=$PWD/syssvc/test_svc.c > /tmp/dcre4-t6-conf.log 2>&1
echo "conf rc=$?"
cmake --build build/musca_b1-2core-tdcre4 > /tmp/dcre4-t6-build.log 2>&1; echo "build rc=$?"
tools/cfg_equivalence.sh build/musca_b1-2core-tdcre4 > /tmp/dcre4-t6-eq.log 2>&1; echo "eq rc=$?"
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 90 cmake --build build/musca_b1-2core-tdcre4 --target run \
  > /tmp/dcre4-t6-run.log 2>&1
grep 'TTSP_RESULT: PASS' /tmp/dcre4-t6-run.log
grep -c 'Check point' /tmp/dcre4-t6-run.log
grep 'Check point 2-1 passed\|Check point 2-2 passed' /tmp/dcre4-t6-run.log
grep -n 'Unexpected\|Assertion failed\|## ' /tmp/dcre4-t6-run.log | head
pgrep -a qemu
```
期待:
- build rc=0、eq rc=0（**AID_DTQ/AID_PDQ/AID_MPF 有りの実構成で両エンジンがバイト一致**）。
- `TTSP_RESULT: PASS` が実在（rc は見ない — Constraint 12）。
- **`grep -c 'Check point'` が 15**（PRC1 の 1..12 の12本 + `check_finish(13)` 自身の1本 +
  PRC2 の `2-1`/`2-2` の2本）。**13 でも 14 でもない。**
- `Check point 2-1 passed.` と `Check point 2-2 passed.` が**両方**出る
  （`1-2`/`2-2` ではない＝出力形式は `prcid-count`）。
- `Unexpected` / `Assertion failed` / `## ` の行が**出ない**。
- `pgrep` の出力なし。

★`timeout` は 90 秒。本テストは `dly_tsk(200ms)` を2回使うだけなので実時間は短いが、
QEMU の起動と `-icount` 無しの実行揺らぎに余裕を持たせる。
★行数が 15 にならなかったら、**まず自分の数え方ではなくログを読む**
（`grep -n 'Check point' /tmp/dcre4-t6-run.log` で実際の並びを見る）。
段階3a では見積りを3回続けて外している。**実測を正とし、計画の期待値のほうを直して記録する。**

- [ ] **Step 7: ★カーネル変異 negative control 1（del_dtq の free-list 返却）**

`kernel/dataqueue.c` の `del_dtq` の
`queue_insert_prev(&free_dtqcb, &(p_dtqcb->swait_queue));` を
**一時的にコメントアウト**して再ビルド・再実行する。

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
# 変異を入れてから：
cmake --build build/musca_b1-2core-tdcre4 > /dev/null 2>&1
timeout -k 5 90 cmake --build build/musca_b1-2core-tdcre4 --target run \
  > /tmp/dcre4-t6-neg1.log 2>&1
grep -c 'TTSP_RESULT: PASS' /tmp/dcre4-t6-neg1.log   # 期待: 0
grep -n 'Assertion failed\|Unexpected\|## ' /tmp/dcre4-t6-neg1.log | head
pgrep -a qemu
```
期待: **PASS 行が出ない**こと。
機序：`AID_DTQ(2)` なので、手順1の `del_dtq` で1個目が返却されず、
`dtqcnt == 0` のデータキューの `acre_dtq` が2個目を取り、その `del_dtq` でも返却されないため、
**手順2の1本目の `acre_dtq` が `E_NOID` を返し `check_assert(erid > DTQ1)` が失敗する**
（`## Assertion failed` 相当の行が出る）＝`del_dtq` の free-list 返却が**生きた経路**である証拠。
（★実際に失敗する行番号は実測で記録すること。予測と一致しなくても、
**PASS が出ないこと + 失敗が「free-list 枯渇に起因する assert」であること**が確認できればよい。）

変異を**復元**し、Step 6 を再実行して `TTSP_RESULT: PASS` に戻ることまで確認する。
**`TNUM_PRCID == 1` でしか通らない死んだ分岐への変異は不可**
（段階1 で2度やらかした型。本変異は 2 コア構成で必ず通る経路である）。

- [ ] **Step 8: ★★カーネル変異 negative control 2（del_mpf の TA_MEMALLOC 解放）**

段階3b の**新規性そのもの**（管理領域／ブロック領域の解放）が生きていることを実演する。
`kernel/mempfix.c` の `del_mpf` の

```c
		if ((p_mpfinib->mpfatr & TA_MEMALLOC) != 0U) {
			free_mpk(p_mpfinib->mpf);
		}
```

を**一時的にコメントアウト**して：

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
cmake --build build/musca_b1-2core-tdcre4 > /dev/null 2>&1
timeout -k 5 90 cmake --build build/musca_b1-2core-tdcre4 --target run \
  > /tmp/dcre4-t6-neg2.log 2>&1
grep -c 'TTSP_RESULT: PASS' /tmp/dcre4-t6-neg2.log   # 期待: 0
grep -n 'Assertion failed\|Check point 11 passed\|Check point 12 passed' /tmp/dcre4-t6-neg2.log
pgrep -a qemu
```
期待: **PASS 行が出ない**こと。**`Check point 11 passed.` は出るが
`Check point 12 passed.` は出ない**（手順8の反復ループの途中で
`acre_mpf` が `E_NOMEM` を返し `check_assert(erid > MPF1)` が失敗する）。
機序：`free_mpk` されないブロック領域（`ROUND_MPF_T(64) * 4 = 256` バイト）が
毎周プールに積み上がり、`count` が 0 に戻らないので `brk` がリセットされない
（訂正G）。`MPK_SIZE = 2048` は 7〜8 周で尽きる。
変異を**復元**し、`TTSP_RESULT: PASS` に戻ることまで確認する。

★**この2つ目の control は省略しないこと。** 段階3b で新しく入る機構は
「管理領域の確保と解放」であり、テストが通っていても
「`free_mpk` の行が実は死んでいる（＝プールを食い潰しているだけ）」可能性を潰す必要がある。
★もし変異を入れても PASS してしまったら、**`MPK_SIZE` が大きすぎるか `MPF_CYCLES` が
少なすぎる**。`MPF_CYCLES` を増やす（`MPK_SIZE` は下げない — 正常系が入らなくなる）。
調整した事実と最終値を記録する。

- [ ] **Step 9: 非退行 — test_dcre1 / test_dcre2 / test_dcre3 / test_int2 の再実行**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
for t in dcre1 dcre2 dcre3 int2; do
  d=build/musca_b1-2core-t$t
  [ -d $d ] || cmake --preset musca_b1-2core -B $d \
      -DFMP3_APPLNAME=test_$t -DFMP3_APPLDIR=test \
      -DFMP3_SYSSVC_TARGET_C_FILES=$PWD/syssvc/test_svc.c > /tmp/dcre4-t6-$t-conf.log 2>&1
  cmake --build $d > /tmp/dcre4-t6-$t-build.log 2>&1; echo "$t build rc=$?"
done
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 60 cmake --build build/musca_b1-2core-tdcre1 --target run > /tmp/dcre4-t6-d1-run.log 2>&1
grep 'TTSP_RESULT: PASS' /tmp/dcre4-t6-d1-run.log ; pgrep -a qemu
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 60 cmake --build build/musca_b1-2core-tdcre2 --target run > /tmp/dcre4-t6-d2-run.log 2>&1
grep 'TTSP_RESULT: PASS' /tmp/dcre4-t6-d2-run.log ; pgrep -a qemu
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 90 cmake --build build/musca_b1-2core-tdcre3 --target run > /tmp/dcre4-t6-d3-run.log 2>&1
grep 'TTSP_RESULT: PASS' /tmp/dcre4-t6-d3-run.log
grep -c 'Check point' /tmp/dcre4-t6-d3-run.log       # 期待: 14（段階3a Task 8 の実測値）
pgrep -a qemu
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 60 cmake --build build/musca_b1-2core-tint2 --target run > /tmp/dcre4-t6-i2-run.log 2>&1
grep 'TTSP_RESULT: PASS' /tmp/dcre4-t6-i2-run.log ; pgrep -a qemu
```
期待: 4本とも `TTSP_RESULT: PASS` が実在。`test_dcre3` の `Check point` 行数は **14**
（段階3a Task 8 の実測値。PRC1 1..11 + `check_finish(12)` 自身 = 12本、PRC2 2本）。

- [ ] **Step 10: 台帳とコミット**

`DIVERGENCE_MAP.md` に `test/test_dcre4.c` `test/test_dcre4.cfg` `test/test_dcre4.h`
（種別 `add (dcre-port)`、理由「動的生成 dtq/pdq/mpf の QEMU 回帰テスト。musca_b1-2core・
8シナリオ・変異 control 2件。`DEF_MPK` が必須（管理領域を `malloc_mpk` から取るため）」）と、
`test/MANIFEST` `test/testexec.rb` の既存行への追記（`test_dcre4` 追加）を行って：

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
git add -A && git commit -m "test(dcre): 動的生成dtq/pdq/mpfの回帰テスト test_dcre4 を追加（2コアQEMU・E_NOMEM順序と両側E_DLT・滞留データ破棄・プール解放の反復実証）"
```

---
### Task 7: 最終回帰と台帳整理

**推奨モデル:** 中位（sonnet）

**Files:**
- Modify: `DIVERGENCE_MAP.md`（掃除・上流報告候補 d の拡張）・`.superpowers/sdd/progress.md`（記録）

**このタスクではコードを直さない。** 欠陥を見つけたら**記録して報告**し、修正は別コミットに切る。

- [ ] **Step 1: 全9プリセット configure+build**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
for p in musca_b1 musca_b1-2core rp2350_pico2 polarfire_soc_kit polarfire_soc_kit-qemu \
         kria_arm64 kria_arm64-1core kria_r5 kria_r5-2core; do
  cmake --preset $p > /tmp/dcre4-t7-conf-$p.log 2>&1; c=$?
  cmake --build build/$p > /tmp/dcre4-t7-build-$p.log 2>&1; b=$?
  echo "$p conf=$c build=$b"
done
```
期待: `polarfire_soc_kit`（実機プリセット）のみ SoftConsole ツールチェーン不在
（`fatal error: cannot read spec file 'nano.specs'`）で**既知の環境ギャップとして fail**。
それ**以外の8構成が exit=0**。

- [ ] **Step 2: 全8構成 + 派生3ビルドの `tools/cfg_equivalence.sh`（計11件）**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
for p in musca_b1 musca_b1-2core rp2350_pico2 polarfire_soc_kit-qemu \
         kria_arm64 kria_arm64-1core kria_r5 kria_r5-2core; do
  tools/cfg_equivalence.sh build/$p > /tmp/dcre4-t7-eq-$p.log 2>&1
  echo "$p eq=$?"
done
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
for d in musca_b1-2core-tmix kria_arm64-tmix musca_b1-2core-tdcre4; do
  tools/cfg_equivalence.sh build/$d > /tmp/dcre4-t7-eq-$d.log 2>&1
  echo "$d eq=$?"
done
```
期待: 全11件 0。**2 は不合格**（Constraint 14）。
- `-tmix`（32bit / 64bit）は **7家族の AID が混在する構成**（tsk/cyc/sem/mtx/dtq/pdq/mpf は
  書き、alm/flg は書かない）、
- `-tdcre4` は **AID_DTQ/AID_PDQ/AID_MPF + DEF_MPK の全部入り構成**
なので、この3つが段階3b の cfg 変更を実構成で検査している本体である。
★段階3a の派生ビルド `musca_b1-2core-tdcre3` も残っていれば併せて eq を取る
（残っていなければ Step 5 で作り直す）。

- [ ] **Step 3: QEMU 起動7構成（★プリセットごとに個別実行）**

段階1 Task 7 では `for` ループで全構成を1コマンドに詰めた結果、Bash ツールの
2分タイムアウトに当たり **qemu が孤児化した**。**1プリセット1コマンド**で実行し、
毎回 `pgrep` で残存を確認する。

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 25 cmake --build build/polarfire_soc_kit-qemu --target run > /tmp/dcre4-t7-run-polarfire.log 2>&1
grep -c 'Processor [1-4] start\.' /tmp/dcre4-t7-run-polarfire.log   # 期待: 4
pgrep -a qemu
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 25 cmake --build build/musca_b1 --target run > /tmp/dcre4-t7-run-musca1.log 2>&1
grep -c 'Processor 1 start\.' /tmp/dcre4-t7-run-musca1.log          # 期待: 1
pgrep -a qemu
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 25 cmake --build build/musca_b1-2core --target run > /tmp/dcre4-t7-run-musca2.log 2>&1
grep -c 'Processor [12] start\.' /tmp/dcre4-t7-run-musca2.log       # 期待: 2
pgrep -a qemu
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 25 cmake --build build/kria_arm64-1core --target run > /tmp/dcre4-t7-run-arm64-1.log 2>&1
grep -c 'Processor 1 start\.' /tmp/dcre4-t7-run-arm64-1.log         # 期待: 1
pgrep -a qemu
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 25 cmake --build build/kria_arm64 --target run > /tmp/dcre4-t7-run-arm64-4.log 2>&1
grep -c 'Processor [1-4] start\.' /tmp/dcre4-t7-run-arm64-4.log     # 期待: 4
pgrep -a qemu
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 25 cmake --build build/kria_r5 --target run > /tmp/dcre4-t7-run-r5-1.log 2>&1
grep -c 'Processor 1 start\.' /tmp/dcre4-t7-run-r5-1.log            # 期待: 1
pgrep -a qemu
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 25 cmake --build build/kria_r5-2core --target run > /tmp/dcre4-t7-run-r5-2.log 2>&1
grep -c 'Processor [12] start\.' /tmp/dcre4-t7-run-r5-2.log         # 期待: 2
pgrep -a qemu
```
（`rp2350_pico2` は QEMU にマシンモデルが無く `run` ターゲット自体が無い＝設計どおり。
`kria_r5-2core` は既知どおり rc=124 になるが、**両 Processor start 行が出ていれば合格**
＝rc 単独で判定しない（Constraint 12）。）
**各コマンドの後に `pgrep -a qemu` が何も出さないこと。** 出たら
`pkill -f qemu-system` で掃除し、その事実を記録する。
★段階3a Task 8 では直後に一瞬 `<defunct>` が見えることがあった（reap-lag）。
3秒後に再確認して消えていれば孤児化ではない。**そう判断した根拠を記録する。**

- [ ] **Step 4: 機能テスト6本の再実行（QEMU 5本 + build のみ 1本）**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 90 cmake --build build/musca_b1-2core-tdcre4 --target run > /tmp/dcre4-t7-tdcre4.log 2>&1
grep 'TTSP_RESULT: PASS' /tmp/dcre4-t7-tdcre4.log
grep -c 'Check point' /tmp/dcre4-t7-tdcre4.log      # 期待: 15
grep 'Check point 2-1 passed\|Check point 2-2 passed' /tmp/dcre4-t7-tdcre4.log
pgrep -a qemu
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 90 cmake --build build/musca_b1-2core-tdcre3 --target run > /tmp/dcre4-t7-tdcre3.log 2>&1
grep 'TTSP_RESULT: PASS' /tmp/dcre4-t7-tdcre3.log
grep -c 'Check point' /tmp/dcre4-t7-tdcre3.log      # 期待: 14
pgrep -a qemu
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 60 cmake --build build/musca_b1-2core-tdcre2 --target run > /tmp/dcre4-t7-tdcre2.log 2>&1
grep 'TTSP_RESULT: PASS' /tmp/dcre4-t7-tdcre2.log ; pgrep -a qemu
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 60 cmake --build build/musca_b1-2core-tdcre1 --target run > /tmp/dcre4-t7-tdcre1.log 2>&1
grep 'TTSP_RESULT: PASS' /tmp/dcre4-t7-tdcre1.log ; pgrep -a qemu
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 60 cmake --build build/musca_b1-2core-tint2 --target run > /tmp/dcre4-t7-tint2.log 2>&1
grep 'TTSP_RESULT: PASS' /tmp/dcre4-t7-tint2.log ; pgrep -a qemu
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
cmake --build build/musca_b1-2core-tmix > /tmp/dcre4-t7-tmix-build.log 2>&1; echo "tmix build rc=$?"
```
期待: QEMU 5本とも `TTSP_RESULT: PASS` が実在。
`test_dcre4` は `Check point` **15行**、`test_dcre3` は **14行**。
`test_dcre_mix` は自身の DIVERGENCE_MAP 記載どおり **build + equivalence のみ**（QEMU 実行は不要）。
★ビルドディレクトリが無いものは Task 6 Step 9 のコマンドで作る。

- [ ] **Step 5: エラー経路回帰マトリクス（既存21件 + 段階3b 新規6件 = 27件）**

★**`run.sh` は4引数形**（`<builddir> <cfg> <期待ercd> [EXTRA_CFLAGS]`）。
`#include "test_int2.h"` を含む cfg は**第4引数が必須**（付けないと rc=2）。
**本計画では全件に引数を明記する。**

まず実在するファイルと突き合わせる（★下の列と食い違ったら、実在するものに合わせ、
**合わせた事実を記録する**）：

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
ls tools/cfg_error_tests/*.cfg | sort | tee /tmp/dcre4-t7-cfgs.txt
grep -c . /tmp/dcre4-t7-cfgs.txt      # 期待: 27（既存21 + 新規6）
```

**既存21件**（段階1/2/3a 分。段階3a Task 8 で全数が確定している）：

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
R=tools/cfg_error_tests/run.sh
T=tools/cfg_error_tests
X="-I$PWD/test"
M=build/musca_b1-2core
$R build/polarfire_soc_kit-qemu $T/e_par_creisr_intno_keyerror.cfg   E_PAR;         echo "01:$?"
$R build/polarfire_soc_kit-qemu $T/e_rsatr_inhno_affinity.cfg        E_RSATR;       echo "02:$?"
$R $M                           $T/musca_b1_e_rsatr_intno_affinity.cfg E_RSATR;     echo "03:$?"
$R build/kria_r5-2core          $T/kria_r5_e_rsatr_intno_affinity.cfg  E_RSATR;     echo "04:$?"
$R build/rp2350_pico2           $T/rp2350_e_rsatr_intno_affinity.cfg   E_PAR;       echo "05:$?"
```
★05 は**期待 ercd が E_PAR** である（ファイル名は `e_rsatr` だが、`build/rp2350_pico2` は
既定1コア構成のため `TNUM_PRCID >= 2` の符号化 intno は `INTNO_VALID[2]` 自体が存在せず
E_PAR になる）。cfg 冒頭のコメントが自己予告しており、段階3a Task 8 で実測確認済み。

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
R=tools/cfg_error_tests/run.sh
T=tools/cfg_error_tests
M=build/musca_b1-2core
$R $M $T/musca_b1_clsid_warning.cfg  CLS_ALL_PRC2  "-DOMIT_MULTIPRC_INTERRUPT"; echo "06:$?"
```
★06 は**期待文字列が ercd ではなく `CLS_ALL_PRC2`** で、`EXTRA_CFLAGS` が
`-DOMIT_MULTIPRC_INTERRUPT` である（段階3a Task 8 の実測）。

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
R=tools/cfg_error_tests/run.sh
T=tools/cfg_error_tests
X="-I$PWD/test"
M=build/musca_b1-2core
$R $M $T/dcre_aid_in_class.cfg          E_RSATR "$X"; echo "07:$?"
$R $M $T/dcre_mpk_in_class.cfg          E_RSATR "$X"; echo "08:$?"
$R $M $T/dcre_mpk_zero.cfg              E_PAR   "$X"; echo "09:$?"
$R $M $T/dcre_mpk_double.cfg            E_OBJ   "$X"; echo "10:$?"
$R $M $T/dcre_mpk_misaligned.cfg        E_PAR   "$X"; echo "11:$?"
$R $M $T/dcre_aid_tsk_no_static.cfg     E_OBJ   "$X"; echo "12:$?"
$R $M $T/dcre_aid_cyc_in_class.cfg      E_RSATR "$X"; echo "13:$?"
$R $M $T/dcre_aid_alm_in_class.cfg      E_RSATR "$X"; echo "14:$?"
$R $M $T/dcre_aid_cyc_no_static.cfg     E_OBJ   "$X"; echo "15:$?"
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
R=tools/cfg_error_tests/run.sh
T=tools/cfg_error_tests
X="-I$PWD/test"
M=build/musca_b1-2core
$R $M $T/dcre_aid_sem_in_class.cfg      E_RSATR "$X"; echo "16:$?"
$R $M $T/dcre_aid_flg_in_class.cfg      E_RSATR "$X"; echo "17:$?"
$R $M $T/dcre_aid_mtx_in_class.cfg      E_RSATR "$X"; echo "18:$?"
$R $M $T/dcre_aid_sem_no_static.cfg     E_OBJ   "$X"; echo "19:$?"
$R $M $T/dcre_aid_flg_no_static.cfg     E_OBJ   "$X"; echo "20:$?"
$R $M $T/dcre_aid_mtx_no_static.cfg     E_OBJ   "$X"; echo "21:$?"
```

**段階3b 新規6件**：

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
R=tools/cfg_error_tests/run.sh
T=tools/cfg_error_tests
X="-I$PWD/test"
M=build/musca_b1-2core
$R $M $T/dcre_aid_dtq_in_class.cfg      E_RSATR "$X"; echo "22:$?"
$R $M $T/dcre_aid_pdq_in_class.cfg      E_RSATR "$X"; echo "23:$?"
$R $M $T/dcre_aid_mpf_in_class.cfg      E_RSATR "$X"; echo "24:$?"
$R $M $T/dcre_aid_dtq_no_static.cfg     E_OBJ   "$X"; echo "25:$?"
$R $M $T/dcre_aid_pdq_no_static.cfg     E_OBJ   "$X"; echo "26:$?"
$R $M $T/dcre_aid_mpf_no_static.cfg     E_OBJ   "$X"; echo "27:$?"
```

期待: **27件すべて 0**（両エンジンが同じ ercd／文言を同じように検出）。
rc=2 は**前提未充足であり合格ではない**（EXTRA_CFLAGS の付け忘れか、
cfg が別のエラーで先に落ちている）。

★`ls` の結果に**上の27件に無いファイル**が見つかった場合は、
「回帰列に入っていなかったファイル」として**実行して結果を記録する**
（対象 builddir・期待 ercd は cfg 冒頭のコメントから読む）。**列に無いから無視、はしない。**
★**`dcre_aid_alm_no_static.cfg` は存在しない**（段階3a 最終レビュー Minor の指摘。
`AID_ALM` だけ no-static 版が欠けている）。段階3b でも**作らない**（スコープ外）が、
**欠けている事実を progress.md に再掲する**。

- [ ] **Step 6: `KERNEL_FCSRCS` 突き合わせ**（AGENTS.md §4。22個のまま不変のはず）

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
diff <(awk '/^KERNEL_FCSRCS/{f=1} f{print} f&&!/\\$/{exit}' kernel/Makefile.kernel \
       | grep -oE '[a-z_]+\.c' | sort -u) \
     <(grep -oE 'kernel/[a-z_]+\.c' CMakeLists.txt | sed 's#kernel/##' | sort -u)
echo "diff rc=$?"
```
期待: rc=0（差分なし）。段階3b は既存 `.c` にしか手を入れていないので変化しないはず。

- [ ] **Step 7: `DIVERGENCE_MAP.md` の完全性監査**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
git diff main...HEAD --name-only > /tmp/dcre4-t7-changed.txt
wc -l /tmp/dcre4-t7-changed.txt
while read f; do
  case "$f" in
    docs/*|tools/*|cmake/*|CMakeLists.txt|CMakePresets.json|cfg_py/*|.superpowers/*|kernel/*.py) continue;;
  esac
  grep -q -- "$f" DIVERGENCE_MAP.md || echo "MISSING: $f"
done < /tmp/dcre4-t7-changed.txt
```
期待: `MISSING:` が**1行も出ない**。
（除外パターンは段階2 Task 8 の裁定に従う＝`kernel/*.py` は Python cfg エンジンの派生
テンプレートで `DIVERGENCE_MAP.md:17` の「kernel/*.py（15個）」枠に記録済み。
`kernel/*.trb` は **pristine なので除外しない**。）

段階3b で触った pristine は次のとおり（段階1/2/3a 分は既に記録済み）：
`kernel/kernel_api.def`、`include/kernel.h`、`kernel/kernel_impl.h`、`kernel/check.h`、
`kernel/dataqueue.h`、`kernel/dataqueue.c`、`kernel/pridataq.h`、`kernel/pridataq.c`、
`kernel/mempfix.h`、`kernel/mempfix.c`、`kernel/allfunc.h`、`kernel/Makefile.kernel`、
`kernel/kernel_rename.def`＋再生成2、
`test/test_dcre4.*`、`test/test_dcre_mix.cfg`、`test/MANIFEST`、`test/testexec.rb`。
漏れが（あってはならないが）見つかったら**理由込みで**追記する。

あわせて次を反映する：
- **上流報告候補 d を拡張する**：段階3a で記録した「dcre `eventflag.c:257` の `del_flg` だけが
  `CHECK_PAR`（E_PAR）」に加えて、**dcre `dataqueue.c:413` の `del_dtq` も `CHECK_PAR`（E_PAR）**
  である（`del_sem`/`del_mtx`/`del_pdq`/`del_mpf` は `CHECK_ID`＝E_ID）。
  **6つの `del_*` のうち2つだけが E_PAR** という不整合で、証拠は行番号つきで完備している。
  候補 d の記述を「`del_flg` と `del_dtq` の2件」へ更新する（送付するかはユーザ判断）。
- **未 hardening の記録**：`acre_dtq`/`acre_pdq` は `dtqcnt`/`pdqcnt` の上限を検査しない
  （dcre 由来）。`sizeof(DTQMB) * dtqcnt` が `size_t` で桁あふれすると `malloc_mpk` に
  小さな値が渡り、**確保が成功してしまう**（そのあと `enqueue_data` が範囲外を書く）。
  現状 `malloc_mempool` の符号混在比較（上流報告候補 c）と同じ「ユーザ誤用経路の
  hardening」課題として引き継ぐ。**段階3b では到達可能性の実証も修正も行っていない。**
- **`acre_mpf` の `ROUND_MPF_T(blksz) * blkcnt` も同じ桁あふれ経路**を持つことを併記する。

- [ ] **Step 8: `.superpowers/sdd/progress.md` へ段階3b 完了を記録し、コミット**

記録に含めること（**推測と事実を分ける**）：
- Task 1 の実装前確認8項目の結論と、それに基づく spec 訂正8件（A〜H）。
  **訂正G/H は「現物の性質の記録」であって現物の誤りではない**ことを明示する。
- 【事実】dtq/pdq/mpf も**非親和オブジェクト**であり、段階2 の Constraint 4
  （`iprcid`/`affinity`/`p_pcb` の充填）に相当するコードは**1行も書いていない**こと。
  free-list のリンクは dtq/pdq が `swait_queue`、mpf が `wait_queue` の直用で、
  **段階2 訂正D（64bit で `callback` が上書きされる）は構造的に発生しなかった**こと。
- 【事実】`DTQID`/`PDQID`/`MPFID` は**既存マクロの2レンジ化置換**であり、
  段階3a の `SEMID`/`FLGID`/`MTXID` と同じ作業だったこと（4・5・6家族目）。
- 【事実】cfg 共通枠組みは **7家族目まで per-object 変更ゼロ**で通ったこと。
  段階3a の申し送り「dtq/pdq/mpf は管理領域を伴うため per-object テンプレートの変更が
  **初めて必要になる見込み**」は**外れた**（管理領域は静的生成分だけが cfg 生成で、
  動的スロット分は実行時 `malloc_mpk` のため）。**見込みが外れた事実を明記する。**
- 【事実】dcre からの意図的な逸脱の一覧：
  (1) 3つの `del_*` の `CHECK_TSKCTX_UNL_MYSTATE`（段階3a 訂正C の継承）、
  (2) `del_dtq` の `CHECK_ID`（訂正F。dcre 側の不整合・`del_flg` に続く2件目）、
  (3) 返値型 `ER_UINT` → `ER_ID`（段階1/2/3a と同じ）、
  (4) glock の対化と空判定 `tnum_* == tnum_s*`（全 `acre_*` 共通）。
  ★段階3a と違い、**恒真 `CHECK_PAR` の除去（訂正E 型）は本段階では発生しなかった**
  （`acre_dtq`/`acre_pdq` に範囲検査自体が無く、`acre_mpf` の `blkcnt != 0`/`blksz != 0` は
  恒真ではない）。
- 【事実】E_NOEXS 挿入は **23関数**（dtq 9 / pdq 8 / mpf 6）で、
  そのうち **5関数（`fsnd_dtq`・`snd_pdq`・`psnd_pdq`・`tsnd_pdq`・`rel_mpf`）は
  字下げのみでは済まず、dcre と同じ構造変更**（ロック前の検査をロック内へ移す）を行ったこと。
  ★`rel_mpf` の変更は **ercd の問題ではなくメモリ安全性の修正**（削除済みプールの
  `free_mpk` 済み管理領域を読んでいた）である。
  ★これらは「上流に先例が無い類推適用」ではない（dcre にすべて先例がある）。
- 【事実】**`TA_NOEXS` は `((ATR)(-1))`＝全ビット 1** であり、
  `(atr & TA_MBALLOC) != 0U` / `(atr & TA_MEMALLOC) != 0U` は TA_NOEXS に対して**必ず真**になる。
  3つの `del_*` はいずれも**属性の読みと `free_mpk` を `TA_NOEXS` 書込みより前**に置いている
  （dcre と同じ順序）。段階3a 最終レビュー Important #1（`MTX_CEILING` の根拠が偽だった件）の
  申し送りを、**今回は真の根拠つきでコード・コメント・台帳に反映した**こと。
- 【事実】カーネルメモリプールは bump allocator で、`free_mempool` は `count == 0` の
  ときだけ `brk` を戻す（訂正G）。この性質を **Task 6 Step 8 の変異 control の梃子**に使い、
  `del_mpf` の `TA_MEMALLOC` 解放を落とすと `MPF_CYCLES` 周のループが `E_NOMEM` で
  倒れることを実演したこと。
- 【事実】プール裁定（spec §1）は段階3b でも**受容継続**であり、
  `free_mempool` の遅延再利用（count==0 まで brk を戻さない）は
  「解放済み管理領域が即座に再利用されない」という**補強材料**になること。
  ただし論証の根拠は「glock 下参照のみ + E_NOEXS ゲート」であって、この補強ではない。
  **最終レビューでこの不存在論証への反証を試みること**（spec §1-1 の指示）。
- 【推測含む・引き継ぎ課題】`acre_dtq`/`acre_pdq`/`acre_mpf` は
  `sizeof(DTQMB) * dtqcnt` 等の**乗算の桁あふれを検査しない**（dcre 由来）。
  `malloc_mempool` の符号混在比較（上流報告候補 c）と組み合わさると、
  巨大な `dtqcnt` で確保が成功しうる。段階1 deferred #1・段階3a の `acre_mtx` の
  未検査 `ceilpri` と**同系統の「ユーザ誤用経路の hardening」課題**として引き継ぐ。
  段階3b では到達可能性の実証も修正も行っていない。
- 【ISR への引き継ぎ】着手順は **3a → 3b → ISR**（裁定済み）。ISR は案B ハイブリッド + API 拡張
  （専用 spec `docs/superpowers/specs/2026-08-04-...-isr-...` を起草済み・commit `958c1f4`）。
  ISR は**唯一の親和オブジェクト**（`intno` にプロセッサが符号化される）なので、
  段階2 の Constraint 4 に相当する扱いが**復活する**見込みである。
- 【回帰の実測値】builds 8/9（polarfire は既知の環境ギャップ）／equivalence 11/11／
  QEMU 7/7 孤児なし／機能テスト 5/5 PASS + mix build-only／
  エラー行列 27/27／FCSRCS 差分0／台帳監査 MISSING=0。
  `test_dcre4` の `Check point` 行数は **15**、`test_dcre3` は **14**。

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
git add -A && git commit -m "chore(dcre): 段階3bの最終回帰と台帳整理"
```

---

## Self-Review 済み事項（計画作成時の検証記録）

**spec 要件 → Task 対応:**

| spec | 内容 | Task |
|---|---|---|
| GC 1 | ブランチ・台帳 | 全 Task（各 Task の最終 Step） |
| GC 2 | スコープ（ISR を含めない） | 計画全体の Files 一覧で境界を固定 |
| GC 3 | dcre 標準 API のみ（T_CDTQ/T_CPDQ/T_CMPF） | T2 Step 3-4（★dcre とのバイト diff で検証） |
| **GC 4** | **プロセッサ親和なし・充填コードを書かない** | **T1 Step 8（ゲート）、T3/T4/T5 の各 Step 2-3 に明示的な禁止注記** |
| GC 5 | F-1 検証（両エンジン・exit 0 のみ合格） | T2 Step 6-11、T3-T6 の各ビルド Step、T7 Step 2 |
| GC 6 | CB はヒープ確保しない・free-list は先頭 QUEUE 直用 | T3/T4/T5 Step 1-2（★mpf だけ `wait_queue`） |
| GC 7 | free-list は FIFO（再議しない） | T3/T4/T5 の initialize（`queue_insert_prev`）、T6 の「空き1個」形テスト |
| GC 8 | 管理領域は malloc_mpk・TA_MBALLOC／mpf は TA_MEMALLOC | T2 Step 5、T3/T4/T5 の acre/del、T6 手順2/8 |
| GC 9 | プール裁定は済み（再議しない・最終レビューで反証） | 本計画は裁定を前提に組んである。T7 Step 8 で反証課題を申し送り |
| GC 10 | 汎用層・KERNEL_FCSRCS 不変 | T7 Step 6 |
| GC 11 | 訂正C（del_* は CHECK_TSKCTX_UNL_MYSTATE） | T3 Step 4／T4 Step 5／T5 Step 4 |
| GC 12-16 | rc=124 単独禁止・パイプ判定禁止・exit2 不合格・QEMU 個別＋pgrep・run.sh 4引数 | T6 Step 6、T7 Step 3/5（全件に引数を明記） |
| §1 | プール裁定（受容継続） | T1 Step 10（訂正G の追記）、T7 Step 8（申し送り） |
| §2.1 | `T_CDTQ`/`T_CPDQ`/`T_CMPF` | T2 Step 3（★訂正A：dcre 原文とバイト diff） |
| §2.2 | 6サービスコール宣言・機能コード | T2 Step 4（コードは既存＝T1 Step 1 で確定） |
| §2.3 | 確保とエラー（E_NOID→確保→E_NOMEM／2段確保と巻き戻し／del の順序） | T1 Step 3（現物確認）、T3/T4/T5 の acre/del、T6 手順2/6/8 |
| §3 | cfg 層（3行・per-object 変更ゼロ・訂正E ガードの自動適用・エラー回帰6件） | T2（全体）、T1 Step 6/7 |
| §4 | カーネル層（TA_MBALLOC・free-list・initialize・2レンジ ID・E_NOEXS 23・配線） | T2 Step 5、T3/T4/T5（全体） |
| §5 | MP 安全性（glock 下・E_NOMEM 時の free-list 一貫性） | T1 Step 3、T3/T4/T5 の acre、T6 手順2（E_NOMEM 後に2個取れること） |
| §6 | test_dcre4 の9シナリオ | T6 Step 4（手順1-8）+ Step 7/8（変異 control ×2）+ Step 9（非退行） |
| §7 | 統治（7タスク・台帳・全構成回帰・引き継ぎ） | T7 |
| §8 | 実装前確認8項目 | T1 Step 1-9 |
| ★3a 申し送り | TA_NOEXS のマスク演算での化け | 「段階3a 最終レビューからの申し送り」節、T1 Step 3-2、T3/T4/T5 の del_* コメント、T6 Step 8 の変異 control |

**現物確認済み（計画作成時に実ファイルで確認した事実）:**
- `TFN_ACRE_DTQ (-196)` / `TFN_ACRE_PDQ (-197)` / `TFN_ACRE_MPF (-201)` /
  `TFN_DEL_DTQ (-212)` / `TFN_DEL_PDQ (-213)` / `TFN_DEL_MPF (-217)`
  （`include/kernel_fncode.h:137,138,141,149,150,153`）＝**6件とも既存**。
- `DTQID`/`PDQID`/`MPFID` が **既に存在**し既に inib ポインタ差分式であること
  （`dataqueue.h:108-109` / `pridataq.h:115-116` / `mempfix.h:120-121`）。
  CB は `DTQCB *const p_dtqcb_table[]`（`dataqueue.h:103`）等の**ポインタ表**。
- 3つの INIB に `iprcid`/`affinity` が**無い**（`dataqueue.h:68-72`：`dtqatr`/`dtqcnt`/`p_dtqmb`、
  `pridataq.h:72-77`：`pdqatr`/`pdqcnt`/`maxdpri`/`p_pdqmb`、
  `mempfix.h:78-84`：`mpfatr`/`blkcnt`/`blksz`/`mpf`/`p_mpfmb`）。
  3つの CB に `p_pcb` が**無い**、かつ**先頭フィールドが QUEUE**
  （dtq/pdq は `swait_queue`＝`dataqueue.h:81`/`pridataq.h:87`、mpf は `wait_queue`＝`mempfix.h:94`）。
- `initialize_dataqueue`（`dataqueue.c:142-163`）/ `initialize_pridataq`（`pridataq.c:135-158`）/
  `initialize_mempfix`（`mempfix.c:119-142`）が**既に全体マスタ限定**
  （`if (p_my_pcb->prcid == TOPPERS_MASTER_PRCID)`）で、
  段階2 `initialize_cyclic` にあった `p_tevtcb == NULL` 判定を**持たない**こと。
- `tnum_dtq`/`tnum_pdq`/`tnum_mpf` が `.c` 側（`dataqueue.c:131` / `pridataq.c:123` /
  `mempfix.c:107`）にあり `.h` に無いこと（移設が要る）。
- `#include <queue.h>` が3つの `.h` の **`:51`** にあること（追加不要）。
- `init_wait_queue(PCB *, QUEUE *)`（`wait.h:249`、本体 `wait.c:215-228`）が
  **MP 対応済み**で `winfo.wercd = E_DLT` → `make_non_wait(p_my_pcb, p_tcb)` を行うこと。
- **dcre `acre_dtq`（`dataqueue.c:339-397`）の順序**：`CHECK_VALIDATR` → 条件付き
  `CHECK_PAR(MB_ALIGN)` → `lock_cpu` → **E_NOID が最初** → `malloc_mpk` → E_NOMEM →
  **成功して初めて `queue_delete_next`**。**dtqcnt の範囲検査は無い。**
  ユーザ供給 `dtqmb` は受理し `TA_MBALLOC` を立てない。`dtqcnt == 0` は確保しない。
- **dcre `acre_pdq`（`pridataq.c:316-379`）** は同型＋`CHECK_PAR(VALID_DPRI(maxdpri))`（`:338`）。
- **dcre `acre_mpf`（`mempfix.c:199-279`）**：`CHECK_PAR(blkcnt != 0)`（`:223`）/
  `CHECK_PAR(blksz != 0)`（`:224`）/ `MPF_ALIGN`（`:226`）/ `MB_ALIGN`（`:229`）、
  2段確保（①`:237-240` ②`:245-248`）、**②失敗時の巻き戻しは `if (pk_cmpf->mpf == NULL)
  { free_mpk(mpf); }`（`:249-252`）＝パケットの元の値で判定**、
  `p_mpfinib->blksz = ROUND_MPF_T(blksz);`（`:260`）。
- **dcre `del_dtq`/`del_pdq`/`del_mpf` の順序**：`init_wait_queue`（dtq/pdq は2回）→
  **`TA_MBALLOC`/`TA_MEMALLOC` のビット検査と `free_mpk`**（`dataqueue.c:427-430` /
  `pridataq.c:409-412` / `mempfix.c:308-314`）→ **`xxxatr = TA_NOEXS;`** →
  `queue_insert_prev` → ディスパッチ判断。★属性の読みが TA_NOEXS 書込みより**前**である。
- dcre `del_dtq`（`dataqueue.c:413`）だけが `CHECK_PAR`（E_PAR）で、
  `del_pdq`（`pridataq.c:395`）`del_mpf`（`mempfix.c:295`）は `CHECK_ID`（E_ID）であること。
- **`TA_NOEXS = ((ATR)(-1))`（`kernel/kernel_impl.h:199`）＝全ビット 1**。
  `TA_MEMALLOC = UINT_C(0x8000)`（`:201-203`、`#ifndef` ガード付き）。
  **`TA_MBALLOC` は FMP3 に無い**（追加が必要）。
- **`VALID_DPRI` は dcre `check.h:71` にあり FMP3 `kernel/check.h` に無い**
  （`VALID_TPRI` は `:70` にある）。`TMIN_DPRI`=1 / `TMAX_DPRI`=16 は `include/kernel.h:624-625`。
- `MPF_ALIGN`/`MB_ALIGN`（`kernel/check.h:408-418`）・`INDEX_NULL`/`INDEX_ALLOC`
  （`kernel/mempfix.h:57-58`）・`MPF_T`（`include/kernel.h:131`）・
  `COUNT_MPF_T`/`ROUND_MPF_T`（`:672-673`）が**すべて既存**であること。
- `DTQMB`（`kernel/dataqueue.h:57-59`、`intptr_t data;` 1個）/ `PDQMB`（`pridataq.h:59-63`）/
  `MPFMB`（`mempfix.h:67-69`、`uint_t next;` 1個）が**既存**であること。
- **cfg（両エンジン）は `TA_MBALLOC` を1度も出力しない**が、`DTQMB`/`MPFMB`/`MPF_T`/
  `COUNT_MPF_T`/`ROUND_MPF_T` は**今日すでに出力してコンパイルが通っている**こと
  （`kernel/dataqueue.py:59-66`・`kernel/mempfix.py:75-96`）。
- cfg 共通枠組み（`kernel/kernel.py:108-277`）が `self.obj_s` から
  `a{obj_s}inib_table` / `_kernel_a{obj_s}cb_<i>` / `_kernel_tmax_s{obj}id` / `TNUM_S{OBJ}ID` を
  **機械的に導く**こと。`DataqueueObject`/`PridataqObject`/`MempfixObject` は
  `prepare`/`generateInib` のみで **`inibList`/`omit_cb`/`generateData` のカスタマイズが無い**こと。
- **FMP3 の5関数がロック取得前に `p_xxxinib` を読んでいる**こと：
  `fsnd_dtq`（`dataqueue.c:485` の `CHECK_ILUSE`）、`snd_pdq`（`pridataq.c:301`）、
  `psnd_pdq`（`:357`）、`tsnd_pdq`（`:408`）、`rel_mpf`（`mempfix.c:324-330` の `CHECK_PAR`×4）。
  dcre は5つとも**ロック内へ移している**（`dataqueue.c:604-606` / `pridataq.c:450-452` ほか /
  `mempfix.c:487-495`）。
- E_NOEXS 対象23関数の定義行：dataqueue.c `313,367,417,474,521,578,622,683,727` /
  pridataq.c `290,345,396,455,513,557,619,663` / mempfix.c `173,222,257,311,370,413`。
- `ini_dtq`（`dataqueue.c:683-`）は `ercd = E_OK;` が dispatch 判定の**後ろ**、
  `ini_pdq`（`pridataq.c:619-`）は**前**、`ini_mpf`（`mempfix.c:370-`）は**両方の枝で代入**
  ＝**3つとも形が違う**こと（「揃える」誘惑を計画本文で明示的に禁止した）。
- `pget_mpf`（`mempfix.c:222-`）が `p_my_pcb` も `p_selftsk` も持たず
  `acquire_glock();` の直後がいきなり `if` であること。
- `kernel/startup.c` の `malloc_mempool`/`free_mempool` が **bump allocator** で、
  `free_mempool` は `count == 0` のときだけ `brk` を先頭へ戻すこと。
  `malloc_mempool` の `((char *)limit - (char *)brk) >= size` が**符号混在比較**であること
  （既存の上流報告候補 c）。
- `syssvc/test_svc.c:110-176,183-190` の `check_point_prc`/`check_finish` の意味論：
  `prcid > 0` は `check_count[prcid-1]`、`prcid == 0` は `check_count[0]`。
  出力は `"Check point %d-%d passed."`（**prcid が先**）／`"Check point %d passed."`。
  **`check_finish(count)` は `check_point_prc(count, 0)` を呼ぶので自分でも1行出す。**
- `TSK_NONE = 0`（`include/kernel.h:604`）。`ext_tsk` の書き方は `ext_tsk();`
  （`test/test_dcre3.c:104,119`）。
- 静的 `CRE_DTQ`/`CRE_PDQ`/`CRE_MPF` を含む `.cfg` は `sample/sample1.cfg`（CRE_DTQ×6 のみ）・
  `target/polarfire_soc_kit_gcc/softconsole/sample1/sample1.cfg`・`test/perf2.cfg`・
  `test/test_dtq1.cfg`・`test/test_mpf1.cfg`・`test/test_notify1.cfg`・`test/test_pdq1.cfg` の
  **7本だけ**で、`syssvc/*.cfg`・`test/test_common1.cfg` には**1件も無い**こと
  （＝段階3a の serial.cfg 罠は dtq/pdq/mpf では起きない）。
  ★`sample/sample1.cfg` に `CRE_PDQ`/`CRE_MPF` が無いため **positive control には使えない**こと。
- `test/test_dcre_mix.cfg` が**既に `DEF_MPK({ MPK_SIZE, NULL })` を持っている**こと
  （段階1 で追加済み。`AID_TSK(2)`/`AID_CYC(2)`/`AID_SEM(2)`/`AID_MTX(1)` も既存）。
- `tools/cfg_error_tests/` の実在 `.cfg` が **21個**であること（段階3a Task 8 の実測と一致）。
- `kernel_api.def` の `CRE_DTQ #dtqid* { .dtqatr .dtqcnt &dtqmb? }`（`:4`）/
  `CRE_PDQ #pdqid* { .pdqatr .pdqcnt +maxdpri &pdqmb? }`（`:5`）/
  `CRE_MPF #mpfid* { .mpfatr .blkcnt .blksz &mpf? &mpfmb? }`（`:8`）と、
  末尾の `AID_TSK`/`DEF_MPK`/`AID_CYC`/`AID_ALM`/`AID_SEM`/`AID_FLG`/`AID_MTX`。
- `kernel/Makefile.kernel` の `dataqueue =`（`:92-94`）/ `pridataq =`（`:96-98`）/
  `mempfix =`（`:103-104`）と `KERNEL_FCSRCS`（`:51-56`、22個）。
- `kernel/allfunc.h` の `/* dataqueue.c */`（`:158-174`）/ `/* pridataq.c */`（`:176-189`）/
  `/* mempfix.c */`（`:208-216`）。
- `kernel/kernel_rename.def` の `# dataqueue.c`（`:77-84`）/ `# pridataq.c`（`:86-91`）/
  `# mempfix.c`（`:103-105`）。
- `include/kernel.h` の挿入目印：`typedef struct t_rdtq {`（`:248`）/ `t_rpdq`（`:254`）/
  `t_rmpf`（`:273`）、`extern ER snd_dtq(...)`（`:375`）/ `snd_pdq`（`:385`）/ `get_mpf`（`:413`）。
  ★隣接する `t_rdtq`/`t_rpdq`/`t_rmpf` は **dcre と同じタブ幅**、段階3a が追加した
  `T_CSEM`/`T_CFLG`/`T_CMTX`（`:227,238,263`）は**タブが1個多い**こと。
- `test/MANIFEST:44-49`（`test_dcre3.*` と `test_dcre_mix.*`）と
  `test/testexec.rb:90-93`（`"dcre3"` / `"dcremix"`）の登録形式。

**未検証（実装者が最初に当たること）:**
- **`DTQMB` 型がテストプログラム（`test/test_dcre4.c`）から見えるか**（T6 Step 4 の注意書き）。
  見えなければ `intptr_t` の配列で代替する。どちらにしたかを記録する。
- **`TARGET_DTQATR`/`TARGET_PDQATR`/`TARGET_MPFATR` 相当のターゲット固有属性ビットの有無**
  （T6 Step 4 の `E_RSATR` テストで使う「未定義ビット `0x04`」が衝突しないか）。
- **`MPK_SIZE = 2048` / `MPF_CYCLES = 16` が変異 control の梃子として十分か**
  （T6 Step 8。足りなければ `MPF_CYCLES` を増やす — `MPK_SIZE` は下げない）。
- **`test_dcre4` の `Check point` 行数が本当に 15 か**（T6 Step 6）。
  段階3a では見積りを3回続けて外している。**実測を正とし、計画の期待値を直して記録する。**
- **no-static 回帰 cfg 3件で実際に E_OBJ が出るか**（T2 Step 12）。
  出なければ隠れた静的インスタンスがある＝T1 Step 6 の結論が誤り。
- **`kria_arm64-tmix` の configure が通るか**（T2 Step 10。新規ビルドディレクトリ）。
- **`fsnd_dtq`/`rel_mpf` の構造変更後に「未使用ラベル `error_exit`」の警告が出ないか**
  （T3 Step 5・T5 Step 5。`CHECK_ID` 等がまだ使うので出ないはずだが確認する）。
- **`build/musca_b1-2core-tdcre3`（段階3a の派生ビルド）が残っているか**（T7 Step 2/4）。
  無ければ作り直す。
- **`ini_pdq` の現物の形**（T4 Step 6 具体例2。`ercd = E_OK;` の位置を現物で確かめてから包む）。

---

