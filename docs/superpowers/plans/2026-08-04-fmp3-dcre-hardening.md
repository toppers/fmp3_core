# FMP3 動的生成 API hardening パス（オーバーフロー検査・メモリプール符号安全化・巻き戻し／完全ドレインの回帰）実装計画

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `feature/dynamic-creation` ブランチ上で、dcre 移植の全段階（段階1〜3b + ISR）を
通じて**意図的に先送りしてきた hardening 課題 4 件 + deferred 1 件**を、まとめて片付ける。
すなわち

1. サイズ乗算のオーバーフロー検査（`acre_dtq`/`acre_pdq`/`acre_mpf`/`acre_tsk`）と
   `malloc_mempool` の符号混在比較（上流報告候補 c）の**一体修正**、
2. `acre_mpf` の 2 段確保・巻き戻し経路の **runtime テスト**（構成可能と判定済み）、
3. `isrseq` 単調化の「走査中に完全ドレイン →enqueue」前提の**回帰テスト**（ISR 段階の課題④）、
4. `ENA_DYNISR` × カーネル管理外 `intpri` の**エラー回帰 cfg**（ISR 段階の deferred）

を実装し、既存の全回帰（8 プリセットビルド・両エンジン等価性・QEMU 7 構成・機能テスト 6 本・
エラー行列）を維持したまま閉じる。

**Architecture:** ★**本パスは新機能を 1 つも足さない。** 追加する API・cfg 記法・
ランタイムオブジェクトはゼロである。触るのは

- **入力の検査**（サービスコール入口の `CHECK_PAR` 3 種 + 1 種）、
- **メモリプールの算術**（`kernel/startup.c` の bump allocator の比較式）、
- **テスト資産**（`test_dcre4.c`・`test_dcre5.{c,h,cfg}`・`test_dcre1.c`・
  `tools/cfg_error_tests/` の 1 ファイル）

の 3 層だけである。したがって**本パスの成功条件は「既存の回帰が 1 件も落ちないこと」**
であり、新しい checkpoint が増えること以外の挙動変化は原則として起きてはならない。
唯一の例外は「これまで未定義動作／誤成功していた入力が `E_PAR`／`NULL` になる」ことである。

本パスの中心は **Task 1 の一体修正**である。段階3b 最終レビューが「①サイズ乗算検査と
②mempool 符号比較は**一体で**修正せよ（片方だけでは穴が残る）」と裁定した。その理由は
Task 1 の冒頭で実装者自身に再導出させる（丸暗記させない）。

**Tech Stack:** C（カーネル・テスト）、Python/Ruby（cfg 両エンジン — 本パスでは
`kernel/interrupt.py`/`.trb` の**既存**チェックに回帰 cfg を当てるだけで、テンプレートは触らない）、
CMake、QEMU（musca_b1-2core ほか）。

---

## Global Constraints（過去段階の裁定・運用規約から転記。実装中に再解釈しない）

1. ブランチ `feature/dynamic-creation`（ISR 段階の続き。着手時 HEAD = `2ef9f9e`）。
   **main へはマージしない。** pristine への改変は `DIVERGENCE_MAP.md` に記録する
   （種別 `mod (dcre-port)` / `add (dcre-port)`）。
   ★着手時に `git log --oneline -3` で HEAD を確認し、`2ef9f9e` でなければ
   **止まって報告する**（別作業が挟まっている＝前提が崩れている）。
2. **★本パスの改変はすべて dcre からの「意図的な逸脱」である。** 段階1〜ISR の改変は
   その大半が「dcre の現物の転写」だったが、本パスは違う。
   `acre_*` のオーバーフロー検査も `malloc_mempool` の符号安全化も、
   **dcre（`/home/honda/TOPPERS/ASP3/asp3_3.7/extension/dcre/`）には存在しない**。
   したがって `DIVERGENCE_MAP.md` の各行には
   「dcre には無い／dcre と異なる」ことと**その理由**を明記する。
   「dcre がそうしているから」ではなく「dcre がそうしていないので直した」である。
3. **★コメントに書く根拠は真でなければならない。** 段階3a 最終レビュー Important #1 の
   再発防止。dcre の現物がどうであるかは**事実の記録**であって、FMP3 でそうすべき
   **理由**とは別物である。両方書く場合は区別して書く。
4. 段階hardening = 上記 4 件のみ。**新 API・新 cfg 記法・`ref_isr` 等を含めない。**
   「ついでに直す」を禁じる。気づいた欠陥は**記録して報告**し、修正は別タスク・別コミットに切る。
5. **free-list は FIFO**（`del_*` = `queue_insert_prev` で末尾へ / `acre_*` =
   `queue_delete_next` で先頭から）。段階1で裁定済み。**再議しない。**
   テストは FIFO/LIFO 不問で決定的になる形に組む。
6. **★`TA_NOEXS` は `((ATR)(-1))`＝全ビットが 1 である**（`kernel/kernel_impl.h:199`）。
   マスク比較（`MTX_CEILING` 型）でもビット検査（`atr & TA_MBALLOC` 型）でも誤一致する。
   `del_*` は全属性読み（`free_mpk` 条件を含む）の**後**に `TA_NOEXS` を書く。
   ★本パスは `del_*` を触らないが、Task 3 の振り付けは
   「`del_isr` が `TA_NOEXS` を quiesce の**前**に書く」（ISR 段階の訂正B）に依存する。
7. 検証 = F-1：cfg の両エンジン（Ruby オラクル + Python 製品）同時対応を維持し、
   `tools/cfg_equivalence.sh`（exit 0=一致 / 1=不一致 / **2=前提未充足であり合格ではない**）を
   主検査に維持する。**exit 2 は合格ではない。**
8. `rc=124`（`timeout` によるタイムアウト）**単独を成功判定に使わない。**
   期待出力の実在を `grep` で確認する。
9. **`cmd | tail` / `cmd | grep` で成否判定しない。** パイプラインの `$?` は最後の要素のもの。
   ファイルへリダイレクトしてから `grep` するか、`${PIPESTATUS[0]}` を見る。
10. QEMU 実行は**プリセットごとに個別コマンド**で行い、ログを別ファイルに落とす。
    `for` ループで全構成を 1 コマンドに詰めると Bash ツールの 2 分タイムアウトに当たり、
    **qemu が孤児化する**（段階1 Task 7 の実害）。各実行後に `pgrep -a qemu` で残存 0 を確認する。
    直後に一瞬 `<defunct>` が見えることがある（reap-lag）。3 秒後に再確認して消えていれば
    孤児化ではない。**そう判断した根拠を記録する。**
11. `tools/cfg_error_tests/run.sh` の呼出しは
    `run.sh <builddir> <cfg> <期待ercd> [EXTRA_CFLAGS]` の**4 引数形**である。
    cfg 内で `#include "test_int2.h"` 等を使うケースは**第 4 引数
    `EXTRA_CFLAGS="-I<repo>/test"` が必須**で、付けないと `rc=2` になる。
    **本計画では全ケースに引数を明記する。**
12. **★2 コア構成でしか通らない経路と、1 コア構成でしか通らない経路を取り違えない。**
    段階1 で「`TNUM_PRCID == 1` でしか通らない死んだ分岐へ変異を入れて control が
    空振りした」事故が 2 回起きている。本パスの変異 control は
    `musca_b1-2core` で**必ず通る**経路に入れる。
13. **★変異 control（negative control）を持たない修正は修正ではない。**
    Task 1 の各検査、Task 2 の巻き戻し、Task 3 の単調 `isrseq` には、それぞれ
    「壊すと予測どおりに落ちる」ことの実演を付ける。**予測を先に書き、実測と照合する。**
14. **★実測を正とする。** 期待値（`Check point` の行数、cfg ファイルの個数、`size` の増分）が
    計画と食い違ったら、**実測に合わせて計画側を直し、直した事実を記録する**。
    段階3a では checkpoint 数の見積りを 3 回続けて外している。
15. `KERNEL_FCSRCS`（`kernel/Makefile.kernel:51-56` の 22 個）は**不変**。
    本パスは新規 `.c` を 1 つも作らない。AGENTS.md §4 の突き合わせを Task 5 で行う。
16. 汎用層 `CMakeLists.txt`・`cmake/fmp3_core.cmake`・`cfg_py/pass1.py`・`cfg_py/pass2.py`・
    `kernel/kernel.py`・`kernel/kernel.trb`・`kernel/interrupt.py`・`kernel/interrupt.trb` は
    **変更しない**（Task 4 は既存チェックに回帰 cfg を当てるだけである）。
17. **★preflight は各 Task の中で行う。** 本計画には独立した preflight タスクが無い。
    各 Task の Step 1 が自分の前提を現物で確かめ、**食い違ったら自己修復せず止まって報告する**。
    「もっともらしいから進む」を禁じる。
18. 各 Task の最後に `DIVERGENCE_MAP.md` を更新してからコミットする。
    コミットメッセージは各 Task に**逐語で**書いてある。変えない。

---

## ★★本パスの中核リスク（3 件）

### リスク1：検査を足したのに、その検査へ到達する入力を誰も試していない

`CHECK_PAR` を 5 箇所に足すのは易しい。難しいのは**それが本当に効いていることの実演**である。
乗算あふれは「32bit ターゲットでのみ到達可能」（`size_t` と `uint_t` が同幅のとき）であり、
64bit ターゲット（`kria_arm64`）では**同じ入力が検査を素通りして `E_NOMEM` になる**。
すなわち「テストが緑」は「検査が効いている」を意味しない。

→ 検証：Task 4 で `#if SIZE_MAX == UINT_MAX` のガード付きで `E_PAR` を直接実測し、
**ガードが真になる構成（musca_b1-2core）でテストが走っていること**を
「そのテストを走らせたビルドが 32bit である」ことの実測（`size`/`nm` ではなく
`SIZE_MAX == UINT_MAX` が真であること自体をテスト内の `check_point` 到達で示す）で固める。
★**「#if で消えていて実は 1 行も実行されていない」が本リスクの本体である。**

### リスク2：一体修正の片方だけを入れて「直った」と言う

段階3b 最終レビューの裁定：

> hardening pass への推奨（次期にまとめて）: サイズ乗算オーバーフロー検査（④）と
> mempool 符号比較（上流候補c）は**一体で**修正（片方だけでは穴が残る）+ 巻き戻しテスト。

サイズ検査だけを入れると、**検査を通った正当な size でもアラインメント調整が
`limit` を越えた瞬間に符号比較が誤って成功する**（`ptrdiff_t` が負 → 符号なしへ変換されて巨大）。
逆に符号比較だけを直すと、**乗算があふれた小さな size が「入る」と正しく判定されて成功し、
実際には要求より遥かに小さい領域が返る**（`blkcnt` 個のブロックが確保できていないプールが生まれる）。
どちらも「テストは緑」である。

→ 対応：Task 1 は**この 2 つを 1 コミットで**入れる。Task 1 Step 2 で実装者に
**この論証を自分の言葉で再導出させ**、report に書かせる。再導出できないまま進むのは禁止。

### リスク3：Task 3 の振り付けが「たまたま通った」だけで、狙った窓を通っていない

Task 3 が狙うのは「`call_isr` の走査中にキューが**完全に空**になり、その空のキューへ
`acre_isr` した ISR が、**同一の割込み起動の中で**拾われること」である。
既存 `test_dcre5` の手順3/手順4 は **INTNO1 に静的 ISR が常駐している**ため
キューが空にならず、**旧リセット挙動でも通ってしまう**（ISR 段階 Task 6 レビューで判明し、
課題④として繰り越した）。同じ罠を繰り返す可能性がある。

→ 対応：Task 3 は **INTNO2（静的 ISR ゼロの動的専用キュー）**の上に組む。そして
**「旧リセット分岐を再導入すると倒れること」を変異 control で実演する**。
★**この変異 control が倒れなければ、テストは狙った窓を通っていない。**
そのときは値を調整するのではなく、**どちらのコアで何が走るかを紙に書いて確かめる**。

---

## ★dcre からの意図的な逸脱（本パスで新たに 6 件）

`DIVERGENCE_MAP.md` に記録すべき「dcre に無い／dcre と異なる」点を先に列挙する。
Task 1・Task 4 の実装者はこの表を根拠に台帳行を書く。

| # | 対象 | dcre の現物 | 本パス | 理由 |
|---|---|---|---|---|
| H-1 | `acre_dtq` | `malloc_mpk(sizeof(DTQMB) * dtqcnt)` に上限検査なし | `CHECK_PAR(dtqcnt <= SIZE_MAX / sizeof(DTQMB))` を追加 | `size_t` と `uint_t` が同幅（32bit）の場合に乗算があふれ、要求より小さい領域が返る |
| H-2 | `acre_pdq` | 同上（`PDQMB`） | 同型の `CHECK_PAR` を追加 | 同上 |
| H-3 | `acre_mpf` | `ROUND_MPF_T(blksz) * blkcnt` と `sizeof(MPFMB) * blkcnt` の**両方**に上限検査なし。さらに `ROUND_MPF_T` 自身の加算があふれうる | `CHECK_PAR` を **3 本**（`blksz` の丸めあふれ・ブロック領域の乗算・管理領域の乗算）追加 | 丸めがあふれると `ROUND_MPF_T(blksz)` が 0 になり、`malloc_mpk(0)` が成功して `blksz == 0` のプールができる |
| H-4 | `acre_tsk` | `ROUND_STK_T(stksz)` に上限検査なし | `CHECK_PAR(stksz <= SIZE_MAX - (sizeof(STK_T) - 1))` を追加 | 乗算は無いが**丸めの加算があふれる**。`stksz` は `size_t` なので 32/64bit の別を問わず到達可能 |
| H-5 | `malloc_mempool`/`aligned_alloc_mempool` | `((char *) limit) - ((char *) brk) >= size`（`ptrdiff_t` と `size_t` の混在比較）＋ `align_pointer` の加算あふれ | 符号なし（`uintptr_t`）で「あふれない」「`limit` を越えていない」を先に確かめてから残量を計算する形へ再構成。`align_pointer` は `alloc_mempool` へ畳み込んで廃止 | **上流報告候補 c の現物**。負のポインタ差が巨大な符号なし値になり、プール外の番地を「成功」として返す |
| H-6 | `tools/cfg_error_tests/dcre_dynisr_unmanaged_intpri.cfg` | dcre に `ENA_DYNISR` が無い（FMP3 拡張） | カーネル管理外 `intpri` の `CFG_INT` に `ENA_DYNISR` を書くと `E_OBJ` になることの回帰 | ISR 段階の deferred（両エンジン対称のチェックが**回帰列に無い**状態だった） |

★H-5 は**上流報告候補 c を「観察」から「修正」へ格上げする**ものである。
`DIVERGENCE_MAP.md` の「既知・対処しない事項」（`:297` 付近）と上流報告候補の一覧（`:332` 付近）を
**両方**更新すること（片方だけ直すと台帳が矛盾する）。

---

## 変更ファイル一覧（全体像）

**pristine（`DIVERGENCE_MAP.md` に記録が要る）**

| ファイル | Task | 種別 |
|---|---|---|
| `kernel/startup.c` | 1 | mod (dcre-port) — H-5 |
| `kernel/dataqueue.c` | 1 | mod (dcre-port) — H-1 |
| `kernel/pridataq.c` | 1 | mod (dcre-port) — H-2 |
| `kernel/mempfix.c` | 1 | mod (dcre-port) — H-3 |
| `kernel/task_manage.c` | 1 | mod (dcre-port) — H-4 |
| `kernel/interrupt.c` | 3 | mod (dcre-port) — コメントのみ（カバレッジギャップの解消を反映） |
| `test/test_dcre4.c` | 2, 4 | mod（既に `add (dcre-port)` で記録済み。行を追記） |
| `test/test_dcre5.c`, `test/test_dcre5.h`, `test/test_dcre5.cfg` | 3 | mod（同上） |
| `test/test_dcre1.c` | 4 | mod（同上） |

**派生（記録不要）**

| ファイル | Task |
|---|---|
| `tools/cfg_error_tests/dcre_dynisr_unmanaged_intpri.cfg` | 4（新規） |
| `.superpowers/sdd/progress.md` | 5 |

**変更しないもの（明示）**

`kernel/interrupt.py` / `kernel/interrupt.trb` / `kernel/kernel.py` / `kernel/kernel.trb` /
`kernel/kernel_api.def` / `CMakeLists.txt` / `cfg_py/*` / `kernel/Makefile.kernel` /
`kernel/kernel_rename.def`（新しい外部シンボルを作らないため）。

---

### Task 1: ★最難関(1) — サイズ乗算オーバーフロー検査と mempool 符号比較の一体修正

**推奨モデル:** 上位（opus 相当）。**本パスで唯一カーネルの実行経路を変える Task** であり、
かつ**片方だけ直すと穴が残る**という非自明な依存がある（中核リスク2）。
`uintptr_t`/`size_t`/`ptrdiff_t` の整数昇格を正しく追える必要がある。

