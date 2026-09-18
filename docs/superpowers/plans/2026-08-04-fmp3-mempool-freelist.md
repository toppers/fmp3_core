# FMP3 カーネルメモリプール — 完全一致フリーリスト再利用 実装計画

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `feature/dynamic-creation` ブランチ上の既定カーネルメモリプール実装
（`kernel/startup.c` の `#ifndef OMIT_MEMPOOL_DEFAULT` ブロック）を、
「解放済みブロックの完全一致サイズ再利用（freelist）」を備えた実装へ**置換**し、
「長寿命割付が1つでも残ると create/delete サイクルでアリーナが単調に痩せる」
病理を除去する。

**Architecture:** 変更は `kernel/startup.c` の既定実装ブロック内に**完全に閉じる**。
外部シンボル4関数（`initialize_mempool`/`malloc_mempool`/`aligned_alloc_mempool`/
`free_mempool`）のシグネチャ・`kernel_rename.def`・`allfunc.h`・`mpk_valid` ラッパ
（`kernel/kernel_impl.h:325-353`）・`OMIT_MEMPOOL_DEFAULT` フック・cfg 両エンジンは
**一切変更しない**。各割付の直前に `MPHDR`（`size_t` 1個）を置き、解放されたユーザ
領域を `FREEBLK`（単方向 LIFO）に転用する。alloc はまず freelist を「サイズ完全一致
＋ポインタ実アライン適合」で線形走査し、ヒットしなければ従来の bump。`count==0` の
全域リセット（brk 先頭戻し＋freelist クリア）は backstop として維持する。
検証は (1) 既存回帰（test_dcre4 の count==0 依存シナリオが canary）、(2) 新設
`test_dcre6`（枯渇耐性＋サイズ不一致の bump 前進観測＋変異 control）、(3) 文書更新
＋全回帰、の3タスク。

**Tech Stack:** C（カーネル・テスト）、CMake、QEMU（musca_b1-2core）。
**cfg 側の変更はゼロ**（spec §7.4。`DEF_MPK` のサイズ計算・出力ロジックに触れない）。

**Spec（正本）:** `docs/superpowers/specs/2026-08-04-fmp3-mempool-freelist-design.md`
（commit `a5818cb` ＋ 裁定 `4f2eb4f`）。食い違いを見つけたら spec を黙って直さず、
本計画末尾の「計画時所見」の流儀で記録して報告する。

---

## Global Constraints（プロジェクト横断規約の転記。実装中に再解釈しない）

1. ブランチ `feature/dynamic-creation`（着手時 HEAD = `4f2eb4f`）。**main へはマージしない。**
   pristine への改変は**必ず `DIVERGENCE_MAP.md` に記録する**（本計画の種別は
   `mod (dcre-port, 機能拡張)` / テスト新設は `add (dcre-port)`）。
2. **`cmd | tail` / `cmd | grep` で成否判定しない。** パイプラインの `$?` は最後の
   要素のもの。ファイルへリダイレクトしてから `grep` するか、`${PIPESTATUS[0]}` を見る。
3. **`rc=124`（timeout）単独を成功判定に使わない。** 期待出力の実在を `grep` で確認する。
4. QEMU 実行は**プリセットごとに個別コマンド**で行い、ログを別ファイルに落とす。
   `for` ループで全構成を 1 コマンドに詰めない（Bash の 2 分タイムアウトで qemu が
   孤児化する）。各実行後に `pgrep -a qemu` で残存 0 を確認する。直後に一瞬
   `<defunct>` が見えることがある（reap-lag）。3 秒後に再確認して消えていれば
   孤児化ではない。そう判断した根拠を記録する。
5. `tools/cfg_equivalence.sh` は **exit 0 のみ合格**（1=不一致 / **2=前提未充足で
   あり合格ではない**）。
6. テスト用ビルドディレクトリは**測定前に `rm -rf` → 再 configure**する
   （ninja は cfg エンジンの依存を追跡しない。本計画は cfg エンジンに触れないが、
   fresh-configure 規則はテストディレクトリに引き続き適用する）。
7. **ROM/RAM の A/B 比較は同一ビルドパスで行う**（ビルドパス長が Thumb2 コード
   サイズへ影響する — hardening Task 5 の実測知見。worktree 別パスでの比較は不可）。
8. **`test_dcre1`〜`test_dcre6` は `musca_b1-2core` プリセットが必須**
   （`.cfg` が CLS_PRC1 のみでも、実行時に PRC2 を使うテストがある。
   単一コア `musca_b1` で走らせた誤検証の前例あり）。
9. コミットは各 Task 末尾で行い、メッセージ末尾に以下のトレーラを付ける：
   ```
   Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
   Claude-Session: https://claude.ai/code/session_01F5qdprYPDaU9QYQTLfoc6Q
   ```
10. **実測を正とする。** 期待値（Check point 行数・size 増分・E_NOMEM の周回数）が
    計画と食い違ったら、実測に合わせて計画・コメント側を直し、直した事実を記録する。
    ただし**変異 control の予測は実行前に記録し、実測と照合する**（後出しの予測は無効）。
11. **変異 control を復元したら `git diff --stat kernel/` が空であることを確認する。**
12. 各 Task の Step 1（preflight）で前提を現物確認し、**食い違ったら自己修復せず
    止まって報告する**。

---

## 実装者向け参照資料（着手前に読む）

- **spec（正本）**: `docs/superpowers/specs/2026-08-04-fmp3-mempool-freelist-design.md`
  — 全文。特に §3（レイアウトとアライン成立性の VERIFY）・§4（アルゴリズム）・
  §7.3（test_dcre4 巻き戻し観測の机上トレース）。
- **現行実装**: `kernel/startup.c:438-563`（ヘッダコメント・`MEMPOOLCB`・
  `initialize_mempool`・逸脱コメント・`alloc_mempool`・`free_mempool`）。
- **canary テスト**: `test/test_dcre4.c:494-543`（巻き戻しシナリオ。
  count==0 backstop に依存する既存観測 — **無変更で通ることが Task 1 の合格条件**）。
- **テストビルドの実働レシピ**: `.superpowers/sdd/2026-08-04-fmp3-dcre-hardening/task-3-report.md`
  （fresh configure・`-DFMP3_APPLNAME`/`-DFMP3_APPLDIR`/`-DFMP3_SYSSVC_TARGET_C_FILES`
  の EXTRA cfg オプション・QEMU 実行手順・プリセット選択の教訓）。

## 変更ファイル一覧（全体像）

**pristine（`DIVERGENCE_MAP.md` に記録が要る）**
- `kernel/startup.c` — 既定メモリプール実装の置換（Task 1）
- `test/test_dcre4.c` — コメントのみ（【算術】数値の新レイアウト化ほか。Task 1）
- `test/test_dcre4.h` — コメントのみ（MPK_SIZE 選定根拠の新実装化。Task 1）
- `test/test_dcre6.c` / `test_dcre6.cfg` / `test_dcre6.h` — 新設（Task 2）
- `test/MANIFEST` / `test/testexec.rb` — test_dcre6 の登録（Task 2）

**派生（台帳行は不要）**
- `docs/dynamic-creation.md` §6・§7.1（＋§7.5 の stale 記述の是正。Task 3）
- `docs/qa-esp32s3-20260804.md`（R-2 追記）・`docs/qa-esp32s3-20260804-2.md`（Q-4 追記）（Task 3）
- `DIVERGENCE_MAP.md`・`.superpowers/sdd/progress.md`・
  `.superpowers/sdd/2026-08-04-fmp3-mempool-freelist/`（各 Task）

---

## 計画時所見（spec との食い違い・計画者の発見。裁定対象）

計画作成時にコードと突き合わせて見つけた点。**spec は黙って直していない。**
本計画は下記の読みで進める（裁定で覆ったら該当 Step を止めて報告する）。

- **所見1（spec §4 の字面と実装の対応）**: spec は「既存の overflow ガード列に
  `size + sizeof(MPHDR)` のあふれ検査を追加」と書くが、ヘッダ直前配置
  （`aligned = align(brk + sizeof(MPHDR), alignment)`）では **size にヘッダを
  加算する式がそもそも存在しない**（ヘッダは brk 側に折り込まれ、size は残量比較
  `size > limit - aligned` にのみ使う）。実際にあふれうる加算は
  `brk + sizeof(MPHDR)` と `+ (alignment - 1)` の2つであり、本計画は**この2つを
  ガードする**（spec の意図＝「全加算をガードせよ」は満たす。字面どおりの
  `size + sizeof(MPHDR)` 検査は対応する式が無いため入れない）。
- **所見2（spec §3 の最小割付分析の誤り — 64bit で前提が崩れるが実害なし）**:
  spec は「最小の malloc_mpk 要求は `sizeof(DTQMB)*1` ＝ポインタ幅なので FREEBLK が
  必ず収まる」と結論するが、**`kernel/mempfix.c:294` の `sizeof(MPFMB) * blkcnt` を
  見落としている**。`MPFMB` は `uint_t next` 1個（`kernel/mempfix.h:67-69`）＝
  **常に 4B** であり、64bit ターゲット（kria_arm64）では `blkcnt==1` の要求サイズ
  4B ＜ `sizeof(FREEBLK)` 8B になる。ただし**メモリ安全性は保たれる**：解放時の
  FREEBLK 書込みのはみ出しは `[user+size, user+sizeof(void*))` であり、次の割付の
  ヘッダは最速でも `user + size + sizeof(MPHDR)` から始まる。
  `sizeof(FREEBLK) == sizeof(void*) == sizeof(size_t) == sizeof(MPHDR)` なので
  `user + sizeof(FREEBLK) <= user + size + sizeof(MPHDR)`（size >= 0 で常に成立）、
  すなわち**はみ出しは必ず「自ユーザ領域末尾〜次ヘッダ直前の死領域（パディング）」に
  収まり、誰の生きたデータも壊さない**。この論証を Task 1 のコードコメントに明記する
  （spec §3 の「ポインタ幅以上」という前提記述は 64bit の mpfmb について偽）。