**★このタスクを分割してはならない理由（task header に固定する）:**

段階3b 最終レビューの裁定「サイズ乗算オーバーフロー検査（④）と mempool 符号比較
（上流候補 c）は**一体で**修正（片方だけでは穴が残る）」の中身は次のとおりである。

- **サイズ検査だけを入れた場合**：`CHECK_PAR` を通った正当な `size` でも、
  `align_pointer` がアラインメント調整で `brk` を `limit` の先へ押し出した瞬間に
  `((char *) limit) - ((char *) brk)` が**負**になる。この `ptrdiff_t` は
  `size_t` との比較で符号なしへ変換され、巨大な値になって比較が成立する。
  **プール外の番地が「成功」として返る。**
- **符号比較だけを直した場合**：`ROUND_MPF_T(blksz) * blkcnt` があふれて小さな値になると、
  その小さな `size` は**正当に**「入る」と判定されて成功する。返るのは
  要求した `blkcnt` 個ぶんに遥かに足りない領域であり、`get_mpf` が
  **プール外を指すブロックを配る**。
- どちらの穴も**現行のテストは緑のまま**である。したがって 2 つは同じ 1 つの穴の
  両端であり、1 コミットで塞ぐ。

**Files:**
- Modify: `kernel/startup.c`（`align_pointer` `:470-474` / `malloc_mempool` `:476-491` /
  `aligned_alloc_mempool` `:493-508`）
- Modify: `kernel/dataqueue.c`（`acre_dtq` `:346-`。`CHECK_VALIDATR` は `:376` 付近）
- Modify: `kernel/pridataq.c`（`acre_pdq` `:323-`。`CHECK_PAR(VALID_DPRI(maxdpri))` の直後）
- Modify: `kernel/mempfix.c`（`acre_mpf` `:205-301`。`CHECK_PAR(blksz != 0)` は `:228`）
- Modify: `kernel/task_manage.c`（`acre_tsk` `:140-`。`CHECK_PAR(stksz >= TARGET_MIN_STKSZ)` は `:167`）
- Modify: `DIVERGENCE_MAP.md`

**Interfaces:**
- `CHECK_PAR(exp)`（`kernel/check.h:304`）— 偽なら `ercd = E_PAR; goto error_exit;`。
  **ロック取得前**に置く（`acre_*` の既存 `CHECK_PAR` 群と同じ位置）。
  これらはコア非依存の引数検査なので、`lock_cpu()` より前で正しい。
- `malloc_mpk(size)` / `aligned_alloc_mpk(alignment, size)` / `free_mpk(ptr)`
  （`kernel/kernel_impl.h:326,340,351`）— `mpk_valid` が偽なら `NULL` を返す薄いラッパ。
- `TOPPERS_ROUND_SZ(sz, unit)` = `(((sz) + (unit) - 1) & ~((unit) - 1))`（`include/kernel.h:704`）。
  `ROUND_STK_T` = `:708`、`ROUND_MPF_T` = `:711`。
- `SIZE_MAX`（`<stdint.h>`。`include/t_stddef.h:65` → `target/musca_b1_gcc/target_stddef.h:17`
  で取り込まれる）。`UINT_MAX` は `arch/gcc/tool_stddef.h:82` の `<limits.h>` 経由。
- 型：`uint_t` = `unsigned int`（`include/t_stddef.h:95`）、`MB_T` = `uintptr_t`（`:131`）、
  `MPF_T` = `intptr_t`（`include/kernel.h:129-131`）、`STK_T` = `intptr_t`（`:123-126`）、
  `MPFMB` = `{ uint_t next; }`（`kernel/mempfix.h:67-69`）。

- [ ] **Step 1: 前提の現物確認（食い違ったら止まる）**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
git log --oneline -3
sed -n '440,523p' kernel/startup.c
sed -n '695,720p' include/kernel.h
grep -n "typedef.*uint_t;\|typedef uintptr_t\s*MB_T" include/t_stddef.h
sed -n '60,72p' kernel/mempfix.h
sed -n '300,315p' kernel/check.h
sed -n '320,355p' kernel/kernel_impl.h
grep -n "malloc_mpk(sizeof(DTQMB)" kernel/dataqueue.c
grep -n "malloc_mpk(sizeof(PDQMB)" kernel/pridataq.c
grep -n "ROUND_MPF_T(blksz) \* blkcnt\|malloc_mpk(sizeof(MPFMB)" kernel/mempfix.c
grep -n "ROUND_STK_T(stksz)" kernel/task_manage.c
grep -rn "malloc_mempool\|aligned_alloc_mempool\|align_pointer" \
     /home/honda/TOPPERS/ASP3/asp3_3.7/extension/dcre/kernel/startup.c
```

確認して report に書くこと（**推測ではなく実測値**）:
- `HEAD` が `2ef9f9e` であること。**違えば止まる。**
- `kernel/startup.c:483` と `:500` が **`>= size` の混在比較**であること
  （`((char *)(p_mempoolcb->limit)) - ((char *) brk) >= size`）。
- dcre 側（`extension/dcre/kernel/startup.c`）が**同じ式**であること
  ＝これが FMP3 固有の劣化ではなく上流由来（報告候補 c）であることの裏取り。
- `MEMPOOLCB` が `{ void *brk; void *limit; uint_t count; }` の 3 フィールドであること。
- `sizeof(MPFMB)` の根拠（`uint_t next;` 1 個）。
- 5 つの改修対象行が上の Files 記載の位置に実在すること。

★**確認結果が上と食い違ったら、自分で辻褄を合わせず止まって報告する**（Constraint 17）。

- [ ] **Step 2: ★一体修正の根拠を自分で再導出する（コードを書く前に）**

report に、**自分の言葉で**次の 2 つの反例を書く。計画からのコピーは不可。

1. 「サイズ検査だけを入れて `malloc_mempool` を直さなかった場合に、
   正当な `size` で `malloc_mempool` がプール外を返す具体的な状況」
   （`brk` と `limit` と `alignment` の具体値で示す）。
2. 「`malloc_mempool` だけを直してサイズ検査を入れなかった場合に、
   `acre_mpf` が『成功したのに `blkcnt` 個ぶんの領域が無い』プールを作る具体的な状況」
   （`blksz`・`blkcnt`・`SIZE_MAX` の具体値で示す）。

★**書けないまま Step 3 へ進まない。** 書けないということは、
片方だけ直して「直った」と報告する状態にいるということである（中核リスク2）。

- [ ] **Step 3: `kernel/startup.c` — メモリプールの符号／あふれ安全化（H-5）**

`:470-508` の `align_pointer` / `malloc_mempool` / `aligned_alloc_mempool` の
**3 つをまとめて**次で置き換える（`initialize_mempool` と `free_mempool` は触らない）。

```c
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
 *      ・調整の加算があふれないこと
 *      ・調整後のbrkがlimitを越えていないこと
 *
 *  をこの順に確かめてから初めて残量（limit - aligned）を計算する．負の差が
 *  生じうる箇所が式の上に存在しないため，(1)の誤判定は構造的に起こらない．
 *
 *  ★この修正だけでは穴は塞がらない．呼出し側（acre_dtq/acre_pdq/acre_mpf/
 *  acre_tsk）が渡すsize自体が乗算・丸めであふれていると，「小さくなったsize」
 *  は正当に「入る」と判定されて成功してしまう．両者は同じ穴の両端であり，
 *  一体で修正している（各acre_*のCHECK_PARを参照）．
 *
 *  alignmentは2の巾乗であること（呼出し側の責任．alignof(MB_T)ないし
 *  aligned_alloc_mpkの引数）．
 */
Inline void *
alloc_mempool(MEMPOOLCB *p_mempoolcb, size_t alignment, size_t size)
{
	uintptr_t	brk = ((uintptr_t)(p_mempoolcb->brk));
	uintptr_t	limit = ((uintptr_t)(p_mempoolcb->limit));
	uintptr_t	adjust = (((uintptr_t) alignment) - 1U);
	uintptr_t	aligned;

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

	p_mempoolcb->brk = ((void *)(aligned + ((uintptr_t) size)));
	p_mempoolcb->count += 1;
	return(((void *) aligned));
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
```

★`align_pointer` は `alloc_mempool` に畳み込んだため**削除する**
（残すと未使用の pristine 関数が残り、レビューで「どちらが本物か」が読めなくなる）。
削除した事実を `DIVERGENCE_MAP.md` に書く。

★`(~((uintptr_t) 0U))` を使うのは `UINTPTR_MAX` へのヘッダ依存を増やさないためである
（`SIZE_MAX` は `acre_*` 側で使うが、そちらは `<stdint.h>` が既に届いていることを
Step 1 で確認済みである）。**この理由をコメントに書く必要はない**（自明な範囲）が、
report には書く。

- [ ] **Step 4: `kernel/dataqueue.c` — `acre_dtq` のオーバーフロー検査（H-1）**

`acre_dtq`（`:346-`）の `CHECK_VALIDATR(dtqatr, TA_TPRI);` と
`if (p_dtqmb != NULL) { CHECK_PAR(MB_ALIGN(p_dtqmb)); }` の**間**に挿入する。

```c
	/*
	 *  管理領域のサイズ計算があふれないこと
	 *
	 *  ★dcre（extension/dcre/kernel/dataqueue.c）にこの検査は無い．
	 *  dtqcntはuint_t（unsigned int），sizeof(DTQMB)はsize_tである．
	 *  size_tとunsigned intが同幅のターゲット（32bit）では，両者の積が
	 *  size_tの中であふれ，要求よりはるかに小さい領域の確保に成功して
	 *  しまう（その領域にdtqcnt個のDTQMBは入らない）．enqueue_dataは
	 *  p_dtqinib->dtqcntを信じてp_dtqmb[tail]へ書くので，プール外破壊に
	 *  なる．64bitターゲットではuint_tの最大値でも積があふれないため，
	 *  この検査は恒真に落ちる（無害）．
	 *
	 *  検査はロック取得前に置く．引数だけから決まりコア非依存である
	 *  （既存のCHECK_VALIDATR/MB_ALIGNと同じ位置づけ）．
	 */
	CHECK_PAR(dtqcnt <= (SIZE_MAX / sizeof(DTQMB)));
```

★`dtqcnt == 0` のとき右辺は正なので検査は通る（`dtqcnt == 0` は管理領域を
確保しない正当な構成であり、既存の `if (dtqcnt != 0 && …)` がそのまま効く）。

- [ ] **Step 5: `kernel/pridataq.c` — `acre_pdq` のオーバーフロー検査（H-2）**

`acre_pdq`（`:323-`）の `CHECK_PAR(VALID_DPRI(maxdpri));` の**直後**に挿入する。

```c
	/*
	 *  管理領域のサイズ計算があふれないこと
	 *
	 *  ★dcreにこの検査は無い．根拠はacre_dtqの同じ検査と同一である
	 *  （pdqcntはuint_t，sizeof(PDQMB)はsize_t．32bitターゲットで積が
	 *  あふれると，pdqcnt個のPDQMBが入らない領域の確保に成功してしまい，
	 *  enqueue_pridataのunusedインデックス越しにプール外を破壊する）．
	 */
	CHECK_PAR(pdqcnt <= (SIZE_MAX / sizeof(PDQMB)));
```

- [ ] **Step 6: `kernel/mempfix.c` — `acre_mpf` のオーバーフロー検査 3 本（H-3）**

`acre_mpf`（`:205-301`）の `CHECK_PAR(blksz != 0);`（`:228`）の**直後**、
`if (mpf != NULL) { … }` の**前**に挿入する。

```c
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
```

★**3 本の順序を入れ替えてはならない**（(a) が (b) の除数の健全性を作っている）。
★`blksz` は `uint_t` なので、64bit ターゲットでは (a) は恒真である。
32bit ターゲットでは `blksz` が `0xFFFFFFFD` 以上のときに `E_PAR` になる。

- [ ] **Step 7: `kernel/task_manage.c` — `acre_tsk` の丸めあふれ検査（H-4）**

★**まず採否を判断し、判断とその根拠を report に書く**（計画は「入れる」と決めているが、
根拠を現物で確かめてから入れること）。

判断の根拠（計画側の結論。Step 1 で確認済みのはず）:
- `acre_tsk` には**乗算が無い**（`stk` は `aligned_alloc_mpk(alignof(STK_T), stksz)` で
  1 回確保するだけ）。したがって `acre_dtq` 系と**同じ class の問題ではない**。
- しかし `stksz = ROUND_STK_T(stksz)` の**丸めの加算はあふれる**。
  `stksz` は `size_t`（`pk_ctsk->stksz`）なので、**32bit / 64bit の別を問わず**
  `SIZE_MAX` 近傍の値で到達可能である。あふれると `stksz` が 0 付近に落ち、
  `aligned_alloc_mpk(alignof(STK_T), 0)` は（Step 3 の修正後も）**成功して**
  ゼロ長のスタックを持つタスクができる。`init_tskinictxb` は `stk + stksz` を
  スタック底として使うので、起動した瞬間に他の領域を踏む。
- 既存の `CHECK_PAR(stksz >= TARGET_MIN_STKSZ)`（`:167`、`TARGET_MIN_STKSZ` は
  未定義時 `1U`）は**下限だけ**を見ており、上限（丸めあふれ）は見ていない。

→ **入れる。** `CHECK_PAR(stksz >= TARGET_MIN_STKSZ);`（`:167`）の**直後**に挿入する。

```c
	/*
	 *  スタックサイズの丸めがあふれないこと
	 *
	 *  ★dcre（extension/dcre/kernel/task_manage.c）にこの検査は無い．
	 *  acre_tskには乗算が無いのでacre_dtq/acre_pdq/acre_mpfとは事情が
	 *  違うが，下のROUND_STK_T(stksz)（＝(stksz + sizeof(STK_T) - 1) &
	 *  ~(sizeof(STK_T) - 1)）の加算はあふれうる．stkszはsize_tなので，
	 *  32bit/64bitのどちらでも到達可能である．あふれると丸め結果が0付近
	 *  へ落ち，aligned_alloc_mpkが「成功」してゼロ長スタックのタスクが
	 *  できあがる（init_tskinictxbはstk+stkszをスタック底に使う）．
	 *
	 *  ★stk != NULL（ユーザ供給）の場合はROUND_STK_Tを通らないが，
	 *  検査を条件つきにする理由が無い（あふれるstkszはいずれにせよ
	 *  正当な入力ではない）ので無条件に置く．
	 */
	CHECK_PAR(stksz <= (SIZE_MAX - (sizeof(STK_T) - 1)));
```

- [ ] **Step 8: 全 8 構成のビルド**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
for p in musca_b1 musca_b1-2core rp2350_pico2 polarfire_soc_kit-qemu \
         kria_arm64 kria_arm64-1core kria_r5 kria_r5-2core; do
  cmake --preset $p > /tmp/hd-t1-conf-$p.log 2>&1; c=$?
  cmake --build build/$p > /tmp/hd-t1-build-$p.log 2>&1; b=$?
  echo "$p conf=$c build=$b"
done
```
期待: 8 構成すべて `conf=0 build=0`。
★特に **`kria_arm64`（64bit）で警告が出ないこと**を確認する
（`SIZE_MAX / sizeof(...)` が `uint_t` の最大値より大きくなるため、
コンパイラが「比較は常に真」と警告する可能性がある）。

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
grep -n "warning:" /tmp/hd-t1-build-kria_arm64.log | head -20
grep -n "warning:" /tmp/hd-t1-build-musca_b1-2core.log | head -20
```
期待: 本 Task が足した行に起因する `warning:` が**無い**こと。
★出た場合（例：`-Wtype-limits` 相当の「常に真」）は、**検査を消して逃げない**。
`(void)` キャストや `#if` での消し込みではなく、**警告が出た事実と、そのビルドでは
その検査が恒真である（＝あふれが構造的に起きない）ことを report と
`DIVERGENCE_MAP.md` に記録**した上で、警告を抑制せずに残す判断が既定である。
判断を変える場合はコントローラへ報告してから変える。

- [ ] **Step 9: ROM 増分の実測（恒常出力の把握）**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
arm-none-eabi-size build/musca_b1-2core/*.elf 2>/dev/null || \
  find build/musca_b1-2core -name '*.elf' -exec arm-none-eabi-size {} \;
```
`2ef9f9e` 時点の値（ISR 段階 Task 7 記録の基準 + ISR 段階増分）と比較し、
**増分をバイトで記録**する。桁が違ったら（例: text が 1KB 以上増える）
実装を疑う — `CHECK_PAR` 5 本と `alloc_mempool` の再構成で増えるのは
**数十〜200 バイト程度**のはずである。

- [ ] **Step 10: 既存機能テストの非退行（QEMU 6 本・★プリセットごとに個別実行）**

★ここが本 Task の主検査である。**挙動を変えていないこと**を実測で固める。
ビルドディレクトリが無い場合は Task 4 / Task 5 の configure コマンドで作る。

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 60 cmake --build build/musca_b1-2core-tdcre1 --target run > /tmp/hd-t1-d1.log 2>&1
grep 'TTSP_RESULT: PASS' /tmp/hd-t1-d1.log ; pgrep -a qemu
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 60 cmake --build build/musca_b1-2core-tdcre2 --target run > /tmp/hd-t1-d2.log 2>&1
grep 'TTSP_RESULT: PASS' /tmp/hd-t1-d2.log ; pgrep -a qemu
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 90 cmake --build build/musca_b1-2core-tdcre3 --target run > /tmp/hd-t1-d3.log 2>&1
grep 'TTSP_RESULT: PASS' /tmp/hd-t1-d3.log
grep -c 'Check point' /tmp/hd-t1-d3.log       # 期待: 14
pgrep -a qemu
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 90 cmake --build build/musca_b1-2core-tdcre4 --target run > /tmp/hd-t1-d4.log 2>&1
grep 'TTSP_RESULT: PASS' /tmp/hd-t1-d4.log
grep -c 'Check point' /tmp/hd-t1-d4.log       # 期待: 15（段階3b Task 6 実測値）
pgrep -a qemu
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 120 cmake --build build/musca_b1-2core-tdcre5 --target run > /tmp/hd-t1-d5.log 2>&1
grep 'TTSP_RESULT: PASS' /tmp/hd-t1-d5.log
grep -c 'Check point' /tmp/hd-t1-d5.log       # 期待: 12（ISR段階 Task 6 実測値。Task 3 で 13 になる）
pgrep -a qemu
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 60 cmake --build build/musca_b1-2core-tint2 --target run > /tmp/hd-t1-i2.log 2>&1
grep 'TTSP_RESULT: PASS' /tmp/hd-t1-i2.log ; pgrep -a qemu
```
期待: 6 本とも `TTSP_RESULT: PASS` が実在し、`Check point` 行数が**変わらない**。
★**`test_dcre4` の `Check point` が 15 のままであること**は特に重要である
（`acre_dtq`/`acre_mpf` に検査を足したのに既存の `E_NOMEM` ケースが
`E_PAR` に変わっていない＝**既存の正当な入力を誤って弾いていない**ことの実測）。
食い違ったら**必ず原因を突き止める**（Constraint 14 の「実測を正とする」は
「期待値の書き換えで済ませてよい」という意味ではない）。

- [ ] **Step 11: `tools/cfg_equivalence.sh`（cfg を触っていないことの確認）**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
git diff --name-only HEAD | grep -E '\.(py|trb|def)$' ; echo "cfg-side rc=$?"
for p in musca_b1-2core kria_arm64 kria_r5-2core; do
  tools/cfg_equivalence.sh build/$p > /tmp/hd-t1-eq-$p.log 2>&1
  echo "$p eq=$?"
done
```
期待: 1 行目の `grep` が**何も出さない**（cfg 側のファイルを 1 つも触っていない）。
`eq` は 3 件とも **0**（**2 は合格ではない**、Constraint 7）。

- [ ] **Step 12: ★変異 control（4 本。壊すと予測どおり落ちること）**

★**この Step を省略した Task 1 は未完了である**（Constraint 13）。
Task 4 で追加する runtime テストが**まだ無い**時点なので、
ここでの control は「既存テストが落ちる形」ではなく
**「検査を外した状態でコンパイル警告／実行結果がどう変わるか」の直接観測**にはならない。
そこで本 Step は **Task 4 完了後に実施する**（Task 4 Step 7 に再掲）。
本 Step では次だけを行い、Task 4 への引き継ぎとして report に書く：

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
git diff --stat kernel/startup.c kernel/dataqueue.c kernel/pridataq.c \
                kernel/mempfix.c kernel/task_manage.c
```
- 変更が上記 5 ファイルに閉じていること（`git diff --stat` の行数を report に書く）。
- Task 4 で当てる control の**予測**を先に書く：
  - `acre_dtq` の `CHECK_PAR` を消すと `test_dcre4` の新ケースが `E_PAR` ではなく
    `E_NOMEM` を返し `check_assert` が落ちる。
  - `acre_mpf` の (a) を消すと `blksz = UINT_MAX` のケースが `E_PAR` ではなく
    `E_NOMEM`（丸め 0 → `malloc_mpk(0)` 成功 → ②で失敗）になる。
  - `alloc_mempool` の `if (aligned > limit)` を消すと…（Task 4 Step 7 で実演する）。

- [ ] **Step 13: `DIVERGENCE_MAP.md` の更新とコミット**

次の 5 行（H-1〜H-5）を pristine 表の末尾へ追加する。**各行に「dcre には無い」と
その理由を書く**（Constraint 2）。

- `kernel/startup.c（hardening）` | `mod (dcre-port)` |
  「`align_pointer`（旧 `:470-474`）を廃止し、`malloc_mempool`/`aligned_alloc_mempool` を
  共通の `alloc_mempool`（`uintptr_t` 算術）へ再構成した。**dcre 原形の
  `((char *) limit) - ((char *) brk) >= size` は `ptrdiff_t` と `size_t` の混在比較**で、
  アラインメント調整が `limit` を越えた場合に負のポインタ差が巨大な符号なし値へ変換され、
  プール外の番地を成功として返す（**上流報告候補 c の現物**）。あわせて
  `align_pointer` の加算あふれも塞いだ。★これは dcre からの**意図的な逸脱**であり、
  上流の欠陥の修正である。呼出し側（`acre_*`）のサイズ検査と**一体で**入れている
  （片方だけでは穴が残る — 段階3b 最終レビューの裁定）」 | **要**（候補 c を「観察」から
  「修正済み・要報告」へ格上げ）
- `kernel/dataqueue.c（hardening）` / `kernel/pridataq.c（hardening）` /
  `kernel/mempfix.c（hardening）` / `kernel/task_manage.c（hardening）` |
  `mod (dcre-port)` | 上の H-1〜H-4 の理由をそれぞれ 1 行で（**dcre に無いこと**を明記）| - |

さらに**既存記述の整合**を取る（★片方だけ直すと台帳が矛盾する）：
- `DIVERGENCE_MAP.md:297` 付近「既知・対処しない事項」の
  「`kernel/startup.c` の `malloc_mempool` が持つ符号混在比較（上流報告候補 c…）」を
  **「hardening パス Task 1 で修正済み」へ書き換える**（消さずに解消の記録として残す）。
- 同「`dtqcnt`/`pdqcnt`/`blkcnt`/`blksz` オーバーフロー未 hardening の既知事項」
  （段階3b Task 7 が追記したもの）も**解消済みへ書き換える**。
- `:332` 付近の上流報告候補一覧の `c` の説明を、
  「`malloc_mempool` の符号混在比較（**本ブランチでは修正済み。上流へは未報告**）」に更新する。

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
grep -n "符号混在比較\|オーバーフロー" DIVERGENCE_MAP.md   # 更新漏れが無いこと
git add -A && git commit -m "fix(kernel): 動的生成のサイズ乗算あふれ検査とメモリプールの符号安全化を一体で修正（上流候補c）"
```

---

### Task 2: `acre_mpf` 巻き戻し経路の runtime テスト

**推奨モデル:** 中位（sonnet）。実装量は小さいが、**サイジングの算術を自分で追える**必要がある
（段階3b 最終レビューが「構成可能」と判定した根拠を実際に構成に変える作業）。

**背景（段階3b 最終レビュー Minor・deferred）:**

`acre_mpf` は 2 段確保である。①ブロック領域（`mpf == NULL` のとき `malloc_mpk`）→
②管理領域（`p_mpfmb == NULL` のとき `malloc_mpk`）。②に失敗したときは
**①でカーネルが確保した分だけを巻き戻す**（`kernel/mempfix.c:267-272`）。

```c
			if (p_mpfmb == NULL) {
				if (pk_cmpf->mpf == NULL) {
					free_mpk(mpf);
				}
				ercd = E_NOMEM;
			}
```

判定に**ローカル変数 `mpf` ではなくパケットの元の値 `pk_cmpf->mpf`** を使うのが肝で、
ローカルの `mpf` は①で上書きされているため「ユーザ供給だったのか、カーネルが確保したのか」を
区別できない。**この経路は段階3b では 1 度も実行されていない**（既存の `E_NOMEM` ケースは
①の時点で失敗するため②へ到達しない）。本 Task でこれを塞ぐ。

**Files:**
- Modify: `test/test_dcre4.c`（手順 8「mpf」の中、`:493-497` 付近の既存 `E_NOMEM` ケースの直後）
- Modify: `DIVERGENCE_MAP.md`（`test/test_dcre4.c` の既存行に追記）

**Interfaces:** `acre_mpf`/`del_mpf`/`ref_mpf`（`include/kernel.h`）、
`T_CMPF { mpfatr, blkcnt, blksz, mpf, mpfmb }`、`check_assert`/`check_ercd`（`syssvc/test_svc.h`）。
`MPK_SIZE`/`MPF_BLKCNT`/`MPF_BLKSZ`/`MPF_CYCLES`（`test/test_dcre4.h`）。

**★checkpoint は増やさない。** 手順 8 は `check_point(12)` の中に閉じており、
本 Task は同じ手順の中にケースを 2 つ足すだけである。したがって
`test_dcre4` の `Check point` 行数は **15 のまま**でなければならない
（Task 5 の回帰行列の期待値を動かさないため）。★実測で 15 以外になったら
**挿入位置を間違えている**（`check_point` を足してしまった）。

- [ ] **Step 1: 前提の現物確認と、サイジング算術の再計算（食い違ったら止まる）**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
sed -n '16,45p' test/test_dcre4.h
sed -n '240,300p' kernel/mempfix.c
sed -n '440,530p' kernel/startup.c
grep -n "MPK_SIZE\|DEF_MPK" test/test_dcre4.cfg
sed -n '488,535p' test/test_dcre4.c
grep -n "typedef struct fixed_memoryblock_management_block" -A4 kernel/mempfix.h
```

**確認して report に書くこと（実測値）:**

1. `MPK_SIZE == 2048`（`test/test_dcre4.h`）、`test_dcre4.cfg` の
   `DEF_MPK({ MPK_SIZE, NULL })`。
2. `MEMPOOLCB` が `{ void *brk; void *limit; uint_t count; }`
   → **musca_b1（32bit ARM）では `sizeof(MEMPOOLCB) == 12`**。
   → プールの実効容量 **R = 2048 - 12 = 2036 バイト**。
3. `MPF_T` = `intptr_t` → **`sizeof(MPF_T) == 4`**、`ROUND_MPF_T(4) == 4`。
4. `MPFMB` = `{ uint_t next; }` → **`sizeof(MPFMB) == 4`**。
5. `free_mempool` は `count` が **0 になったときにだけ** `brk` を先頭へ戻す
   （`kernel/startup.c:510-519`。段階3b 訂正G）。
6. **挿入位置の直前で `count == 0` である**こと。根拠は
   「直前の `del_mpf(mpfid1)` が①②の両方を `free_mpk` しており、
   それ以前に生成した dtq/pdq はすべて削除済みである」+
   「**既存の `MPF_CYCLES`(=16) 周のループが現に通っている**こと自体が、
   1 周ごとに `count` が 0 に戻って `brk` がリセットされている証拠である」
   （`test/test_dcre4.h` の `MPK_SIZE` コメントに書かれている論証）。
   ★この 2 つ目の根拠のほうが強い。report にはこちらを書く。

**サイジング（上の 1〜6 から導く。実装者は自分で再計算して一致を確かめる）:**

| ケース | `blksz` | `blkcnt` | ①`ROUND_MPF_T(blksz)*blkcnt` | ①後の残量 | ②`sizeof(MPFMB)*blkcnt` | 結果 |
|---|---|---|---|---|---|---|
| **A（巻き戻し発火）** | 4 | 400 | 4×400 = **1600** ≤ 2036 → **成功** | 2036-1600 = **436** | 4×400 = **1600** > 436 → **失敗** | `E_NOMEM`（★①を巻き戻す） |
| **B（巻き戻しの証拠）** | 4 | 200 | 4×200 = **800** | 2036-800 = 1236 | 4×200 = **800** ≤ 1236 → 成功 | **成功**（`erid == mpfid1`） |

- ケース A が成立する条件は `1600 ≤ R < 3200`。R = 2036 なので**成立**。
  余裕は `R ≥ 1600` 側に **436 バイト**ある（＝挿入時点で 436 バイト未満の
  取りこぼしがあっても A は成立する）。
- ケース B が成立する条件は `R ≥ 1600`（①800 + ②800）。**A の巻き戻しが効いて
  `count` が 0 に戻り `brk` がリセットされたときにだけ**成立する。
- **巻き戻しが無い場合**（＝変異時）：A で①の 1600 バイトが返らず `count` が 1 のまま
  残るので `brk` はリセットされない。残量は 436 バイトであり、
  **B の①（800 バイト）が入らず `E_NOMEM`** になる。
  ★これが「巻き戻しが起きたことの直接の観測」である。

★**表の数値が Step 1 の実測と 1 つでも食い違ったら、値をいじって通す前に止まって報告する。**
（例：`sizeof(MEMPOOLCB)` が 12 でなかった場合、R が変わるので A/B の成立条件を引き直す。
引き直しの式は上に書いてある通りで、`blkcnt` を `R/8 < blkcnt ≤ R/4` の範囲に取れば A が、
`blkcnt ≤ R/8` に取れば B が成立する。）

- [ ] **Step 2: `test/test_dcre4.c` へケース A/B を挿入**

既存の

```c
	/*  ブロック領域が入らない大きさ → E_NOMEM  */
	cmpf.blkcnt = MPK_SIZE;
	cmpf.blksz = 32U;
	check_assert(acre_mpf(&cmpf) == E_NOMEM);
```

の**直後**（次の `/*  E_NOMEM で free-list が減っていないこと…  */` の**前**）へ挿入する。

```c
	/*
	 *  ★2段確保の②で失敗したときの巻き戻し（段階3b Task 5 の未実証経路）
	 *
	 *  上の E_NOMEM は①（ブロック領域）の時点で失敗するので，②の失敗と
	 *  巻き戻しの経路（mempfix.c の pk_cmpf->mpf 判定）を通らない．ここは
	 *  「①は入るが①+②は入らない」サイジングで②を失敗させる．
	 *
	 *  【算術】カーネルメモリプールの実効容量は
	 *    MPK_SIZE(2048) - sizeof(MEMPOOLCB)(12) = 2036 バイト
	 *  である（32bit ターゲット）．ROUND_MPF_T(4) == sizeof(MPF_T) == 4，
	 *  sizeof(MPFMB) == 4 なので，
	 *
	 *    ケースA: blkcnt=400 → ① 4*400=1600 ≤ 2036（成功）
	 *                          ② 4*400=1600 > 2036-1600=436（失敗）→ E_NOMEM
	 *    ケースB: blkcnt=200 → ① 4*200=800 ＋ ② 4*200=800 = 1600 ≤ 2036（成功）
	 *
	 *  【なぜケースBが巻き戻しの証拠になるか】
	 *  kernel/startup.c のプールは bump allocator で，free_mempool は count が
	 *  0 になったときにだけ brk を先頭へ戻す．ケースAで①が巻き戻されれば
	 *  count は 0 に戻り brk がリセットされるので，直後のケースB（1600B）は
	 *  入る．巻き戻しが無ければ count は 1 のまま，残量は 436B しかなく，
	 *  ケースBは①（800B）の時点で E_NOMEM になる．
	 *  ★したがって「ケースBが成功すること」が①の解放の直接の観測である．
	 *
	 *  ★このサイジングは 32bit ターゲット（musca_b1）の値である．本テストは
	 *  musca_b1-2core でのみ実行される（DIVERGENCE_MAP.md 参照）．
	 */
	cmpf.blkcnt = 400U;							/*  ケースA  */
	cmpf.blksz = 4U;
	check_assert(acre_mpf(&cmpf) == E_NOMEM);	/*  ②で失敗 → ①を巻き戻す  */

	cmpf.blkcnt = 200U;							/*  ケースB  */
	cmpf.blksz = 4U;
	erid = acre_mpf(&cmpf);
	check_assert(erid == mpfid1);				/*  ★①が返っていなければ E_NOMEM  */
	check_ercd(ref_mpf(mpfid1, &rmpf), E_OK);
	check_assert(rmpf.fblkcnt == 200U);
	check_ercd(del_mpf(mpfid1), E_OK);
```

★挿入後、その直後の既存ブロック（`cmpf.blkcnt = MPF_BLKCNT; cmpf.blksz = MPF_BLKSZ;`）が
`blkcnt`/`blksz` を元へ戻しているので、以降の手順は影響を受けない。
**この事実を目で確かめること**（戻していなければ以降が全部壊れる）。

- [ ] **Step 3: ビルドと実行**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
cmake --preset musca_b1-2core -B build/musca_b1-2core-tdcre4 \
  -DFMP3_APPLNAME=test_dcre4 -DFMP3_APPLDIR=test \
  -DFMP3_SYSSVC_TARGET_C_FILES=$PWD/syssvc/test_svc.c > /tmp/hd-t2-conf.log 2>&1
echo "conf rc=$?"
cmake --build build/musca_b1-2core-tdcre4 > /tmp/hd-t2-build.log 2>&1; echo "build rc=$?"
tools/cfg_equivalence.sh build/musca_b1-2core-tdcre4 > /tmp/hd-t2-eq.log 2>&1; echo "eq rc=$?"
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 90 cmake --build build/musca_b1-2core-tdcre4 --target run > /tmp/hd-t2-run.log 2>&1
grep 'TTSP_RESULT: PASS' /tmp/hd-t2-run.log
grep -c 'Check point' /tmp/hd-t2-run.log      # 期待: 15（★増えていないこと）
grep -n 'Assertion failed\|Unexpected' /tmp/hd-t2-run.log
pgrep -a qemu
```
期待: `conf/build/eq` すべて 0、`TTSP_RESULT: PASS` が実在、`Check point` が **15**、
`Assertion failed` が **0 行**。

★`Check point` が 15 でない場合は**挿入位置か checkpoint の追加を間違えている**。
★`E_NOMEM` ではなく別の ercd で落ちた場合は、**Task 1 で足した `CHECK_PAR` に
引っかかっていないか**を最初に疑う（`blkcnt = 400`・`blksz = 4` は
`SIZE_MAX / 4 = 0x3FFFFFFF` を遥かに下回るので通るはずである）。

- [ ] **Step 4: ★変異 control（巻き戻し条件の破壊。予測を先に書く）**

**予測（実行前に report へ書く）:** `kernel/mempfix.c` の

```c
				if (pk_cmpf->mpf == NULL) {
```

を

```c
				if (mpf == NULL) {
```

へ**一時的に**変える。ローカルの `mpf` は①の成功で非 NULL になっているため
`free_mpk(mpf)` が呼ばれなくなり、①が漏れる。したがって

- ケースA は変わらず `E_NOMEM`（②の失敗は同じ）、
- **ケースB の `check_assert(erid == mpfid1)` が落ちる**（`acre_mpf` が `E_NOMEM` を返し、
  `erid` が負になるため）。ログには `## Assertion failed` と `TTSP_RESULT: FAIL` が出る。

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
# ここで上記の1行を変異させる（編集）
cmake --build build/musca_b1-2core-tdcre4 > /dev/null 2>&1
timeout -k 5 90 cmake --build build/musca_b1-2core-tdcre4 --target run > /tmp/hd-t2-mut.log 2>&1
grep -n 'Assertion failed\|TTSP_RESULT' /tmp/hd-t2-mut.log
pgrep -a qemu
```
期待: `TTSP_RESULT: FAIL` と `Assertion failed` が出る。**予測した行で落ちていること**を
ログの直前の `Check point` 番号（11 が最後に出ていること）で確認する。

★**落ちなかった場合**：テストは巻き戻し経路を通っていない（＝ケースA が②ではなく①で
失敗している、または `count` が 0 に戻っていない）。**値を調整して通す前に、
Step 1 の算術のどこが実測と違ったかを突き止める。**

- [ ] **Step 5: 変異を戻し、非退行を再確認してコミット**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
git diff --stat kernel/mempfix.c          # ★空であること（変異が残っていない）
git status --short
cmake --build build/musca_b1-2core-tdcre4 > /dev/null 2>&1
timeout -k 5 90 cmake --build build/musca_b1-2core-tdcre4 --target run > /tmp/hd-t2-restore.log 2>&1
grep 'TTSP_RESULT: PASS' /tmp/hd-t2-restore.log
grep -c 'Check point' /tmp/hd-t2-restore.log   # 期待: 15
pgrep -a qemu
```
★`git diff --stat kernel/mempfix.c` が**空でない**なら変異が残っている。**必ず戻す。**

`DIVERGENCE_MAP.md` の `test/test_dcre4.c` の行に追記する：
「hardening パス Task 2 で `acre_mpf` の**2 段確保・②失敗時の巻き戻し**経路の
runtime 検査を追加（`blkcnt=400/blksz=4` で①成功・②失敗させ、直後に
`blkcnt=200/blksz=4` が成功することで①の解放を観測する。bump allocator の
`count==0` リセットを梃子にした間接観測であり、`free_mpk` の呼出しそのものは
公開 API から観測できない）。変異 control（`pk_cmpf->mpf` → ローカル `mpf`）で
ケースB が倒れることを実演済み。checkpoint は増やしていない（15 のまま）」

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
git add -A && git commit -m "test(dcre): acre_mpf の2段確保・巻き戻し経路を runtime で実証（変異control付き）"
```

---

### Task 3: ★最難関(2) — `isrseq` 完全ドレイン→enqueue の回帰テスト（ISR 段階の課題④）

**推奨モデル:** 上位（opus 相当）。**2 コア 3 タスク 2 ISR のハンドシェイク設計**であり、
「どちらのコアで何が走るか」を正確に追えないと組めない。
うまく動かないときは**値を調整するのではなく、まず紙に書いて確かめる**
（ISR 段階 Task 6 の申し送り）。

**背景（ISR 段階 Task 4 の裁定 → Task 6 レビューで判明したギャップ）:**

`enqueue_isr` はもともと「キューが空になったとき `isrseq` を 0 へ戻す」実装だった。
ISR 段階 Task 4 のレビューで、**走査中にキューが一時的に空になると、その後
`acre_isr` された ISR の `isrseq` が 0 から振り直され、走査側の継続キー `cur` に負けて
同一起動では拾われない**という合成ギャップが判明し、Task 5 で**リセットを撤去**した
（`kernel/interrupt.c:172-193` のコメント）。

しかし ISR 段階 Task 6 の `test_dcre5` は **INTNO1 に静的 ISR が常駐する**構成でしか
走査中の del/acre を試しておらず、**キューが完全に空になる窓を 1 度も通っていない**。
すなわち**旧リセット挙動でも通ってしまうテスト**である（`test/test_dcre5.c:371-383` の
コメントに「hardening pass の課題」として明記されている）。本 Task でこれを閉じる。

**Files:**
- Modify: `test/test_dcre5.h`（`CMD_FIRE_DRAIN` / `DRAIN_DELAY` / extern 3 個）
- Modify: `test/test_dcre5.cfg`（`CLS_PRC1` に `TASK2` を 1 行）
- Modify: `test/test_dcre5.c`（グローバル 6 個・ISR 2 本・`task2`・`task3` の分岐 1 個・
  `task1` の手順9・ヘッダコメント・手順3 のギャップ記述）
- Modify: `kernel/interrupt.c`（`enqueue_isr` のコメントに回帰テストの所在を追記。**コード不変**）
- Modify: `DIVERGENCE_MAP.md`

**Interfaces:**
- `acre_isr`/`del_isr`/`ras_int`/`act_tsk`/`dly_tsk`/`ext_tsk`（`include/kernel.h`）。
- `T_CISR { isratr, exinf, intno, isr, isrpri }`。
- `INTNO2` / `INTNO2_INTATR` / `INTNO2_INTPRI` / `intno2_clear()`
  （`target/musca_b1_gcc/target_test.h:49-53`。`intno2_clear()` は**空マクロ**）。
- `check_point` / `check_point_prc` / `check_ercd` / `check_assert` / `check_finish`
  （`syssvc/test_svc.h`）。★`check_finish(count)` は内部で `check_point_prc(count, 0)` を
  呼ぶので、**それ自身が "Check point <count> passed." を 1 本出す**。
- 既存の `isr_log_put` / `isr_log_is` / `SPIN_LIMIT` / `prc2_cmd` / `prc2_quit`
  （`test/test_dcre5.c`）。

**★このテストが検査する性質（1 行で）:**

> `call_isr` の走査中にキューが**完全に空**になったあと、同じ `isrpri` で `acre_isr` された
> ISR が、**2 度目の割込みを起こさずに、同一の割込み起動の中で**実行される。

- [ ] **Step 1: 前提の現物確認（5 件。1 つでも食い違ったら止まる）**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
sed -n '408,460p' kernel/interrupt.py       # ENA_DYNISR のエラーチェック群
sed -n '430,445p' kernel/interrupt.py       # 「静的 CRE_ISR が0個」ガードの粒度
sed -n '196,215p' kernel/interrupt.c        # enqueue_isr（リセット分岐が無いこと）
sed -n '635,720p' kernel/interrupt.c        # del_isr（unlink→TA_NOEXS→quiesce の順）
sed -n '437,462p' kernel/interrupt.c        # call_isr の走査（ISR_KEY_GT の継続キー）
sed -n '148,160p' kernel/task_manage.c      # act_tsk の文脈検査
sed -n '26,45p' test/test_dcre5.cfg         # INTNO2 の CLASS(CLS_PRC2) ブロック
grep -n "AID_ISR" test/test_dcre5.cfg
grep -n "CRE_TSK" test/test_dcre5.cfg
```

**確認して report に書くこと（file:line つき）:**

1. **「`ENA_DYNISR` が 1 個以上あるのに静的 `CRE_ISR` が 0 個」のガードは
   *システム全体*の本数で判定している**（`kernel/interrupt.py:438` の
   `if len(dynIsrList) > 0 and len(cfgData["CRE_ISR"]) == 0:`。Ruby は
   `kernel/interrupt.trb` の対応行）。
   → **intno ごとに静的 ISR が要るわけではない**。既存 `test_dcre5.cfg` の
   `INTNO2` は静的 ISR を 1 本も持たずに `ENA_DYNISR` されており、
   `INTNO1` の `ISR_S4`/`ISR_S2` がシステム全体の要求を満たしている。
   ★**この前提が本 Task の全体を支えている。** 食い違ったら止まる。
2. **`enqueue_isr` に空キューリセットの分岐が無い**こと（`kernel/interrupt.c:197-214`。
   `p_isrcb->isrseq = p_isr_queue->isrseq; p_isr_queue->isrseq += 1U;` の 2 行だけ）。
3. **`del_isr` が `queue_delete` →`isratr = TA_NOEXS`→ quiesce の順**であること
   （`kernel/interrupt.c:641`/`:668-669`/`:708-714`。ISR 段階の訂正B）。
   → **unlink はキュー操作の一番最初**なので、A を消した瞬間に INTNO2 のキューは空になる。
   → quiesce ループは毎周 `release_glock(); unlock_cpu(); delay_for_interrupt();` する
   （`:709-711`）ので、**PRC1 で他タスクがディスパッチされる窓が周回ごとに開く**。
4. **`act_tsk` は非タスクコンテキストからも呼べる**（`kernel/task_manage.c:150` が
   `CHECK_UNL_MYSTATE(&p_selftsk, &context)` であり `CHECK_TSKCTX_*` ではない）。
   ★本 Task の最終設計では **ISR 本体から `act_tsk` を呼ばない**が、
   「呼べること」自体は確認しておく（初期案からの設計変更の根拠になる）。
5. `test_dcre5.cfg` に `AID_ISR(4)`（動的スロット 4 個）があり、手順9 の時点で
   生存している動的 ISR が **0 個**であること（手順8 までにすべて `del_isr` 済み）。
   → A と B の 2 個で足りる。

★1 つでも食い違ったら**自己修復せず止まって報告する**（Constraint 17）。

- [ ] **Step 2: `test/test_dcre5.h` への追加**

`CMD_QUIT` の行の直後に 1 行、`#ifndef TOPPERS_MACRO_ONLY` の前に定数 1 個を足す。

```c
#define CMD_FIRE_DRAIN	4		/*  完全ドレイン→enqueue（手順9）  */

/*
 *  手順9 で TASK2 が最初に眠る時間（単位: マイクロ秒）
 *
 *  ★TASK2 は TASK1 より高優先度なので，act_tsk された瞬間に PRC1 を奪う．
 *  そこで最初に dly_tsk で眠り，その間に TASK1 が del_isr へ入って
 *  「unlink（キューが空になる）→ TA_NOEXS → quiesce ループ」へ到達できる
 *  ようにする．quiesce ループは周回ごとにジャイアントロックとCPUロックを
 *  解放するので，起床した TASK2 はその窓で TASK1 を横取りして acre_isr できる．
 *
 *  ★短すぎて TASK1 が del_isr に入る前に TASK2 が acre してしまうと，
 *  キューが空にならず本テストは空虚になる．その場合は変異 control
 *  （旧リセット分岐の再導入）が倒れなくなるので必ず気づける
 *  ＝control の成否が振り付けの成否を兼ねている．足りない場合は増やす
 *  （減らさない）．調整したら最終値を記録すること．
 */
#define DRAIN_DELAY		10000U		/* 10ms */
```

`extern` 宣言ブロックへ 3 行追加する。

```c
extern void	task2(EXINF exinf);
extern void	drain_isr_a(EXINF exinf);
extern void	drain_isr_b(EXINF exinf);
```

- [ ] **Step 3: `test/test_dcre5.cfg` — `TASK2`（PRC1・高優先度・休止起動）を追加**

`CLASS(CLS_PRC1) { … }` の中、`CRE_TSK(TASK1, …)` の**直後**に 1 行足す。

```c
	/*
	 *  TASK2: 手順9 で「quiesce のロック解放窓に割り込んで acre_isr する」役．
	 *  TASK1（MID_PRIORITY）より高優先度・TA_NULL（休止）で生成し，
	 *  TASK1 の act_tsk で起動する．
	 */
	CRE_TSK(TASK2, { TA_NULL, 2, task2, HIGH_PRIORITY, STACK_SIZE, NULL });
```

★`INTNO2` 側の `CLASS(CLS_PRC2)` ブロックは**触らない**
（静的 ISR ゼロ + `ENA_DYNISR` のまま。これが本 Task の前提である）。

- [ ] **Step 4: `test/test_dcre5.c` — グローバルと ISR 2 本**

`/* ISR 文脈からのサービスコール（手順7）*/` のブロックの直後に足す。

```c
/*
 *  ★走査中の完全ドレイン→enqueue（手順9・hardening 課題④）
 */
static volatile bool_t	dr_a_started;	/*  A の本体に入った  */
static volatile bool_t	dr_a_finished;	/*  A の本体を抜けた  */
static volatile bool_t	dr_a_timeout;	/*  A の待ちが上限に達した  */
static volatile bool_t	dr_b_acred;		/*  TASK2 が B を生成し終えた  */
static volatile bool_t	dr_b_fired;		/*  B の本体が呼ばれた  */
static volatile ER_ID	dr_erid_b;		/*  B の ISRID（結果）  */
```

`ctx_isr` の直後に ISR 2 本を足す。

```c
/*
 *  手順9 の A：キューに自分しかいない状態で走り，B が生成されるまで待つ
 *
 *  ★この待ちが「走査中にキューが完全に空になる」窓そのものである．
 *  待ちは有界で，上限に達したら dr_a_timeout を立てて抜ける（QEMU を
 *  ハングさせない）．
 */
void
drain_isr_a(EXINF exinf)
{
	uint32_t	i;

	intno2_clear();
	isr_log_put('A');
	dr_a_started = true;

	for (i = 0U; i < SPIN_LIMIT && !dr_b_acred; i++) {
	}
	if (!dr_b_acred) {
		dr_a_timeout = true;
	}
	dr_a_finished = true;
}

/*
 *  手順9 の B：空になったキューへ acre_isr された ISR
 *
 *  ★2度目の ras_int をしていないのにこれが呼ばれることが，
 *  isrseq の単調性（＝空キューでリセットしないこと）の証拠である．
 */
void
drain_isr_b(EXINF exinf)
{
	intno2_clear();
	isr_log_put('B');
	dr_b_fired = true;
}
```

- [ ] **Step 5: `test/test_dcre5.c` — `task2` と `task3` の分岐**

`task3` の**直前**に `task2` を足す。

```c
/*
 *  PRC1 側の横取り役（手順9）
 *
 *  TASK1（MID）より高優先度なので act_tsk された瞬間に走るが，
 *  最初に dly_tsk で眠って TASK1 を del_isr の quiesce まで進ませる．
 *  起床後は quiesce のロック解放窓で TASK1 を横取りし，**空になった**
 *  INTNO2 のキューへ B を acre_isr する．
 */
void
task2(EXINF exinf)
{
	T_CISR	cisr;

	(void) dly_tsk(DRAIN_DELAY);

	cisr.isratr = TA_NULL;
	cisr.exinf = (EXINF) 'B';
	cisr.intno = INTNO2;
	cisr.isr = drain_isr_b;
	cisr.isrpri = 1;			/*  ★A と同じ isrpri（isrseq でしか区別できない）  */
	dr_erid_b = acre_isr(&cisr);
	dr_b_acred = true;
	ext_tsk();
}
```

`task3` の `while (!prc2_quit)` の中、`CMD_FIRE_LONG` の分岐の**直後**に足す。

```c
			else if (prc2_cmd == CMD_FIRE_DRAIN) {
				/*  手順9：INTNO2 を1度だけ発火する（★2度目は無い）  */
				(void) ras_int(INTNO2);
				prc2_cmd = CMD_NONE;
			}
```

★`isrpri` を A と B で**同じ値（1）**にすることが本テストの要である。
異なる `isrpri` にすると `ISR_KEY_GT` の第 1 キーだけで判定が付き、
`isrseq` の単調性を検査していないテストになる（＝空虚）。**この理由をコメントに残す。**

- [ ] **Step 6: `test/test_dcre5.c` — `task1` の手順9 と checkpoint の付け替え**

既存の

```c
	/*
	 *  8) 削除後は呼ばれない（静的 ISR だけに戻る）
	 */
	isr_log_cnt = 0U;
	check_ercd(ras_int(INTNO1), E_OK);
	check_assert(isr_log_is("24"));
	check_point(9);

	prc2_quit = true;
	check_finish(10);
```

を次で置き換える（**`check_finish` の番号が 10→11 に変わる。他の番号は変えない**）。

```c
	/*
	 *  8) 削除後は呼ばれない（静的 ISR だけに戻る）
	 */
	isr_log_cnt = 0U;
	check_ercd(ras_int(INTNO1), E_OK);
	check_assert(isr_log_is("24"));
	check_point(9);

	/*
	 *  9) ★走査中にキューが完全に空になってからの acre_isr
	 *     （ISR段階の hardening 課題④。isrseq 単調化の直接の回帰）
	 *
	 *  INTNO2 は静的 ISR を1本も持たない動的専用の割込み番号である
	 *  （システム全体としての「静的 CRE_ISR が1本以上」という cfg の要求は
	 *  INTNO1 の ISR_S4/ISR_S2 が満たしている — kernel/interrupt.py の
	 *  ENA_DYNISR チェックはシステム全体の本数で判定する）．
	 *  そこへ A（isrpri 1）だけを生成して発火させると，PRC2 の call_isr は
	 *  A を呼んでいる間，キューに A しか持たない．A の実行中に PRC1 から
	 *  del_isr(A) すると，unlink の時点で **キューは完全に空** になる．
	 *  その空のキューへ TASK2 が B（★A と同じ isrpri 1）を acre_isr する．
	 *
	 *  【検査する性質】isrseq はキューの生存期間を通じて単調なので，B の
	 *  isrseq は A の isrseq より大きく，走査側の継続キー cur = (1, seqA) を
	 *  上回る．したがって B は **同じ割込み起動の中で** 呼ばれる
	 *  （2度目の ras_int をしていないのに 'B' がログに載る）．
	 *  旧実装（キューが空のとき isrseq を 0 へ戻す）では B の isrseq が 0 に
	 *  なって cur に負け，本起動では呼ばれなかった．ISR段階 Task 5 で
	 *  リセットを撤去した裁定の直接の回帰である．
	 *
	 *  【振り付け】
	 *   (1) TASK1(PRC1,MID): A を acre → TASK3 へ CMD_FIRE_DRAIN
	 *   (2) TASK3(PRC2,HIGH): ras_int(INTNO2) → PRC2 で call_isr → A を実行
	 *   (3) A の本体: 'A' を記録し dr_b_acred を待つ（有界）
	 *   (4) TASK1: dr_a_started を見てから act_tsk(TASK2)
	 *   (5) TASK2(PRC1,HIGH): 即座に dly_tsk(DRAIN_DELAY) で眠る
	 *   (6) TASK1: del_isr(A) → unlink（★ここでキューが空）→ TA_NOEXS → quiesce
	 *   (7) TASK2: 起床し，quiesce のロック解放窓で TASK1 を横取りして
	 *              B を acre_isr（空のキューへ enqueue）→ dr_b_acred
	 *   (8) A の本体: dr_b_acred を見て終了 → call_isr が走査を再決定し
	 *              B を **同一起動で** 実行 → 'B' を記録
	 *   (9) TASK1: quiesce 完了で del_isr(A) が E_OK を返す
	 *
	 *  【なぜ act_tsk を A の本体から呼ばないか】
	 *  A の本体から act_tsk(TASK2) すると，「TASK1 が del_isr に入る前に
	 *  TASK2 が走ってしまう」窓を排除できない．TASK1 自身が act_tsk して
	 *  から del_isr を呼び，TASK2 は最初に眠る形にすると，TASK2 が起きる
	 *  のは TASK1 が del_isr の中でロックを解放したとき（＝quiesce ループの
	 *  中）に限られる．★それでも「TASK2 が早すぎる」可能性は完全には
	 *  排除できないが，その場合キューは空にならず，変異 control（旧リセット
	 *  分岐の再導入）が倒れなくなる．すなわち control の成否が振り付けの
	 *  成否を兼ねている（本テストが空虚でないことの検出器である）．
	 */
	cisr.isratr = TA_NULL;
	cisr.intno = INTNO2;
	cisr.isr = drain_isr_a;
	cisr.exinf = (EXINF) 'A';
	cisr.isrpri = 1;
	erid = acre_isr(&cisr);
	check_assert(erid > ISR_S4);
	id_a = (ID) erid;

	isr_log_cnt = 0U;
	dr_a_started = false;
	dr_a_finished = false;
	dr_a_timeout = false;
	dr_b_acred = false;
	dr_b_fired = false;
	dr_erid_b = 0;

	prc2_cmd = CMD_FIRE_DRAIN;

	for (i = 0U; i < SPIN_LIMIT && !dr_a_started; i++) {
	}
	check_assert(dr_a_started);

	check_ercd(act_tsk(TASK2), E_OK);		/*  TASK2 は即眠るのですぐ戻る  */
	check_ercd(del_isr(id_a), E_OK);		/*  unlink→空→TA_NOEXS→quiesce  */
	check_assert(dr_a_finished);			/*  quiesce の帰結（手順5と同型）  */
	check_assert(!dr_a_timeout);
	check_assert(dr_b_acred);
	check_assert(dr_erid_b > ISR_S4);

	/*  ★B が同一起動の走査で呼ばれる（2度目の ras_int はしていない）  */
	for (i = 0U; i < SPIN_LIMIT && !dr_b_fired; i++) {
	}
	check_assert(dr_b_fired);
	check_assert(isr_log_is("AB"));

	check_ercd(del_isr((ID) dr_erid_b), E_OK);
	check_ercd(del_isr(id_a), E_NOEXS);
	check_point(10);

	prc2_quit = true;
	check_finish(11);
```

**あわせてファイル冒頭のコメントを直す（3 箇所）:**

1. 【テストの目的】に 1 項足す：
   ```
    *	(H) ★走査中にキューが完全に空になった後で acre_isr された ISR が，
    *	    同一の割込み起動の中で拾われること（isrseq 単調化の直接の回帰．
    *	    ISR段階の hardening 課題④）．
   ```
2. 【使用リソース】の `TASK3` の行の前に
   `TASK2: 高優先度タスク，TA_NULL属性（静的・PRC1．手順9 の横取り役）` を足し、
   `INTNO2` の行を
   `INTNO2（PRC2）: 静的 ISR なし ＋ ENA_DYNISR（★手順9 の完全ドレインに使う）` に直す。
3. 【チェックポイント】を実測に合わせて直す：
   ```
    *	PRC1（check_count[0]，TASK1）: 1..10 + check_finish(11)
    *	PRC2（check_count[1]，TASK3）: 1,2（出力は "Check point 2-1/2-2 passed."）
    *	  ＝ログ中の "Check point" 行は合計 13 本（check_finish 自身の1本を含む）
    *	  ★この本数は実測で確かめ，違っていたら実測値を正とする．
   ```

- [ ] **Step 7: ビルドと等価性**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
cmake --preset musca_b1-2core -B build/musca_b1-2core-tdcre5 \
  -DFMP3_APPLNAME=test_dcre5 -DFMP3_APPLDIR=test \
  -DFMP3_SYSSVC_TARGET_C_FILES=$PWD/syssvc/test_svc.c > /tmp/hd-t3-conf.log 2>&1
echo "conf rc=$?"
cmake --build build/musca_b1-2core-tdcre5 > /tmp/hd-t3-build.log 2>&1; echo "build rc=$?"
tools/cfg_equivalence.sh build/musca_b1-2core-tdcre5 > /tmp/hd-t3-eq.log 2>&1; echo "eq rc=$?"
grep -c "call_isr" build/musca_b1-2core-tdcre5/generated/kernel_cfg.c   # 期待: 2
grep -n "TASK2" build/musca_b1-2core-tdcre5/generated/kernel_cfg.h
```
期待: `conf/build/eq` すべて 0（**2 は不合格**）、`call_isr` が **2**（INTNO1/INTNO2 の 2 本）、
`kernel_cfg.h` に `TASK2` の ID マクロが実在。

- [ ] **Step 8: 実行（★checkpoint 数は実測を正とする）**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 120 cmake --build build/musca_b1-2core-tdcre5 --target run > /tmp/hd-t3-run.log 2>&1
grep 'TTSP_RESULT: PASS' /tmp/hd-t3-run.log
grep -c 'Check point' /tmp/hd-t3-run.log       # 期待: 13
grep -n 'Check point' /tmp/hd-t3-run.log
grep -n 'Assertion failed\|Unexpected' /tmp/hd-t3-run.log
pgrep -a qemu
```
期待:
- `TTSP_RESULT: PASS` が実在。
- `Check point` が **13**（PRC1 の 1..10 + `check_finish(11)` + PRC2 の 2-1/2-2）。
  ★12 のままなら手順9 が実行されていない。14 以上なら checkpoint を足しすぎている。
- `Assertion failed` が 0 行。
- **実測が 13 でなければ実測を正とし、テストのヘッダコメントと Task 5 の期待値を直す**
  （Constraint 14）。直した事実を report に書く。

★**`dr_a_timeout` で落ちた場合**（`Assertion failed` が `check_assert(!dr_a_timeout)` の
位置）：TASK2 が起きて acre する前に A の待ちが尽きている。
`DRAIN_DELAY` を**減らす**か（TASK2 の起床を早める）、`SPIN_LIMIT` を**増やす**。
★**`dr_b_fired` で落ちた場合**：これは本 Task が検査している性質そのものが
成立していないということである。**値をいじる前に**
`kernel/interrupt.c:197-214` にリセット分岐が混入していないかを確認する。

- [ ] **Step 9: ★★変異 control（旧リセット分岐の再導入。本 Task の本命）**

**予測（実行前に report へ書く）:** `kernel/interrupt.c` の `enqueue_isr`（`:197-214`）の

```c
	p_isrcb->isrseq = p_isr_queue->isrseq;
```

の**直前**へ、ISR 段階 Task 5 で撤去した分岐を**一時的に**戻す。

```c
	if (queue_empty(&(p_isr_queue->isr_queue))) {
		p_isr_queue->isrseq = 0U;
	}
```

すると手順9 では
- A は空の INTNO2 キューへ enqueue されるので `isrseq = 0`、
- B も空の（A が unlink 済みの）キューへ enqueue されるので `isrseq = 0`、
- 走査側の継続キーは `cur = (1, 0)`、B のキーも `(1, 0)` で
  `ISR_KEY_GT` が**偽** → B は本起動で呼ばれない、

したがって **`check_assert(dr_b_fired)` が落ち**（その前に `SPIN_LIMIT` 回のスピンを消費する）、
`isr_log` は `"A"` のままになる。ログの最後の `Check point` は **9**。
手順1〜8（INTNO1 は静的 ISR が常駐するのでキューが空にならない／手順5 の INTNO2 は
ISR が 1 本だけ）は**すべて通る**。

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
# ここで上記3行を enqueue_isr へ一時的に戻す（編集）
cmake --build build/musca_b1-2core-tdcre5 > /dev/null 2>&1
timeout -k 5 180 cmake --build build/musca_b1-2core-tdcre5 --target run > /tmp/hd-t3-mut.log 2>&1
grep -n 'Assertion failed\|TTSP_RESULT' /tmp/hd-t3-mut.log
grep -n 'Check point' /tmp/hd-t3-mut.log | tail -3
pgrep -a qemu
```
期待: `TTSP_RESULT: FAIL` と `Assertion failed`、最後の `Check point` が **9**。
（`SPIN_LIMIT` の空回しが入るので `timeout` は 180 秒に伸ばしてある。）

★**倒れなかった場合、本テストは狙った窓を通っていない**（中核リスク3）。
そのときは `DRAIN_DELAY` を調整して誤魔化さず、**どちらのコアで何が走るかを紙に書き、
「B を enqueue した時点でキューは本当に空だったか」を再検討する**。
考えられる原因は 2 つだけである：
(a) TASK2 が TASK1 の `del_isr` より先に acre してしまった（キューに A が残っていた）、
(b) INTNO2 のキューに A 以外の何かが載っていた（cfg を変えてしまった）。

- [ ] **Step 10: 変異を戻し、カバレッジギャップの記述を「解消済み」へ更新**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
git diff --stat kernel/interrupt.c      # ★変異が残っていないこと（この時点では空）
```

**(a) `test/test_dcre5.c` の手順3 のコメント**（現行 `:371-383`）の
「…当該性質（完全ドレイン後の cur 以降位置への enqueue が走査中の cur に負けない）の
回帰テストはスイート内に未整備 — hardening pass の課題．」の部分を、次で置き換える。

```c
	 *    ★この性質の回帰は hardening パス Task 3 で**手順9 に追加した**
	 *    （INTNO2＝静的 ISR ゼロの動的専用キューを使い，走査中に完全ドレイン
	 *    させてから同一 isrpri で acre する）．旧リセット分岐を戻すと手順9 が
	 *    倒れることを変異 control で実演済みである．本手順（手順3）は
	 *    引き続き「同一 isrpri の順序が isrseq タイブレークに依存すること」
	 *    だけを検査する．
```

**(b) `kernel/interrupt.c` の `enqueue_isr` コメント**（`:183-193` のブロックの末尾）に
1 文を足す（**コードは 1 バイトも変えない**）。

```c
 *  ★この単調性の回帰テストは test/test_dcre5.c の手順9 である
 *  （静的ISRを持たないINTNO2のキューを走査中に完全ドレインさせ，
 *  同一isrpriで再acreしたISRが同一起動内で拾われることを実測する）．
 *  リセット分岐を戻すと手順9 が倒れることを変異controlで確認済み．
```

**(c) `DIVERGENCE_MAP.md`** の `kernel/interrupt.c` の行と `test/test_dcre5.*` の行に、
「ISR 段階で未整備だった『走査中の完全ドレイン→enqueue』の回帰カバレッジを
hardening パス Task 3 で解消（`test_dcre5` 手順9・変異 control 付き）」を追記する。
★ISR 段階が「既知・対処しない事項」等へ**ギャップとして書いた記述があれば、
それも解消済みへ書き換える**（片方だけ直すと台帳が矛盾する）。

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
grep -n "hardening\|課題④\|完全ドレイン" DIVERGENCE_MAP.md test/test_dcre5.c kernel/interrupt.c
```

- [ ] **Step 11: 非退行の再確認とコミット**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
cmake --build build/musca_b1-2core-tdcre5 > /dev/null 2>&1
timeout -k 5 120 cmake --build build/musca_b1-2core-tdcre5 --target run > /tmp/hd-t3-restore.log 2>&1
grep 'TTSP_RESULT: PASS' /tmp/hd-t3-restore.log
grep -c 'Check point' /tmp/hd-t3-restore.log   # 期待: 13
pgrep -a qemu
git status --short
git diff --stat kernel/interrupt.c             # ★コメント行だけの差分であること
```
★`kernel/interrupt.c` の差分が**コメント以外を含んでいたら変異が残っている**。必ず戻す。

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
git add -A && git commit -m "test(dcre): 走査中の完全ドレイン→enqueue を test_dcre5 手順9 で実証（isrseq単調化の回帰・変異control付き）"
```

---

### Task 4: `intpri` エラー回帰 cfg と、Task 1 の検査の runtime 実演

**推奨モデル:** 中位（sonnet）。作業は 2 系統（cfg 回帰 1 件 + runtime の `E_PAR` 4〜5 件 +
Task 1 から繰り越した変異 control）だが、いずれも既存の型どおりである。
ただし**中核リスク1（`#if` で消えていて 1 行も実行されていない）**の担保だけは慎重に。

**Files:**
- Create: `tools/cfg_error_tests/dcre_dynisr_unmanaged_intpri.cfg`
- Modify: `test/test_dcre4.c`（手順8 の末尾に `E_PAR` 4 件）
- Modify: `test/test_dcre1.c`（`:196-205` の `stk=NULL` 節に `E_PAR` 1 件）
- Modify: `DIVERGENCE_MAP.md`

**Interfaces:**
- `tools/cfg_error_tests/run.sh <builddir> <cfg> <期待ercd> [EXTRA_CFLAGS]`（4 引数形。Constraint 11）。
- `kernel/interrupt.py:411-416` / `kernel/interrupt.trb:455-` の
  「`intno` でカーネル管理外の割込みを指定した場合（`E_OBJ`）」の分岐。**触らない**。
- `SIZE_MAX`（`<stdint.h>`）/ `UINT_MAX`（`<limits.h>` 経由 `arch/gcc/tool_stddef.h:82`）。
- `MPK_SIZE`/`STACK_SIZE`（`test/test_dcre1.h`・`test/test_dcre4.h`）。

- [ ] **Step 1: 前提の現物確認（食い違ったら止まる）**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
grep -n "TMIN_INTPRI" target/musca_b1_gcc/target_kernel.h
grep -n "TBITW_IPRI" target/musca_b1_gcc/target_sil.h
grep -n "INTPRI_CFGINT_VALID" target/musca_b1_gcc/target_kernel.py target/musca_b1_gcc/target_kernel.trb
sed -n '405,420p' kernel/interrupt.py
sed -n '450,462p' kernel/interrupt.trb
cat tools/cfg_error_tests/dcre_dynisr_no_cfgint.cfg
ls tools/cfg_error_tests/*.cfg | wc -l
sed -n '40,55p' target/musca_b1_gcc/target_test.h
```

**確認して report に書くこと（実測値）:**

1. `TMIN_INTPRI == -3`（`target/musca_b1_gcc/target_kernel.h:66`）、
   `TBITW_IPRI == 3`（`target/musca_b1_gcc/target_sil.h:18`）。
2. `INTPRI_CFGINT_VALID = list(range(-(1 << TBITW_IPRI), 0))` = **-8〜-1**
   （`target/musca_b1_gcc/target_kernel.py:55`。Ruby は `target_kernel.trb:57`）。
   → **`intpri = -4` は `CFG_INT` としては合法**（`E_PAR` にならない）**だが
   `TMIN_INTPRI(-3)` より小さい＝カーネル管理外**である。
   ★この 2 つの範囲が食い違っていることが、本 cfg テストが成立する理由である。
   もし `INTPRI_CFGINT_VALID` が `TMIN_INTPRI..TMAX_INTPRI` に落ちていたら
   （＝ターゲット側の上書きが無かったら）、`intpri = -4` は先に `E_PAR` で弾かれ、
   **`ENA_DYNISR` の `E_OBJ` へ到達できない**。そのときは止まって報告する。
3. `kernel/interrupt.py:411-416` に
   `if intnoParams["intpri"] < TMIN_INTPRI: error_ercd("E_OBJ", …)` があること、
   Ruby 側（`kernel/interrupt.trb:455` 付近）にも同じ分岐があること
   ＝**両エンジン対称**であること。
4. `tools/cfg_error_tests/*.cfg` の実在個数（ISR 段階 Task 7 時点で **37**）。
   本 Task で **38** になる。★食い違ったら実測を正とし、Task 5 の期待値を直す。
5. `INTNO1 == (1U << 16) | 76`、`INTNO1 + 1` が `CFG_INT` されていない予備 IRQ61 であること
   （`dcre_dynisr_no_cfgint.cfg` が既に使っている）。

- [ ] **Step 2: `tools/cfg_error_tests/dcre_dynisr_unmanaged_intpri.cfg` を作る**

既存 `dcre_dynisr_no_cfgint.cfg` と同じ骨格にする（`test_common1.cfg` を INCLUDE し、
`#include "test_int2.h"` を使うので **`EXTRA_CFLAGS` が必須**）。

```c
/*
 *  カーネル管理外の割込み優先度を持つ intno に ENA_DYNISR を書くと E_OBJ
 *
 *  musca_b1 の CFG_INT は intpri に -8〜-1 を許すが（TBITW_IPRI = 3）、
 *  TMIN_INTPRI は -3 である。したがって intpri = -4 は
 *  「CFG_INT としては合法だがカーネル管理外」という状態を作れる。
 *  割込みサービスルーチンはカーネル管理外の割込みを扱えないので、
 *  その intno を ENA_DYNISR すると E_OBJ になる
 *  （kernel/interrupt.py の ENA_DYNISR チェック群 / kernel/interrupt.trb の同一分岐。
 *  静的な CRE_ISR に対する同じ検査は両エンジンとも既に存在し、
 *  ENA_DYNISR 側は ISR 段階で追加したが回帰列に無かった＝本ファイルで埋める）。
 *
 *  実行時に EXTRA_CFLAGS として「-I<repo>/test」（第4引数）を渡すこと。
 */
INCLUDE("test/test_common1.cfg");
#include "test_int2.h"
CLASS(CLS_PRC1) {
	CRE_TSK(TASK1, { TA_ACT, 1, task1, MID_PRIORITY, STACK_SIZE, NULL });

	/*  静的 CRE_ISR を1本置く（ENA_DYNISR の「システム全体で静的ISRが1本以上」
	 *  という要求を満たすため。これが無いと別の E_OBJ が先に出て、
	 *  本ファイルが何を検査しているのか読めなくなる）  */
	CFG_INT(INTNO1, { INTNO1_INTATR, INTNO1_INTPRI });
	CRE_ISR(ISR1, { TA_NULL, 1, INTNO1, isr1, 1 });

	/*  ★カーネル管理外（intpri < TMIN_INTPRI = -3）の割込み要求ライン  */
	CFG_INT(INTNO1 + 1, { INTNO1_INTATR, -4 });
	ENA_DYNISR(INTNO1 + 1);
}
AID_ISR(1);
```

★**`INTNO1_INTATR`（`TA_ENAINT`）がカーネル管理外の `intpri` と両立しない**エラーが出た場合は
`TA_NULL` に落とす。**落とした事実を cfg のコメントと report に書く**（Constraint 14）。

- [ ] **Step 3: 両エンジンで `E_OBJ` を実測**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
tools/cfg_error_tests/run.sh build/musca_b1-2core \
  tools/cfg_error_tests/dcre_dynisr_unmanaged_intpri.cfg E_OBJ "-I$PWD/test"
echo "rc=$?"
```
期待: **`rc=0`**（Ruby / Python が同じ ercd と同じ文言でエラーにした）。
- `rc=1` は不一致（片方のエンジンだけが検出した／文言が違う）。
- **`rc=2` は前提未充足であり合格ではない**（Constraint 7）。
  第 4 引数を渡し忘れると `rc=2` になる。

★`E_OBJ` が**2 回**出る可能性がある（`ENA_DYNISR` が `continue` するので
`dynIsrList` が空になり、`AID_ISR(1)` 側の「`AID_ISR requires at least one ENA_DYNISR`」も
発火する）。`run.sh` は期待文字列の**実在**を見るので `rc=0` のままだが、
**何個出たかをログで確認して report に書く**（`dcre_dynisr_no_cfgint.cfg` と同じ挙動のはず）。

- [ ] **Step 4: `test/test_dcre4.c` — Task 1 の検査 4 件の runtime 実演**

手順8（mpf）の

```c
	cmpf.mpfatr = TA_TPRI | 0x04U;				/*  未定義ビット → E_RSATR  */
	check_assert(acre_mpf(&cmpf) == E_RSATR);
	cmpf.mpfatr = TA_TPRI;
```

の**直後**、`/*  ★プールが実際に返っていることの実証  */` の**前**に挿入する。

```c
	/*
	 *  ★サイズ計算のあふれ検査（hardening パス Task 1 で追加した CHECK_PAR）
	 *
	 *  dtqcnt/pdqcnt/blkcnt は uint_t，sizeof(DTQMB) 等は size_t である．
	 *  両者が同幅のターゲット（32bit）でのみ積があふれうるので，検査に
	 *  到達できるのも 32bit ターゲットに限る．64bit では同じ入力が検査を
	 *  素通りして E_NOMEM になる（あふれないので正しい挙動である）．
	 *
	 *  ★この #if が丸ごと消えていて 1 行も実行されていない，という事態を
	 *  防ぐため，#if の外で同じ条件を実行時にも確かめる（下の check_assert）．
	 *  本テストは musca_b1-2core でのみ実行するので，ここは必ず真である．
	 */
	check_assert(sizeof(size_t) == sizeof(uint_t));

#if SIZE_MAX == UINT_MAX
	/*  (a) acre_dtq: dtqcnt * sizeof(DTQMB) があふれる  */
	cdtq.dtqcnt = UINT_MAX;
	check_assert(acre_dtq(&cdtq) == E_PAR);

	/*  (b) acre_pdq: pdqcnt * sizeof(PDQMB) があふれる  */
	cpdq.pdqcnt = UINT_MAX;
	check_assert(acre_pdq(&cpdq) == E_PAR);

	/*  (c) acre_mpf: ROUND_MPF_T(blksz) * blkcnt があふれる  */
	cmpf.blkcnt = UINT_MAX;
	cmpf.blksz = MPF_BLKSZ;
	check_assert(acre_mpf(&cmpf) == E_PAR);

	/*  (d) acre_mpf: ROUND_MPF_T(blksz) の丸めそのものがあふれる
	 *      （検査が無いと丸め結果が 0 になり，blksz==0 のプールができる）  */
	cmpf.blkcnt = MPF_BLKCNT;
	cmpf.blksz = UINT_MAX;
	check_assert(acre_mpf(&cmpf) == E_PAR);
#endif /* SIZE_MAX == UINT_MAX */

	/*  以降の手順のためにパケットを元へ戻す  */
	cdtq.dtqcnt = 1U;
	cpdq.pdqcnt = 3U;
	cmpf.blkcnt = MPF_BLKCNT;
	cmpf.blksz = MPF_BLKSZ;
```

★**checkpoint は増やさない**（`test_dcre4` の `Check point` は **15** のまま）。
★`cdtq`/`cpdq` は `task1` のローカルでこの位置でも生存している。
**復元の 4 行を忘れない**（忘れると直後の `MPF_CYCLES` ループが全滅する）。

- [ ] **Step 5: `test/test_dcre1.c` — `acre_tsk` の丸めあふれ（H-4）の実演**

`test/test_dcre1.c:196-205` の

```c
	ctsk.stksz = MPK_SIZE * 4;				/* プールに入らない大きさ */
	…
	check_assert(erid == E_NOMEM);
	ctsk.stksz = STACK_SIZE;
```

の直後（`ctsk.stksz = STACK_SIZE;` の**後**）に挿入する。

```c
	/*
	 *  ★スタックサイズの丸めがあふれる場合は E_PAR
	 *  （hardening パス Task 1 の H-4。ROUND_STK_T(stksz) の加算あふれ）
	 *
	 *  stksz は size_t なので，32bit/64bit のどちらでも到達可能である
	 *  （dtqcnt 等と違って #if のガードが要らない）．検査が無いと丸め結果が
	 *  0 付近へ落ち，aligned_alloc_mpk が「成功」してゼロ長スタックの
	 *  タスクができあがる．
	 */
	ctsk.stksz = SIZE_MAX;
	check_assert(acre_tsk(&ctsk) == E_PAR);
	ctsk.stksz = STACK_SIZE;
```

★変数名（`ctsk`）と `acre_tsk` の呼び方は**現物に合わせる**（Step 1 で `sed -n '190,210p'` して
確認してから書く）。`erid = acre_tsk(&ctsk);` の形をとっている場合はそれに揃える。
★**checkpoint は増やさない**。

- [ ] **Step 6: ビルドと実行（tdcre1 / tdcre4）**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
cmake --build build/musca_b1-2core-tdcre4 > /tmp/hd-t4-b4.log 2>&1; echo "build4 rc=$?"
timeout -k 5 90 cmake --build build/musca_b1-2core-tdcre4 --target run > /tmp/hd-t4-r4.log 2>&1
grep 'TTSP_RESULT: PASS' /tmp/hd-t4-r4.log
grep -c 'Check point' /tmp/hd-t4-r4.log      # 期待: 15（★増えていないこと）
grep -n 'Assertion failed' /tmp/hd-t4-r4.log
pgrep -a qemu
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
cmake --build build/musca_b1-2core-tdcre1 > /tmp/hd-t4-b1.log 2>&1; echo "build1 rc=$?"
timeout -k 5 60 cmake --build build/musca_b1-2core-tdcre1 --target run > /tmp/hd-t4-r1.log 2>&1
grep 'TTSP_RESULT: PASS' /tmp/hd-t4-r1.log
grep -c 'Check point' /tmp/hd-t4-r1.log      # 期待: ISR段階 Task 7 の実測値と同じ
pgrep -a qemu
```
期待: 両方 `TTSP_RESULT: PASS`、`Check point` 行数が**変わらない**、`Assertion failed` が 0 行。
★`check_assert(sizeof(size_t) == sizeof(uint_t))` が落ちたら、
**`#if` ブロックが 1 行も実行されていない**（中核リスク1 の的中）。止まって報告する。

- [ ] **Step 7: ★★Task 1 から繰り越した変異 control 4 本**

Task 1 Step 12 で予測を書いた control を、ここで**実際に当てる**。
**1 本ずつ入れて 1 本ずつ戻す**（複数同時に壊さない）。各回、
「予測 → 実行 → 実測」の 3 点セットを report に書く。

| # | 変異箇所 | 予測される失敗 |
|---|---|---|
| C-1 | `kernel/dataqueue.c` の `CHECK_PAR(dtqcnt <= …)` を削除 | `test_dcre4` の (a) が `E_PAR` ではなく `E_NOMEM` を返し `check_assert` が落ちる |
| C-2 | `kernel/mempfix.c` の (a)（`blksz <= SIZE_MAX - …`）を削除 | `test_dcre4` の (d) が落ちる（丸め 0 → `malloc_mpk(0)` 成功 → ②で `E_NOMEM`） |
| C-3 | `kernel/task_manage.c` の `CHECK_PAR(stksz <= …)` を削除 | `test_dcre1` の新ケースが `E_PAR` ではなく **`E_OK`（成功）** になり `check_assert` が落ちる ★ゼロ長スタックのタスクが生成される |
| C-4 | `kernel/startup.c` の `if (aligned > limit) { return(NULL); }` を削除 | **落ちない可能性が高い**（下記） |

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
# C-1: kernel/dataqueue.c の CHECK_PAR を1行コメントアウト → 再ビルド → 実行
cmake --build build/musca_b1-2core-tdcre4 > /dev/null 2>&1
timeout -k 5 90 cmake --build build/musca_b1-2core-tdcre4 --target run > /tmp/hd-t4-c1.log 2>&1
grep -n 'Assertion failed\|TTSP_RESULT' /tmp/hd-t4-c1.log
pgrep -a qemu
```
（C-2 も同じ手順で `tdcre4`、C-3 は `tdcre1` に対して行う。各回のあと**必ず変異を戻し、
`git diff --stat` が空であることを確認してから次へ進む**。）

★**C-4 について（重要）：** `if (aligned > limit)` を消しても、
**現行の 3 プリセットのプール配置では `aligned > limit` になる入力を
公開 API から作れない可能性が高い**（`DEF_MPK` の領域が十分大きく、
`alignof(MB_T)`＝4 の調整で `limit` を跨ぐ状況にならない）。
その場合は**「control が空振りした」と正直に記録する**。
「落ちなかったから安全」ではなく、**「この防御は現構成では到達不能な入力に対するもので、
runtime control を持たない」**と書く（ISR 段階の Codex 指摘 #3 に対する
`search_isr_queue` の扱いと同じ流儀）。
★C-4 を通すために `MPK_SIZE` を歪める等の作為をしてはならない。
到達不能なら到達不能と書く（Constraint 13 の趣旨は「実演できないものを実演したと言わない」）。

- [ ] **Step 8: 変異の全撤去確認・`DIVERGENCE_MAP.md`・コミット**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
git diff --stat kernel/        # ★Task 1 のコミット以降の kernel/ 差分が無いこと（＝空）
git status --short
cmake --build build/musca_b1-2core-tdcre4 > /dev/null 2>&1
timeout -k 5 90 cmake --build build/musca_b1-2core-tdcre4 --target run > /tmp/hd-t4-restore4.log 2>&1
grep 'TTSP_RESULT: PASS' /tmp/hd-t4-restore4.log ; pgrep -a qemu
cmake --build build/musca_b1-2core-tdcre1 > /dev/null 2>&1
timeout -k 5 60 cmake --build build/musca_b1-2core-tdcre1 --target run > /tmp/hd-t4-restore1.log 2>&1
grep 'TTSP_RESULT: PASS' /tmp/hd-t4-restore1.log ; pgrep -a qemu
```

`DIVERGENCE_MAP.md` に追記する：
- `tools/cfg_error_tests/` は派生なので**台帳行は不要**だが、
  `kernel/interrupt.py`/`.trb` の行に「ISR 段階で deferred だった
  『カーネル管理外 `intpri` × `ENA_DYNISR` → `E_OBJ`』の回帰 cfg を
  hardening パス Task 4 で追加（`dcre_dynisr_unmanaged_intpri.cfg`。
  musca_b1 は `INTPRI_CFGINT_VALID`(-8..-1) ⊋ `TMIN_INTPRI`(-3) なので構成できる）」
  と 1 文を足す。
- `test/test_dcre4.c` / `test/test_dcre1.c` の行に「Task 1 の
  オーバーフロー検査 4 件 / 1 件の `E_PAR` を runtime で実演（32bit 限定の 4 件は
  `#if SIZE_MAX == UINT_MAX` で囲み、`#if` が生きていることを
  `check_assert(sizeof(size_t) == sizeof(uint_t))` で担保）。変異 control C-1/C-2/C-3 実演済み、
  C-4（`alloc_mempool` の `aligned > limit`）は**現構成から到達不能で control 無し**」を追記。

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
git add -A && git commit -m "test(dcre): カーネル管理外intpriのENA_DYNISR回帰cfgと、あふれ検査のE_PARをruntimeで実演"
```

---

### Task 5: 最終回帰と台帳整理

**推奨モデル:** 中位（sonnet）

**Files:**
- Modify: `DIVERGENCE_MAP.md`（掃除・監査）・`.superpowers/sdd/progress.md`（フラット台帳へ完了記録）

**このタスクではコードを直さない。** 欠陥を見つけたら**記録して報告**し、修正は別コミットに切る。

- [ ] **Step 1: 全 9 プリセット configure+build**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
for p in musca_b1 musca_b1-2core rp2350_pico2 polarfire_soc_kit polarfire_soc_kit-qemu \
         kria_arm64 kria_arm64-1core kria_r5 kria_r5-2core; do
  cmake --preset $p > /tmp/hd-t5-conf-$p.log 2>&1; c=$?
  cmake --build build/$p > /tmp/hd-t5-build-$p.log 2>&1; b=$?
  echo "$p conf=$c build=$b"
done
```
期待: `polarfire_soc_kit`（実機プリセット）のみ SoftConsole ツールチェーン不在
（`fatal error: cannot read spec file 'nano.specs'`）で**既知の環境ギャップとして fail**。
それ**以外の 8 構成が exit=0**。

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
for p in musca_b1 musca_b1-2core rp2350_pico2 polarfire_soc_kit-qemu \
         kria_arm64 kria_arm64-1core kria_r5 kria_r5-2core; do
  n=$(grep -c "warning:" /tmp/hd-t5-build-$p.log)
  echo "$p warnings=$n"
done
```
期待: 本パスが足した行に起因する `warning:` が**無い**こと（0 でない場合は
中身を読み、既存由来か本パス由来かを切り分けて report に書く）。

- [ ] **Step 2: ★ROM 増分の最終確認**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
find build/musca_b1-2core -name '*.elf' -exec arm-none-eabi-size {} \;
```
ISR 段階 Task 7 の記録値（`text +1796 / data +0 / bss +72`、ベースライン `f1f1d53` 比）に対し、
**本パスの増分**をバイトで記録する。`CHECK_PAR` 5 本と `alloc_mempool` の再構成なので
**数十〜200 バイト程度**が期待値である。桁が違ったら実装を疑う。

- [ ] **Step 3: 全 8 構成 + 派生ビルドの `tools/cfg_equivalence.sh`**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
for p in musca_b1 musca_b1-2core rp2350_pico2 polarfire_soc_kit-qemu \
         kria_arm64 kria_arm64-1core kria_r5 kria_r5-2core; do
  tools/cfg_equivalence.sh build/$p > /tmp/hd-t5-eq-$p.log 2>&1
  echo "$p eq=$?"
done
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
for d in musca_b1-2core-tmix kria_arm64-tmix musca_b1-2core-tdcre5 \
         musca_b1-2core-tdcre4 musca_b1-2core-tdcre3 musca_b1-2core-tint2; do
  tools/cfg_equivalence.sh build/$d > /tmp/hd-t5-eq-$d.log 2>&1
  echo "$d eq=$?"
done
```
期待: **14/14 が exit 0**。**2 は不合格**（Constraint 7）。
★本パスは cfg テンプレートを 1 行も触っていないので、ISR 段階 Task 7 と**同じ結果**に
ならなければおかしい。差が出たら止まって原因を突き止める。

- [ ] **Step 4: QEMU 起動 7 構成（★プリセットごとに個別実行・毎回 `pgrep`）**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 25 cmake --build build/polarfire_soc_kit-qemu --target run > /tmp/hd-t5-run-polarfire.log 2>&1
grep -c 'Processor [1-4] start\.' /tmp/hd-t5-run-polarfire.log   # 期待: 4
pgrep -a qemu
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 25 cmake --build build/musca_b1 --target run > /tmp/hd-t5-run-musca1.log 2>&1
grep -c 'Processor 1 start\.' /tmp/hd-t5-run-musca1.log          # 期待: 1
pgrep -a qemu
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 25 cmake --build build/musca_b1-2core --target run > /tmp/hd-t5-run-musca2.log 2>&1
grep -c 'Processor [12] start\.' /tmp/hd-t5-run-musca2.log       # 期待: 2
pgrep -a qemu
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 25 cmake --build build/kria_arm64-1core --target run > /tmp/hd-t5-run-arm64-1.log 2>&1
grep -c 'Processor 1 start\.' /tmp/hd-t5-run-arm64-1.log         # 期待: 1
pgrep -a qemu
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 25 cmake --build build/kria_arm64 --target run > /tmp/hd-t5-run-arm64-4.log 2>&1
grep -c 'Processor [1-4] start\.' /tmp/hd-t5-run-arm64-4.log     # 期待: 4
pgrep -a qemu
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 25 cmake --build build/kria_r5 --target run > /tmp/hd-t5-run-r5-1.log 2>&1
grep -c 'Processor 1 start\.' /tmp/hd-t5-run-r5-1.log            # 期待: 1
pgrep -a qemu
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 25 cmake --build build/kria_r5-2core --target run > /tmp/hd-t5-run-r5-2.log 2>&1
grep -c 'Processor [12] start\.' /tmp/hd-t5-run-r5-2.log         # 期待: 2
pgrep -a qemu
```
（`rp2350_pico2` は QEMU にマシンモデルが無く `run` ターゲット自体が無い＝設計どおり。
`kria_r5-2core` は既知どおり `rc=124` になるが、**両 Processor start 行が出ていれば合格**
＝rc 単独で判定しない（Constraint 8）。）
**各コマンドの後に `pgrep -a qemu` が何も出さないこと。** 出たら
`pkill -f qemu-system` で掃除し、その事実を記録する。
★直後に一瞬 `<defunct>` が見えることがある（reap-lag）。3 秒後に再確認して消えていれば
孤児化ではない。**そう判断した根拠を記録する。**

★**起動 7/7 は、本パスが `sta_ker()` のメモリプール初期化経路
（`kernel/startup.c:188` の `initialize_mempool`）を壊していないことの検査でもある。**
`alloc_mempool` への再構成は `initialize_mempool` を触っていないが、
`malloc_mempool` は起動直後から使われうる（`AID_*` 構成）。

- [ ] **Step 5: 機能テスト 7 本の再実行（QEMU 6 本 + build のみ 1 本）**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 120 cmake --build build/musca_b1-2core-tdcre5 --target run > /tmp/hd-t5-tdcre5.log 2>&1
grep 'TTSP_RESULT: PASS' /tmp/hd-t5-tdcre5.log
grep -c 'Check point' /tmp/hd-t5-tdcre5.log       # 期待: 13（Task 3 の実測値）
grep 'Check point 2-1 passed\|Check point 2-2 passed' /tmp/hd-t5-tdcre5.log
pgrep -a qemu
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 90 cmake --build build/musca_b1-2core-tdcre4 --target run > /tmp/hd-t5-tdcre4.log 2>&1
grep 'TTSP_RESULT: PASS' /tmp/hd-t5-tdcre4.log
grep -c 'Check point' /tmp/hd-t5-tdcre4.log       # 期待: 15（★増えていないこと）
pgrep -a qemu
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 90 cmake --build build/musca_b1-2core-tdcre3 --target run > /tmp/hd-t5-tdcre3.log 2>&1
grep 'TTSP_RESULT: PASS' /tmp/hd-t5-tdcre3.log
grep -c 'Check point' /tmp/hd-t5-tdcre3.log       # 期待: 14
pgrep -a qemu
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 60 cmake --build build/musca_b1-2core-tdcre2 --target run > /tmp/hd-t5-tdcre2.log 2>&1
grep 'TTSP_RESULT: PASS' /tmp/hd-t5-tdcre2.log ; pgrep -a qemu
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 60 cmake --build build/musca_b1-2core-tdcre1 --target run > /tmp/hd-t5-tdcre1.log 2>&1
grep 'TTSP_RESULT: PASS' /tmp/hd-t5-tdcre1.log
grep -c 'Check point' /tmp/hd-t5-tdcre1.log       # 期待: ISR段階 Task 7 と同じ値
pgrep -a qemu
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 60 cmake --build build/musca_b1-2core-tint2 --target run > /tmp/hd-t5-tint2.log 2>&1
grep 'TTSP_RESULT: PASS' /tmp/hd-t5-tint2.log ; pgrep -a qemu
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
cmake --build build/musca_b1-2core-tmix > /tmp/hd-t5-tmix-build.log 2>&1; echo "tmix build rc=$?"
cmake --build build/kria_arm64-tmix > /tmp/hd-t5-tmixa-build.log 2>&1; echo "tmix64 build rc=$?"
```
期待: QEMU 6 本とも `TTSP_RESULT: PASS` が実在。
`test_dcre_mix` は自身の `DIVERGENCE_MAP.md` 記載どおり **build + equivalence のみ**。
★**変わってよい `Check point` は `test_dcre5` の 12→13 だけ**である。
他が変わっていたら、本パスが意図せず挙動を変えている。止まって報告する。

- [ ] **Step 6: エラー経路回帰マトリクス（既存 37 件 + 本パス 1 件 = 38 件）**

★**`run.sh` は 4 引数形**。`#include "test_int2.h"` を含む cfg は**第 4 引数が必須**
（付けないと `rc=2`）。**本計画では全件に引数を明記する。**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
ls tools/cfg_error_tests/*.cfg | sort | tee /tmp/hd-t5-cfgs.txt
grep -c . /tmp/hd-t5-cfgs.txt      # 期待: 38（既存37 + 本パス1）
grep -n "dcre_dynisr_unmanaged_intpri" /tmp/hd-t5-cfgs.txt   # 期待: 1 行
```

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
R=tools/cfg_error_tests/run.sh
T=tools/cfg_error_tests
$R build/polarfire_soc_kit-qemu $T/e_par_creisr_intno_keyerror.cfg   E_PAR;         echo "01:$?"
$R build/polarfire_soc_kit-qemu $T/e_rsatr_inhno_affinity.cfg        E_RSATR;       echo "02:$?"
$R build/musca_b1-2core         $T/musca_b1_e_rsatr_intno_affinity.cfg E_RSATR;     echo "03:$?"
$R build/kria_r5-2core          $T/kria_r5_e_rsatr_intno_affinity.cfg  E_RSATR;     echo "04:$?"
$R build/rp2350_pico2           $T/rp2350_e_rsatr_intno_affinity.cfg   E_PAR;       echo "05:$?"
$R build/musca_b1-2core $T/musca_b1_clsid_warning.cfg CLS_ALL_PRC2 "-DOMIT_MULTIPRC_INTERRUPT"; echo "06:$?"
```
★05 は**期待 ercd が `E_PAR`**（ファイル名は `e_rsatr` だが `build/rp2350_pico2` は
既定 1 コア構成のため）。★06 は**期待文字列が ercd ではなく `CLS_ALL_PRC2`**。
いずれも段階3a Task 8 で実測確認済み。

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
$R $M $T/dcre_aid_dtq_in_class.cfg      E_RSATR "$X"; echo "22:$?"
$R $M $T/dcre_aid_pdq_in_class.cfg      E_RSATR "$X"; echo "23:$?"
$R $M $T/dcre_aid_mpf_in_class.cfg      E_RSATR "$X"; echo "24:$?"
$R $M $T/dcre_aid_dtq_no_static.cfg     E_OBJ   "$X"; echo "25:$?"
$R $M $T/dcre_aid_pdq_no_static.cfg     E_OBJ   "$X"; echo "26:$?"
$R $M $T/dcre_aid_mpf_no_static.cfg     E_OBJ   "$X"; echo "27:$?"
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
R=tools/cfg_error_tests/run.sh
T=tools/cfg_error_tests
X="-I$PWD/test"
M=build/musca_b1-2core
$R $M $T/dcre_aid_isr_in_class.cfg       E_RSATR "$X"; echo "28:$?"
$R $M $T/dcre_aid_isr_no_static.cfg      E_OBJ   "$X"; echo "29:$?"
$R $M $T/dcre_aid_isr_no_dynisr.cfg      E_OBJ   "$X"; echo "30:$?"
$R $M $T/dcre_dynisr_out_of_class.cfg    E_RSATR "$X"; echo "31:$?"
$R $M $T/dcre_dynisr_class_mismatch.cfg  E_RSATR "$X"; echo "32:$?"
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
R=tools/cfg_error_tests/run.sh
T=tools/cfg_error_tests
X="-I$PWD/test"
M=build/musca_b1-2core
$R $M $T/dcre_dynisr_no_cfgint.cfg          E_OBJ "$X"; echo "33:$?"
$R $M $T/dcre_dynisr_definh_conflict.cfg    E_OBJ "$X"; echo "34:$?"
$R $M $T/dcre_dynisr_no_static.cfg          E_OBJ "$X"; echo "35:$?"
$R $M $T/dcre_dynisr_bad_intno.cfg          E_PAR "$X"; echo "36:$?"
$R $M $T/dcre_dynisr_duplicated.cfg         E_OBJ "$X"; echo "37:$?"
$R $M $T/dcre_dynisr_unmanaged_intpri.cfg   E_OBJ "$X"; echo "38:$?"
```

期待: **38 件すべて 0**（両エンジンが同じ ercd／文言を同じように検出）。
`rc=2` は**前提未充足であり合格ではない**。
★`ls` の結果に**上の 38 件に無いファイル**が見つかった場合は、
「回帰列に入っていなかったファイル」として**実行して結果を記録する**
（対象 builddir・期待 ercd は cfg 冒頭のコメントから読む）。**列に無いから無視、はしない。**
★**`dcre_aid_alm_no_static.cfg` は依然として存在しない**（段階3a 最終レビュー Minor。
`AID_ALM` だけ no-static 版が欠けている）。本パスでも**作らない**（スコープ外）が、
**欠けている事実を `progress.md` に再掲する**。

- [ ] **Step 7: `KERNEL_FCSRCS` 突き合わせ**（AGENTS.md §4。22 個のまま不変のはず）

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
diff <(awk '/^KERNEL_FCSRCS/{f=1} f{print} f&&!/\\$/{exit}' kernel/Makefile.kernel \
       | grep -oE '[a-z_]+\.c' | sort -u) \
     <(grep -oE 'kernel/[a-z_]+\.c' CMakeLists.txt | sed 's#kernel/##' | sort -u)
echo "diff rc=$?"
```
期待: `rc=0`（差分なし）。本パスは既存 `.c` にしか手を入れていないので変化しないはず。

- [ ] **Step 8: `DIVERGENCE_MAP.md` の完全性監査と整合チェック**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
git diff main...HEAD --name-only > /tmp/hd-t5-changed.txt
wc -l /tmp/hd-t5-changed.txt
while read f; do
  case "$f" in
    docs/*|tools/*|cmake/*|CMakeLists.txt|CMakePresets.json|cfg_py/*|.superpowers/*|kernel/*.py) continue;;
  esac
  grep -q -- "$f" DIVERGENCE_MAP.md || echo "MISSING: $f"
done < /tmp/hd-t5-changed.txt
```
期待: `MISSING:` が**1 行も出ない**。

★あわせて**台帳の自己整合**を検査する（本パス特有。片方だけ直すと矛盾する）：

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
grep -n "符号混在比較\|オーバーフロー\|未 hardening\|課題④\|完全ドレイン\|候補 c\|候補c" DIVERGENCE_MAP.md
```
確認すること:
- `malloc_mempool` の符号混在比較が「既知・対処しない事項」に**残っていない**
  （＝「hardening パス Task 1 で修正済み」へ書き換わっている）。
- `dtqcnt`/`pdqcnt`/`blkcnt`/`blksz` の「未 hardening」記述が**解消済みへ**書き換わっている。
- 上流報告候補一覧の `c` が「本ブランチでは修正済み・上流へは未報告」になっている。
- ISR 段階の「走査中の完全ドレイン→enqueue の回帰カバレッジ無し」が**解消済み**になっている。
- 上流報告候補は **5 件のまま**である（a〜e。本パスで**増えない**）。
  ★候補 c だけが「観察」から「修正済み・要報告」へ状態が変わる。この状態変化を明記する。

- [ ] **Step 9: `.superpowers/sdd/progress.md`（フラット台帳）へ完了を記録し、コミット**

記録に含めること（**推測と事実を分ける**）：

- 【事実】**本パスは新機能を 1 つも足していない。** 追加 API・cfg 記法・ランタイム
  オブジェクトはゼロ。触ったのは入力検査・メモリプール算術・テスト資産の 3 層のみ。
- 【事実】**dcre からの意図的な逸脱 6 件（H-1〜H-6）**。段階1〜ISR の逸脱と違い、
  本パスの逸脱は**すべて「dcre がやっていないことをやる」型**である
  （転写ではなく上流欠陥の修正）。H-5（`malloc_mempool` の符号混在比較）は
  **上流報告候補 c の現物であり、本ブランチでは修正済み・上流へは未報告**。
- 【事実】**一体修正の根拠**：サイズ検査だけ／符号比較だけでは穴が残る
  （Task 1 Step 2 で実装者が再導出した 2 つの反例を、その要旨とともに記録する）。
- 【事実】**変異 control の結果**：C-1（`acre_dtq` 検査削除）・C-2（`acre_mpf` 丸め検査削除）・
  C-3（`acre_tsk` 検査削除）・Task 2（`pk_cmpf->mpf`→ローカル `mpf`）・
  Task 3（旧リセット分岐の再導入）の**5 本すべて予測どおり倒れたか**を 1 本ずつ書く。
  ★**C-4（`alloc_mempool` の `aligned > limit`）は現構成から到達不能で control が無い**
  ことを正直に書く（「落ちなかったから安全」と書かない）。
- 【事実】**Task 3 が閉じたギャップ**：ISR 段階 Task 6 レビューが特定した
  「走査中の完全ドレイン→enqueue」の回帰が `test_dcre5` 手順9 で塞がった。
  **旧リセット分岐を戻すと手順9 が倒れる**ことを実演した。
  これで ISR 段階の hardening 課題④が閉じる。
- 【事実】**`test_dcre5` の `Check point` は 12→13** に増えた（唯一の期待値変更）。
  `test_dcre4` は **15 のまま**、`test_dcre3` は **14 のまま**。
- 【事実】**エラー行列は 37→38**（`dcre_dynisr_unmanaged_intpri.cfg` を追加）。
  musca_b1 は `INTPRI_CFGINT_VALID`(-8..-1) が `TMIN_INTPRI`(-3) より広いため
  「`CFG_INT` としては合法だがカーネル管理外」という `intpri` を構成でき、
  これが `ENA_DYNISR` の `E_OBJ` へ到達する唯一の道である。
  ★**`dcre_aid_alm_no_static.cfg` は依然として存在しない**（段階3a Minor の再掲）。
- 【事実】**ROM 増分**（`build/musca_b1-2core`、`size` 実測）：text=<実測> /
  data=<実測> / bss=<実測> バイト。
- 【推測含む・引き継ぎ課題】
  (a) `alloc_mempool` の `aligned > limit` 分岐は**現構成の公開 API から到達不能**であり、
      runtime control を持たない。将来 `DEF_MPK` の領域配置が変わる／
      `aligned_alloc_mpk` に大きな `alignment` を渡す API が増えると到達しうる。
  (b) 64bit ターゲット（`kria_arm64`）では H-1〜H-3 の検査が**恒真**である。
      すなわち**あの検査群が実際に効くことを実測できているのは 32bit 構成だけ**である。
  (c) ISR 段階からの既存課題は本パスでは触っていない：長時間 ISR による `del_isr` 待ち／
      `call_isr` の O(n^2) 走査／`isrseq` の u32 ラップ（到達不能想定）／
      PLIC/GIC ターゲットでの 2 コア同時走査の実証。**引き継ぎのまま。**
  (d) 2 レンジ ID マクロの異配列間ポインタ比較が ISO-C 上は未定義（段階1 から継承・
      フラットメモリでは無害・`×9` になった事実）も**引き継ぎのまま**。
- 【回帰の実測値】builds 8/9（polarfire は既知の環境ギャップ）／equivalence 14/14／
  QEMU 7/7 孤児なし／機能テスト 6/6 PASS + mix build-only（計 7〜8 本）／
  エラー行列 38/38／FCSRCS 差分 0／台帳監査 MISSING=0／
  `test_dcre5`=13・`test_dcre4`=15・`test_dcre3`=14。

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
git status --short
git add -A && git commit -m "chore(dcre): hardening パスの最終回帰と台帳整理（あふれ検査・巻き戻し・完全ドレインの3課題を closed）"
```

---

## Self-Review 済み事項（計画作成時の検証記録）

**hardening 課題 → Task 対応:**

| 課題の出所 | 内容 | Task |
|---|---|---|
| 段階3b 最終レビュー / ISR 台帳 ①③ | サイズ乗算あふれ検査 + mempool 符号比較の**一体**修正 | **T1**（実装）・**T4 Step 4/5/7**（runtime 実演と変異 control） |
| 段階3b 最終レビュー Minor ② | `acre_mpf` 巻き戻し経路の runtime テスト | **T2** |
| ISR 段階 Task 6 レビュー ④ | `isrseq` 完全ドレイン→enqueue の回帰 | **T3** |
| ISR 段階 deferred | `intpri` エラー回帰 cfg（両エンジン対称） | **T4 Step 2/3** |
| 全段階共通 | 全構成回帰・台帳整合・フラット台帳への記録 | **T5** |

**現物確認済み（計画作成時に実ファイルで確認した事実）:**

- `kernel/startup.c:448-452` の `MEMPOOLCB { void *brk; void *limit; uint_t count; }`、
  `:454-468` `initialize_mempool`、`:470-474` `align_pointer`、
  `:476-491` `malloc_mempool`、`:493-508` `aligned_alloc_mempool`、
  `:510-519` `free_mempool`（`count == 0` で `brk` を先頭へ戻す）。
  **`:483` と `:500` が `((char *)(p_mempoolcb->limit)) - ((char *) brk) >= size`**。
- `kernel/dataqueue.c:346-` `acre_dtq`：`CHECK_VALIDATR(dtqatr, TA_TPRI)` →
  `MB_ALIGN` → ロック → `malloc_mpk(sizeof(DTQMB) * dtqcnt)`。
  ★`E_NOMEM` のとき free-list から CB を取り出していない（段階3b の不変量）。
- `kernel/pridataq.c:323-` `acre_pdq`：`CHECK_PAR(VALID_DPRI(maxdpri))` の後に
  `malloc_mpk(sizeof(PDQMB) * pdqcnt)`。
- `kernel/mempfix.c:205-301` `acre_mpf`：`CHECK_PAR(blkcnt != 0)`（`:227`）/
  `CHECK_PAR(blksz != 0)`（`:228`）→ ロック → ①`malloc_mpk(ROUND_MPF_T(blksz) * blkcnt)`（`:248`）
  → ②`malloc_mpk(sizeof(MPFMB) * blkcnt)`（`:264`）→ ②失敗時に
  `if (pk_cmpf->mpf == NULL) { free_mpk(mpf); }`（`:268-270`）。
- `kernel/task_manage.c:140-` `acre_tsk`：`CHECK_PAR(stksz >= TARGET_MIN_STKSZ)`（`:167`）→
  ロック → `stksz = ROUND_STK_T(stksz); stk = aligned_alloc_mpk(alignof(STK_T), stksz);`（`:182-183`）。
- `include/kernel.h:701-714`：`TOPPERS_COUNT_SZ`/`TOPPERS_ROUND_SZ`/`ROUND_STK_T`/
  `ROUND_MPF_T`/`ROUND_MB_T`。`:123-131` の `STK_T`=`intptr_t`・`MPF_T`=`intptr_t`。
- `include/t_stddef.h:94-95` の `int_t`/`uint_t` = `signed/unsigned int`、`:131` の `MB_T`=`uintptr_t`、
  `:65` の `#include "target_stddef.h"` → `target/musca_b1_gcc/target_stddef.h:17` の `<stdint.h>`
  （`SIZE_MAX`/`UINTPTR_MAX` の出所）→ `arch/gcc/tool_stddef.h:82` の `<limits.h>`（`UINT_MAX`）。
- `kernel/mempfix.h:67-69` の `MPFMB { uint_t next; }`（**4 バイト**）。
- `kernel/check.h:304` の `CHECK_PAR`、`:407-439` の `STKSZ_ALIGN`/`MPF_ALIGN`/`MB_ALIGN`。
- `kernel/interrupt.c:161-163` `ISR_KEY_GT`、`:197-214` `enqueue_isr`（**リセット分岐が無い**）、
  `:406-495` `call_isr`（継続キー `cur_isrpri`/`cur_isrseq`、`running` ビットマップ、3 分岐ロック復元）、
  `:607-` `del_isr`（`:641` `queue_delete` →`:668-669` `TA_NOEXS`→`:708-714` quiesce→`:717` free-list）。
- **撤去済みリセット分岐の原文**（`git show 8c34fad:kernel/interrupt.c`）：
  `if (queue_empty(&(p_isr_queue->isr_queue))) { p_isr_queue->isrseq = 0U; }` が
  `p_isrcb->isrseq = p_isr_queue->isrseq;` の直前にあった。**Task 3 の変異 control はこれを戻す。**
- `test/test_dcre5.cfg`：`CLS_PRC1` に `TASK1`(MID)+`CFG_INT(INTNO1)`+`CRE_ISR(ISR_S4/ISR_S2)`+
  `ENA_DYNISR(INTNO1)`、`CLS_PRC2` に `TASK3`(HIGH)+`CFG_INT(INTNO2)`+`ENA_DYNISR(INTNO2)`、
  クラス外に `AID_ISR(4)`。**`INTNO2` に静的 ISR が 1 本も無い**（Task 3 の前提）。
- `test/test_dcre5.c`：手順1〜8 と `check_point(1..9)`+`check_finish(10)`、
  PRC2 の `check_point_prc(1,2)`/`(2,2)`、`SPIN_LIMIT`=1e8、`isr_log_put`/`isr_log_is`、
  `task1_ready` による起動レース回避、`:371-383` の**カバレッジギャップの明記**。
- `test/test_dcre4.h`：`MPK_SIZE`=2048・`MPF_BLKCNT`=4・`MPF_BLKSZ`=64・`MPF_CYCLES`=16・
  `USER_DTQCNT`=2・`TEST_TIME_PROC`=200000。
- `test/test_dcre4.c`：手順8（mpf）の `:493-497` の既存 `E_NOMEM`（`blkcnt=MPK_SIZE`/`blksz=32`＝
  **①で失敗するので巻き戻し経路を通らない**）、`:500-504` の再 acre（`erid == mpfid1`）、
  `:525-531` の `MPF_CYCLES` ループ、`check_point(1..12)`+`check_finish(13)`＝**15 行**。
- `test/test_dcre1.c:132` の `T_CTSK ctsk;`、`:196-205` の
  `ctsk.stksz = MPK_SIZE * 4;` → `E_NOMEM` → `ctsk.stksz = STACK_SIZE;`（Task 4 の挿入点）。
- `kernel/interrupt.py:106-107`（`INTPRI_CFGINT_VALID` の既定）/ `:170-172`（`E_PAR`）/
  `:411-416`（**カーネル管理外 `intpri` × `ENA_DYNISR` → `E_OBJ`**）/ `:438`（静的 `CRE_ISR` が
  0 個のガードは**システム全体の本数**）/ `:448`（`AID_ISR` × `ENA_DYNISR` 0 個）。
  Ruby 側の対応行は `kernel/interrupt.trb:96-97,175-177,455,`。
- `target/musca_b1_gcc/target_kernel.py:55` / `target_kernel.trb:57` の
  `INTPRI_CFGINT_VALID = -(1<<TBITW_IPRI) .. -1`、`target_sil.h:18` の `TBITW_IPRI 3`、
  `target_kernel.h:66` の `TMIN_INTPRI (-3)`、`arch/arm_m_gcc/common/core_kernel.h:63` の
  `TMAX_INTPRI (-1)` → **-8..-1 ⊋ -3..-1** が Task 4 の cfg 回帰の成立根拠。
- `target/musca_b1_gcc/target_test.h:43-53` の `INTNO1`/`INTNO2`（`(prcid << 16) | (60+16)`）と
  **空マクロ**の `intno1_clear()`/`intno2_clear()`。
- `kernel/task_manage.c:150` の `act_tsk` が `CHECK_UNL_MYSTATE`（＝**非タスクコンテキスト可**）。
- `tools/cfg_error_tests/run.sh` の 4 引数形と `exit 2 = 前提未充足`、
  `dcre_dynisr_no_cfgint.cfg` の骨格（`INCLUDE("test/test_common1.cfg")` +
  `#include "test_int2.h"` + `CLASS(CLS_PRC1)` + `AID_ISR(1)`）。
- `test/MANIFEST:47-52` と `test/testexec.rb:93-94` に `dcre4`/`dcre5` が**既に登録済み**
  （本パスは新規テストファイルを作らないので**この 2 ファイルは触らない**）。

**★計画作成時に算術で確かめたこと（Task 2 のサイジング）:**
- musca_b1（32bit）で `sizeof(MEMPOOLCB) == 12` → 実効容量 `R = 2048 - 12 = 2036`。
- `sizeof(MPF_T) == 4` / `sizeof(MPFMB) == 4` → `blksz = 4` のとき
  ① `= 4 × blkcnt`、② `= 4 × blkcnt`。
- ケースA（`blkcnt = 400`）：① 1600 ≤ 2036 成功 → 残 436 → ② 1600 > 436 失敗 → **巻き戻し発火**。
- ケースB（`blkcnt = 200`）：①+② = 1600 ≤ 2036 → **巻き戻しが効いていれば成功、
  効いていなければ①(800) > 436 で `E_NOMEM`**。
- ケースA の成立条件は `1600 ≤ R < 3200`、ケースB は `R ≥ 1600`。R=2036 で**両立**、
  余裕 436 バイト。★`R` が変わった場合の引き直し式（`R/8 < blkcnt ≤ R/4` が A、
  `blkcnt ≤ R/8` が B）も計画本文に書いてある。

**未検証（実装者が最初に当たること）:**
- **`SIZE_MAX / sizeof(...)` の比較が 64bit ビルドで警告を出さないか**（T1 Step 8）。
  出たら**検査を消して逃げず**、恒真である事実とともに記録する。
- **`INTNO1_INTATR`（`TA_ENAINT`）とカーネル管理外 `intpri` が両立するか**（T4 Step 2）。
  両立しなければ `TA_NULL` に落とし、落とした事実を記録する。
- **`dcre_dynisr_unmanaged_intpri.cfg` で `E_OBJ` が何回出るか**（T4 Step 3）。
  `AID_ISR` 側のガードも同時に発火して 2 回出る可能性がある（`dcre_dynisr_no_cfgint.cfg` と同型）。
- **`DRAIN_DELAY = 10000U`（10ms）が Task 3 の梃子として適切か**（T3 Step 8）。
  長すぎれば `dr_a_timeout`、短すぎれば**変異 control が倒れなくなる**（＝空虚化）。
  ★**倒れないときは値をいじる前に紙に書く。**
- **Task 3 の `Check point` が本当に 13 か**（T3 Step 8）。段階3a では見積りを 3 回外している。
  **実測を正とし、計画とテストのコメントを直して記録する。**
- **Task 2 の `sizeof(MEMPOOLCB)` が本当に 12 か**（T2 Step 1）。違えば `R` が変わり、
  ケースA/B の `blkcnt` を引き直す必要がある。
- **`test_dcre1.c` の acre 呼出しの書き方**（`erid = acre_tsk(&ctsk);` か
  `check_assert(acre_tsk(&ctsk) == …)` か）（T4 Step 5）。現物に合わせる。
- **`tools/cfg_error_tests/*.cfg` の実在個数が着手時点で 37 か**（T4 Step 1 / T5 Step 6）。
  実測を正とする。
- **C-4（`alloc_mempool` の `aligned > limit`）に到達する入力が公開 API から作れるか**
  （T4 Step 7）。**作れないと予想している。** 作れなければ「control 無し」と正直に記録する。
- **本パスの ROM 増分**（T5 Step 2）。数十〜200 バイトを見込むが実測を記録する。

---