- **所見3（stale コメントの範囲が spec §7.3 の指定より広い）**: spec は
  `test/test_dcre4.c:506-520` の【算術】数値の更新のみ要求するが、旧 bump 意味論を
  焼き込んだコメントは他に2箇所ある：`test/test_dcre4.c:596-604`（MPF_CYCLES ループの
  実証コメント）と `test/test_dcre4.h:17-36`（MPK_SIZE 選定根拠）。放置すると
  「説明とコードが食い違ったドキュメント」（spec が §7.3 で禁じた状態そのもの）に
  なるため、**Task 1 で3箇所とも更新する**。
- **所見4（`docs/dynamic-creation.md` の既存 stale 記述 — 本計画の §6 編集と衝突）**:
  §6 末尾の「注意（未 hardening）：…乗算オーバーフローを検査しない」（443-448行）と
  §7.5「サイズ乗算オーバーフロー未検査（hardening 予定）」（518-526行）は、
  **hardening パス（commit `fb5e369`）で検査が入った後も更新されていない**
  （台帳 `DIVERGENCE_MAP.md` は更新済み・本文書だけ取り残された）。§6 を書き換える
  Task 3 でこの矛盾隣接記述を残すと文書が自己矛盾するため、**Task 3 で最小限の
  是正（「解消済み」への書き換え）を併せて行う**。スコープ外裁定が出たら該当 Step
  のみ落とす。
- **決定1（Task 2 の配置 = 新設 `test_dcre6`）**: spec §7.1 は配置を実装計画に委ねた。
  **`test_dcre4` の拡張ではなく新設**とする。理由：枯渇耐性シナリオは「長寿命割付を
  N サイクルにわたって保持し続ける」ことが本体であり、これは spec §7.3 の机上トレース
  が依拠する **test_dcre4 の不変条件「すべての acre_* は次の acre_* より前に del_* と
  1:1 対になっている（＝要所で count==0）」を構造的に破壊する**。また checkpoint 数
  15 も不変に保てない（コントローラの条件「cp 数不変かつ count==0 前提を乱さない場合
  のみ拡張」の両方に反する）。
- **決定2（spec §8 の assert 推奨 → 採用）**: `alloc_mempool` 冒頭にアライン前提の
  `assert`（デバッグビルド限定・`kernel/startup.c:255` 等と同じ既存流儀）と、
  `sizeof(MPHDR) == sizeof(size_t)` のコンパイル時検査（typedef 配列トリック、
  ビルドフラグ非依存・コード生成ゼロ）を入れる。

---

### Task 1: `kernel/startup.c` 置換実装＋既存回帰＋ROM/RAM A/B 実測

**Files:**
- Modify: `kernel/startup.c:438-563`（既定メモリプールブロックの全置換）
- Modify: `test/test_dcre4.c:499-525`・`test/test_dcre4.c:596-604`（コメントのみ）
- Modify: `test/test_dcre4.h:17-36`（コメントのみ）
- Modify: `DIVERGENCE_MAP.md`（startup.c 新規行＋test_dcre4.c/.h 既存行への追記）
- Create: `.superpowers/sdd/2026-08-04-fmp3-mempool-freelist/task-1-report.md`（A/B 実測値の記録）

**Interfaces:**
- Consumes: 既存の外部シグネチャ（変更しない）
  `bool_t initialize_mempool(MB_T *mempool, size_t size);`
  `void *malloc_mempool(MB_T *mempool, size_t size);`
  `void *aligned_alloc_mempool(MB_T *mempool, size_t alignment, size_t size);`
  `void free_mempool(MB_T *mempool, void *ptr);`（`kernel/kernel_impl.h:316-320`）
- Produces（Task 2/3 が依存する事実）:
  - **挙動契約**: alloc = freelist 完全一致（サイズ==要求サイズ かつ
    `((uintptr_t)ptr & (alignment-1)) == 0`）を先頭から走査 → 無ければ bump。
    free = FREEBLK push（LIFO）＋ `count--`、`count==0` で brk 全リセット＋
    `freelist = NULL`。
  - **算術定数（32bit）**: `sizeof(MEMPOOLCB) == 16`（12→16）、割付ヘッダ
    `sizeof(MPHDR) == 4`、割付フットプリント = 4 + 要求サイズ（4の倍数要求なら
    パディング無し）。Task 2 の全数値がこれに依存する。
  - **ROM/RAM A/B 実測値**（text/data/bss のバイト差分）を
    `.superpowers/sdd/2026-08-04-fmp3-mempool-freelist/task-1-report.md` に記録。
    Task 3 の qa 追記がこの数値を転記する。
  - コミットハッシュ（Task 3 の qa 追記が参照する）。

- [ ] **Step 1: preflight（食い違ったら止まって報告）**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
git log --oneline -1                    # 期待: 4f2eb4f（またはこれを祖先に持つ本計画のコミットのみ）
git status --porcelain                  # 期待: 空（本計画のファイル以外の変更が無いこと）
sed -n '438,444p;448,452p;550,559p' kernel/startup.c
grep -n "【算術】" test/test_dcre4.c    # 期待: 506 付近
grep -n "MPK_SIZE" test/test_dcre4.h | head -3
```
確認事項（1つでも外れたら停止）:
- `kernel/startup.c:438-444` のヘッダコメントが「…再利用しないメモリプール管理機能」。
- `:448-452` の `MEMPOOLCB` が `brk`/`limit`/`count` の3フィールド。
- `:550-559` の `free_mempool` が `count==0` でのみ brk を戻す形。
- `test/test_dcre4.c` の【算術】コメントが 506 行付近にあり、`12`/`2036`/`436` の
  旧数値を含む。

- [ ] **Step 2: `kernel/startup.c:438-563` を以下へ全置換**

置換範囲は `:438` の `/*` から `:563` のファイル末尾
（`#endif /* TOPPERS_kermem */`）まで。**以下が置換後の完全な新テキスト**である。

```c
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
 *  トは，alignment が2の巾乗（呼出し側の責任）かつ alignof(size_t) 以
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
 *  [user + size, user + sizeof(FREEBLK)) であり，次の割付けのヘッダは
 *  最速でも user + size + sizeof(MPHDR) から始まる．
 *  sizeof(FREEBLK) == sizeof(void *) == sizeof(size_t) == sizeof(MPHDR)
 *  なので，はみ出しは必ず「自ユーザ領域末尾〜次ヘッダ直前の死領域（誰
 *  も読み書きしないパディング）」に収まり，他の生きたデータを壊さない．
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

	if (size >= sizeof(MEMPOOLCB)) {
		p_mempoolcb->brk = ((char *) mempool) + sizeof(MEMPOOLCB);
		p_mempoolcb->limit = ((char *) mempool) + size;
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
 *  のため alignment >= alignof(size_t) であること（現行の全呼出し側で成立
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
	 *  aligned は alignment の倍数，alignment は alignof(size_t) 以上
	 *  の2の巾乗（冒頭の assert），sizeof(MPHDR) == sizeof(size_t)
	 *  （mphdr_size_check）なので，aligned - sizeof(MPHDR) も
	 *  alignof(size_t) の倍数であり，この書込みは常にアラインする．
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
```

注意：
- `assert` は `kernel/startup.c:255` 等で既に使われている既存流儀
  （`t_stddef.h` 定義、NDEBUG で消える）。追加 include は不要。
- `initialize_mempool` の `size >= sizeof(MEMPOOLCB)` 最小サイズ検査は
  **そのまま維持**（比較対象が 12→16 に自然に増えるだけ）。
- 外部シンボル・`kernel_rename.def`・`allfunc.h` は**触らない**（spec §5）。

- [ ] **Step 3: サンプルビルドでコンパイル確認（fresh）**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
rm -rf build/musca_b1-2core
cmake --preset musca_b1-2core > /tmp/mp-t1-conf.log 2>&1; echo "conf rc=$?"
cmake --build build/musca_b1-2core > /tmp/mp-t1-build.log 2>&1; echo "build rc=$?"
grep -c "warning:" /tmp/mp-t1-build.log
```
期待: conf/build とも rc=0。`startup.c` 由来の新規 warning が 0
（0 でなければ中身を読んで既存由来か切り分け、本 Task 由来なら修正）。

- [ ] **Step 4: `test/test_dcre4.c` の stale コメント2箇所を更新（コードは1バイトも変えない）**

(a) `test/test_dcre4.c:499-525` の【算術】コメントブロック
（`/*` から `*/` まで。`check_assert` 行は変えない）を以下へ置換：

```c
	/*
	 *  ★2段確保の②で失敗したときの巻き戻し（段階3b Task 5 の未実証経路）
	 *
	 *  上の E_NOMEM は①（ブロック領域）の時点で失敗するので，②の失敗と
	 *  巻き戻しの経路（mempfix.c の pk_cmpf->mpf 判定）を通らない．ここは
	 *  「①は入るが①+②は入らない」サイジングで②を失敗させる．
	 *
	 *  【算術】カーネルメモリプールの実効容量は
	 *    MPK_SIZE(2048) - sizeof(MEMPOOLCB)(16) = 2032 バイト
	 *  である（32bit ターゲット・freelist 実装）．各割付けには 4B の
	 *  ヘッダ（MPHDR）が付く．ROUND_MPF_T(4) == sizeof(MPF_T) == 4，
	 *  sizeof(MPFMB) == 4 なので，
	 *
	 *    ケースA: blkcnt=400 → ① 4+4*400=1604 ≤ 2032（成功）
	 *                          ② 4+4*400=1604 > 2032-1604=428（失敗）→ E_NOMEM
	 *    ケースB: blkcnt=200 → ① 4+4*200=804 ＋ ② 4+4*200=804
	 *                          = 1608 ≤ 2032（成功）
	 *
	 *  【なぜケースBが巻き戻しの証拠になるか】
	 *  kernel/startup.c の free_mempool は，count が 0 になったときに
	 *  brk を先頭へ戻し freelist を空にする（backstop）．この検査の直前
	 *  で count は 0・freelist は空なので，ケースA/B の割付けはすべて
	 *  bump 経路を通る（freelist の完全一致再利用はここでは効かない）．
	 *  ケースAで①が巻き戻されれば count は 1→0 に戻り backstop が効く
	 *  ので，直後のケースB（計1608B）は入る．巻き戻しが無ければ count
	 *  は 1 のまま，残量は 428B しかなく，ケースBは①（804B）の時点で
	 *  E_NOMEM になる．
	 *  ★したがって「ケースBが成功すること」が①の解放の直接の観測である．
	 *
	 *  ★このサイジングは 32bit ターゲット（musca_b1）の値である．本テストは
	 *  musca_b1-2core でのみ実行される（DIVERGENCE_MAP.md 参照）．
	 */
```

(b) `test/test_dcre4.c:596-604` の MPF_CYCLES ループ実証コメントを以下へ置換：

```c
	/*
	 *  ★プールが実際に返っていることの実証
	 *
	 *  del_mpf が TA_MEMALLOC のブロック領域を free_mpk しなければ，
	 *  ブロック領域（4+ROUND_MPF_T(64)*4 = 260B）が周回ごとに返らず
	 *  count が 0 に戻らないため，brk の全域リセット（backstop）も
	 *  256B の完全一致再利用（freelist に載るのは管理領域 16B だけ）も
	 *  効かず，brk が単調に進んで MPK_SIZE を数周で使い切り acre_mpf が
	 *  E_NOMEM になる．正常な実装では1周ごとに count が 0 に戻り brk が
	 *  リセットされる（freelist 実装でも backstop として不変）ので，
	 *  MPF_CYCLES 周まわしても消費は頭打ちである．
	 */
```

- [ ] **Step 5: `test/test_dcre4.h:17-36` の MPK_SIZE 選定根拠コメントを更新**

`/*` から `*/` まで（`#ifndef MPK_SIZE` の直前まで）を以下へ置換：

```c
/*
 *  カーネルメモリプールのサイズ
 *
 *  ★この値は変異controlの成立条件でもある．kernel/startup.cのプールは
 *  バンプ確保＋完全一致サイズ再利用（freelist）で，free_mempoolはcount
 *  が0になったときにbrkを先頭へ戻しfreelistを空にする（backstop）．
 *  本テストの反復（手順8）は1周ごとにcountが0に戻るため，freelistの
 *  再利用ではなくこのbackstopで消費が頭打ちになる．したがって
 *
 *    ・正常時：acre_mpf/del_mpfを1周するとcountが0に戻りbrkがリセット
 *              されるので，MPF_CYCLES回まわしても消費は1周分（280B前後．
 *              各割付けに4Bのヘッダが付く）で頭打ちになる．
 *    ・del_mpfのTA_MEMALLOC解放を落とすと：1周ごとにブロック領域
 *              （4+ROUND_MPF_T(64)*4 = 260B）が返らずcountが0に戻らない
 *              ため，backstopも256Bの完全一致再利用も効かず（freelistに
 *              載るのは管理領域16Bだけ），brkが単調に進んでMPK_SIZEを
 *              7〜8周で使い切りacre_mpfがE_NOMEMになる．
 *
 *  MPK_SIZE = 2048 は「1周分（280B前後）は十分入るが，MPF_CYCLES(=16)周
 *  ぶんの累積（4KB超）は入らない」ように選んである．
 */
```

- [ ] **Step 6: canary 回帰（test_dcre4、fresh）— 15cp 不変で PASS すること**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
rm -rf build/musca_b1-2core-tdcre4
cmake --preset musca_b1-2core -B build/musca_b1-2core-tdcre4 \
  -DFMP3_APPLNAME=test_dcre4 -DFMP3_APPLDIR=test \
  -DFMP3_SYSSVC_TARGET_C_FILES=$PWD/syssvc/test_svc.c > /tmp/mp-t1-d4conf.log 2>&1
echo "conf rc=$?"
cmake --build build/musca_b1-2core-tdcre4 > /tmp/mp-t1-d4build.log 2>&1; echo "build rc=$?"
tools/cfg_equivalence.sh build/musca_b1-2core-tdcre4 > /tmp/mp-t1-d4eq.log 2>&1; echo "eq rc=$?"
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 90 cmake --build build/musca_b1-2core-tdcre4 --target run > /tmp/mp-t1-d4run.log 2>&1
grep 'TTSP_RESULT: PASS' /tmp/mp-t1-d4run.log
grep -c 'Check point' /tmp/mp-t1-d4run.log     # 期待: 15（★不変。増減したら停止）
grep -c 'Assertion\|Unexpected' /tmp/mp-t1-d4run.log   # 期待: 0
pgrep -a qemu                                  # 期待: 出力なし
```
期待: PASS・15行・孤児なし。**ここが spec §7.3 の canary**（count==0 依存の
巻き戻しシナリオと MPF_CYCLES 反復が新実装でも無変更で通ること）。
落ちたら実装を疑う（テスト側をいじって通さない）。

- [ ] **Step 7: 回帰（test_dcre1、fresh）— 13cp で PASS すること**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
rm -rf build/musca_b1-2core-tdcre1
cmake --preset musca_b1-2core -B build/musca_b1-2core-tdcre1 \
  -DFMP3_APPLNAME=test_dcre1 -DFMP3_APPLDIR=test \
  -DFMP3_SYSSVC_TARGET_C_FILES=$PWD/syssvc/test_svc.c > /tmp/mp-t1-d1conf.log 2>&1
echo "conf rc=$?"
cmake --build build/musca_b1-2core-tdcre1 > /tmp/mp-t1-d1build.log 2>&1; echo "build rc=$?"
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 60 cmake --build build/musca_b1-2core-tdcre1 --target run > /tmp/mp-t1-d1run.log 2>&1
grep 'TTSP_RESULT: PASS' /tmp/mp-t1-d1run.log
grep -c 'Check point' /tmp/mp-t1-d1run.log     # 期待: 13
pgrep -a qemu                                  # 期待: 出力なし
```
（test_dcre1 はスタックの `aligned_alloc_mpk` 経路＝`alignof(STK_T)=8` を通す
唯一の既存 runtime テスト。ヘッダ直前配置＋8Bアラインの組合せの実証になる。）

- [ ] **Step 8: `DIVERGENCE_MAP.md` 更新**

(a) 新規行（表の dcre 関連ブロック末尾に追加）：

```markdown
| kernel/startup.c（mempool freelist 化） | mod (dcre-port, 機能拡張) | 既定メモリプール実装（`OMIT_MEMPOOL_DEFAULT` ブロック）を「バンプ＋完全一致サイズ再利用」へ置換。各割付けの直前に `MPHDR`（`size_t` 1個＝要求サイズ無加工）を置き、解放されたユーザ領域を `FREEBLK`（単方向 LIFO）へ転用。alloc は freelist を「サイズ完全一致＋ポインタ実アライン適合」で先頭から走査し、無ければ従来の bump（`brk + sizeof(MPHDR)` のあふれ検査をガード列に追加）。free は O(1) push、`count==0` の全域リセット（brk 先頭戻し）は backstop として維持し freelist も同時にクリア。`MEMPOOLCB` は 12B→16B（32bit）、割付け1件あたり +4B/+8B のヘッダ。外部シグネチャ4関数・`kernel_rename.def`・`allfunc.h`・`OMIT_MEMPOOL_DEFAULT` フックは不変。dcre の原形は「全解放まで再利用しない」単純バンプで、長寿命割付が1つでも残ると create/delete 反復でアリーナが単調に痩せて E_NOMEM に到達する（esp32_s3 実機で同一タスクIDが1ブート中6回再利用される実測パターンが動機）。これを完全一致再利用で除去する（ユーザ裁定2件：既定実装を置換／first-fit・分割・サイズクラス化は不採用。`docs/superpowers/specs/2026-08-04-fmp3-mempool-freelist-design.md`）。★64bit ターゲットでは `sizeof(MPFMB)*1 = 4B < sizeof(FREEBLK) 8B` の割付けで FREEBLK 書込みがユーザ領域末尾を越えるが、はみ出し先は次ヘッダ直前の死領域に必ず収まる（`sizeof(FREEBLK) == sizeof(MPHDR)` による。spec §3 の「最小要求はポインタ幅」という記述はこの mpfmb 経路を見落としており、計画時所見2として訂正・論証をコード内コメントに記録） | - （不具合修正ではなく拡張のため上流報告しない） |
```

(b) 既存の `test/test_dcre4.c（dcre段階3b Task 6）` 行の末尾に追記：

```markdown
★mempool freelist 化（本行の startup.c 置換と同一コミット）で【算術】コメント（`sizeof(MEMPOOLCB)` 12→16・実効容量 2036→2032・残量 436→428・ヘッダ4B/割付け）と MPF_CYCLES ループの実証コメントを新レイアウトへ更新（コード・check_assert は無変更、checkpoint 15 のまま。ケースA のロールバックは count==0 backstop 経由で旧実装とビット単位で同一の全域リセットになるため判定は不変 — spec §7.3 の机上トレースどおり QEMU 実測 PASS で確認）。
```

(c) 既存の `test/test_dcre4.h（dcre段階3b Task 6）` 行の末尾に追記：

```markdown
★mempool freelist 化で MPK_SIZE 選定根拠コメントを新実装（バンプ＋完全一致再利用、1周 272B→280B、backstop で頭打ち）へ更新（値は 2048 のまま、negative control の成立条件も維持：del_mpf の TA_MEMALLOC 解放を落とすと freelist に 256B エントリが載らないため 7〜8 周で E_NOMEM になる関係は保存される）。
```

- [ ] **Step 9: コミット**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
git add kernel/startup.c test/test_dcre4.c test/test_dcre4.h DIVERGENCE_MAP.md
git commit -m "feat(kernel): カーネルメモリプールを完全一致フリーリスト再利用へ置換（dcre意図的逸脱・機能拡張）

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01F5qdprYPDaU9QYQTLfoc6Q"
git log --oneline -1        # ★このハッシュを記録する（Task 3 の qa 追記が参照）
```

- [ ] **Step 10: ROM/RAM A/B 実測（同一パス・コミット前後比較）**

★Global Constraint 7：**同一ビルドディレクトリパス**（`build/musca_b1-2core-ab`）で
親コミットと HEAD を比較する。worktree・別パスでの比較は不可。

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
git switch --detach HEAD~1
rm -rf build/musca_b1-2core-ab
cmake --preset musca_b1-2core -B build/musca_b1-2core-ab > /tmp/mp-ab-confA.log 2>&1; echo "confA rc=$?"
cmake --build build/musca_b1-2core-ab > /tmp/mp-ab-buildA.log 2>&1; echo "buildA rc=$?"
find build/musca_b1-2core-ab -name '*.elf' -exec arm-none-eabi-size {} \; | tee /tmp/mp-ab-sizeA.txt
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
git switch feature/dynamic-creation
rm -rf build/musca_b1-2core-ab
cmake --preset musca_b1-2core -B build/musca_b1-2core-ab > /tmp/mp-ab-confB.log 2>&1; echo "confB rc=$?"
cmake --build build/musca_b1-2core-ab > /tmp/mp-ab-buildB.log 2>&1; echo "buildB rc=$?"
find build/musca_b1-2core-ab -name '*.elf' -exec arm-none-eabi-size {} \; | tee /tmp/mp-ab-sizeB.txt
rm -rf build/musca_b1-2core-ab
```
text/data/bss の差分（B−A）をバイトで記録する。期待レンジ: **text +50〜+250B、
data 0、bss 0**（`freelist`/ヘッダはプール領域＝`_kernel_mpk` の内側に住むため
.bss は増えない。sample1 は `DEF_MPK` を持たないが ALLFUNC ビルドのため
コードは常にリンクされ text 差分は出る）。桁が違ったら実装を疑い停止。

- [ ] **Step 11: 実測値を task report に記録してコミット**

`.superpowers/sdd/2026-08-04-fmp3-mempool-freelist/task-1-report.md` に、
Step 6/7 の実測（PASS・cp 数）、Step 10 の A/B 数値（text/data/bss、比較コミット
ペア）、Step 9 のコミットハッシュを**事実として**記録（推測を書く場合は区別する）。

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
git add .superpowers/sdd/2026-08-04-fmp3-mempool-freelist/task-1-report.md
git commit -m "chore(dcre): mempool freelist 化の ROM/RAM A/B 実測を記録

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01F5qdprYPDaU9QYQTLfoc6Q"
```

---

### Task 2: 新規テスト `test_dcre6`（枯渇耐性＋bump 前進観測＋変異 control）

**Files:**
- Create: `test/test_dcre6.c` / `test/test_dcre6.cfg` / `test/test_dcre6.h`
- Modify: `test/MANIFEST`（`test_dcre5.h` の後・`test_dcre_mix.c` の前に3行）
- Modify: `test/testexec.rb`（`"dcre5"` の後・`"dcremix"` の前に1行）
- Modify: `DIVERGENCE_MAP.md`（新規5行）

**Interfaces:**
- Consumes: Task 1 の挙動契約と算術定数（`sizeof(MEMPOOLCB)==16`・ヘッダ4B・
  完全一致再利用・count==0 backstop）。公開 API は `acre_dtq`/`del_dtq`/
  `acre_mpf`/`del_mpf`/`pget_mpf`/`rel_mpf` のみ。
- Produces: `test_dcre6`（musca_b1-2core、**Check point 行数 = 6**）。
  Task 3 の全回帰リストに加わる。

**配置の決定（計画時所見・決定1の再掲）**: `test_dcre4` 拡張ではなく**新設**。
長寿命割付を N サイクル保持するシナリオは test_dcre4 の「acre/del 1:1 対」不変条件
（spec §7.3 の count==0 トレースの根拠）を壊し、checkpoint 数 15 も保てないため。

**机上算術（全数値の根拠。test_dcre6.h のコメントと同一）**

32bit（musca_b1、`sizeof(size_t)==4`）・`MPK_SIZE=2048`・`sizeof(MEMPOOLCB)=16`・
ヘッダ 4B・全要求が 4 の倍数（パディング無し）。プール先頭からのオフセットで書く：

| 操作 | 要求サイズ | フットプリント | brk（後） |
|---|---|---|---|
| 初期状態 | - | - | 16 |
| フェーズ0: 長寿命 dtq（dtqcnt=1） | 4 | 8 | 24 |
| フェーズA 1周目 ①mpf ブロック領域 `ROUND_MPF_T(64)*4` | 256 | 260 | 284 |
| フェーズA 1周目 ②mpf 管理領域 `sizeof(MPFMB)*4` | 16 | 20 | 304 |
| フェーズA 2〜16周目（freelist 完全一致で①②とも再利用） | - | 0 | **304 で頭打ち** |
| フェーズC probe dtq（dtqcnt=2、256/16 と不一致→bump） | 8 | 12 | 316 |
| フェーズC fill dtq（dtqcnt=432） | 1728 | 1732 | 2048（=limit、ちょうど） |
| フェーズC 境界 dtq（dtqcnt=1、freelist に 4 は無い） | 4 | 8 | **E_NOMEM**（残量0） |
| フェーズD 全 del → count==0 → backstop | - | - | 16 |
| フェーズD big dtq（dtqcnt=500） | 2000 | 2004 | 2020（≤2048、成功） |

**フェーズCの判別力**: probe（8B）が誤って 16B エントリを再利用する実装
（完全一致でなく「以上」で拾う実装）だと brk は 304 のまま → fill 後 brk=2036 →
残量 12B で境界 dtq（フットプリント8B）が**成功してしまう**。境界の E_NOMEM が
「不一致の要求で bump が前進した」ことの決定的観測になる（fill の成功が brk≤316、
境界の E_NOMEM が brk≥2041 を挟み撃ちで証明する）。

**変異 control の予測（実行前に記録すること）**: freelist 走査を無効化すると
フェーズAの周回ごとに 280B（260+20）が bump で消費され、8周目の①
（brk=24+280×7=1984、1984+260 > 2048）で E_NOMEM →
ループ内の `check_assert(erid > MPF1)` が失敗 → 最後に出る checkpoint は 1、
`TTSP_RESULT: FAIL`。**これは旧 bump 実装での必然の失敗の再現であり、
spec §7.1 の positive control（現行実装での失敗と新実装での成功の対比）そのもの。**

- [ ] **Step 1: preflight**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
git log --oneline -2                       # 期待: Task 1 の2コミットが先頭
grep -n "freelist" kernel/startup.c | head -3   # 期待: ヒットあり（Task 1 適用済み）
ls test/test_dcre6.* 2>/dev/null           # 期待: 存在しない
grep -n "dcre5\"" test/testexec.rb         # 挿入位置の確認
grep -n "test_dcre5.h" test/MANIFEST       # 挿入位置の確認
```

- [ ] **Step 2: `test/test_dcre6.h` を作成**

```c
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
```

- [ ] **Step 3: `test/test_dcre6.cfg` を作成**

```c
/*
 *		カーネルメモリプールの完全一致フリーリスト再利用のテストの
 *		システムコンフィギュレーションファイル
 *
 *  $Id$
 */
INCLUDE("test_common1.cfg");

#include "test_dcre6.h"

CLASS(CLS_PRC1) {
	CRE_TSK(TASK1, { TA_ACT, 1, task1, MID_PRIORITY, STACK_SIZE, NULL });
	/*
	 *  静的な dtq/mpf．AID_DTQ/AID_MPF が「静的オブジェクトが1個以上
	 *  あること」を要求する（訂正E ガード）ため必須．いずれも管理領域・
	 *  ブロック領域は cfg が静的に確保するのでカーネルメモリプールを
	 *  消費しない（プールを使うのは acre_* だけ）．
	 */
	CRE_DTQ(DTQ1, { TA_TPRI, 2, NULL });
	CRE_MPF(MPF1, { TA_TPRI, 2, 32, NULL, NULL });
}

/*  AID_* と DEF_MPK はクラス外専用  */
AID_DTQ(4);
AID_MPF(1);
DEF_MPK({ MPK_SIZE, NULL });
```

（`AID_DTQ(4)` の根拠：フェーズCで長寿命＋probe＋fill の3本が同時生存した
状態で境界 dtq を acre する。スロットが 3 以下だと境界検査が E_NOMEM ではなく
E_NOID になってしまう。）

- [ ] **Step 4: `test/test_dcre6.c` を作成**

ライセンスヘッダは `test/test_dcre4.c:1-38` と同一（Copyright (C) 2026 by
Embedded and Real-Time Systems Laboratory, Graduate School of Information
Science, Nagoya Univ., JAPAN / `$Id$`）を転記すること。以下はその直後からの
完全なコード：

```c
/*
 *		カーネルメモリプールの完全一致フリーリスト再利用のテスト
 *
 * 【テストの目的】
 *
 *	kernel/startup.c の既定メモリプール（バンプ＋完全一致サイズ再利用）の
 *	freelist 経路を runtime で実証する．
 *
 *	(A) 枯渇しないこと：長寿命割付（dtq 1本）を保持したまま同一サイズの
 *	    acre_mpf/del_mpf を MPF_CYCLES(=16) 周反復しても E_NOMEM に
 *	    ならない．旧実装（bump のみ）ではこの手順は 8 周目で必ず
 *	    E_NOMEM になる（★変異 control で再現する＝positive control）．
 *	(B) 完全一致再利用が同一番地を返すこと：acre_mpf → pget_mpf の
 *	    ブロック番地が del → 再 acre をまたいで一致する．
 *	(C) サイズ不一致は再利用されないこと：freelist に 256B/16B が載った
 *	    状態での 8B 要求が bump を前進させることを，残量ちょうどの fill
 *	    成功と直後の E_NOMEM の挟み撃ちで観測する（機序は test_dcre6.h
 *	    の机上算術コメント参照）．
 *	(D) count==0 backstop：全解放後に 2000B の大型割付が入る＝brk
 *	    リセットと freelist クリアの実証．
 *
 * 【使用リソース】
 *
 *	TASK1: 中優先度タスク，TA_ACT属性（静的・PRC1）
 *	DTQ1/MPF1: 静的なデータキュー／固定長メモリプール（AID_* の訂正E
 *	           ガード要件のため．プールは消費しない）
 *	AID_DTQ(4)/AID_MPF(1): 動的スロット
 *	DEF_MPK({ MPK_SIZE, NULL }): カーネルメモリプール（2048B）
 *
 * 【チェックポイント】
 *
 *	PRC1（TASK1 のみ）: 1..5 + check_finish(6)
 *	  ＝ログ中の "Check point" 行は合計 6 本（check_finish 自身の 1 本を含む）
 *
 * 【前提】
 *
 *	musca_b1-2core でのみ実行する（32bit・机上算術の前提．実行時に
 *	check_assert(sizeof(size_t) == 4U) で固定する）．
 */

#include <kernel.h>
#include <t_syslog.h>
#include "syssvc/test_svc.h"
#include "kernel_cfg.h"
#include "test_dcre6.h"

/* DO NOT DELETE THIS LINE -- gentest depends on it. */

void
task1(EXINF exinf)
{
	T_CDTQ		cdtq;
	T_CMPF		cmpf;
	ER_ID		erid;
	ID			dtqid_long, dtqid_probe, dtqid_fill, dtqid_big, mpfid1;
	void		*blk_a;
	void		*blk_b;
	uint_t		i;

	test_start(__FILE__);
	check_point(1);

	/*  本テストの机上算術は 32bit（sizeof(size_t)==4）前提（test_dcre6.h）  */
	check_assert(sizeof(size_t) == 4U);

	/*
	 *  フェーズ0: 長寿命割付（count が 0 に戻らない状況を作る）
	 */
	cdtq.dtqatr = TA_TPRI;
	cdtq.dtqcnt = LONG_DTQCNT;
	cdtq.dtqmb = NULL;
	erid = acre_dtq(&cdtq);
	check_assert(erid > DTQ1);
	dtqid_long = (ID) erid;

	/*
	 *  フェーズA: 長寿命割付を保持したままの acre_mpf/del_mpf × MPF_CYCLES
	 *
	 *  旧実装（bump のみ）では 8 周目の①（ブロック領域 260B）で E_NOMEM
	 *  になる（机上算術は test_dcre6.h）．新実装では 2 周目以降①②とも
	 *  freelist の完全一致で再利用され，E_NOMEM にならない．
	 *  ★変異 control（freelist 走査の無効化）では 8 周目（i==7）で
	 *  下の check_assert(erid > MPF1) が落ちる．
	 */
	cmpf.mpfatr = TA_TPRI;
	cmpf.blkcnt = MPF_BLKCNT;
	cmpf.blksz = MPF_BLKSZ;
	cmpf.mpf = NULL;							/*  ブロック領域を自動確保  */
	cmpf.mpfmb = NULL;							/*  管理領域を自動確保  */
	for (i = 0U; i < MPF_CYCLES; i++) {
		erid = acre_mpf(&cmpf);
		check_assert(erid > MPF1);
		mpfid1 = (ID) erid;
		check_ercd(del_mpf(mpfid1), E_OK);
	}
	check_point(2);

	/*
	 *  フェーズB: 完全一致再利用が同一番地を返すことの直接観測
	 *
	 *  直前の del でブロック領域（256B）と管理領域（16B）が freelist に
	 *  載っている．同一サイズの acre_mpf はどちらも完全一致で再利用する
	 *  ので，pget_mpf が返すブロック番地は del/再 acre をまたいで一致する．
	 */
	erid = acre_mpf(&cmpf);
	check_assert(erid > MPF1);
	mpfid1 = (ID) erid;
	check_ercd(pget_mpf(mpfid1, &blk_a), E_OK);
	check_ercd(rel_mpf(mpfid1, blk_a), E_OK);
	check_ercd(del_mpf(mpfid1), E_OK);

	erid = acre_mpf(&cmpf);
	check_assert(erid > MPF1);
	mpfid1 = (ID) erid;
	check_ercd(pget_mpf(mpfid1, &blk_b), E_OK);
	check_assert(blk_a == blk_b);				/*  ★同一番地の再利用  */
	check_ercd(rel_mpf(mpfid1, blk_b), E_OK);
	check_ercd(del_mpf(mpfid1), E_OK);
	check_point(3);

	/*
	 *  フェーズC: サイズ不一致は再利用されない（bump 前進の観測）
	 *
	 *  ここで brk=304・freelist={256B, 16B}（机上算術は test_dcre6.h）．
	 */
	cdtq.dtqcnt = PROBE_DTQCNT;					/*  8B：どちらとも不一致  */
	erid = acre_dtq(&cdtq);
	check_assert(erid > DTQ1);
	dtqid_probe = (ID) erid;

	cdtq.dtqcnt = FILL_DTQCNT;					/*  1728B：残量ちょうど  */
	erid = acre_dtq(&cdtq);
	check_assert(erid > DTQ1);					/*  ★brk が 316 以下だった証拠  */
	dtqid_fill = (ID) erid;

	cdtq.dtqcnt = BOUND_DTQCNT;					/*  4B：残量 0  */
	check_assert(acre_dtq(&cdtq) == E_NOMEM);	/*  ★probe の bump 前進の決定的観測  */
	check_point(4);

	/*
	 *  フェーズD: 全解放 → count==0 backstop の全域リセット
	 */
	check_ercd(del_dtq(dtqid_fill), E_OK);
	check_ercd(del_dtq(dtqid_probe), E_OK);
	check_ercd(del_dtq(dtqid_long), E_OK);		/*  ここで count==0 → リセット  */

	cdtq.dtqcnt = BIG_DTQCNT;					/*  2000B  */
	erid = acre_dtq(&cdtq);
	check_assert(erid > DTQ1);					/*  ★全域リセットの実証  */
	dtqid_big = (ID) erid;
	check_ercd(del_dtq(dtqid_big), E_OK);
	check_point(5);

	check_finish(6);
}
```

- [ ] **Step 5: `test/MANIFEST` と `test/testexec.rb` に登録**

`test/MANIFEST`：`test_dcre5.h` の行の直後（`test_dcre_mix.c` の前。
アルファベット順：`5` < `6` < `_`）に3行追加：

```
test_dcre6.c
test_dcre6.cfg
test_dcre6.h
```

`test/testexec.rb`：`"dcre5"    => { SRC: "test_dcre5" },` の直後に1行追加：

```ruby
  "dcre6"    => { SRC: "test_dcre6" },
```

- [ ] **Step 6: ビルド＋等価性（fresh）**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
rm -rf build/musca_b1-2core-tdcre6
cmake --preset musca_b1-2core -B build/musca_b1-2core-tdcre6 \
  -DFMP3_APPLNAME=test_dcre6 -DFMP3_APPLDIR=test \
  -DFMP3_SYSSVC_TARGET_C_FILES=$PWD/syssvc/test_svc.c > /tmp/mp-t2-conf.log 2>&1
echo "conf rc=$?"
cmake --build build/musca_b1-2core-tdcre6 > /tmp/mp-t2-build.log 2>&1; echo "build rc=$?"
tools/cfg_equivalence.sh build/musca_b1-2core-tdcre6 > /tmp/mp-t2-eq.log 2>&1; echo "eq rc=$?"
```
期待: 3つとも rc=0（eq の 2 は不合格）。

- [ ] **Step 7: QEMU 実行（正常系）**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 90 cmake --build build/musca_b1-2core-tdcre6 --target run > /tmp/mp-t2-run.log 2>&1
grep 'TTSP_RESULT: PASS' /tmp/mp-t2-run.log
grep -c 'Check point' /tmp/mp-t2-run.log       # 期待: 6
grep -c 'Assertion\|Unexpected' /tmp/mp-t2-run.log   # 期待: 0
pgrep -a qemu                                  # 期待: 出力なし
```
★期待 6 と食い違ったら（Global Constraint 10）実測を正としてヘッダコメントを
直すのではなく、**まずどのフェーズで数が変わったかをログで特定する**
（このテストの cp 数は机上算術と1:1対応しており、ズレは算術の誤り＝実装か
計画の欠陥を意味する）。

- [ ] **Step 8: 変異 control（★予測を先に記録してから実行）**

**予測（report に転記してから実行すること）**: freelist 走査を無効化すると、
フェーズAの 8 周目（i==7）の `acre_mpf` が E_NOMEM を返し、ループ内の
`check_assert(erid > MPF1)` で Assertion failed → 最後に出る checkpoint は 1、
`TTSP_RESULT: FAIL`。

変異：`kernel/startup.c` の `alloc_mempool` 内、freelist 走査の一致判定に
`0 &&` を挿入する（unlink を不能化。unused 警告を出さない最小変異）：

```c
		if (0 && p_mphdr->size == size
				&& (((uintptr_t) p_freeblk) & adjust) == 0U) {
```

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
cmake --build build/musca_b1-2core-tdcre6 > /dev/null 2>&1
timeout -k 5 90 cmake --build build/musca_b1-2core-tdcre6 --target run > /tmp/mp-t2-mut.log 2>&1
grep 'TTSP_RESULT' /tmp/mp-t2-mut.log          # 期待: FAIL
grep 'Assertion' /tmp/mp-t2-mut.log            # 期待: erid > MPF1 の失敗
grep -c 'Check point' /tmp/mp-t2-mut.log       # 期待: 1（フェーズA前の cp1 のみ）
pgrep -a qemu
```
★実測が予測と食い違ったら（例: 8周目以外で落ちる）、値を調整して通すのでは
なく**机上算術のどこが違ったかを特定して report に記録する**。

- [ ] **Step 9: 変異を復元し、非退行を再確認**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
# 変異（0 && ）を取り除いたのち：
git diff --stat kernel/                        # 期待: 空
cmake --build build/musca_b1-2core-tdcre6 > /dev/null 2>&1
timeout -k 5 90 cmake --build build/musca_b1-2core-tdcre6 --target run > /tmp/mp-t2-restore.log 2>&1
grep 'TTSP_RESULT: PASS' /tmp/mp-t2-restore.log
grep -c 'Check point' /tmp/mp-t2-restore.log   # 期待: 6
pgrep -a qemu
```

- [ ] **Step 10: `DIVERGENCE_MAP.md` に5行追加**

```markdown
| test/test_dcre6.c（mempool freelist 化） | add (dcre-port) | カーネルメモリプールの完全一致フリーリスト再利用の QEMU 回帰テスト本体（musca_b1-2core 専用・32bit 机上算術前提を `check_assert(sizeof(size_t) == 4U)` で固定）。(A) 長寿命 dtq（4B）保持下で同一サイズ acre_mpf/del_mpf × 16 周が E_NOMEM にならない（旧 bump 実装では 8 周目で必然的に E_NOMEM＝変異 control で再現する positive control）、(B) del/再 acre をまたいだ pget_mpf ブロック番地の一致（完全一致再利用の直接観測）、(C) サイズ不一致（8B 要求 vs freelist{256B,16B}）が bump を前進させることを「残量ちょうど 1728B の fill 成功」と「直後 4B 要求の E_NOMEM」の挟み撃ちで観測、(D) 全解放後の 2000B 割付成功による count==0 backstop（brk リセット＋freelist クリア）の実証、の4フェーズ・checkpoint 6。変異 control（`alloc_mempool` の freelist 一致判定への `0 &&` 挿入）で 8 周目の `check_assert(erid > MPF1)` が予測どおり倒れることを実演し、復元後 `git diff --stat kernel/` が空であることを確認 | - |
| test/test_dcre6.cfg（mempool freelist 化） | add (dcre-port) | 上記テストのシステムコンフィギュレーションファイル。CLS_PRC1 に TASK1 と静的 DTQ1/MPF1（AID_* の訂正E ガード要件。cfg 静的確保のためプール不使用）、クラス外に AID_DTQ(4)（フェーズCで dtq 3本同時生存＋境界検査の第4スロットが必要——3以下だと境界が E_NOMEM でなく E_NOID になる）・AID_MPF(1)・DEF_MPK({ MPK_SIZE, NULL }) | - |
| test/test_dcre6.h（mempool freelist 化） | add (dcre-port) | 上記テストのヘッダ。MPK_SIZE=2048・MPF_BLKCNT=4/MPF_BLKSZ=64/MPF_CYCLES=16・LONG/PROBE/FILL/BOUND/BIG_DTQCNT=1/2/432/1/500 と、全数値の机上算術（実効容量 2032・1周 280B・旧実装 8 周目 E_NOMEM・fill 1732B ちょうど・リセット後 2004B）のコメント | - |
| test/MANIFEST（mempool freelist 化） | mod (dcre-port) | `test_dcre5.h` の後・`test_dcre_mix.c` の前（アルファベット順）に `test_dcre6.c`/`test_dcre6.cfg`/`test_dcre6.h` の3行を追加 | - |
| test/testexec.rb（mempool freelist 化） | mod (dcre-port) | `"dcre5"` の後・`"dcremix"` の前に `"dcre6" => { SRC: "test_dcre6" },` を追加 | - |
```

- [ ] **Step 11: コミット**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
git add test/test_dcre6.c test/test_dcre6.cfg test/test_dcre6.h \
        test/MANIFEST test/testexec.rb DIVERGENCE_MAP.md
git commit -m "test(dcre): メモリプール完全一致再利用の枯渇耐性テスト test_dcre6（変異control付き）

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01F5qdprYPDaU9QYQTLfoc6Q"
```

---

### Task 3: 文書更新＋最終回帰＋台帳

**Files:**
- Modify: `docs/dynamic-creation.md`（§6 全面書換え・§7.1 経路追記・
  §6末尾/§7.5 の stale 是正〔計画時所見4〕）
- Modify: `docs/qa-esp32s3-20260804.md`（末尾に R-2 変更確定の追記）
- Modify: `docs/qa-esp32s3-20260804-2.md`（末尾に Q-4 変更確定の追記）
- Modify: `.superpowers/sdd/progress.md`（フラット台帳へ完了記録）

**Interfaces:**
- Consumes: Task 1 のコミットハッシュ（`git log` で取得）と
  `.superpowers/sdd/2026-08-04-fmp3-mempool-freelist/task-1-report.md` の
  ROM 実測値（qa 追記へ転記）。Task 2 の `test_dcre6`（回帰リスト）。
- Produces: 完結した文書群と全回帰の実測記録。

- [ ] **Step 1: preflight**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
git log --oneline -4        # Task 1×2 + Task 2×1 のコミットが積まれていること
H=$(git log --format=%h --grep='完全一致フリーリスト再利用へ置換' -1); echo "T1=$H"
grep -n "text" .superpowers/sdd/2026-08-04-fmp3-mempool-freelist/task-1-report.md | head -3
sed -n '413,428p;443,448p;456,462p;518,526p' docs/dynamic-creation.md
```
`$H`（Task 1 ハッシュ）と ROM 実測値を控える。dynamic-creation.md の行番号が
ずれていたら現物の見出しで位置を特定し直す（内容一致を正とする）。

- [ ] **Step 2: `docs/dynamic-creation.md` §6 を書き換え**

(a) §6 冒頭〜箇条書き（現 415-427 行）を以下へ置換：

```markdown
`DEF_MPK` が定義するカーネルメモリプールは，**バンプ確保＋完全一致サイズ
再利用（freelist）**のアロケータである（TLSF 等の汎用アロケータではない。
dcre 原形の「全解放まで再利用しない」単純 bump を，本プロジェクトで機能拡張
した——`DIVERGENCE_MAP.md` の「mempool freelist 化」行参照）。`malloc_mpk`/
`free_mpk`（`kernel/kernel_impl.h`）経由で使う。

- 確保は，まず解放済みブロックの単方向リスト（freelist）を先頭から走査し，
  **要求サイズが解放時の要求サイズと完全に一致し，かつ番地が要求アライン
  メントに適合する**最初のブロックがあればそれを再利用する。無ければ従来
  どおり「現在のブレークポイントからヘッダ＋必要バイト数を切り出して前進
  させる」バンプ確保になる。**ブロックの分割・結合は行わない**。
- **「途中の1個だけ返す」操作でも，同一サイズの次の要求はそのブロックを
  再利用する**。したがって `del_*` → `acre_*` を同一サイズで繰り返す典型的
  な使い方では，他の長寿命割付が生存していてもアリーナは痩せない
  （`test_dcre6` が実演している。旧実装ではこの手順は必然的に `E_NOMEM` に
  到達した）。サイズの異なる要求には再利用が効かず，ブレークポイントが
  前進する点は従来どおり。
- **返却された確保数（`count`）がゼロになったときは，ブレークポイントを
  プール先頭へ戻し freelist も空にする**（従来の全域リセットを backstop
  として維持）。
- 各割付の直前に `size_t` 1個分のヘッダが付くため，割付1件あたり
  4B（32bit）／8B（64bit）のオーバーヘッドがある。プール制御ブロックも
  12B→16B（32bit）に増えた。`mpksz` の見積りにはこの分を積むこと。
- 確保の計算量は O(freelist 長)（上界はプールに収まる最大割付数で決定的に
  有界），解放は O(1)。ギャント・ロック下でしか呼ばれないため，アロケータ
  自体は排他制御を持たない。
```

(b) §6 の「サイズ見積り」段落（現 436-441 行）は無変更で残す。

(c) §6 末尾の「**注意（未 hardening）**」段落（現 443-448 行）を以下へ置換
（計画時所見4の是正。hardening パス commit `fb5e369` で検査は入っている）：

```markdown
**注意（hardening 済み）**：`acre_dtq`/`acre_pdq`/`acre_mpf`/`acre_tsk` の
管理領域・ブロック領域・スタックのサイズ計算（乗算・丸め）のオーバーフロー
は，hardening パスで追加された `CHECK_PAR`（計6検査）が `E_PAR` で塞いでいる
（確定内容は `docs/qa-esp32s3-20260804.md` 末尾の追記を参照）。§7.5 も参照。
```

- [ ] **Step 3: `docs/dynamic-creation.md` §7.1 に完全一致再利用の経路を追記**

(a) §7.1 導入文（現 458-461 行）の「bump アロケータの再利用」を
「アロケータの再利用（count==0 の全域リセット，または完全一致サイズ再利用）」
へ書き換える：

```markdown
いずれも「giant lock の外でアプリケーションが直接触れる資源」と「アロケータ
の再利用（`count==0` の全域リセット，または完全一致サイズ再利用——後者は
メモリプール freelist 化で追加された経路）」が絡む，理論上の競合ウィンドウ
である。実害が到達可能であることをテストで実証したものではないが，発生条件を
明示した上で受容する判断をしている。
```

(b) 窓(1) の段落中「プールの全確保数が0になれば（bump アロケータの再利用
条件），次の `acre_tsk` が同一メモリを新タスクへ渡し得る。」を以下へ置換：

```markdown
プールの全確保数が0になる（全域リセット）か，**同一 `stksz` の割付要求が来る
（完全一致サイズ再利用）**かのいずれかで，次の `acre_tsk` が同一メモリを
新タスクへ渡し得る。
```

(c) 窓(2) の競合列中「タスクBが `del_mpf`（プールの確保数が0になり bump の
先頭がリセット）→ タスクBが別種の `acre_*`（新しい管理領域が同一バイト列に
確保される）」を以下へ置換：

```markdown
タスクBが `del_mpf`（プールの確保数が0になり全域リセットされるか，解放された
領域が freelist に載る）→ タスクBが別の `acre_*`（全域リセット後の再確保，
または**同一サイズ要求の完全一致再利用**により，新しい管理領域が同一バイト列
に確保される）
```

(d) §7.1 末尾（現 488-491 行の spec 参照段落の直後）に裁定記録を追加：

```markdown
**【2026-08-04 追記・裁定】** メモリプールの完全一致フリーリスト化
（`docs/superpowers/specs/2026-08-04-fmp3-mempool-freelist-design.md` §8）に
より，上記2件の窓は「`count==0` の全域リセット」を待たず「同一サイズの再要求」
という局所条件だけで到達しうるようになった（**機序自体は不変・到達確率が
上がる方向の変化**。§1.2 の動機——同一スタックサイズのタスクの反復
create/delete——はまさにこの新経路が最も起こりやすい形である）。
ユーザはこれを受容した。根拠：(a) 解放済みメモリの再利用を行うアロケータ
（TLSF/o1heap 含む）は本質的に同じ性質を持つ，(b) 窓2件は段階1/3b で
「アプリ契約」（休止確認後の del・未返却ブロック持ち del は契約違反）として
裁定済みであり，契約を守る限り窓は開かない，(c) 本節に成立経路として明記した
（この追記）。
```

- [ ] **Step 4: `docs/dynamic-creation.md` §7.5 の stale 記述を是正（計画時所見4）**

§7.5 の本文（現 518-526 行）を以下へ置換（見出しも変える）：

```markdown
### 7.5 サイズ乗算オーバーフロー検査（hardening 済み）

§6 末尾のとおり，`acre_dtq`/`acre_pdq`/`acre_mpf`/`acre_tsk` のサイズ計算の
乗算・丸めオーバーフローは，hardening パスで追加された `CHECK_PAR`（4 API・
6検査）が `E_PAR` で塞いだ（確定内容・64bit での各検査の生死は
`docs/qa-esp32s3-20260804.md` 末尾の追記を参照）。`kernel/startup.c` の
`malloc_mempool` が持っていた符号混在比較（§9 上流報告候補 c）も同パスで
一体修正済みである（本ブランチでは修正済み・上流へは未報告）。
なお本節の旧記述（「未検査・hardening 予定」）は hardening パス完了後も
本文書だけ更新されずに残っていたもので，メモリプール freelist 化の文書更新
（2026-08-04）で是正した。
```

- [ ] **Step 5: `docs/qa-esp32s3-20260804.md` 末尾に R-2 変更確定を追記**

ファイル末尾（既存の「追記（2026-08-04・hardening pass 確定）」節の後）に追加。
`<T1ハッシュ>`・`<text増分>` は Step 1 で控えた実値に置き換えること：

```markdown
---

## 追記（2026-08-04・メモリプール freelist 化）R-2 の「単調に痩せる」前提の変更確定

R-2 の「★設計判断に効く追加情報」で述べた **「free_mpk は割当カウントを減らす
だけで brk は戻らない／プール上に他の生存割当が1つでも残る限り create/self-delete
の反復でアリーナは単調に痩せ続ける」は，commit `<T1ハッシュ>`（feat(kernel):
カーネルメモリプールを完全一致フリーリスト再利用へ置換）で前提が変わった**。

- 新実装：解放済みブロックを freelist に記録し，**要求サイズが完全一致（かつ
  アライン適合）する場合は count==0 を待たず即座に再利用する**。同一 `stksz` の
  タスクや同一諸元の dtq/pdq/mpf を create/delete で回す限り，長寿命割付が共存
  していてもアリーナは痩せない（`test/test_dcre6.c` で QEMU 実証。旧実装では
  同手順が 8 周目で必然的に E_NOMEM——変異 control で再現済み）。
- 変わらないもの：`count==0` の全域リセット（backstop）・API・呼出し側・
  `OMIT_MEMPOOL_DEFAULT` フック。**分割・結合・サイズクラス化はしない**
  （異なるサイズの要求は従来どおり bump を前進させる）。
- コスト：割付1件あたり +4B（32bit）／+8B（64bit）のヘッダ，プール制御
  ブロック +4B/+8B。`mpksz` の見積りにはこの分を積むこと。
- **実測 ROM 増分**（musca_b1-2core・同一ビルドパスでの A/B 比較，事前見積り
  +100〜250B に対する実測値）：**text <text増分>B / data 0 / bss 0**。
- 貴側への含意：R-2 で挙げた回避策（ユーザ供給スタック／世代単位の全解放）は
  引き続き有効だが，「プール利用＝単調劣化」という前提での必須策ではなくなった。
  なお貴側は `_kernel_mpksz = 0`（プール不使用）であり本変更の受益者ではない
  （貴側 REPLY3 の帰属訂正どおり）。設計判断の再考材料として通知する。
```

- [ ] **Step 6: `docs/qa-esp32s3-20260804-2.md` 末尾に Q-4 変更確定を追記**

ファイル末尾（「追記: test_dcre5 自立化 完了」節の後）に追加。
ハッシュは Step 5 と同じ実値：

```markdown
---

## 追記（2026-08-04・メモリプール freelist 化）Q-4 補足の変更確定

Q-4 の 4.(a) で「tsk/dtq/pdq/mpf で NULL 供給するとカーネルメモリプール
（bump アロケータ・前回 R-2 参照）を使う」と述べた **bump アロケータの部分が
commit `<T1ハッシュ>` で「バンプ＋完全一致サイズ再利用」へ変わった**
（詳細・実測 ROM 増分は `docs/qa-esp32s3-20260804.md` 末尾の追記を参照）。

- Q-4 の本旨（AID_*(n) の上限は RAM の線形増だけ・スロット表の概算）は
  **不変**（AID_* のスロット表はプールと無関係の静的確保）。
- 変わるのは NULL 供給時のプール劣化特性のみ：同一サイズの create/delete
  反復ではアリーナが痩せなくなった。貴側の「tsk/dtq はユーザ供給（案B）」の
  判断は引き続き成立するが，その根拠のうち「プールは単調に痩せる」は
  過去形になった点だけ訂正する。
- 割付1件あたり +4B/+8B のヘッダが付くため，NULL 供給を使う場合の `mpksz`
  見積りには割付件数×ヘッダ分を積み増すこと。
```

- [ ] **Step 7: 全 8 プリセット configure+build（fresh は不要、通常ビルド）**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
for p in musca_b1 musca_b1-2core rp2350_pico2 polarfire_soc_kit-qemu \
         kria_arm64 kria_arm64-1core kria_r5 kria_r5-2core; do
  cmake --preset $p > /tmp/mp-t3-conf-$p.log 2>&1; c=$?
  cmake --build build/$p > /tmp/mp-t3-build-$p.log 2>&1; b=$?
  echo "$p conf=$c build=$b"
done
```
期待: 8 構成すべて conf=0 build=0（実機用 `polarfire_soc_kit` プリセットは
ツールチェーン不在の既知の環境ギャップのため対象外）。
★kria_arm64（64bit）のビルド成功は、所見2（4B mpfmb と 8B FREEBLK）の
コンパイル面の確認を兼ねる（runtime は既存回帰の範囲外——64bit で
`acre_mpf(blkcnt=1)` を del する runtime テストは存在しない。これは既知の
検証限界として台帳に記す）。

- [ ] **Step 8: QEMU 起動 7 構成（プリセットごとに個別実行・毎回 pgrep）**

以下を**1構成ずつ**実行する（1コマンドにまとめない）：

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 60 cmake --build build/musca_b1 --target run > /tmp/mp-t3-q-musca_b1.log 2>&1
grep -c 'Processor .* start' /tmp/mp-t3-q-musca_b1.log    # 期待: 1
pgrep -a qemu
```
同型で残り6構成（期待値つき）：
- `musca_b1-2core` → `Processor .* start` 2行
- `polarfire_soc_kit-qemu` → 4行
- `kria_arm64-1core` → 1行
- `kria_arm64` → 4行
- `kria_r5` → 1行
- `kria_r5-2core` → 2行

（musca_b1-2core PRC2／kria_r5-2core PRC1 の
`no time event is processed in hrt interrupt` は CLAUDE.md 記載の既知の
非致命的通知であり失敗ではない。）

- [ ] **Step 9: 機能テスト全回帰（fresh dir・正しいプリセット・個別実行）**

tdcre1/tdcre4/tdcre6 は Task 1/2 で fresh 実行済みだが、**文書コミット後の
HEAD で全部やり直す**（fresh 規則）。各テストとも次の形（`<N>` を差し替え）：

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
rm -rf build/musca_b1-2core-tdcre<N>
cmake --preset musca_b1-2core -B build/musca_b1-2core-tdcre<N> \
  -DFMP3_APPLNAME=test_dcre<N> -DFMP3_APPLDIR=test \
  -DFMP3_SYSSVC_TARGET_C_FILES=$PWD/syssvc/test_svc.c > /tmp/mp-t3-c<N>.log 2>&1
echo "conf rc=$?"
cmake --build build/musca_b1-2core-tdcre<N> > /tmp/mp-t3-b<N>.log 2>&1; echo "build rc=$?"
tools/cfg_equivalence.sh build/musca_b1-2core-tdcre<N> > /tmp/mp-t3-e<N>.log 2>&1; echo "eq rc=$?"
timeout -k 5 120 cmake --build build/musca_b1-2core-tdcre<N> --target run > /tmp/mp-t3-r<N>.log 2>&1
grep 'TTSP_RESULT: PASS' /tmp/mp-t3-r<N>.log
grep -c 'Check point' /tmp/mp-t3-r<N>.log
pgrep -a qemu
```

| テスト | プリセット | 期待 Check point 行数 |
|---|---|---|
| test_dcre1 | musca_b1-2core | 13 |
| test_dcre2 | musca_b1-2core | 11 |
| test_dcre3 | musca_b1-2core | 14 |
| test_dcre4 | musca_b1-2core | **15（★不変が合格条件）** |
| test_dcre5 | musca_b1-2core | 13 |
| test_dcre6 | musca_b1-2core | 6 |
| test_int2 | musca_b1（単一コア で正しい） | PASS（行数は記録） |

`test_dcre_mix`（musca_b1-2core-tmix）・`kria_arm64-tmix` は台帳記載どおり
**build + equivalence のみ**（QEMU 実行対象外）：

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
rm -rf build/musca_b1-2core-tmix
cmake --preset musca_b1-2core -B build/musca_b1-2core-tmix \
  -DFMP3_APPLNAME=test_dcre_mix -DFMP3_APPLDIR=test \
  -DFMP3_SYSSVC_TARGET_C_FILES=$PWD/syssvc/test_svc.c > /tmp/mp-t3-cmix.log 2>&1
echo "conf rc=$?"
cmake --build build/musca_b1-2core-tmix > /tmp/mp-t3-bmix.log 2>&1; echo "build rc=$?"
tools/cfg_equivalence.sh build/musca_b1-2core-tmix > /tmp/mp-t3-emix.log 2>&1; echo "eq rc=$?"
```
（kria_arm64-tmix も同型：`--preset kria_arm64 -B build/kria_arm64-tmix ...`。
test_int2 は `--preset musca_b1 -B build/musca_b1-tint2` で同型。）

★どの Check point 数も**変わってよいものは無い**（test_dcre6 の 6 は新規）。
変わっていたら本計画が意図せず挙動を変えている——止まって報告する。

- [ ] **Step 10: エラー経路回帰マトリクス 38/38**

`docs/superpowers/plans/2026-08-04-fmp3-dcre-hardening.md` の Task 5 Step 6 に
**全 38 件の逐語コマンド列**（builddir・期待 ercd・`EXTRA_CFLAGS` 明記）がある。
それをそのまま実行する（run.sh は 4 引数形。`-I$PWD/test` が要る cfg に注意）。
期待: **38 件すべて rc=0**。`rc=2` は前提未充足で不合格。
`ls tools/cfg_error_tests/*.cfg | wc -l` が 38 のまま（本計画は cfg を増やさない）
であることも確認する。

- [ ] **Step 11: 全プリセット equivalence + FCSRCS 突き合わせ**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
for p in musca_b1 musca_b1-2core rp2350_pico2 polarfire_soc_kit-qemu \
         kria_arm64 kria_arm64-1core kria_r5 kria_r5-2core; do
  tools/cfg_equivalence.sh build/$p > /tmp/mp-t3-eq-$p.log 2>&1
  echo "$p eq=$?"
done
```
期待: 8/8 が exit 0（本計画は cfg に1バイトも触れていないので当然一致のはず。
差が出たら止まって原因を突き止める）。

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
diff <(awk '/^KERNEL_FCSRCS/{f=1} f{print} f&&!/\\$/{exit}' kernel/Makefile.kernel \
       | grep -oE '[a-z_]+\.c' | sort -u) \
     <(grep -oE 'kernel/[a-z_]+\.c' CMakeLists.txt | sed 's#kernel/##' | sort -u)
echo "diff rc=$?"
```
期待: rc=0（本計画は `.c` を kernel に増やしていない。`KERNEL_FCSRCS` 22個のまま）。

- [ ] **Step 12: `DIVERGENCE_MAP.md` 完全性監査**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
git diff main...HEAD --name-only > /tmp/mp-t3-changed.txt
while read f; do
  case "$f" in
    docs/*|tools/*|cmake/*|CMakeLists.txt|CMakePresets.json|cfg_py/*|.superpowers/*|kernel/*.py) continue;;
  esac
  grep -q -- "$f" DIVERGENCE_MAP.md || echo "MISSING: $f"
done < /tmp/mp-t3-changed.txt
```
期待: `MISSING:` が 1 行も出ない。

- [ ] **Step 13: `.superpowers/sdd/progress.md`（フラット台帳）へ完了記録**

記録に含めること（**事実と推測を分ける**）：
- 【事実】既定メモリプールを完全一致フリーリストへ**置換**（ユーザ裁定2件：
  置換／完全一致のみ）。API・外部シンボル・cfg・`OMIT_MEMPOOL_DEFAULT` 不変。
- 【事実】canary（test_dcre4 15cp）不変で PASS＝count==0 backstop 保存の実測。
- 【事実】test_dcre6 新設（6cp）。変異 control（freelist 無効化）が予測どおり
  8 周目で倒れた／倒れなかったの実測（予測と実測の照合結果を明記）。
- 【事実】ROM/RAM A/B 実測値（text/data/bss、比較コミットペア、同一パス）。
- 【事実】文書4点の更新（dynamic-creation §6/§7.1/§7.5・qa×2 のハッシュ入り追記）
  と、§6末尾/§7.5 の hardening 済み記述への是正（計画時所見4）。
- 【事実】回帰: builds 8/8・QEMU 起動 7/7 孤児なし・機能テスト（dcre1〜6・int2）
  の PASS と cp 数・mix×2 build+eq・エラー行列 38/38・equivalence 8/8＋
  テストdir 分・FCSRCS diff rc=0・台帳監査 MISSING=0。
- 【引き継ぎ・推測含む】(a) 64bit で `sizeof(MPFMB)*1=4B < FREEBLK 8B` の割付は
  死領域はみ出しで安全（論証はコードコメント）だが **runtime 実測は無い**
  （64bit で mpf blkcnt=1 の acre/del を回すテストが存在しない）。
  (b) freelist 走査は O(長)。多品種・多サイズ反復では線形コスト（spec §8 受容済み）。
  (c) 窓(1)(2) の到達確率が上がる方向の変化はユーザ受容済み（`4f2eb4f` 裁定）。

- [ ] **Step 14: コミット**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
git add docs/dynamic-creation.md docs/qa-esp32s3-20260804.md \
        docs/qa-esp32s3-20260804-2.md .superpowers/sdd/progress.md
git commit -m "docs(dcre): メモリプール freelist 化の文書更新と最終回帰（dynamic-creation §6/§7.1・qa 2文書へ変更確定を追記）

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01F5qdprYPDaU9QYQTLfoc6Q"
```

---

## Self-Review（計画作成時に実施済み・記録）

1. **Spec 網羅**: §2 決定1/2→Task 1（置換・完全一致のみ・分割なし）。§3 データ構造
   ・レイアウト・最小割付・size==0 非対応→Task 1 Step 2（コード＋コメント）、
   64bit mpfmb の見落としは所見2として訂正込みで実装。§4 アルゴリズム（走査条件・
   あふれガード・O(1) free・backstop）→Task 1 Step 2。§5 整合（rename/allfunc/
   mpk_valid/OMIT 不変）→Task 1 注意書き＋Step 3 ビルド。§6 波及（startup.c
   コメント→T1、dynamic-creation §6→T3 Step 2、qa×2→T3 Step 5/6、
   DIVERGENCE_MAP→T1 Step 8）。§7.1 新規シナリオ3点（枯渇しない／不一致非再利用
   ／mutation control）→Task 2 フェーズA/C＋Step 8。§7.2 監査3点→T1 Step 6
   （canary 15cp）＋机上確認を所見・コメントに反映（negative control の成立条件は
   UINT_MAX 系でヘッダ増減に非依存＝検証不要のまま維持）。§7.3 の【算術】更新→
   T1 Step 4。§7.4 全回帰・A/B 同一パス・cfg 影響ゼロ→T3 Step 7-11＋T1 Step 10。
   §8 assert 検討→決定2で採用。§8 裁定（窓の到達性）→T3 Step 3(d) の裁定記録。
   §9 スコープ外（TLSF/o1heap/分割）→どのタスクにも含めていない。ギャップなし。
2. **プレースホルダ走査**: 「TBD」「後で」「〜と同様」なし。全コード・全コメント・
   全文書差分は逐語で記載。qa 追記の `<T1ハッシュ>`/`<text増分>` は Task 3 Step 1 で
   取得する実測値の**差し込み位置指定**であり、取得手順を同 Step に明記済み。
3. **型・名称整合**: `MPHDR.size`（size_t）／`FREEBLK.next`／`MEMPOOLCB.freelist`
   （FREEBLK*）は Task 1 定義と Task 2 の算術（16B CB・4B ヘッダ）・Task 3 の文書
   記述で一致。テスト定数名（LONG/PROBE/FILL/BOUND/BIG_DTQCNT・MPF_CYCLES）は
   .h と .c で一致。cp 数（dcre4=15 不変・dcre6=6）は全 Task で一致。
