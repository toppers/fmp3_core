# FMP3 動的生成API 段階3a（acre_sem/del_sem・acre_flg/del_flg・acre_mtx/del_mtx）実装計画

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `feature/dynamic-creation` ブランチ上で `acre_sem`/`del_sem`・`acre_flg`/`del_flg`・
`acre_mtx`/`del_mtx` と `AID_SEM`/`AID_FLG`/`AID_MTX` を、cfg 両エンジン（Ruby オラクル +
Python 製品）同時対応・QEMU 回帰テスト付きで動かす。あわせて段階2最終レビューが
段階3a へ持ち越した hardening 4件を実装する。

**Architecture:** ASP3 dcre の機構（cfg 予約スロットの free-list + RAM `aseminib_table[]` 等）を
忠実移植し、FMP3 固有の3点（ジャイアントロック・named-static CB + const ポインタ表・
MP 対応済みの `init_wait_queue`/`remove_mutex`/`mutex_drop_priority`）を局所適応する。
段階1が一般化し段階2が cyc/alm で実証した cfg 共通枠組み（`@aidapi`/`inibList`/
`inibSizeToken`/予約 CB/訂正E ガード）は**そのまま再利用**でき、sem/flg/mtx 側の
per-object テンプレート（`semaphore.py`/`.trb` 等）の変更は**ゼロ**である。

★段階2との最大の相違は **sem/flg/mtx が非親和オブジェクトである**こと。
INIB に `iprcid`/`affinity` が無く、CB に `p_pcb` が無い。段階2 の Constraint 4
（PRC1 固定・`TOPPERS_TEPP_PRC` 充填）に相当するコードは**書いてはならない**。
free-list のリンクも tmevtb オーバーレイではなく **CB 先頭の `wait_queue` を直接流用**する。

**Tech Stack:** C（カーネル）、Python/Ruby（cfg テンプレート）、CMake、QEMU（musca_b1・kria_r5 ほか）。

---

## Global Constraints（spec から転記。実装中に再解釈しない）

1. ブランチ `feature/dynamic-creation`（段階2 の続き、HEAD = `072075b`）。**main へはマージしない。**
   pristine への改変は `DIVERGENCE_MAP.md` に記録する（種別 `mod (dcre-port)`、上流報告欄 `-`）。
2. 段階3a = sem/flg/mtx の `acre_*`/`del_*` + `AID_SEM`/`AID_FLG`/`AID_MTX` +
   段階2 hardening 4件のみ。**dtq/pdq/mpf（段階3b）・ISR（別計画）・段階4的な何かを含めない。**
3. API 面は dcre 標準のみ：`T_CSEM`/`T_CFLG`/`T_CMTX` は dcre `include/kernel.h:223-291` と
   同一定義。独自 API なし。`AID_*` はクラス外専用（クラス内は E_RSATR）。
4. ★★**プロセッサ親和なし。** sem/flg/mtx の INIB には `iprcid`/`affinity` が**存在しない**
   （現物確認済み: `kernel/semaphore.h:61-65`・`eventflag.h:61-64`・`mutex.h:59-62`）。
   CB にも `p_pcb` は無い（`semaphore.h:74-78`・`eventflag.h:73-77`・`mutex.h:73-78`）。
   **段階2 Constraint 4 の類推で `p_*inib->iprcid = TOPPERS_MASTER_PRCID;` や
   `->affinity = TOPPERS_TEPP_PRC;` や `p_*cb->p_pcb = ...;` を書いたら、それは
   コンパイルエラーになるか、なったとしてもバグである。書かないこと。**
   `initialize_semaphore`/`initialize_eventflag`/`initialize_mutex` は
   **現行が既にマスタプロセッサのみのループ**（`semaphore.c:126-133`・`eventflag.c:134-141`・
   `mutex.c:132-142`）であり、**プロセッサ別フィルタは存在しないし追加もしない**。
   変えるのは**静的ループの境界（`tnum_sem`→`tnum_ssem` 等）と、その直後に足す
   動的スロット節だけ**である。
5. 検証 = F-1：Ruby `.trb`（オラクル）にも同時移植し `tools/cfg_equivalence.sh`
   （exit 0=一致 / 1=不一致 / **2=前提未充足であり合格ではない**）を主検査に維持。
6. CB はヒープ確保しない。予約 CB（named static + ポインタ表末尾）+ RAM inib 配列。
   free-list のリンクには **CB 先頭の `wait_queue`（既存 QUEUE フィールド）を直接流用**する
   （dcre `semaphore.c:145-161` と同一）。**段階2 の tmevtb オーバーレイ技法は使わない**
   ＝したがって段階2 訂正D（64bit で `callback` が上書きされる問題）は**構造的に発生しない**。
   `acre_*` 側で何かを「再設定」する必要はない（`queue_initialize` は dcre どおり行う）。
7. **free-list は FIFO**（`del_*` = `queue_insert_prev` で末尾へ / `acre_*` =
   `queue_delete_next` で先頭から）。これは段階1で**裁定済み**の設計であり、
   実装者・レビュアーとも再議しない。テストは FIFO/LIFO 不問で決定的になる形
   （「空きが1個だけの状態で del → 再 acre」）に組む。
8. 汎用層 `CMakeLists.txt`・`cmake/fmp3_core.cmake`・`cfg_py/pass1.py`・`cfg_py/pass2.py` は
   **変更しない**。`KERNEL_FCSRCS`（`kernel/Makefile.kernel:51-56` の22個）も**不変**
   （`acre_*`/`del_*` はいずれも既存 `.c` に入る。新規 `.c` を作らない）。
9. `rc=124` 単独を成功判定に使わない（期待出力の実在を `grep` で確認する）。
10. **`cmd | tail` / `cmd | grep` で成否判定しない。** パイプラインの `$?` は最後の要素のもの。
    ファイルへリダイレクトしてから `grep` するか、`${PIPESTATUS[0]}` を見る。
11. `tools/cfg_equivalence.sh` の **exit 2 は合格ではない**（前提未充足）。exit 0 のみ合格。
12. QEMU 実行は**プリセットごとに個別コマンド**で行い、ログを別ファイルに落とす。
    `for` ループで全構成を1コマンドに詰めると Bash ツールの 2 分タイムアウトに当たり、
    **qemu が孤児化する**（段階1 Task 7 の実害）。各実行後に `pgrep -a qemu` で残存 0 を確認する。
13. `tools/cfg_error_tests/run.sh` の呼出しは
    `run.sh <builddir> <cfg> <期待ercd> [EXTRA_CFLAGS]` の**4引数形**である。
    cfg 内で `#include "test_int2.h"` 等を使うケースは**第4引数
    `EXTRA_CFLAGS="-I<repo>/test"` が必須**で、付けないと `rc=2` になる
    （段階2 Task 8 で実装者が自力訂正した実害。本計画では**全ケースに引数を明記する**）。

---

## ★spec からの訂正7件（Task 1 で spec に反映してから実装に入ること）

計画作成時に現物確認した結果、spec の記述と実装対象の現物が食い違う点、
および spec が「実装前確認で決める」としていて計画作成時に**決着がついた**点が7件ある。
いずれも **Task 1 で spec 本文を直してから** 後続タスクに入る。

**訂正A：機能コードは6件とも既存。値は spec の見込みどおりだが番号は連続していない。**
`include/kernel_fncode.h` に
`TFN_ACRE_SEM (-194)`（`:135`）/ `TFN_ACRE_FLG (-195)`（`:136`）/ `TFN_ACRE_MTX (-199)`（`:139`）/
`TFN_DEL_SEM (-210)`（`:147`）/ `TFN_DEL_FLG (-211)`（`:148`）/ `TFN_DEL_MTX (-215)`（`:151`）が
**6件とも既存**。→ `include/kernel_fncode.h` は**変更しない**。spec §1.2 の
「既存の見込み — 実装前確認（§8-1）」を実測値で確定させる。

**訂正B：`SEMID`/`FLGID`/`MTXID` は既に存在し、既に inib ポインタ式である。**
spec §4 は「新規定義または置換」と書くが、現物は
`semaphore.h:98-99` / `eventflag.h:97-98` / `mutex.h:98-99` に
`#define SEMID(p_semcb) ((ID)(((p_semcb)->p_seminib - seminib_table) + TMIN_SEMID))`
の形で**既に存在**する（段階2 の CYCID が**存在しなかった**のと対照的）。
→ **判断: 新規定義ではなく「既存マクロの2レンジ化置換」である。** 段階2 の CYCID と
同じ形（`a*inib_table` レンジ判定 → 動的 ID / それ以外 → 既存式）に**書き換える**。
AID 無し構成では `tnum_sem == tnum_ssem` となり判定式の第1項が常に偽になるため
**既存式に落ちる＝挙動不変**。この「置換であって新設ではない」ことを spec §4 に明記する。

**訂正C：`del_*` の呼出しコンテキスト検査は `CHECK_TSKCTX_UNL_MYSTATE(&p_selftsk)` を使う。**
dcre の `del_sem`/`del_flg`/`del_mtx` は `CHECK_TSKCTX_UNL()` を使い、ディスパッチ判断を
`if (p_runtsk != p_schedtsk)` で行う。FMP3 には大域 `p_runtsk` が無く、
**同じ状況を扱う FMP3 の現物（`ini_flg`＝`eventflag.c:435,444`、`ini_mtx`＝`mutex.c:598,615`）は
`CHECK_TSKCTX_UNL_MYSTATE(&p_selftsk)` + `if (p_selftsk != p_my_pcb->p_schedtsk)`** で書かれている
（`ini_sem`＝`semaphore.c:340,350` だけは `CHECK_TSKCTX_UNL()` + `p_my_pcb->p_runtsk` を使う異形）。
`check_tskctx_unl_mystate`（`kernel/check.h:177-199`）は `check_tskctx_unl` と
**同一の E_CTX 判定に `*pp_selftsk = p_my_pcb->p_runtsk` の取得を足しただけ**であり、
検査は厳密に等価（現物確認済み）。
→ **3つの `del_*` すべてで `CHECK_TSKCTX_UNL_MYSTATE(&p_selftsk)` を使う**（多数派の流儀に揃える）。
dcre からの意図的な逸脱として台帳に記録する。

**訂正D：dcre `del_flg` の ID 検査は `CHECK_PAR`（E_PAR）＝dcre 側の不整合。FMP3 は `CHECK_ID` に揃える。**
dcre `del_sem`（`semaphore.c:230`）と `del_mtx`（`mutex.c:440`）は `CHECK_ID(VALID_*ID(...))`（E_ID）だが、
`del_flg`（`eventflag.c:257`）だけ `CHECK_PAR(VALID_FLGID(flgid))`（**E_PAR**）になっている。
FMP3 の既存 flg 系サービスコールは**全て `CHECK_ID(VALID_FLGID(flgid))`**（`eventflag.c:243,276,331,374,436,475`）。
→ **FMP3 の `del_flg` は `CHECK_ID` を使う**（dcre からの意図的な逸脱・台帳記録）。
これを dcre 側の潜在バグとして**上流報告候補に追加するかは Task 8 で判断**する
（現行 dcre では不正 ID に E_PAR が返り、TOPPERS 標準の E_ID と食い違う）。

**訂正E：`acre_sem` の `0 <= isemcnt` は uint_t に対して恒真。落とす。**
dcre `semaphore.c:189` は `CHECK_PAR(0 <= isemcnt && isemcnt <= maxsem);` と書くが
`isemcnt` は `uint_t` なので `0 <= isemcnt` は恒真。段階2 で `acre_cyc` の
`0 <= cycphs` を同じ理由で落とした（hardening #4 の対象）のと**完全に同型**である。
→ `CHECK_PAR(isemcnt <= maxsem);` と書く。意味は同一。
**`1 <= maxsem` は恒真ではない**（unsigned で `maxsem != 0` と等価）ので残す。
**`maxsem <= TMAX_MAXSEM` も残す**：`TMAX_MAXSEM` は `kernel_sym.def:39` 経由の
ターゲット可変シンボルであり、現行 `include/kernel.h:659` が `UINT_MAX` なだけで
構造的な恒真ではない。
hardening #4 の DIVERGENCE_MAP 追記（Task 2）と**同じ行にこの2件目もまとめて書く**。

**訂正F：`del_mtx` は `p_mtxcb->p_loctsk = NULL;` を入れる（dcre には無い）。**
dcre `del_mtx`（`mutex.c:450-471`）は `p_loctsk` をローカルに退避して
`remove_mutex`/`mutex_drop_priority` を呼ぶが、`p_mtxcb->p_loctsk` 自身は**クリアしない**
（free-list 復帰後 `acre_mtx` が `NULL` を入れ直すので実害は無い）。
FMP3 の同状況の現物 `ini_mtx`（`mutex.c:606-614`）は `p_mtxcb->p_loctsk = NULL;` を**入れている**。
spec §2 は「呼び出し順・引数は現物の unl_mtx / ini_mtx の流儀に合わせる」と指示している。
→ **`ini_mtx` に倣って入れる**（`remove_mutex` は退避済みローカル `p_loctsk` を使うので順序上安全）。
dcre からの意図的な逸脱として台帳に記録する。

**訂正G：hardening #3（混在 AID の equivalence サンプル cfg）は2段階で入れる。**
spec §5-3 は「sem/flg/mtx の混在も1ケースに含める」とするが、`AID_SEM`/`AID_FLG`/`AID_MTX` は
**Task 3 まで `kernel_api.def` に存在しない**（cfg エラーになる）。
→ **Task 2 で CYC/ALM のみの混在サンプル `test/test_dcre_mix.{c,cfg,h}` を新設**し、
**Task 3 で同じ cfg に `AID_SEM(2)` / `AID_MTX(1)` を追記**して sem/flg/mtx の混在も覆う
（`AID_FLG` は**意図的に書かない**＝これが「混在」の実体）。
なお **cfg 単独のバリアントは作れない**：`CMakeLists.txt:234,683` が
`${FMP3_APPLDIR}/${FMP3_APPLNAME}.cfg` と `.c` を**同じ `FMP3_APPLNAME` から導く**ため、
`.cfg` だけ差し替えることができない（現物確認済み）。よって最小の `.c`/`.h` も作る。

**訂正H：hardening #1（TEPP_PRC マスタ bit の cfg enforcement）に対する
「エラー回帰 cfg」は現行5ターゲットでは構成不能。mutation control で代替する。**
段階2 Task 1 Step 5 の実測により、**全8プリセットで `TOPPERS_TEPP_PRC` の bit0（PRC1）が立つ**。
`TOPPERS_TEPP_PRC` は `target/*/target_kernel.h` 由来で `.cfg` からは変更できない。
→ **エラーを踏ませる cfg は書けない。** 代わりに Task 2 で
**両エンジンの条件式を一時的に反転させ、musca_b1-2core で実際に cfg エラーが出て
文言が一致することを実演し、復元して消えることまで確認する**（positive/negative control）。
「永続的なエラー回帰 cfg は追加しない・その理由」を spec と progress.md に明記する。
**したがって Task 8 のエラー行列は「既存12件 + 段階3a 新規6件 = 18件」である**
（brief 段階の見込み19件は本訂正で18件に確定）。

---

## 変更ファイル一覧（全体像）

| 層 | ファイル | 種別 |
|---|---|---|
| spec | `docs/superpowers/specs/2026-08-04-fmp3-dcre-stage3a-sem-flg-mtx-design.md` | 派生・修正 |
| cfg 共通枠組み | `kernel/kernel.py` | 派生 |
| cfg 共通枠組み | `kernel/kernel.trb` | **pristine・台帳** |
| cfg per-object | `kernel/cyclic.py` `kernel/alarm.py` | 派生（hardening #1 のフックのみ） |
| cfg per-object | `kernel/cyclic.trb` `kernel/alarm.trb` | **pristine・台帳**（同上） |
| cfg 定義 | `kernel/kernel_api.def` | **pristine・台帳** |
| API | `include/kernel.h`（T_CSEM/T_CFLG/T_CMTX + 6宣言） | **pristine・台帳** |
| カーネル sem | `kernel/semaphore.h` `kernel/semaphore.c` | **pristine・台帳** |
| カーネル flg | `kernel/eventflag.h` `kernel/eventflag.c` | **pristine・台帳** |
| カーネル mtx | `kernel/mutex.h` `kernel/mutex.c` | **pristine・台帳** |
| 配線 | `kernel/allfunc.h` `kernel/Makefile.kernel` | **pristine・台帳** |
| rename | `kernel/kernel_rename.def` →（再生成）`kernel_rename.h` `kernel_unrename.h` | **pristine・台帳** |
| コメント追記 | `kernel/cyclic.c` `kernel/alarm.c`（hardening #2 の1行コメント） | **pristine・台帳** |
| テスト | `test/test_dcre3.{c,cfg,h}` `test/test_dcre_mix.{c,cfg,h}` `test/MANIFEST` `test/testexec.rb` | **pristine・台帳** |
| エラー回帰 | `tools/cfg_error_tests/dcre_aid_{sem,flg,mtx}_in_class.cfg`（3） `dcre_aid_{sem,flg,mtx}_no_static.cfg`（3） | 派生 |
| 台帳 | `DIVERGENCE_MAP.md` | 派生 |
| 記録 | `.superpowers/sdd/progress.md` | 派生 |

**READ ONLY（読むが変更しない）:** `kernel/wait.c` `kernel/wait.h`
（`init_wait_queue` は**再利用するだけ**。MP 対応済み＝`wait.c:215-228`、`wait.h:249`）。
`include/kernel_fncode.h`（訂正A により変更不要）。
`kernel/semaphore.py` `kernel/semaphore.trb` `kernel/eventflag.py` `kernel/eventflag.trb`
`kernel/mutex.py` `kernel/mutex.trb`（**per-object テンプレートの変更はゼロ** — spec §3）。

---

### Task 1: 実装前確認（spec §8 の7項目）と spec の訂正7件反映

**推奨モデル:** 最安価（現物確認と機械的な文書修正。判断は本計画が既に与えている）

**Files:**
- Modify: `docs/superpowers/specs/2026-08-04-fmp3-dcre-stage3a-sem-flg-mtx-design.md`
  （§1.2・§2・§3・§4・§5・§7・末尾に §10 を新設）
- Create: なし

**Interfaces（後続 Task が参照する記録）:**
- Produces: 本 Task の**確認結果表**（spec 末尾 `## 10. 実装前確認の結果` として追記）。
  後続 Task は「Task 1 の記録」としてこれを参照する。特に
  (a) `TFN_*` 6件の既存値、(b) `SEMID`/`FLGID`/`MTXID` の現行実装（＝置換対象）、
  (c) `ini_sem`/`ini_flg`/`ini_mtx` の `init_wait_queue` 呼出し流儀とディスパッチ判断、
  (d) `remove_mutex`/`mutex_drop_priority` のシグネチャと `ini_mtx` での呼出し順、
  (e) `TMAX_MAXSEM`/`VALID_TPRI`/`INT_PRIORITY` の所在、
  (f) dcre 転写元の行範囲6組、(g) 訂正E ガードが sem/flg/mtx で発火すること。

**★ゲート条件（BLOCKED 判定）:** 下記のいずれかが成り立ったら、**以降のステップに進まず
発見内容を報告して停止する**（設計が無効になるため）。
- sem/flg/mtx の INIB または CB に `iprcid`/`affinity`/`p_pcb` が**存在した**
  （Global Constraint 4 の前提が崩れる）。
- `initialize_semaphore`/`initialize_eventflag`/`initialize_mutex` が
  **マスタプロセッサ限定でなかった**（動的スロット初期化の置き場所が変わる）。
- CB の**先頭フィールドが `wait_queue` でなかった**
  （`(SEMCB *) queue_delete_next(&free_semcb)` の型 punning が成立しない）。

- [ ] **Step 1: §8-1 機能コードの既存有無**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
grep -n "TFN_ACRE_SEM\|TFN_DEL_SEM\|TFN_ACRE_FLG\|TFN_DEL_FLG\|TFN_ACRE_MTX\|TFN_DEL_MTX" \
     include/kernel_fncode.h > /tmp/dcre3-t1-fncode.txt
cat /tmp/dcre3-t1-fncode.txt
grep -c . /tmp/dcre3-t1-fncode.txt      # 期待: 6
```
期待（計画作成時の実測）:
`TFN_ACRE_SEM (-194)`:135 / `TFN_ACRE_FLG (-195)`:136 / `TFN_ACRE_MTX (-199)`:139 /
`TFN_DEL_SEM (-210)`:147 / `TFN_DEL_FLG (-211)`:148 / `TFN_DEL_MTX (-215)`:151。
→ **訂正A**：`include/kernel_fncode.h` は**変更不要**。6値を確認結果表に転記する。

- [ ] **Step 2: §8-2 `SEMID`/`FLGID`/`MTXID` の現行実装（2レンジ化の形を確定）**

```bash
grep -n "define\s*SEMID\|define\s*FLGID\|define\s*MTXID" -A2 \
     kernel/semaphore.h kernel/eventflag.h kernel/mutex.h
grep -n "p_semcb_table\|p_flgcb_table\|p_mtxcb_table" kernel/semaphore.h kernel/eventflag.h kernel/mutex.h
grep -n "define tnum_sem\|define tnum_flg\|define tnum_mtx" kernel/semaphore.c kernel/eventflag.c kernel/mutex.c
```
期待（実測）:
- 3マクロとも**既に存在**し、既に **inib ポインタ差分式**（`semaphore.h:98-99` /
  `eventflag.h:97-98` / `mutex.h:98-99`）。CB は `SEMCB *const p_semcb_table[]`
  （`semaphore.h:93`）＝**ポインタ表**（dcre の実体配列 `semcb_table[]` とは別物）。
- `tnum_sem`/`tnum_flg`/`tnum_mtx` は `.c` 側（`semaphore.c:107` / `eventflag.c:115` /
  `mutex.c:106`）にあり、`.h` には無い。
→ **判断: 訂正B のとおり「既存マクロの2レンジ化置換」。** 段階2 の `CYCID`
（`kernel/cyclic.h:119-129`）と同型にする。`tnum_*` は `.h` へ移設が必要
（マクロから参照するため）。この判断と根拠を確認結果表に明記する。

- [ ] **Step 3: §8-3 `ini_sem`/`ini_flg`/`ini_mtx` の現物（`del_*` はこれを正として組む）**

```bash
sed -n '330,366p' kernel/semaphore.c      # ini_sem
sed -n '424,460p' kernel/eventflag.c      # ini_flg
sed -n '586,631p' kernel/mutex.c          # ini_mtx
sed -n '536,581p' kernel/mutex.c          # unl_mtx（優先度復帰の流儀）
grep -n "init_wait_queue" kernel/wait.h kernel/wait.c
sed -n '211,228p' kernel/wait.c           # init_wait_queue 本体（E_DLT）
grep -n "remove_mutex\|mutex_drop_priority\|mutex_calc_priority" kernel/mutex.c
```
期待（実測）:
- `init_wait_queue(PCB *p_my_pcb, QUEUE *p_wait_queue)`（`wait.h:249`、本体 `wait.c:215-228`）は
  **MP 対応済み**で、待ちタスクを `wait_dequeue_tmevtb` → `winfo.wercd = E_DLT` →
  `make_non_wait(p_my_pcb, p_tcb)` で解除する。**新規の解除機構は書かない。**
- `ini_sem`: `CHECK_TSKCTX_UNL()` + `if (p_my_pcb->p_runtsk != p_my_pcb->p_schedtsk)`。
- `ini_flg`: `CHECK_TSKCTX_UNL_MYSTATE(&p_selftsk)` + `if (p_selftsk != p_my_pcb->p_schedtsk)`。
- `ini_mtx`: `CHECK_TSKCTX_UNL_MYSTATE(&p_selftsk)`、`init_wait_queue(p_my_pcb, &(p_mtxcb->wait_queue));`
  の**後**に `p_loctsk = p_mtxcb->p_loctsk;` → `p_mtxcb->p_loctsk = NULL;` →
  `(void) remove_mutex(p_loctsk, p_mtxcb);` → `if (MTX_CEILING(p_mtxcb))
  { mutex_drop_priority(p_my_pcb, p_loctsk, p_mtxcb->p_mtxinib->ceilpri); }` →
  `if (p_selftsk != p_my_pcb->p_schedtsk) { release_glock(); dispatch(); ... }`。
- `remove_mutex(TCB *p_tcb, MTXCB *p_mtxcb)`（`mutex.c:223-237`、Inline・dcre と同一シグネチャ）。
- `mutex_drop_priority(PCB *p_my_pcb, TCB *p_tcb, uint_t oldpri)`（`mutex.c:271-284`、
  **MP 版は `p_my_pcb` が先頭に付く**。dcre は `mutex_drop_priority(TCB *, uint_t)`）。
→ **訂正C**（3つの `del_*` とも `CHECK_TSKCTX_UNL_MYSTATE`）と
**訂正F**（`del_mtx` に `p_loctsk = NULL` を入れる）の根拠を確認結果表に記録する。
★あわせて **`MTX_CEILING(p_mtxcb)` は `p_mtxcb->p_mtxinib->mtxatr` を読む**ので、
`del_mtx` では **`mtxatr = TA_NOEXS` を書く前に優先度復帰を済ませる**必要があることを明記する
（順序を誤ると ceiling mutex の優先度が復帰しない。Task 6 の実装順の根拠）。

- [ ] **Step 4: §8-4 定数・マクロの存在**

```bash
grep -n "define TMAX_MAXSEM" include/kernel.h
grep -n "TMAX_MAXSEM" kernel/kernel_sym.def
grep -n "define VALID_TPRI" kernel/check.h
grep -n "define INT_PRIORITY" kernel/task.h
grep -n "define TA_CEILING\|define TA_TPRI\|define TA_WMUL\|define TA_CLR" include/kernel.h
grep -n "define TA_NOEXS" kernel/kernel_impl.h
grep -n "define CHECK_VALIDATR\|define CHECK_ID\|define CHECK_PAR\|define CHECK_TSKCTX_UNL_MYSTATE" kernel/check.h
sed -n '44,52p' kernel/mutex.c            # task.h が include されているか
```
期待（実測）: `TMAX_MAXSEM`＝`include/kernel.h:659`（`UINT_MAX`）かつ `kernel_sym.def:39`、
`VALID_TPRI`＝`check.h:70`、`INT_PRIORITY`＝`task.h:70`（`mutex.c:47` が `task.h` を include 済み）、
`TA_TPRI`=0x01（`:506`）/`TA_WMUL`=0x02（`:508`）/`TA_CLR`=0x04（`:509`）/`TA_CEILING`=0x03（`:511`）、
`TA_NOEXS`＝`kernel_impl.h:199`、`CHECK_VALIDATR`＝`check.h:282`（段階1 で追加済み）、
`CHECK_TSKCTX_UNL_MYSTATE`＝`check.h:195`。**追加の移植は不要**であることを記録する。

- [ ] **Step 5: §8-5 dcre 転写元の行範囲を自分で確定する（DIFF ではなく現行ソース）**

```bash
D=/home/honda/TOPPERS/ASP3/asp3_3.7/extension/dcre/kernel
grep -n "^#ifdef TOPPERS_acre_sem\|^#endif /\* TOPPERS_acre_sem\|^#ifdef TOPPERS_del_sem\|^#endif /\* TOPPERS_del_sem\|^#ifdef TOPPERS_semini\|^#endif /\* TOPPERS_semini" $D/semaphore.c
grep -n "^#ifdef TOPPERS_acre_flg\|^#endif /\* TOPPERS_acre_flg\|^#ifdef TOPPERS_del_flg\|^#endif /\* TOPPERS_del_flg\|^#ifdef TOPPERS_flgini\|^#endif /\* TOPPERS_flgini" $D/eventflag.c
grep -n "^#ifdef TOPPERS_acre_mtx\|^#endif /\* TOPPERS_acre_mtx\|^#ifdef TOPPERS_del_mtx\|^#endif /\* TOPPERS_del_mtx\|^#ifdef TOPPERS_mtxini\|^#endif /\* TOPPERS_mtxini" $D/mutex.c
grep -n "typedef struct t_csem\|typedef struct t_cflg\|typedef struct t_cmtx" -A6 \
     /home/honda/TOPPERS/ASP3/asp3_3.7/extension/dcre/include/kernel.h
```
期待（計画作成時の実測。**食い違ったら現行ソースを正とし、確認結果表に実測値を書く**）:

| 転写元 | ファイル | 区画 | 行範囲 |
|---|---|---|---|
| `initialize_semaphore` + `free_semcb` | dcre `semaphore.c` | `TOPPERS_semini` | 132-165 |
| `acre_sem` | dcre `semaphore.c` | `TOPPERS_acre_sem` | 170-214 |
| `del_sem` | dcre `semaphore.c` | `TOPPERS_del_sem` | 219-257 |
| `initialize_eventflag` + `free_flgcb` | dcre `eventflag.c` | `TOPPERS_flgini` | 140-173 |
| `acre_flg` | dcre `eventflag.c` | `TOPPERS_acre_flg` | 203-241 |
| `del_flg` | dcre `eventflag.c` | `TOPPERS_del_flg` | 246-284 |
| `initialize_mutex` + `free_mtxcb` | dcre `mutex.c` | `TOPPERS_mtxini` | 138-174 |
| `acre_mtx` | dcre `mutex.c` | `TOPPERS_acre_mtx` | 378-423 |
| `del_mtx` | dcre `mutex.c` | `TOPPERS_del_mtx` | 428-479 |
| `T_CSEM`/`T_CFLG`/`T_CMTX` | dcre `include/kernel.h` | — | 223-291 |

- [ ] **Step 6: §8-6 訂正E ガード（AID>0 かつ静的0個）が sem/flg/mtx でも発火すること**

```bash
sed -n '125,155p' kernel/kernel.py         # has_aid / numAutoObjid / 訂正E ガード
sed -n '130,160p' kernel/kernel.trb
grep -n "class SemaphoreObject\|class EventflagObject\|class MutexObject" -A3 \
     kernel/semaphore.py kernel/eventflag.py kernel/mutex.py
```
期待: ガードは `KernelObject.generate()` の中で `self.aidapi` / `self.api` のみを見ており、
**オブジェクト種別に依存しない**。sem/flg/mtx の各クラスは
`super().__init__("sem", "semaphore")` のように**共通枠組みをそのまま継承**しており
（`inibList` のカスタマイズすら無い）、`AID_SEM` 等を `kernel_api.def` に足せば
**自動的に有効化される**。→ **per-object テンプレートの変更はゼロ**（spec §3）であることを確定させる。
オブジェクト種依存の穴が無いこと（`self.obj_s` から `aseminib_table`/`_kernel_asemcb_<i>` が
機械的に導かれること＝`kernel.py:122,225,233`）も確認結果表に書く。

- [ ] **Step 7: §8-7 `wait_queue` を free-list リンクに使うことと TA_TPRI の非干渉**

```bash
sed -n '61,99p' kernel/semaphore.h        # SEMINIB / SEMCB / SEMID
sed -n '61,98p' kernel/eventflag.h        # FLGINIB / FLGCB / FLGID
sed -n '59,99p' kernel/mutex.h            # MTXINIB / MTXCB / MTXID
grep -n "include <queue.h>" kernel/semaphore.h kernel/eventflag.h kernel/mutex.h
grep -n "queue_initialize" -B3 -A3 /home/honda/TOPPERS/ASP3/asp3_3.7/extension/dcre/kernel/semaphore.c | sed -n '1,30p'
```
期待（実測・★ゲート対象）:
- 3つの CB とも**先頭フィールドが `QUEUE wait_queue`**
  （`semaphore.h:74-78` / `eventflag.h:73-77` / `mutex.h:73-78`）
  → `(SEMCB *) queue_delete_next(&free_semcb)` の型 punning が成立する。
- 3つの INIB とも **`iprcid`/`affinity` を持たない**（`semaphore.h:61-65`：`sematr`/`isemcnt`/`maxsem`、
  `eventflag.h:61-64`：`flgatr`/`iflgptn`、`mutex.h:59-62`：`mtxatr`/`ceilpri`）。
  3つの CB とも **`p_pcb` を持たない**。→ **Global Constraint 4 の再確認**。
- 3つの `.h` とも `#include <queue.h>` を**既に持つ**（`semaphore.h:51` 等）
  → `extern QUEUE free_semcb;` を `.h` に書くための追加 include は**不要**
  （段階2 の cyclic.h では必要だった点との相違）。
- dcre の `acre_*` は free-list から pop した直後に必ず
  `queue_initialize(&(p_semcb->wait_queue));` を呼び直す（`semaphore.c:203`、
  `eventflag.c:230`、`mutex.c:412`）→ **free-list のリンクとして使われていた
  `wait_queue` の中身は acre 時に必ず作り直される**ので、TA_TPRI（優先度順キュー）の
  初期化状態とは干渉しない（TA_TPRI はキューヘッダの形ではなく `wobj_make_wait` 側の
  挿入位置決定に効く属性である）。→ spec §8-7 の懸念は**不成立**であることを記録する。

- [ ] **Step 8: `initialize_*` の現行形（マスタ限定であること・★ゲート対象）**

```bash
sed -n '118,136p' kernel/semaphore.c
sed -n '126,144p' kernel/eventflag.c
sed -n '124,145p' kernel/mutex.c
```
期待（実測）: 3つとも `if (p_my_pcb->prcid == TOPPERS_MASTER_PRCID) { ... }` で
**既に全体がマスタ限定**。段階2 の `initialize_cyclic` にあった
`if (p_my_pcb->p_tevtcb == NULL) { return; }` のような**時間イベント処理プロセッサ判定は無い**。
`initialize_mutex` だけは冒頭で `mtxhook_check_ceilpri`/`mtxhook_release_all` を設定する
（この2行はマスタ限定ブロックの中にある）。
→ **変えるのは静的ループの境界と、その直後に足す動的スロット節だけ**であることを確定させる。
**プロセッサフィルタを新設しない**ことを確認結果表に太字で書く（Constraint 4）。

- [ ] **Step 9: spec への反映（訂正7件 + §10 新設）**
  - §1.2 の「機能コードは既存の見込み — 実装前確認（§8-1）」を **訂正A** の実測6値で確定させる。
  - §1.3 の `acre_sem` 範囲検査を **訂正E**（`0 <= isemcnt` は落とす／`1 <= maxsem` と
    `maxsem <= TMAX_MAXSEM` は残す・理由付き）に書き換える。
  - §1.3 の `del_*` 共通に **訂正D**（`del_flg` も `CHECK_ID`＝E_ID。dcre の E_PAR は
    dcre 側の不整合）を追記する。
  - §2 に **訂正C**（`CHECK_TSKCTX_UNL_MYSTATE` を使う）と **訂正F**（`p_loctsk = NULL` を入れる）と
    **`mtxatr = TA_NOEXS` を書く前に優先度復帰を済ませる順序制約**を追記する。
  - §4 の「2レンジ ID マクロ ... 新規定義または置換」を **訂正B**（**置換**であること・
    `tnum_*` を `.h` へ移設すること）で確定させる。
  - §5 に **訂正G**（混在 AID サンプルは Task 2 で cyc/alm、Task 3 で sem/mtx を追記。
    cfg 単独バリアントは CMake の制約で作れない）と **訂正H**（TEPP_PRC 検査の
    エラー回帰 cfg は構成不能・mutation control で代替・エラー行列は18件）を追記する。
  - §7 に、テストで使う静的オブジェクト（`SEM1`/`FLG1`/`MTX1`）とタスク編成
    （`TASK1`=MID/PRC1、`TASK2`=HIGH/PRC1、`TASK3`=HIGH/PRC2）を確定として書く。
  - spec 末尾に `## 10. 実装前確認の結果（2026-08-04 実測）` を新設し、
    Step 1-8 の確認結果を**表**で記録する（後続 Task はここを参照する）。
    Step 5 の転写元行範囲表は**そのまま**転記する。

- [ ] **Step 10: コミット**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
git add docs/superpowers/specs/2026-08-04-fmp3-dcre-stage3a-sem-flg-mtx-design.md
git commit -m "docs(spec): dcre段階3aの実装前確認と訂正7件（TFN既存・ID2レンジは置換・MYSTATE・del_flgのE_ID・恒真CHECK_PAR・p_loctsk・混在AIDとTEPP回帰の構成可否）"
```

---

### Task 2: 段階2 hardening 4件

**推奨モデル:** 中位（sonnet）。両エンジンの対応関係と「検査が空虚でないこと」の実演判断が要る。

**Files:**
- Modify: `kernel/kernel.py`（`checkAutoObjid` フックの新設・呼出し）
- Modify: `kernel/kernel.trb`（同・pristine）
- Modify: `kernel/cyclic.py` `kernel/alarm.py`（フックのオーバライド）
- Modify: `kernel/cyclic.trb` `kernel/alarm.trb`（同・pristine）
- Modify: `kernel/cyclic.c` `kernel/alarm.c`（`del_cyc`/`del_alm` に p_pcb-stale コメント1行・pristine）
- Create: `test/test_dcre_mix.c` `test/test_dcre_mix.cfg` `test/test_dcre_mix.h`（pristine 領域・台帳）
- Modify: `test/MANIFEST` `test/testexec.rb`（pristine・台帳）
- Modify: `DIVERGENCE_MAP.md`

**Interfaces:**
- Consumes: Task 1 の確認結果表（訂正G/H）。
- Produces: `KernelObject.checkAutoObjid(numAutoObjid)` フック（Task 3 以降は**触らない**。
  sem/flg/mtx は既定の no-op を継承する）、`test/test_dcre_mix.cfg`（Task 3 が拡張する）。
- **Tasks 3-8 とは独立**。cfg ガードを新しい `AID_*` が増える前に入れておくため**最初に**行う。

**この Task はカーネルの挙動を変えない**（cfg 側の検査追加とコメント追加のみ）。

- [ ] **Step 1: 基準生成物の保存（管理された差分の比較元）**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
cmake --build build/musca_b1-2core > /tmp/dcre3-t2-base-build.log 2>&1; echo "rc=$?"
rm -rf /tmp/dcre3-t2-base-generated
cp -r build/musca_b1-2core/generated /tmp/dcre3-t2-base-generated
```

- [ ] **Step 2: hardening #1 — 共通枠組みに `checkAutoObjid` フックを新設（Python）**

`kernel/kernel.py` の `KernelObject` に、`generate()` の**手前**（`__init__` の直後）へ
既定実装を追加する：

```python
    def checkAutoObjid(self, numAutoObjid):
        """AID_xxx が 1 個以上ある構成に対するオブジェクト種別ごとの追加検査．

        既定では何もしない．cyc/alm だけが「マスタプロセッサが時間イベント
        処理プロセッサであること」を要求するためオーバライドする
        （段階2 最終レビュー Minor 1 の hardening）．
        """
        pass
```

`generate()` の中、訂正E ガード（`if numAutoObjid > 0 and len(cfgData[self.api]) == 0:` の
ブロック。`kernel.py:145-152` 相当）の**直後**に呼出しを足す：

```python
        # オブジェクト種別ごとの追加検査（既定は no-op）
        if numAutoObjid > 0:
            self.checkAutoObjid(numAutoObjid)
```

- [ ] **Step 3: hardening #1 — Ruby 側（オラクル）へ同一変更**

`kernel/kernel.trb` の `KernelObject` に、`generate()` の手前へ：

```ruby
  #
  #  AID_xxxが1個以上ある構成に対するオブジェクト種別ごとの追加検査．
  #
  #  既定では何もしない．cyc/almだけが「マスタプロセッサが時間イベント
  #  処理プロセッサであること」を要求するためオーバライドする（段階2
  #  最終レビュー Minor 1 の hardening）．
  #
  def checkAutoObjid(numAutoObjid)
  end
```

`generate()` の中、訂正E ガード（`kernel.trb:150-157` 相当）の**直後**に：

```ruby
    # オブジェクト種別ごとの追加検査（既定はno-op）
    if numAutoObjid > 0
      checkAutoObjid(numAutoObjid)
    end
```

- [ ] **Step 4: hardening #1 — cyclic/alarm でオーバライド（両エンジン）**

`kernel/cyclic.py` の `CyclicObject` に（`__init__` の直後、`prepare` の手前）：

```python
    def checkAutoObjid(self, numAutoObjid):
        # 動的生成された周期通知は iprcid = TOPPERS_MASTER_PRCID 固定で生成
        # される（kernel/cyclic.c の acre_cyc）。マスタプロセッサが時間イベ
        # ント処理プロセッサでない構成では initialize_cyclic が即 return し、
        # free_cyccb が BSS ゼロのまま acre_cyc の queue_delete_next が NULL を
        # 辿る（段階2 最終レビュー Minor 1）。現行5ターゲットでは
        # TOPPERS_TEPP_PRC の bit0 が必ず立つため到達しないが、将来ターゲット
        # のための構造的な予防として cfg エラーで弾く。
        if (TOPPERS_TEPP_PRC & (1 << (TOPPERS_MASTER_PRCID - 1))) == 0:
            for _, params in cfgData[self.aidapi].items():
                error_ercd("E_RSATR", params,
                           f"{self.aidapi} requires the master processor "
                           "to be a time event processor")
```

`kernel/alarm.py` の `AlarmObject` に**同一のメソッド**（コメントの「周期通知」を
「アラーム通知」、`initialize_cyclic`/`free_cyccb`/`acre_cyc` を
`initialize_alarm`/`free_almcb`/`acre_alm` に読み替えたもの）を追加する。

`kernel/cyclic.trb` の `CyclicObject` に：

```ruby
  def checkAutoObjid(numAutoObjid)
    # 動的生成された周期通知はiprcid = TOPPERS_MASTER_PRCID固定で生成さ
    # れる（kernel/cyclic.cのacre_cyc）．マスタプロセッサが時間イベント
    # 処理プロセッサでない構成ではinitialize_cyclicが即returnし，
    # free_cyccbがBSSゼロのままacre_cycのqueue_delete_nextがNULLを辿る
    # （段階2最終レビュー Minor 1）．現行5ターゲットではTOPPERS_TEPP_PRC
    # のbit0が必ず立つため到達しないが，将来ターゲットのための構造的な
    # 予防としてcfgエラーで弾く．
    if ($TOPPERS_TEPP_PRC & (1 << ($TOPPERS_MASTER_PRCID - 1))) == 0
      $cfgData[@aidapi].each do |_, params|
        error_ercd("E_RSATR", params,
                   "#{@aidapi} requires the master processor " \
                   "to be a time event processor")
      end
    end
  end
```

`kernel/alarm.trb` にも同一のメソッド（同じ読み替え）を追加する。

**★両エンジンでエラー文言を完全に一致させること**（`cfg_error_tests/run.sh` も
`cfg_equivalence.sh` も文言を突き合わせる）。Python の f-string と Ruby の式展開で
`AID_CYC requires the master processor to be a time event processor` という
**同一の1行**になることを、次の Step の mutation control で実際に確かめる。

- [ ] **Step 5: ★★hardening #1 の positive control（検査が空虚でないことの実演）**

★訂正H のとおり、**この検査を踏ませる `.cfg` は現行5ターゲットでは書けない**
（`TOPPERS_TEPP_PRC` は `target/*/target_kernel.h` 由来で cfg から変更できず、
全8プリセットで bit0 が立つ＝段階2 Task 1 Step 5 の実測）。
そこで**条件式を一時的に反転させて、検査が実際に発火し文言が一致することを実演する**。

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
# 1) 反転を注入（両エンジン・cyc 側だけでよい）
sed -i 's/== 0:$/!= 0:/' kernel/cyclic.py        # ★該当行が1つだけであることを先に確認
grep -n "TOPPERS_TEPP_PRC & (1 <<" -A1 kernel/cyclic.py
sed -i 's/)) == 0$/)) != 0/' kernel/cyclic.trb
grep -n "TOPPERS_TEPP_PRC & (1 <<" -A1 kernel/cyclic.trb
```
（`sed` が意図しない行を巻き込む可能性があるので、**必ず前後を `grep` で目視確認**し、
巻き込んだら `git checkout -- <file>` で戻して手で編集する。）

反転した状態で、`AID_CYC` を含む cfg を通す：

```bash
tools/cfg_error_tests/run.sh build/musca_b1-2core \
    tools/cfg_error_tests/dcre_aid_cyc_in_class.cfg E_RSATR \
    "-I$PWD/test" > /tmp/dcre3-t2-tepp-pos.log 2>&1; echo "rc=$?"
grep -n "time event processor" /tmp/dcre3-t2-tepp-pos.log
```
期待: **両エンジンが `AID_CYC requires the master processor to be a time event processor` を
出す**こと（`run.sh` は E_RSATR を期待しているので rc は 0 でも 1 でもよい。
**見るのは文言が両エンジンで一致して出ていること**）。
文言が片側にしか出ない／文字列が違うなら、両エンジンの実装が非対称である。

```bash
# 2) 復元して、検査が黙ることを確認（negative control）
git checkout -- kernel/cyclic.py kernel/cyclic.trb
tools/cfg_error_tests/run.sh build/musca_b1-2core \
    tools/cfg_error_tests/dcre_aid_cyc_in_class.cfg E_RSATR \
    "-I$PWD/test" > /tmp/dcre3-t2-tepp-neg.log 2>&1; echo "rc=$?"
grep -c "time event processor" /tmp/dcre3-t2-tepp-neg.log   # 期待: 0
git diff --stat kernel/cyclic.py kernel/cyclic.trb          # 期待: 差分なし（＝Step 4 の変更も消えている）
```
★`git checkout --` は Step 4 の変更ごと消すので、**Step 4 のオーバライドを入れ直す**こと。
入れ直したら `grep -n "checkAutoObjid" kernel/cyclic.py kernel/cyclic.trb` で 1 件ずつ在ることを確認する。
（消えたことに気づかず先へ進むと hardening #1 が入っていないまま完了報告する事故になる。）

- [ ] **Step 6: hardening #2 — `del_cyc`/`del_alm` に p_pcb-stale コメント**

`kernel/cyclic.c` の `del_cyc` の else 節、`queue_insert_prev(&free_cyccb, ...)` の**直前**に：

```c
		/*
		 *  p_cyccb->p_pcbはfree-list滞在中staleなまま残るが，acre_cycが
		 *  取り出し時に無条件で再設定する（get_pcb(TOPPERS_MASTER_PRCID)）
		 *  ため支障はない．TA_NOEXS状態のCBのp_pcbを読む経路は存在しない
		 *  ことがこの不変量の前提である（段階2最終レビュー triage ①）．
		 */
```

`kernel/alarm.c` の `del_alm` の同じ位置に、`p_cyccb`→`p_almcb`・`acre_cyc`→`acre_alm` と
読み替えた同一コメントを入れる。**コードは1文字も変えない。**

- [ ] **Step 7: hardening #3 — 混在 AID の equivalence サンプル（★訂正G）**

`test/test_dcre_mix.h`（新規）：

```c
/*
 *		AID_xxx 混在構成の cfg 等価性検査用サンプル
 */

#include <kernel.h>
#include "target_test.h"

#define MID_PRIORITY	10

#ifndef STACK_SIZE
#define	STACK_SIZE		4096
#endif /* STACK_SIZE */

#ifndef MPK_SIZE
#define MPK_SIZE		4096
#endif /* MPK_SIZE */

#ifndef TOPPERS_MACRO_ONLY
extern void	task1(EXINF exinf);
#endif /* TOPPERS_MACRO_ONLY */
```

`test/test_dcre_mix.c`（新規。著作権ヘッダは `test/test_dcre1.c` と同形式で付ける）：

```c
/*
 *		AID_xxx 混在構成の cfg 等価性検査用サンプル
 *
 * 【このテストの目的】
 *
 *	AID_xxx を「一部のオブジェクト種にだけ」書いた構成（混在構成）で、
 *	cfg の Ruby オラクルと Python 製品がバイト一致することを検査するための
 *	最小アプリケーション（段階2 最終レビュー Minor 3 の hardening）。
 *
 *	★このプログラムは cfg_equivalence.sh のための入力であり、機能の
 *	  検査は行わない（QEMU で走らせる必要も無い）。走らせた場合は
 *	  ただちに check_finish(1) で PASS して終わる。
 */

#include <kernel.h>
#include <t_syslog.h>
#include "syssvc/test_svc.h"
#include "kernel_cfg.h"
#include "test_dcre_mix.h"

/* DO NOT DELETE THIS LINE -- gentest depends on it. */

void
task1(EXINF exinf)
{
	test_start(__FILE__);
	check_finish(1);
}
```

`test/test_dcre_mix.cfg`（新規。**Task 2 時点では cyc/alm の混在のみ**。
`AID_ALM` を意図的に書かないことが「混在」の実体）：

```c
/*
 *		AID_xxx 混在構成の cfg 等価性検査用サンプルの
 *		システムコンフィギュレーションファイル
 *
 *  $Id$
 */
INCLUDE("test_common1.cfg");

#include "test_dcre_mix.h"

CLASS(CLS_PRC1) {
	CRE_TSK(TASK1, { TA_ACT, 1, task1, MID_PRIORITY, STACK_SIZE, NULL });
	/*
	 *  訂正E ガード（AID_xxx > 0 なら静的オブジェクトが1個以上必要）を
	 *  満たすための静的オブジェクト．通知ハンドラには task1 を流用する
	 *  （発火させないので意味は問わない）．
	 */
	CRE_CYC(CYC1, { TA_NULL, { TNFY_HANDLER, 0, task1 }, 1000000, 0 });
	CRE_ALM(ALM1, { TA_NULL, { TNFY_HANDLER, 0, task1 } });
}

/*
 *  ★混在の実体：AID_TSK と AID_CYC は書き，AID_ALM は書かない．
 *  「同一構成の中に has_aid が真のオブジェクトと偽のオブジェクトが
 *  共存する」ことが両エンジンで同じ出力になることを検査する．
 *  （段階3a Task 3 で AID_SEM / AID_MTX を追記し，AID_FLG は
 *  書かないまま残す．）
 */
AID_TSK(2);
DEF_MPK({ MPK_SIZE, NULL });
AID_CYC(2);
```

登録：
- `test/MANIFEST` の `test_dcre2.h`（`:43`）の直後に、アルファベット順で
  `test_dcre_mix.c` `test_dcre_mix.cfg` `test_dcre_mix.h` の3行。
- `test/testexec.rb` の `"dcre2"    => { SRC: "test_dcre2" },`（`:91`）の直後に
  `"dcremix"  => { SRC: "test_dcre_mix" },`。

- [ ] **Step 8: 混在サンプルのビルドと等価性**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
cmake --preset musca_b1-2core -B build/musca_b1-2core-tmix \
  -DFMP3_APPLNAME=test_dcre_mix -DFMP3_APPLDIR=test \
  -DFMP3_SYSSVC_TARGET_C_FILES=$PWD/syssvc/test_svc.c > /tmp/dcre3-t2-mix-conf.log 2>&1
echo "conf rc=$?"
cmake --build build/musca_b1-2core-tmix > /tmp/dcre3-t2-mix-build.log 2>&1; echo "build rc=$?"
tools/cfg_equivalence.sh build/musca_b1-2core-tmix > /tmp/dcre3-t2-mix-eq.log 2>&1; echo "eq rc=$?"
grep -n "TNUM_SCYCID\|_kernel_acycinib_table\|_kernel_tmax_scycid" \
     build/musca_b1-2core-tmix/generated/kernel_cfg.c
grep -n "EMPTY_LABEL(ALMINIB\|EMPTY_LABEL(T_NFYINFO, _kernel_aalm" \
     build/musca_b1-2core-tmix/generated/kernel_cfg.c
```
期待: conf/build/eq とも rc=0（**exit=2 は不合格**）。
`_kernel_acycinib_table[2]`（実体）と `_kernel_aalminib_table`（`TOPPERS_EMPTY_LABEL`）が
**同じ `kernel_cfg.c` に同居**していること＝これが「混在」が実際に生成されている証拠。
両方とも出ないなら cfg が混在構成になっていない（AID の書き忘れ等）。

- [ ] **Step 9: hardening #4 — DIVERGENCE_MAP へ CHECK_PAR 逸脱の半文追記**

`DIVERGENCE_MAP.md` の `kernel/cyclic.c（dcre段階2 Task 6）` の行（`:93` 付近）の理由欄末尾に：

> なお `acre_cyc` の範囲検査は dcre `cyclic.c` の
> `CHECK_PAR(0 <= cycphs && cycphs <= TMAX_RELTIM)` から
> **恒真条件 `0 <= cycphs` を落として** `CHECK_PAR(cycphs <= TMAX_RELTIM)` としている
> （`RELTIM` は符号なしのため意味的にヌルな比較。dcre からの意図的な逸脱・意味は同一）。

を追記する（**半文の追記であって行の新設ではない**）。
★段階3a では `acre_sem` に**同型の逸脱がもう1件**出る（訂正E：`0 <= isemcnt`）ので、
**Task 4 で `kernel/semaphore.c` の台帳行を書くときに同じ書き方で書く**こと
（本 Step ではまだ `semaphore.c` の行は作らない）。

- [ ] **Step 10: 全8構成のビルドと等価性（無回帰）**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
for p in musca_b1 musca_b1-2core rp2350_pico2 polarfire_soc_kit-qemu \
         kria_arm64 kria_arm64-1core kria_r5 kria_r5-2core; do
  cmake --build build/$p > /tmp/dcre3-t2-build-$p.log 2>&1; echo "$p build rc=$?"
  tools/cfg_equivalence.sh build/$p > /tmp/dcre3-t2-eq-$p.log 2>&1; echo "$p eq rc=$?"
done
```
期待: すべて rc=0。**exit=2 は不合格**（Constraint 11）。

- [ ] **Step 11: 管理された差分の検査（AID 無し構成の生成物が不変であること）**

```bash
diff -u /tmp/dcre3-t2-base-generated/kernel_cfg.h build/musca_b1-2core/generated/kernel_cfg.h
echo "h diff rc=$?"
diff -u /tmp/dcre3-t2-base-generated/kernel_cfg.c build/musca_b1-2core/generated/kernel_cfg.c
echo "c diff rc=$?"
```
期待: **両方とも rc=0（差分なし）**。本 Task は
「AID が 0 個の構成では `checkAutoObjid` を呼びさえしない」「コメントしか足していない」ので、
生成物は**1バイトも変わってはならない**。1行でも差分が出たら不合格。

- [ ] **Step 12: QEMU 起動2構成（無回帰）**

```bash
timeout -k 5 25 cmake --build build/musca_b1-2core --target run > /tmp/dcre3-t2-run-musca.log 2>&1
grep -c 'Processor [12] start\.' /tmp/dcre3-t2-run-musca.log     # 期待: 2
pgrep -a qemu                                                    # 期待: 何も出ない
```
```bash
timeout -k 5 25 cmake --build build/kria_r5-2core --target run > /tmp/dcre3-t2-run-r5.log 2>&1
grep -c 'Processor [12] start\.' /tmp/dcre3-t2-run-r5.log        # 期待: 2
pgrep -a qemu                                                    # 期待: 何も出ない
```

- [ ] **Step 13: 台帳とコミット**

`DIVERGENCE_MAP.md` に次を追記する（Step 9 の半文追記に加えて）：
- `kernel/kernel.trb`（既存行の理由欄に「`checkAutoObjid` フックを追加（段階3a Task 2 hardening #1）」を追記）
- `kernel/cyclic.trb` `kernel/alarm.trb`（同、「`checkAutoObjid` オーバライドを追加」）
- `kernel/cyclic.c` `kernel/alarm.c`（同、「`del_*` に p_pcb-stale の不変量コメントを追加（コード不変）」）
- `test/test_dcre_mix.c` `test/test_dcre_mix.cfg` `test/test_dcre_mix.h`（新規追加・種別 `add (dcre-port)`、
  理由「AID_xxx 混在構成の cfg 等価性検査用サンプル。QEMU 実行は不要」）
- `test/MANIFEST` `test/testexec.rb`（既存行の理由欄に `test_dcre_mix` 追加を追記）

```bash
git add -A && git commit -m "fix(cfg,kernel): 段階2 hardening 4件（TEPP_PRCマスタbit検査・p_pcb staleコメント・混在AIDサンプル・CHECK_PAR逸脱の台帳追記）"
```

---

### Task 3: cfg 両エンジン — AID_SEM / AID_FLG / AID_MTX と T_CSEM/T_CFLG/T_CMTX

**推奨モデル:** 中位（sonnet）。テンプレート2言語の対応関係と許容差分リストの厳密判定が要る。

**Files:**
- Modify: `kernel/kernel_api.def`（3行追加・pristine）
- Modify: `include/kernel.h`（`T_CSEM`/`T_CFLG`/`T_CMTX` + 6宣言・pristine）
- Modify: `test/test_dcre_mix.cfg`（★訂正G：`AID_SEM(2)` / `AID_MTX(1)` を追記・pristine）
- Create: `tools/cfg_error_tests/dcre_aid_sem_in_class.cfg`
  `tools/cfg_error_tests/dcre_aid_flg_in_class.cfg`
  `tools/cfg_error_tests/dcre_aid_mtx_in_class.cfg`
  `tools/cfg_error_tests/dcre_aid_sem_no_static.cfg`
  `tools/cfg_error_tests/dcre_aid_flg_no_static.cfg`
  `tools/cfg_error_tests/dcre_aid_mtx_no_static.cfg`
- Modify: `DIVERGENCE_MAP.md`

**★この Task で `kernel/semaphore.py` `semaphore.trb` `eventflag.py` `eventflag.trb`
`mutex.py` `mutex.trb` を編集してはならない**（Task 1 Step 6 の結論：per-object 変更はゼロ）。
編集したくなったら、それは共通枠組みの理解が誤っている合図である。

**Interfaces（後続 Task が依存する生成物）:**
- Produces（`kernel_cfg.c`/`kernel_cfg.h` に恒常出力）:
  `#define TNUM_SSEMID <n>` / `const ID _kernel_tmax_ssemid`、
  `SEMINIB _kernel_aseminib_table[N]`（N=0 時 `TOPPERS_EMPTY_LABEL(SEMINIB, _kernel_aseminib_table);`）、
  予約 SEMCB `static SEMCB _kernel_asemcb_<i>;`（i=1..N）と `_kernel_p_semcb_table` 末尾への追加。
  flg 側は `TNUM_SFLGID` / `_kernel_tmax_sflgid` / `_kernel_aflginib_table` / `_kernel_aflgcb_<i>`、
  mtx 側は `TNUM_SMTXID` / `_kernel_tmax_smtxid` / `_kernel_amtxinib_table` / `_kernel_amtxcb_<i>`。
  `TNUM_SEMID`/`TNUM_FLGID`/`TNUM_MTXID` は**総数**（静的+AID）へ意味変更。
- Produces（`include/kernel.h`）: `T_CSEM` `T_CFLG` `T_CMTX` と6つの `extern` 宣言。
- Consumes: Task 1 の確認結果表（訂正A/B/G）。

- [ ] **Step 1: 基準生成物の保存（管理された差分の比較元）**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
cmake --build build/musca_b1-2core > /tmp/dcre3-t3-base-build.log 2>&1; echo "rc=$?"
rm -rf /tmp/dcre3-base-generated
cp -r build/musca_b1-2core/generated /tmp/dcre3-base-generated
```

- [ ] **Step 2: `kernel/kernel_api.def` に dcre と同一の3行を追加**

ファイル末尾（`AID_ALM .noalm` の次）に：

```
AID_SEM .nosem
AID_FLG .noflg
AID_MTX .nomtx
```

（`AID_TSK .notsk` / `AID_CYC .nocyc` / `AID_ALM .noalm` と同じ形。
共通枠組みが `self.noobj = "no" + obj` で参照するため、パラメータ名は
`nosem`/`noflg`/`nomtx` でなければならない＝`kernel.py:121`。）

- [ ] **Step 3: `include/kernel.h` にパケット型3つを追加**

`typedef struct t_rmtx {`（`include/kernel.h:252` 付近）の**直前**に `T_CMTX`、
`typedef struct t_rsem {`（`:228` 付近）の**直前**に `T_CSEM`、
`typedef struct t_rflg {`（`:233` 付近）の**直前**に `T_CFLG` を置く
（＝既存の `T_CCYC`/`T_RCYC`・`T_CALM`/`T_RALM` と同じく「生成パケットを参照パケットの直前に置く」流儀。
実際の行番号は Task 2 までの変更で動いている可能性があるので、**行番号ではなく
`typedef struct t_rsem {` 等の文字列を目印にする**）。

dcre `include/kernel.h:223-291` からそのまま転記する：

```c
typedef struct t_csem {
	ATR			sematr;		/* セマフォ属性 */
	uint_t		isemcnt;	/* セマフォの初期資源数 */
	uint_t		maxsem;		/* セマフォの最大資源数 */
} T_CSEM;
```

```c
typedef struct t_cflg {
	ATR			flgatr;		/* イベントフラグ属性 */
	FLGPTN		iflgptn;	/* イベントフラグの初期ビットパターン */
} T_CFLG;
```

```c
typedef struct t_cmtx {
	ATR			mtxatr;		/* ミューテックス属性 */
	PRI			ceilpri;	/* ミューテックスの上限優先度 */
} T_CMTX;
```

（`ATR`/`uint_t`/`FLGPTN`/`PRI` はいずれも既存。追加の include は不要。
★`T_CMTX.ceilpri` は **`PRI`（外部表現）**であって `MTXINIB.ceilpri` の
`uint_t`（内部表現）ではない。`acre_mtx` が `INT_PRIORITY()` で変換する。）

- [ ] **Step 4: `include/kernel.h` に6つのサービスコール宣言を追加**

`extern ER sig_sem(ID semid) throw();`（`:337` 付近）の**直前**に：
```c
extern ER_ID	acre_sem(const T_CSEM *pk_csem) throw();
extern ER		del_sem(ID semid) throw();
```
`extern ER set_flg(ID flgid, FLGPTN setptn) throw();`（`:344` 付近）の**直前**に：
```c
extern ER_ID	acre_flg(const T_CFLG *pk_cflg) throw();
extern ER		del_flg(ID flgid) throw();
```
`extern ER loc_mtx(ID mtxid) throw();`（`:376` 付近）の**直前**に：
```c
extern ER_ID	acre_mtx(const T_CMTX *pk_cmtx) throw();
extern ER		del_mtx(ID mtxid) throw();
```

（機能コードは Task 1 Step 1 の記録どおり6件とも `include/kernel_fncode.h` に**既存**＝
`kernel_fncode.h` は**変更しない**。
返値型は段階1 の `acre_tsk`・段階2 の `acre_cyc`/`acre_alm` と揃えて **`ER_ID`** にする。
dcre の `.c` は `ER_UINT` を使うが、どちらも `int_t` の別名であり宣言と定義で揃っていれば問題ない
— **段階1/2 で確立済みの意図的な逸脱**として台帳に1文書く。）

- [ ] **Step 5: 全8構成のビルドと等価性**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
for p in musca_b1 musca_b1-2core rp2350_pico2 polarfire_soc_kit-qemu \
         kria_arm64 kria_arm64-1core kria_r5 kria_r5-2core; do
  cmake --preset $p > /tmp/dcre3-t3-conf-$p.log 2>&1 || { echo "$p CONF FAIL"; continue; }
  cmake --build build/$p > /tmp/dcre3-t3-build-$p.log 2>&1; echo "$p build rc=$?"
  tools/cfg_equivalence.sh build/$p > /tmp/dcre3-t3-eq-$p.log 2>&1; echo "$p eq rc=$?"
done
```
期待: すべて rc=0（`RESULT = MATCH`）。**exit=2 は不合格**として原因を調べる。

- [ ] **Step 6: ★管理された差分の検査（AID 無し構成の生成物）**

```bash
diff -u /tmp/dcre3-base-generated/kernel_cfg.h build/musca_b1-2core/generated/kernel_cfg.h
echo "h diff rc=$?"
diff -u /tmp/dcre3-base-generated/kernel_cfg.c build/musca_b1-2core/generated/kernel_cfg.c \
     > /tmp/dcre3-t3-managed-diff.txt; cat /tmp/dcre3-t3-managed-diff.txt
```
期待: `kernel_cfg.h` は**差分なし（rc=0）**。`kernel_cfg.c` の差分が次の
**許容リストと完全一致**すること（**1件でも余分な差分があれば不合格**）：

1. Semaphore ブロック: `#define TNUM_SSEMID <静的個数>` 行と
   `const ID _kernel_tmax_ssemid = (TMIN_SEMID + TNUM_SSEMID - 1);` 行の追加
   （`_kernel_tmax_semid` 行の直後。空行位置の変化を含む）
2. `_kernel_seminib_table` のサイズトークン `[TNUM_SEMID]` → `[TNUM_SSEMID]`
3. `TOPPERS_EMPTY_LABEL(SEMINIB, _kernel_aseminib_table);` の追加
4. Eventflag ブロック: 1〜3 の flg 版
   （`TNUM_SFLGID` / `_kernel_tmax_sflgid` / `[TNUM_SFLGID]` /
   `TOPPERS_EMPTY_LABEL(FLGINIB, _kernel_aflginib_table);`）
5. Mutex ブロック: 1〜3 の mtx 版
   （`TNUM_SMTXID` / `_kernel_tmax_smtxid` / `[TNUM_SMTXID]` /
   `TOPPERS_EMPTY_LABEL(MTXINIB, _kernel_amtxinib_table);`）

**★次のものが差分に出たら不合格である**（設計の誤りを示す）：
- `_kernel_atinib_table` / cyclic / alarm / dataqueue / pridataq / mempfix ブロックの変化
  （段階3a は sem/flg/mtx にしか触っていない）
- `T_NFYINFO` を伴う `a*_nfyinfo_table`（sem/flg/mtx は通知機構を持たない＝
  `inibList` が既定の1エントリだけであることの裏取り）

- [ ] **Step 7: positive control — AID 有り構成で出力が実際に変わり、両エンジンでバイト一致**

`sample/sample1.cfg` は `CRE_SEM`/`CRE_FLG`/`CRE_MTX` を既に含む
（`grep -n "CRE_SEM\|CRE_FLG\|CRE_MTX" sample/sample1.cfg` で確認する。含まなければ
`test/test_dcre_mix.cfg` を使う経路＝Step 9 に切り替える）。
**一時的に3行追記**して AID 有り実構成を作る：

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
grep -n "CRE_SEM\|CRE_FLG\|CRE_MTX" sample/sample1.cfg
cp sample/sample1.cfg /tmp/dcre3-sample1.cfg.orig
printf '\nAID_SEM(2);\nAID_FLG(1);\nAID_MTX(2);\n' >> sample/sample1.cfg
cmake --build build/musca_b1-2core > /tmp/dcre3-t3-aid-build.log 2>&1; echo "build rc=$?"
grep -n "TNUM_SSEMID\|_kernel_aseminib_table\|_kernel_asemcb_" \
     build/musca_b1-2core/generated/kernel_cfg.c
grep -n "TNUM_SFLGID\|_kernel_aflginib_table\|_kernel_aflgcb_" \
     build/musca_b1-2core/generated/kernel_cfg.c
grep -n "TNUM_SMTXID\|_kernel_amtxinib_table\|_kernel_amtxcb_" \
     build/musca_b1-2core/generated/kernel_cfg.c
grep -n "define TNUM_SEMID\|define TNUM_FLGID\|define TNUM_MTXID" \
     build/musca_b1-2core/generated/kernel_cfg.h
tools/cfg_equivalence.sh build/musca_b1-2core > /tmp/dcre3-t3-aid-eq.log 2>&1; echo "eq rc=$?"
```
期待:
- `SEMINIB _kernel_aseminib_table[2];`（`TOPPERS_EMPTY_LABEL` ではなく**実体**）、
  `FLGINIB _kernel_aflginib_table[1];`、`MTXINIB _kernel_amtxinib_table[2];`。
- `static SEMCB _kernel_asemcb_1;` `_kernel_asemcb_2;` と
  `_kernel_p_semcb_table` 末尾への `&_kernel_asemcb_1` `&_kernel_asemcb_2` 追加。flg/mtx も同様。
- `TNUM_SEMID` が「静的個数 + 2」、`TNUM_SSEMID` が静的個数。flg は +1、mtx は +2。
- `cfg_equivalence.sh` rc=0（**Ruby/Python がバイト一致**）。

- [ ] **Step 8: ★★compile-through control（段階1 COUNT_MB_T ／ 段階2 訂正B の再発防止）**

Step 7 の `build rc=0` は**単なる cfg 生成の成功ではなく、`kernel_cfg.c` の
実コンパイルとリンクの成功**でなければならない。`cfg_equivalence.sh` は両エンジンの
**生成文字列を diff するだけでコンパイルしない**ため、
「両エンジンが同じように未定義の型/マクロを出力する」欠陥を**構造的に検出できない**。

```bash
ls -l sample/sample1.cfg
find build/musca_b1-2core -name 'kernel_cfg.c.obj' -newer /tmp/dcre3-sample1.cfg.orig -print
find build/musca_b1-2core -name '*.elf' -newer /tmp/dcre3-sample1.cfg.orig -print
```
期待: `kernel_cfg.c.obj` と `.elf` が **AID 追記後のタイムスタンプで実在**すること
（＝`SEMINIB _kernel_aseminib_table[2];` 等を含む `kernel_cfg.c` が実際にコンパイル・
リンクされた）。`.obj` 名が違う環境なら `find build/musca_b1-2core -name 'kernel_cfg*.o*'` で探す。

さらに**64bit ターゲットでも**同じ確認を行う：

```bash
cmake --build build/kria_arm64 > /tmp/dcre3-t3-aid-build-arm64.log 2>&1; echo "rc=$?"
tools/cfg_equivalence.sh build/kria_arm64 > /tmp/dcre3-t3-aid-eq-arm64.log 2>&1; echo "eq rc=$?"
```
期待: 両方 rc=0。
（★段階2 訂正D の 64bit 問題は本段階では**構造的に発生しない**〔Constraint 6〕が、
`SEMINIB`/`MTXINIB` の RAM 配列がリンクできることの確認としては意味がある。）

- [ ] **Step 9: negative control（等価性検査が空虚でないことの実演）**

`kernel/kernel_api.def` の `AID_SEM .nosem` の行を**一時的に `AID_SEM .nosemx` に書き換える**
のではなく（両エンジン共通の入力なので差が出ない）、
**Python 側だけを壊す**：`kernel/kernel.py` の

```python
            kernelCfgC.add(f"#define TNUM_S{self.OBJ}ID\t"
                           f"{len(cfgData[self.api])}")
```

を一時的に `f"{len(cfgData[self.api]) + 1}"` に書き換えて：

```bash
cmake --build build/musca_b1-2core > /dev/null 2>&1
tools/cfg_equivalence.sh build/musca_b1-2core > /tmp/dcre3-t3-neg.log 2>&1; echo "rc=$?"
grep -n "MISMATCH\|RESULT" /tmp/dcre3-t3-neg.log
```
期待: **rc=1（MISMATCH）**。rc=0 なら等価性検査が空虚である。**rc=2 も不合格**（前提未充足）。
確認後、書き換えを**復元**して rc=0 に戻ることを再確認する
（`git diff kernel/kernel.py` が空になることを見る）。

- [ ] **Step 10: `sample/sample1.cfg` を復元し、基準状態に戻す**

```bash
cp /tmp/dcre3-sample1.cfg.orig sample/sample1.cfg
git diff --stat sample/sample1.cfg     # 期待: 出力が空（差分なし）
cmake --build build/musca_b1-2core > /tmp/dcre3-t3-restore.log 2>&1; echo "rc=$?"
diff -u /tmp/dcre3-t3-managed-diff.txt \
     <(diff -u /tmp/dcre3-base-generated/kernel_cfg.c \
              build/musca_b1-2core/generated/kernel_cfg.c) \
  && echo "managed diff reproduced"
```
期待: `sample/sample1.cfg` に差分が残っていないこと、管理された差分が Step 6 と一致すること。

- [ ] **Step 11: ★訂正G — 混在 AID サンプルを sem/mtx まで拡張**

`test/test_dcre_mix.cfg` を次のように書き換える
（`CLASS(CLS_PRC1)` の中に静的 sem/mtx を足し、末尾に `AID_SEM(2)` / `AID_MTX(1)` を足す。
**`AID_FLG` は書かない**＝これが混在の実体）：

```c
CLASS(CLS_PRC1) {
	CRE_TSK(TASK1, { TA_ACT, 1, task1, MID_PRIORITY, STACK_SIZE, NULL });
	/*
	 *  訂正E ガード（AID_xxx > 0 なら静的オブジェクトが1個以上必要）を
	 *  満たすための静的オブジェクト．通知ハンドラには task1 を流用する
	 *  （発火させないので意味は問わない）．
	 */
	CRE_CYC(CYC1, { TA_NULL, { TNFY_HANDLER, 0, task1 }, 1000000, 0 });
	CRE_ALM(ALM1, { TA_NULL, { TNFY_HANDLER, 0, task1 } });
	CRE_SEM(SEM1, { TA_TPRI, 0, 1 });
	CRE_FLG(FLG1, { TA_TPRI, 0 });
	CRE_MTX(MTX1, { TA_TPRI });
}

/*
 *  ★混在の実体：AID_TSK / AID_CYC / AID_SEM / AID_MTX は書き，
 *  AID_ALM と AID_FLG は書かない．同一構成の中に has_aid が真の
 *  オブジェクトと偽のオブジェクトが共存することを両エンジンで
 *  検査する（段階2 最終レビュー Minor 3 の hardening）．
 */
AID_TSK(2);
DEF_MPK({ MPK_SIZE, NULL });
AID_CYC(2);
AID_SEM(2);
AID_MTX(1);
```

```bash
cmake --build build/musca_b1-2core-tmix > /tmp/dcre3-t3-mix-build.log 2>&1; echo "build rc=$?"
tools/cfg_equivalence.sh build/musca_b1-2core-tmix > /tmp/dcre3-t3-mix-eq.log 2>&1; echo "eq rc=$?"
grep -c "_kernel_aseminib_table\[2\]\|_kernel_amtxinib_table\[1\]" \
     build/musca_b1-2core-tmix/generated/kernel_cfg.c        # 期待: 2
grep -c "EMPTY_LABEL(FLGINIB, _kernel_aflginib_table)\|EMPTY_LABEL(ALMINIB, _kernel_aalminib_table)" \
     build/musca_b1-2core-tmix/generated/kernel_cfg.c        # 期待: 2
```
期待: build/eq とも rc=0、実体2件と EMPTY_LABEL 2件が**同じ `kernel_cfg.c` に同居**。

- [ ] **Step 12: エラー回帰ケース6件の追加（in-class E_RSATR ×3 + no-static E_OBJ ×3）**

★既存の `dcre_aid_cyc_no_static.cfg` の書式（`INCLUDE("test/test_common1.cfg")` +
第4引数 `EXTRA_CFLAGS="-I<repo>/test"`）を**そのまま踏襲**する。

`tools/cfg_error_tests/dcre_aid_sem_in_class.cfg`：
```c
/*
 *  AID_SEM をクラスの囲みの中に書くと E_RSATR（クラス外専用 API）
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
	CRE_SEM(SEM1, { TA_TPRI, 0, 1 });
	AID_SEM(1);
}
```

`tools/cfg_error_tests/dcre_aid_flg_in_class.cfg`（同じヘッダコメント。`SEM`→`FLG`）：
```c
INCLUDE("test/test_common1.cfg");
#include "test_int2.h"
CLASS(CLS_PRC1) {
	CRE_TSK(TASK1, { TA_ACT, 1, task1, MID_PRIORITY, STACK_SIZE, NULL });
	CRE_FLG(FLG1, { TA_TPRI, 0 });
	AID_FLG(1);
}
```

`tools/cfg_error_tests/dcre_aid_mtx_in_class.cfg`（同。`SEM`→`MTX`）：
```c
INCLUDE("test/test_common1.cfg");
#include "test_int2.h"
CLASS(CLS_PRC1) {
	CRE_TSK(TASK1, { TA_ACT, 1, task1, MID_PRIORITY, STACK_SIZE, NULL });
	CRE_MTX(MTX1, { TA_TPRI });
	AID_MTX(1);
}
```

`tools/cfg_error_tests/dcre_aid_sem_no_static.cfg`（訂正E ガードの sem 版）：
```c
/*
 *  静的な CRE_SEM が1個も無いのに AID_SEM を書くと E_OBJ（訂正E ガード）
 *
 *  実行時に EXTRA_CFLAGS として「-I<repo>/test」（第4引数）を渡すこと。
 */
INCLUDE("test/test_common1.cfg");
#include "test_int2.h"
CLASS(CLS_PRC1) {
	CRE_TSK(TASK1, { TA_ACT, 1, task1, MID_PRIORITY, STACK_SIZE, NULL });
}
AID_SEM(2);
```

`tools/cfg_error_tests/dcre_aid_flg_no_static.cfg` / `dcre_aid_mtx_no_static.cfg` は
同じ内容で最終行を `AID_FLG(2);` / `AID_MTX(2);` に替えたもの（コメントの `CRE_SEM`/`AID_SEM` も読み替える）。

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
R=tools/cfg_error_tests/run.sh
X="-I$PWD/test"
$R build/musca_b1-2core tools/cfg_error_tests/dcre_aid_sem_in_class.cfg   E_RSATR "$X"; echo "1:$?"
$R build/musca_b1-2core tools/cfg_error_tests/dcre_aid_flg_in_class.cfg   E_RSATR "$X"; echo "2:$?"
$R build/musca_b1-2core tools/cfg_error_tests/dcre_aid_mtx_in_class.cfg   E_RSATR "$X"; echo "3:$?"
$R build/musca_b1-2core tools/cfg_error_tests/dcre_aid_sem_no_static.cfg  E_OBJ   "$X"; echo "4:$?"
$R build/musca_b1-2core tools/cfg_error_tests/dcre_aid_flg_no_static.cfg  E_OBJ   "$X"; echo "5:$?"
$R build/musca_b1-2core tools/cfg_error_tests/dcre_aid_mtx_no_static.cfg  E_OBJ   "$X"; echo "6:$?"
```
期待: 6件とも rc=0（両エンジンが同じ ercd を同じ文言で検出）。
rc=2 が出たら**前提未充足**＝第4引数を渡し忘れているか、cfg が別のエラーで先に落ちている。
後者なら cfg を最小形に調整し、**調整した事実を記録する**。

- [ ] **Step 13: 台帳とコミット**

`DIVERGENCE_MAP.md` に次を追記する：
- `kernel/kernel_api.def`（既存行の理由欄に「段階3a Task 3 で `AID_SEM`/`AID_FLG`/`AID_MTX` の3行を追加」を追記）
- `include/kernel.h`（既存行の理由欄に「段階3a Task 3 で `T_CSEM`/`T_CFLG`/`T_CMTX` と
  `acre_sem`/`del_sem`/`acre_flg`/`del_flg`/`acre_mtx`/`del_mtx` の6宣言を追加。
  dcre `include/kernel.h:223-291` と同一。返値型のみ dcre の `ER_UINT` から
  `ER_ID` へ揃えた（段階1/2 と同じ意図的逸脱、`int_t` の別名で実体は同じ）」を追記）
- `test/test_dcre_mix.cfg`（既存行の理由欄に「段階3a Task 3 で `AID_SEM(2)`/`AID_MTX(1)` と
  静的 `CRE_SEM`/`CRE_FLG`/`CRE_MTX` を追記し、混在の対象を sem/mtx へ拡張。
  `AID_FLG`/`AID_ALM` は意図的に書かない」を追記）

```bash
git add -A && git commit -m "feat(cfg): AID_SEM/AID_FLG/AID_MTX と T_CSEM/T_CFLG/T_CMTX を両エンジンへ追加（dcre段階3a）"
```

---

### Task 4: semaphore 層 — free_semcb・initialize・2レンジ SEMID・acre_sem/del_sem・E_NOEXS ×6

**推奨モデル:** 中位（sonnet）

**Files:**
- Modify: `kernel/semaphore.h`（externs / `tnum_sem`・`tnum_ssem` 移設 / `SEMID` 置換）
- Modify: `kernel/semaphore.c`（`tnum_sem` 重複削除 / `free_semcb` / `initialize_semaphore` /
  `acre_sem` / `del_sem` / E_NOEXS ×6）
- Modify: `kernel/allfunc.h`（`/* semaphore.c */` 節に2行）
- Modify: `kernel/Makefile.kernel`（`semaphore =` 行に .o 2個）
- Modify: `kernel/kernel_rename.def` →（再生成）`kernel_rename.h` `kernel_unrename.h`
- Modify: `DIVERGENCE_MAP.md`

**Interfaces:**
- Consumes: Task 3 の `_kernel_tmax_ssemid` / `_kernel_aseminib_table` / 予約 SEMCB /
  `T_CSEM` / `acre_sem`・`del_sem` の宣言。Task 1 の確認結果表（訂正B/C/E、転写元行範囲）。
- Produces: `QUEUE free_semcb` / `tmax_ssemid` / `tnum_ssem` / `aseminib_table[]` /
  2レンジ `SEMID(p_semcb)` / `ER_ID acre_sem(const T_CSEM *)` / `ER del_sem(ID)`。Task 7 が使う。

**AID 無し構成の挙動は不変である**（`tnum_ssem == tnum_sem` となり動的スロットのループは
空振りし、`SEMID` は既存式に落ち、既存 API は `sematr != TA_NOEXS` なので E_NOEXS 分岐を通らない）。

- [ ] **Step 1: `kernel/semaphore.h` — extern 追加・`tnum_*` 移設・`SEMID` の2レンジ置換**

`extern const ID tmax_semid;` のブロック（`semaphore.h:80-83`）を次に置換：

```c
/*
 *  セマフォIDの最大値（kernel_cfg.c）
 */
extern const ID	tmax_semid;
extern const ID	tmax_ssemid;		/* 静的生成セマフォのID番号の最大値 */

/*
 *  使用していないセマフォ管理ブロックのリスト（semaphore.c）
 *
 *  SEMCBの先頭フィールドがQUEUE（wait_queue）なので，そのまま
 *  free-listのリンクに流用する（dcre semaphore.c:145-161 と同一）．
 *  段階2のcyc/almで用いたtmevtb領域のオーバレイは不要である．
 */
extern QUEUE	free_semcb;
```

`extern const SEMINIB seminib_table[];` のブロック（`semaphore.h:85-88`）の**直後**に追加：

```c
/*
 *  動的生成セマフォの初期化ブロックのエリア（kernel_cfg.c・RAM）
 */
extern SEMINIB	aseminib_table[];
```

`extern SEMCB *const p_semcb_table[];`（`semaphore.h:93`）の**直後**、
既存の `SEMID` マクロブロック（`semaphore.h:95-99`）を次で**置換**する
（★Task 1 Step 2 の記録に従う。**新設ではなく置換**である）：

```c
/*
 *  セマフォの数
 *
 *  SEMIDマクロから参照するためsemaphore.cから移設した．
 */
#define tnum_sem	((uint_t)(tmax_semid - TMIN_SEMID + 1))
#define tnum_ssem	((uint_t)(tmax_ssemid - TMIN_SEMID + 1))

/*
 *  セマフォ管理ブロックからセマフォIDを取り出すためのマクロ
 *
 *  FMP3のSEMCBはポインタ表（p_semcb_table）経由で参照される個別の
 *  named staticであり，SEMCB自身の配列位置から番号を引けない．元から
 *  SEMINIBへのポインタ差分で求めていた式を，動的生成セマフォ
 *  （p_seminibがaseminib_tableを指す）と静的生成セマフォ
 *  （p_seminibがseminib_tableを指す）の2レンジに拡張する
 *  （段階2のCYCIDと同型）．AID_SEMが無い構成ではtnum_sem == tnum_ssem
 *  となり第1項が常に偽＝従来と同一の式に落ちる．
 */
#define	SEMID(p_semcb) \
	((((p_semcb)->p_seminib >= aseminib_table) \
		&& ((p_semcb)->p_seminib < &aseminib_table[tnum_sem - tnum_ssem])) \
	  ? ((ID)(((p_semcb)->p_seminib - aseminib_table) + TMIN_SEMID + tnum_ssem)) \
	  : ((ID)(((p_semcb)->p_seminib - seminib_table) + TMIN_SEMID)))
```

★`#include <queue.h>` は `semaphore.h:51` に**既にある**ので追加不要
（Task 1 Step 7 で確認済み。段階2 の `cyclic.h` では追加が必要だった点との相違）。

- [ ] **Step 2: `kernel/semaphore.c` — `tnum_sem` の重複削除と `initialize_semaphore` の改造**

`semaphore.c:104-107` の

```c
/*
 *  セマフォの数
 */
#define tnum_sem	((uint_t)(tmax_semid - TMIN_SEMID + 1))
```

を**削除**する（Step 1 で `semaphore.h` に移した）。
`INDEX_SEM`/`get_semcb`（`:109-113`）は**そのまま残す**。

`#ifdef TOPPERS_semini` ブロック（`semaphore.c:118-136`）を次で置換する：

```c
#ifdef TOPPERS_semini

/*
 *  使用していないセマフォ管理ブロックのリスト
 *
 *  SEMCBの先頭フィールドがQUEUE（wait_queue）なので，そのまま
 *  free-listのリンクに流用する．acre_semは取り出した直後に
 *  queue_initializeで作り直すため，TA_TPRI（優先度順待ちキュー）と
 *  干渉しない．
 */
QUEUE	free_semcb;

void
initialize_semaphore(PCB *p_my_pcb)
{
	uint_t	i, j;
	SEMCB	*p_semcb;
	SEMINIB	*p_seminib;

	if (p_my_pcb->prcid == TOPPERS_MASTER_PRCID) {
		for (i = 0; i < tnum_ssem; i++) {
			p_semcb = p_semcb_table[i];
			queue_initialize(&(p_semcb->wait_queue));
			p_semcb->p_seminib = &(seminib_table[i]);
			p_semcb->semcnt = p_semcb->p_seminib->isemcnt;
		}

		/*
		 *  動的生成用スロットの初期化
		 *
		 *  セマフォはプロセッサ親和を持たない（SEMINIBにiprcid/affinity
		 *  が無く，SEMCBにp_pcbが無い）ため，段階2のcyc/almのような
		 *  プロセッサ判定や充填は一切不要である．本関数は元から
		 *  マスタプロセッサ限定なので，そのブロックの中で続けて初期化
		 *  する．他プロセッサへの可視性は，本関数の呼出し後の
		 *  barrier_syncが保証する（段階1のfree_tcbと同じ論証）．
		 */
		queue_initialize(&free_semcb);
		for (j = 0; i < tnum_sem; i++, j++) {
			p_semcb = p_semcb_table[i];
			p_seminib = &(aseminib_table[j]);
			p_seminib->sematr = TA_NOEXS;
			p_semcb->p_seminib = ((const SEMINIB *) p_seminib);
			queue_insert_prev(&free_semcb, &(p_semcb->wait_queue));
		}
	}
}

#endif /* TOPPERS_semini */
```

（**FIFO**＝`queue_insert_prev` で末尾へ。Constraint 7 のとおり LIFO 化しない。
`i` は静的ループから引き継ぐ＝dcre `semaphore.c:149,156` と同じ書き方。
`semcnt` は動的スロットでは設定しない — `acre_sem` が設定する。dcre も同じ。）

- [ ] **Step 3: `kernel/semaphore.c` に `acre_sem` を追加**（`TOPPERS_semini` 区画の直後、
  `TOPPERS_sig_sem` 区画の直前）

```c
/*
 *  セマフォの生成［NGKI1453］
 */
#ifdef TOPPERS_acre_sem

#ifndef LOG_ACRE_SEM_ENTER
#define LOG_ACRE_SEM_ENTER(pk_csem)
#endif /* LOG_ACRE_SEM_ENTER */

#ifndef LOG_ACRE_SEM_LEAVE
#define LOG_ACRE_SEM_LEAVE(ercd)
#endif /* LOG_ACRE_SEM_LEAVE */

ER_ID
acre_sem(const T_CSEM *pk_csem)
{
	SEMCB	*p_semcb;
	SEMINIB	*p_seminib;
	ATR		sematr;
	uint_t	isemcnt, maxsem;
	ER		ercd;

	LOG_ACRE_SEM_ENTER(pk_csem);
	CHECK_TSKCTX_UNL();							/*［NGKI1454］［NGKI1455］*/

	sematr = pk_csem->sematr;
	isemcnt = pk_csem->isemcnt;
	maxsem = pk_csem->maxsem;

	CHECK_VALIDATR(sematr, TA_TPRI);			/*［NGKI1456］*/
	CHECK_PAR(isemcnt <= maxsem);				/*［NGKI1466］*/
	CHECK_PAR(1 <= maxsem && maxsem <= TMAX_MAXSEM);	/*［NGKI1468］*/

	lock_cpu();
	acquire_glock();
	if (tnum_sem == tnum_ssem || queue_empty(&free_semcb)) {
		ercd = E_NOID;							/*［NGKI1462］*/
	}
	else {										/*［NGKI5189］*/
		p_semcb = ((SEMCB *) queue_delete_next(&free_semcb));
		p_seminib = (SEMINIB *)(p_semcb->p_seminib);
		p_seminib->sematr = sematr;
		p_seminib->isemcnt = isemcnt;
		p_seminib->maxsem = maxsem;

		queue_initialize(&(p_semcb->wait_queue));	/*［NGKI1464］*/
		p_semcb->semcnt = p_semcb->p_seminib->isemcnt;
		ercd = SEMID(p_semcb);
	}
	release_glock();
	unlock_cpu();

  error_exit:
	LOG_ACRE_SEM_LEAVE(ercd);
	return(ercd);
}

#endif /* TOPPERS_acre_sem */
```

dcre（`semaphore.c:170-214`）からの適応点は**3つだけ**：
1. `lock_cpu()` の直後に `acquire_glock()`／末尾に `release_glock()`（FMP3 の giant lock 規約）。
2. 空判定を `tnum_sem == 0` → `tnum_sem == tnum_ssem`（FMP3 は静的分が別レンジのため）。
3. ★訂正E：`CHECK_PAR(0 <= isemcnt && isemcnt <= maxsem)` の**恒真な前半を落とす**
   （`isemcnt` は `uint_t`。段階2 の `acre_cyc` の `0 <= cycphs` と同型の逸脱）。
   `1 <= maxsem`（unsigned で `maxsem != 0` と等価・恒真でない）と
   `maxsem <= TMAX_MAXSEM`（`TMAX_MAXSEM` はターゲット可変シンボル）は**残す**。

**★書いてはいけないもの**（Constraint 4）：`p_seminib->iprcid = ...` / `->affinity = ...` /
`p_semcb->p_pcb = ...`。SEMINIB にも SEMCB にもこれらのフィールドは存在しない。

- [ ] **Step 4: `kernel/semaphore.c` に `del_sem` を追加**（`acre_sem` の直後）

```c
/*
 *  セマフォの削除［NGKI1487］
 */
#ifdef TOPPERS_del_sem

#ifndef LOG_DEL_SEM_ENTER
#define LOG_DEL_SEM_ENTER(semid)
#endif /* LOG_DEL_SEM_ENTER */

#ifndef LOG_DEL_SEM_LEAVE
#define LOG_DEL_SEM_LEAVE(ercd)
#endif /* LOG_DEL_SEM_LEAVE */

ER
del_sem(ID semid)
{
	SEMCB	*p_semcb;
	SEMINIB	*p_seminib;
	ER		ercd;
	TCB		*p_selftsk;
	PCB		*p_my_pcb;

	LOG_DEL_SEM_ENTER(semid);
	CHECK_TSKCTX_UNL_MYSTATE(&p_selftsk);		/*［NGKI1488］［NGKI1489］*/
	CHECK_ID(VALID_SEMID(semid));				/*［NGKI1490］*/
	p_semcb = get_semcb(semid);

	lock_cpu();
	acquire_glock();
	p_my_pcb = get_my_pcb();
	if (p_semcb->p_seminib->sematr == TA_NOEXS) {
		ercd = E_NOEXS;							/*［NGKI1491］*/
	}
	else if (semid <= tmax_ssemid) {
		ercd = E_OBJ;							/*［NGKI1493］*/
	}
	else {
		/*
		 *  待ちタスクがいても削除は成功し，待ちタスクはE_DLTで強制
		 *  解除される［NGKI1495］［NGKI1496］．init_wait_queueは
		 *  MP対応済み（wait.c:215-228）で，既存のini_semと同一の機構
		 *  である．新規の解除機構は書かない．
		 */
		init_wait_queue(p_my_pcb, &(p_semcb->wait_queue));
		p_seminib = (SEMINIB *)(p_semcb->p_seminib);
		p_seminib->sematr = TA_NOEXS;
		queue_insert_prev(&free_semcb, &(p_semcb->wait_queue));	/*［NGKI1494］*/
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
	LOG_DEL_SEM_LEAVE(ercd);
	return(ercd);
}

#endif /* TOPPERS_del_sem */
```

★**順序が重要**：`init_wait_queue` で待ちキューを空にした**後**に
`queue_insert_prev(&free_semcb, &(p_semcb->wait_queue))` で同じ `wait_queue` を
free-list へ繋ぐ。逆順にすると free-list のリンクを `init_wait_queue` が壊す。
dcre（`semaphore.c:241-244`）も同じ順序である。

dcre からの適応点は**3つ**：(1) glock の対化、(2) ★訂正C＝`CHECK_TSKCTX_UNL()` +
`p_runtsk` を `CHECK_TSKCTX_UNL_MYSTATE(&p_selftsk)` + `p_selftsk != p_my_pcb->p_schedtsk` に
（FMP3 の `ini_flg`/`ini_mtx` の流儀）、(3) `init_wait_queue` に `p_my_pcb` 引数が付く。

- [ ] **Step 5: E_NOEXS 検査の挿入（semaphore.c の6関数）**

段階1/2 と同じ **existence-before-state** 規約：`acquire_glock()`（および直後の
`p_my_pcb = get_my_pcb();` があればその後）の**直後・既存の状態判定の最初の分岐**として
挿入し、既存本体は `else if` / `else` 連鎖へ**字下げのみ**で繰り込む
（★既存ロジックの**バイト保存**。式や順序を変えない）。

判定式はすべて `p_semcb->p_seminib->sematr == TA_NOEXS`。

| 関数 | 現在の定義行 | 挿入後の形 |
|---|---|---|
| `sig_sem` | `semaphore.c:144` | `if (TA_NOEXS) { E_NOEXS } else { if (!queue_empty…) {…} else if (semcnt < maxsem) {…} else { E_QOVR } }` |
| `wai_sem` | `:201` | `if (TA_NOEXS) { E_NOEXS } else { if (raster) {…} else if (semcnt>=1) {…} else {…wobj_make_wait…} }` |
| `pol_sem` | `:247` | `if (TA_NOEXS) { E_NOEXS } else { if (semcnt>=1) {…} else { E_TMOUT } }` |
| `twai_sem` | `:282` | `if (TA_NOEXS) { E_NOEXS } else { if (raster) … else if … else if (tmout==TMO_POL) … else {…} }` |
| `ini_sem` | `:333` | `if (TA_NOEXS) { E_NOEXS } else { init_wait_queue…; semcnt = isemcnt; if (p_runtsk != p_schedtsk) {…} ercd = E_OK; }` |
| `ref_sem` | `:374` | `if (TA_NOEXS) { E_NOEXS } else { pk_rsem->wtskid = …; pk_rsem->semcnt = …; ercd = E_OK; }` |

★具体例（`sig_sem`。`acquire_glock(); p_my_pcb = get_my_pcb();` の直後を置換）：

```c
	if (p_semcb->p_seminib->sematr == TA_NOEXS) {
		ercd = E_NOEXS;
	}
	else if (!queue_empty(&(p_semcb->wait_queue))) {
		p_tcb = (TCB *) queue_delete_next(&(p_semcb->wait_queue));
		wait_complete(p_my_pcb, p_tcb);	/*［NGKI1505］［NGKI1506］［NGKI1507］*/
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
	else if (p_semcb->semcnt < p_semcb->p_seminib->maxsem) {
		p_semcb->semcnt += 1;					/*［NGKI1508］*/
		ercd = E_OK;
	}
	else {
		ercd = E_QOVR;							/*［NGKI1509］*/
	}
```
（＝既存の `if` を `else if` に変えて先頭に E_NOEXS 分岐を足しただけ。**本体は無改変**。）

★具体例（`ini_sem`。既存本体は `if` 連鎖でないため `else { … }` で丸ごと包む）：

```c
	if (p_semcb->p_seminib->sematr == TA_NOEXS) {
		ercd = E_NOEXS;
	}
	else {
		init_wait_queue(p_my_pcb, &(p_semcb->wait_queue));
													/*［NGKI1533］［NGKI1534］*/
		p_semcb->semcnt = p_semcb->p_seminib->isemcnt;	/*［NGKI1532］*/
		if (p_my_pcb->p_runtsk != p_my_pcb->p_schedtsk) {
			release_glock();
			dispatch();
			ercd = E_OK;
			goto unlock_and_exit;
		}
		ercd = E_OK;
	}
```
（`ini_sem` の既存の `p_my_pcb->p_runtsk` 比較は**そのまま残す** — 訂正C は `del_*` に対する
決定であって既存関数を書き換える指示ではない。既存コードのバイト保存を優先する。）

**除外**: `initialize_semaphore`（ID を取らない）。**`acre_sem`/`del_sem` 自身**
（`del_sem` は E_NOEXS を自前で持ち、`acre_sem` は ID を取らない）。
→ **semaphore.c で E_NOEXS を挿入するのは上表の6関数だけ。FMP3 固有関数は無い**
（段階2 の `msta_cyc`/`msta_alm` のような「上流に先例が無い類推適用」は本 Task では発生しない）。

- [ ] **Step 6: 配線（allfunc.h / Makefile.kernel）**

`kernel/allfunc.h` の `/* semaphore.c */` 節（`:134-141`）の `#define TOPPERS_semini` の直後に2行：
```c
#define TOPPERS_acre_sem
#define TOPPERS_del_sem
```
（ALLFUNC ビルド＝`CMakeLists.txt:562` で全区画を有効化するため、**ここに書かないと
関数が1つもコンパイルされない**。）

`kernel/Makefile.kernel:86-87` を次に変更（上流形式の維持。CMake は参照しない）：
```
semaphore = semini.o acre_sem.o del_sem.o sig_sem.o \
		wai_sem.o pol_sem.o twai_sem.o ini_sem.o ref_sem.o
```
**`KERNEL_FCSRCS`（`Makefile.kernel:51-56`）は触らない**（22個のまま）。

- [ ] **Step 7: rename 追加・再生成**

`kernel/kernel_rename.def` の `# semaphore.c` 節（`:64-65`）を次に置換する：

```
# semaphore.c
initialize_semaphore
free_semcb
tmax_ssemid
aseminib_table

```
（`acre_sem`/`del_sem` は**公開名なのでリネームしない**＝段階2 の `acre_cyc` と同じ。
`tmax_ssemid`/`aseminib_table` は cfg が `_kernel_tmax_ssemid`/`_kernel_aseminib_table` として
出力する＝`kernel.py:177,254` の `_kernel_tmax_s{obj}id` / `_kernel_{array}`。）

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core/kernel && ruby ../utils/genrename.rb kernel && cd ..
git diff --stat kernel/kernel_rename.h kernel/kernel_unrename.h   # 期待: 各 +3 行・削除0
grep -n "free_semcb\|tmax_ssemid\|aseminib_table" kernel/kernel_rename.h kernel/kernel_unrename.h
```
期待: `_kernel_free_semcb` / `_kernel_tmax_ssemid` / `_kernel_aseminib_table` が実在。
**削除行が1行でもあれば不合格**（genrename の実行位置ミス等を疑う）。

- [ ] **Step 8: 全8構成ビルド + 等価性**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
for p in musca_b1 musca_b1-2core rp2350_pico2 polarfire_soc_kit-qemu \
         kria_arm64 kria_arm64-1core kria_r5 kria_r5-2core; do
  cmake --build build/$p > /tmp/dcre3-t4-build-$p.log 2>&1; echo "$p build rc=$?"
  tools/cfg_equivalence.sh build/$p > /tmp/dcre3-t4-eq-$p.log 2>&1; echo "$p eq rc=$?"
done
cmake --build build/musca_b1-2core-tmix > /tmp/dcre3-t4-build-tmix.log 2>&1; echo "tmix build rc=$?"
tools/cfg_equivalence.sh build/musca_b1-2core-tmix > /tmp/dcre3-t4-eq-tmix.log 2>&1; echo "tmix eq rc=$?"
```
期待: 全て rc=0。**exit=2 は不合格**。
`build/musca_b1-2core-tmix` は AID_SEM(2) を含む実構成なので、
ここで `acre_sem`/`del_sem` が**実際にリンクされる**ことも同時に検査している。

- [ ] **Step 9: QEMU 起動（非退行）— musca_b1-2core**

```bash
timeout -k 5 25 cmake --build build/musca_b1-2core --target run > /tmp/dcre3-t4-run-musca.log 2>&1
grep -c 'Processor [12] start\.' /tmp/dcre3-t4-run-musca.log     # 期待: 2
grep -c 'Sample program starts' /tmp/dcre3-t4-run-musca.log      # 参考: サンプル走行の証拠
pgrep -a qemu                                                    # 期待: 何も出ない
```
★`sample1` は静的セマフォを使う（`grep -n "CRE_SEM" sample/sample1.cfg`）ので、
`initialize_semaphore` の静的ループ境界を `tnum_sem` → `tnum_ssem` に変えた影響が
AID 無し構成に無いことの実証になっている。
`pgrep` が qemu を出したら `pkill -f qemu-system-arm` で掃除してから次へ進む。

- [ ] **Step 10: 台帳とコミット**

`DIVERGENCE_MAP.md` に次の行を追記する（種別はすべて `mod (dcre-port)`）：
- `kernel/semaphore.h（dcre段階3a Task 4）` — 理由：「`tmax_ssemid`／`QUEUE free_semcb`／
  `aseminib_table[]` の extern を追加。`tnum_sem` を `semaphore.c` から移設し `tnum_ssem` を新設。
  既存の `SEMID(p_semcb)` マクロを段階2 の `CYCID` と同型の**2レンジ版へ置換**（新設ではない）。
  AID 無し構成では `tnum_sem == tnum_ssem` となり従来と同一の式に落ちる＝挙動不変。
  `#include <queue.h>` は元から `:51` にあるため追加不要（cyclic.h とは異なる）」
- `kernel/semaphore.c（dcre段階3a Task 4）` — 理由：「`#define tnum_sem` を削除（`.h` へ移設）。
  `QUEUE free_semcb;` の定義を追加。`initialize_semaphore` の静的ループ境界を
  `tnum_sem` → `tnum_ssem` に変更し、**既存のマスタ限定ブロックの中に**動的スロット初期化
  （`queue_initialize(&free_semcb)`、`aseminib_table[j].sematr = TA_NOEXS`、
  `queue_insert_prev` で FIFO 挿入）を追加。**セマフォは非親和オブジェクトのため
  `iprcid`/`affinity`/`p_pcb` の充填やプロセッサ判定は一切行っていない**（段階2 cyc/alm との相違）。
  `acre_sem`/`del_sem` を追加（dcre `semaphore.c:170-214`/`:219-257` の転写）。
  dcre からの意図的な逸脱3件：(1) glock の対化、(2) 空判定 `tnum_sem == 0` →
  `tnum_sem == tnum_ssem`、(3) `del_sem` の `CHECK_TSKCTX_UNL()`＋`p_runtsk` を
  `CHECK_TSKCTX_UNL_MYSTATE(&p_selftsk)`＋`p_selftsk != p_my_pcb->p_schedtsk` へ
  （FMP3 の `ini_flg`/`ini_mtx` の流儀に合わせた。`check_tskctx_unl_mystate` は
  `check_tskctx_unl` と E_CTX 判定が厳密に等価＝`check.h:177-199`）。
  さらに `acre_sem` の範囲検査は dcre の
  `CHECK_PAR(0 <= isemcnt && isemcnt <= maxsem)` から**恒真条件 `0 <= isemcnt` を落として**
  `CHECK_PAR(isemcnt <= maxsem)` としている（`uint_t` に対する意味的にヌルな比較。
  段階2 `acre_cyc` の `0 <= cycphs` と同型の意図的逸脱・意味は同一）。
  `sig_sem`/`wai_sem`/`pol_sem`/`twai_sem`/`ini_sem`/`ref_sem` の6関数に
  existence-before-state 規約の E_NOEXS 分岐を挿入（既存ロジックは字下げのみで byte-preserve）」
- `kernel/allfunc.h（dcre段階3a Task 4）`（既存行の理由欄に「`/* semaphore.c */` 節に
  `TOPPERS_acre_sem`/`TOPPERS_del_sem` を追加」を追記）
- `kernel/Makefile.kernel（dcre段階3a Task 4）`（同、「`semaphore =` 行に
  `acre_sem.o`/`del_sem.o` を追加。`KERNEL_FCSRCS` は不変」）
- `kernel/kernel_rename.def（dcre段階3a Task 4）` — 理由：「`# semaphore.c` 節に
  `free_semcb`/`tmax_ssemid`/`aseminib_table` の3エントリを追加」
- `kernel/kernel_rename.h・kernel_unrename.h（dcre段階3a Task 4）` — 種別
  `mod (dcre-port, 生成物)`、理由：「`utils/genrename.rb kernel` で再生成（手編集ではない）。
  各+3行・削除0行」

```bash
git add -A && git commit -m "feat(kernel): acre_sem/del_sem・free_semcb・2レンジSEMID とE_NOEXS検査6箇所（dcre段階3a）"
```

---

### Task 5: eventflag 層 — free_flgcb・initialize・2レンジ FLGID・acre_flg/del_flg・E_NOEXS ×7

**推奨モデル:** 中位（sonnet）

**Files:**
- Modify: `kernel/eventflag.h`（externs / `tnum_flg`・`tnum_sflg` 移設 / `FLGID` 置換）
- Modify: `kernel/eventflag.c`（`tnum_flg` 重複削除 / `free_flgcb` / `initialize_eventflag` /
  `acre_flg` / `del_flg` / E_NOEXS ×7）
- Modify: `kernel/allfunc.h`（`/* eventflag.c */` 節に2行）
- Modify: `kernel/Makefile.kernel`（`eventflag =` 行に .o 2個）
- Modify: `kernel/kernel_rename.def` →（再生成）`kernel_rename.h` `kernel_unrename.h`
- Modify: `DIVERGENCE_MAP.md`

**Interfaces:**
- Consumes: Task 3 の `_kernel_tmax_sflgid` / `_kernel_aflginib_table` / 予約 FLGCB /
  `T_CFLG` / `acre_flg`・`del_flg` の宣言。Task 1 の確認結果表（訂正B/C/D、転写元行範囲）。
- Produces: `QUEUE free_flgcb` / `tmax_sflgid` / `tnum_sflg` / `aflginib_table[]` /
  2レンジ `FLGID(p_flgcb)` / `ER_ID acre_flg(const T_CFLG *)` / `ER del_flg(ID)`。Task 7 が使う。

**★本 Task は Task 4 と同型だが、「sem と同様に」で済ませない。** flg 固有の相違が3点ある：
(a) E_NOEXS の対象は**7関数**（`clr_flg` が増える）、(b) `del_flg` は dcre が `CHECK_PAR` を
使っている（★訂正D で `CHECK_ID` に直す）、(c) `check_flg_cond`（`TOPPERS_flgcnd` 区画）は
ID を取らない内部関数なので E_NOEXS 挿入の対象外。

**AID 無し構成の挙動は不変である**（`tnum_sflg == tnum_flg`）。

- [ ] **Step 1: `kernel/eventflag.h` — extern 追加・`tnum_*` 移設・`FLGID` の2レンジ置換**

`extern const ID tmax_flgid;` のブロック（`eventflag.h:79-82`）を次に置換：

```c
/*
 *  イベントフラグIDの最大値（kernel_cfg.c）
 */
extern const ID	tmax_flgid;
extern const ID	tmax_sflgid;		/* 静的生成イベントフラグのID番号の最大値 */

/*
 *  使用していないイベントフラグ管理ブロックのリスト（eventflag.c）
 *
 *  FLGCBの先頭フィールドがQUEUE（wait_queue）なので，そのまま
 *  free-listのリンクに流用する（dcre eventflag.c:157-170 と同一）．
 *  段階2のcyc/almで用いたtmevtb領域のオーバレイは不要である．
 */
extern QUEUE	free_flgcb;
```

`extern const FLGINIB flginib_table[];` のブロック（`eventflag.h:84-87`）の**直後**に追加：

```c
/*
 *  動的生成イベントフラグの初期化ブロックのエリア（kernel_cfg.c・RAM）
 */
extern FLGINIB	aflginib_table[];
```

`extern FLGCB *const p_flgcb_table[];`（`eventflag.h:92`）の**直後**、
既存の `FLGID` マクロブロック（`eventflag.h:94-98`）を次で**置換**する：

```c
/*
 *  イベントフラグの数
 *
 *  FLGIDマクロから参照するためeventflag.cから移設した．
 */
#define tnum_flg	((uint_t)(tmax_flgid - TMIN_FLGID + 1))
#define tnum_sflg	((uint_t)(tmax_sflgid - TMIN_FLGID + 1))

/*
 *  イベントフラグ管理ブロックからイベントフラグIDを取り出すためのマクロ
 *
 *  FMP3のFLGCBはポインタ表（p_flgcb_table）経由で参照される個別の
 *  named staticであり，FLGCB自身の配列位置から番号を引けない．元から
 *  FLGINIBへのポインタ差分で求めていた式を，動的生成イベントフラグ
 *  （p_flginibがaflginib_tableを指す）と静的生成イベントフラグ
 *  （p_flginibがflginib_tableを指す）の2レンジに拡張する
 *  （段階2のCYCIDと同型）．AID_FLGが無い構成ではtnum_flg == tnum_sflg
 *  となり第1項が常に偽＝従来と同一の式に落ちる．
 */
#define	FLGID(p_flgcb) \
	((((p_flgcb)->p_flginib >= aflginib_table) \
		&& ((p_flgcb)->p_flginib < &aflginib_table[tnum_flg - tnum_sflg])) \
	  ? ((ID)(((p_flgcb)->p_flginib - aflginib_table) + TMIN_FLGID + tnum_sflg)) \
	  : ((ID)(((p_flgcb)->p_flginib - flginib_table) + TMIN_FLGID)))
```

★`#include <queue.h>` は `eventflag.h:51` に**既にある**ので追加不要（Task 1 Step 7 で確認済み）。

- [ ] **Step 2: `kernel/eventflag.c` — `tnum_flg` の重複削除と `initialize_eventflag` の改造**

`eventflag.c:112-115` の

```c
/*
 *  イベントフラグの数
 */
#define tnum_flg	((uint_t)(tmax_flgid - TMIN_FLGID + 1))
```

を**削除**する（Step 1 で `eventflag.h` に移した）。
`INDEX_FLG`/`get_flgcb`（`:117-121`）は**そのまま残す**。

`#ifdef TOPPERS_flgini` ブロック（`eventflag.c:126-144`）を次で置換する：

```c
#ifdef TOPPERS_flgini

/*
 *  使用していないイベントフラグ管理ブロックのリスト
 *
 *  FLGCBの先頭フィールドがQUEUE（wait_queue）なので，そのまま
 *  free-listのリンクに流用する．acre_flgは取り出した直後に
 *  queue_initializeで作り直すため，TA_TPRI（優先度順待ちキュー）と
 *  干渉しない．
 */
QUEUE	free_flgcb;

void
initialize_eventflag(PCB *p_my_pcb)
{
	uint_t	i, j;
	FLGCB	*p_flgcb;
	FLGINIB	*p_flginib;

	if (p_my_pcb->prcid == TOPPERS_MASTER_PRCID) {
		for (i = 0; i < tnum_sflg; i++) {
			p_flgcb = p_flgcb_table[i];
			queue_initialize(&(p_flgcb->wait_queue));
			p_flgcb->p_flginib = &(flginib_table[i]);
			p_flgcb->flgptn = p_flgcb->p_flginib->iflgptn;
		}

		/*
		 *  動的生成用スロットの初期化
		 *
		 *  イベントフラグはプロセッサ親和を持たない（FLGINIBに
		 *  iprcid/affinityが無く，FLGCBにp_pcbが無い）ため，段階2の
		 *  cyc/almのようなプロセッサ判定や充填は一切不要である．本関数は
		 *  元からマスタプロセッサ限定なので，そのブロックの中で続けて
		 *  初期化する．他プロセッサへの可視性は，本関数の呼出し後の
		 *  barrier_syncが保証する（段階1のfree_tcbと同じ論証）．
		 */
		queue_initialize(&free_flgcb);
		for (j = 0; i < tnum_flg; i++, j++) {
			p_flgcb = p_flgcb_table[i];
			p_flginib = &(aflginib_table[j]);
			p_flginib->flgatr = TA_NOEXS;
			p_flgcb->p_flginib = ((const FLGINIB *) p_flginib);
			queue_insert_prev(&free_flgcb, &(p_flgcb->wait_queue));
		}
	}
}

#endif /* TOPPERS_flgini */
```

（`flgptn` は動的スロットでは設定しない — `acre_flg` が設定する。dcre `eventflag.c:163-170` も同じ。）

- [ ] **Step 3: `kernel/eventflag.c` に `acre_flg` を追加**（`TOPPERS_flgcnd` 区画の直後、
  `TOPPERS_set_flg` 区画の直前）

★置き場所が sem と違う（sem は `TOPPERS_semini` の直後）。dcre も
`TOPPERS_flgcnd`（`:178-194`）の後に `TOPPERS_acre_flg`（`:203`）を置いている。
**`check_flg_cond` を跨がないこと**（跨ぐと `acre_flg` が `check_flg_cond` を見えなくする
わけではないが、dcre との構造対応が崩れて後の追従マージで衝突しやすくなる）。

```c
/*
 *  イベントフラグの生成
 *
 *  pk_cflg->iflgptnは，エラーチェックをせず，一度しか参照しないため，
 *  ローカル変数にコピーする必要がない（途中で書き換わっても支障がな
 *  い）．
 */
#ifdef TOPPERS_acre_flg

#ifndef LOG_ACRE_FLG_ENTER
#define LOG_ACRE_FLG_ENTER(pk_cflg)
#endif /* LOG_ACRE_FLG_ENTER */

#ifndef LOG_ACRE_FLG_LEAVE
#define LOG_ACRE_FLG_LEAVE(ercd)
#endif /* LOG_ACRE_FLG_LEAVE */

ER_ID
acre_flg(const T_CFLG *pk_cflg)
{
	FLGCB	*p_flgcb;
	FLGINIB	*p_flginib;
	ATR		flgatr;
	ER		ercd;

	LOG_ACRE_FLG_ENTER(pk_cflg);
	CHECK_TSKCTX_UNL();

	flgatr = pk_cflg->flgatr;

	CHECK_VALIDATR(flgatr, TA_TPRI|TA_WMUL|TA_CLR);

	lock_cpu();
	acquire_glock();
	if (tnum_flg == tnum_sflg || queue_empty(&free_flgcb)) {
		ercd = E_NOID;
	}
	else {
		p_flgcb = ((FLGCB *) queue_delete_next(&free_flgcb));
		p_flginib = (FLGINIB *)(p_flgcb->p_flginib);
		p_flginib->flgatr = flgatr;
		p_flginib->iflgptn = pk_cflg->iflgptn;

		queue_initialize(&(p_flgcb->wait_queue));
		p_flgcb->flgptn = p_flgcb->p_flginib->iflgptn;
		ercd = FLGID(p_flgcb);
	}
	release_glock();
	unlock_cpu();

  error_exit:
	LOG_ACRE_FLG_LEAVE(ercd);
	return(ercd);
}

#endif /* TOPPERS_acre_flg */
```

dcre（`eventflag.c:203-241`）からの適応点は**2つだけ**：
(1) glock の対化、(2) 空判定 `tnum_flg == 0` → `tnum_flg == tnum_sflg`。
★訂正E に相当する恒真 `CHECK_PAR` は `acre_flg` には**無い**（範囲検査自体が無い）。

**★書いてはいけないもの**（Constraint 4）：`p_flginib->iprcid` / `->affinity` / `p_flgcb->p_pcb`。

- [ ] **Step 4: `kernel/eventflag.c` に `del_flg` を追加**（`acre_flg` の直後）

```c
/*
 *  イベントフラグの削除
 */
#ifdef TOPPERS_del_flg

#ifndef LOG_DEL_FLG_ENTER
#define LOG_DEL_FLG_ENTER(flgid)
#endif /* LOG_DEL_FLG_ENTER */

#ifndef LOG_DEL_FLG_LEAVE
#define LOG_DEL_FLG_LEAVE(ercd)
#endif /* LOG_DEL_FLG_LEAVE */

ER
del_flg(ID flgid)
{
	FLGCB	*p_flgcb;
	FLGINIB	*p_flginib;
	ER		ercd;
	TCB		*p_selftsk;
	PCB		*p_my_pcb;

	LOG_DEL_FLG_ENTER(flgid);
	CHECK_TSKCTX_UNL_MYSTATE(&p_selftsk);
	CHECK_ID(VALID_FLGID(flgid));
	p_flgcb = get_flgcb(flgid);

	lock_cpu();
	acquire_glock();
	p_my_pcb = get_my_pcb();
	if (p_flgcb->p_flginib->flgatr == TA_NOEXS) {
		ercd = E_NOEXS;
	}
	else if (flgid <= tmax_sflgid) {
		ercd = E_OBJ;
	}
	else {
		/*
		 *  待ちタスクがいても削除は成功し，待ちタスクはE_DLTで強制
		 *  解除される．init_wait_queueはMP対応済み（wait.c:215-228）で，
		 *  既存のini_flgと同一の機構である．新規の解除機構は書かない．
		 */
		init_wait_queue(p_my_pcb, &(p_flgcb->wait_queue));
		p_flginib = (FLGINIB *)(p_flgcb->p_flginib);
		p_flginib->flgatr = TA_NOEXS;
		queue_insert_prev(&free_flgcb, &(p_flgcb->wait_queue));
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
	LOG_DEL_FLG_LEAVE(ercd);
	return(ercd);
}

#endif /* TOPPERS_del_flg */
```

★**順序が重要**：`init_wait_queue` → `flgatr = TA_NOEXS` → `queue_insert_prev` の順。
dcre（`eventflag.c:268-271`）と同一。

dcre からの適応点は**4つ**：(1) glock の対化、(2) ★訂正C＝`CHECK_TSKCTX_UNL()` +
`p_runtsk` を `CHECK_TSKCTX_UNL_MYSTATE(&p_selftsk)` + `p_selftsk != p_my_pcb->p_schedtsk` に、
(3) `init_wait_queue` に `p_my_pcb` 引数が付く、
(4) ★訂正D＝dcre `eventflag.c:257` の **`CHECK_PAR(VALID_FLGID(flgid))`（E_PAR）を
`CHECK_ID(VALID_FLGID(flgid))`（E_ID）に直す**。dcre 自身の不整合であり
（`del_sem`/`del_mtx` は `CHECK_ID`）、FMP3 の flg 系サービスコールは全て `CHECK_ID` である。

- [ ] **Step 5: E_NOEXS 検査の挿入（eventflag.c の7関数）**

判定式はすべて `p_flgcb->p_flginib->flgatr == TA_NOEXS`。
挿入位置は `acquire_glock()`（および直後の `p_my_pcb = get_my_pcb();` があればその後）の**直後**、
既存の状態判定の**最初の分岐**。既存本体は `else if` / `else` へ**字下げのみ**で繰り込む。

| 関数 | 現在の定義行 | 既存本体の形 | 挿入後 |
|---|---|---|---|
| `set_flg` | `eventflag.c:173` | 単文列（`p_flgcb->flgptn \|= setptn;` からの走査ループ） | `else { … }` で丸ごと包む |
| `clr_flg` | `:236` | `p_flgcb->flgptn &= clrptn; ercd = E_OK;` | `else { … }` |
| `wai_flg` | `:266` | `if (raster) … else if (TA_WMUL…) … else if (check_flg_cond…) … else {…}` | 先頭に `if (TA_NOEXS)`、既存 `if` を `else if` に |
| `pol_flg` | `:324` | `if (TA_WMUL…) … else if (check_flg_cond…) … else { E_TMOUT }` | 同上 |
| `twai_flg` | `:364` | `if (raster) … else if … else if … else if (tmout==TMO_POL) … else {…}` | 同上 |
| `ini_flg` | `:427` | 単文列（`init_wait_queue`／`flgptn = iflgptn`／dispatch 判定） | `else { … }` |
| `ref_flg` | `:468` | `pk_rflg->wtskid = …; pk_rflg->flgptn = …; ercd = E_OK;` | `else { … }` |

★具体例（`clr_flg`。`acquire_glock();` の直後を置換）：

```c
	if (p_flgcb->p_flginib->flgatr == TA_NOEXS) {
		ercd = E_NOEXS;
	}
	else {
		p_flgcb->flgptn &= clrptn; 
		ercd = E_OK;
	}
```
（★既存行 `p_flgcb->flgptn &= clrptn; ` の**行末の空白1文字も含めてバイト保存**する。
`sed` や整形ツールで trailing space を落とさないこと。落とすと diff が汚れ、
「既存ロジックは字下げのみ」という主張が崩れる。）

★具体例（`pol_flg`。既存の `if` を `else if` に変えて先頭に足す）：

```c
	if (p_flgcb->p_flginib->flgatr == TA_NOEXS) {
		ercd = E_NOEXS;
	}
	else if ((p_flgcb->p_flginib->flgatr & TA_WMUL) == 0U
					&& !queue_empty(&(p_flgcb->wait_queue))) {
		ercd = E_ILUSE;
	}
	else if (check_flg_cond(p_flgcb, waiptn, wfmode, p_flgptn)) {
		ercd = E_OK;
	}
	else {
		ercd = E_TMOUT;
	}
```

★具体例（`ini_flg`。既存本体を `else { … }` で丸ごと包む）：

```c
	if (p_flgcb->p_flginib->flgatr == TA_NOEXS) {
		ercd = E_NOEXS;
	}
	else {
		init_wait_queue(p_my_pcb, &(p_flgcb->wait_queue));
		p_flgcb->flgptn = p_flgcb->p_flginib->iflgptn;
		if (p_selftsk != p_my_pcb->p_schedtsk) {
			release_glock();
			dispatch();
			ercd = E_OK;
			goto unlock_and_exit;
		}
		ercd = E_OK;
	}
```

★`set_flg`（`:170-228`）は本体が長い（待ちキュー走査ループ + `wait_complete` +
`request_dispatch_retint` 等）。**中身を1文字も変えずに `else { … }` で包み、
インデントを1段深くするだけ**にすること。`goto unlock_and_exit;` を含む
制御フローはそのまま残る（`else` ブロックの外にラベルがある）。

**除外**: `initialize_eventflag`、`check_flg_cond`（`TOPPERS_flgcnd`。ID を取らない内部関数）、
`acre_flg`（ID を取らない）、`del_flg`（自前で E_NOEXS を持つ）。
→ **eventflag.c で E_NOEXS を挿入するのは上表の7関数だけ。FMP3 固有関数は無い。**

- [ ] **Step 6: 配線（allfunc.h / Makefile.kernel）**

`kernel/allfunc.h` の `/* eventflag.c */` 節（`:143-152`）の `#define TOPPERS_flgcnd` の直後に2行：
```c
#define TOPPERS_acre_flg
#define TOPPERS_del_flg
```

`kernel/Makefile.kernel:89-90` を次に変更：
```
eventflag = flgini.o flgcnd.o acre_flg.o del_flg.o set_flg.o clr_flg.o \
		wai_flg.o pol_flg.o twai_flg.o ini_flg.o ref_flg.o
```
**`KERNEL_FCSRCS` は触らない**（22個のまま）。

- [ ] **Step 7: rename 追加・再生成**

`kernel/kernel_rename.def` の `# eventflag.c` 節（Task 4 で `# semaphore.c` 節が
3行増えているので**行番号は動いている**。`# eventflag.c` の文字列を目印にする）を
次に置換する：

```
# eventflag.c
initialize_eventflag
check_flg_cond
free_flgcb
tmax_sflgid
aflginib_table

```

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core/kernel && ruby ../utils/genrename.rb kernel && cd ..
git diff --stat kernel/kernel_rename.h kernel/kernel_unrename.h   # 期待: 各 +3 行・削除0
grep -n "free_flgcb\|tmax_sflgid\|aflginib_table" kernel/kernel_rename.h kernel/kernel_unrename.h
```
期待: `_kernel_free_flgcb` / `_kernel_tmax_sflgid` / `_kernel_aflginib_table` が実在。
**削除行があれば不合格。**
（★Task 4 のコミット後に走らせるので、diff は本 Task 分の +3 行だけになるはず。
+6 行になったら Task 4 の再生成が commit に入っていない。）

- [ ] **Step 8: 全8構成ビルド + 等価性 + 混在サンプル**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
for p in musca_b1 musca_b1-2core rp2350_pico2 polarfire_soc_kit-qemu \
         kria_arm64 kria_arm64-1core kria_r5 kria_r5-2core; do
  cmake --build build/$p > /tmp/dcre3-t5-build-$p.log 2>&1; echo "$p build rc=$?"
  tools/cfg_equivalence.sh build/$p > /tmp/dcre3-t5-eq-$p.log 2>&1; echo "$p eq rc=$?"
done
cmake --build build/musca_b1-2core-tmix > /tmp/dcre3-t5-build-tmix.log 2>&1; echo "tmix build rc=$?"
tools/cfg_equivalence.sh build/musca_b1-2core-tmix > /tmp/dcre3-t5-eq-tmix.log 2>&1; echo "tmix eq rc=$?"
```
期待: 全て rc=0。**exit=2 は不合格**。
★`test_dcre_mix.cfg` には `AID_FLG` を**書いていない**ので、
`tnum_flg == tnum_sflg` の側（`FLGID` が既存式に落ちる側）が実構成で通ることの検査になっている。

- [ ] **Step 9: QEMU 起動（非退行）— musca_b1-2core と kria_arm64**

```bash
timeout -k 5 25 cmake --build build/musca_b1-2core --target run > /tmp/dcre3-t5-run-musca.log 2>&1
grep -c 'Processor [12] start\.' /tmp/dcre3-t5-run-musca.log     # 期待: 2
pgrep -a qemu                                                    # 期待: 何も出ない
```
```bash
timeout -k 5 25 cmake --build build/kria_arm64 --target run > /tmp/dcre3-t5-run-arm64.log 2>&1
grep -c 'Processor [1-4] start\.' /tmp/dcre3-t5-run-arm64.log    # 期待: 4
pgrep -a qemu                                                    # 期待: 何も出ない
```
（kria_arm64 は 64bit ＝ ポインタ差分による2レンジ `FLGID` が最も壊れやすい構成なので、
ここで静的イベントフラグを使うサンプルが走り続けることを確認する意味がある。）

- [ ] **Step 10: 台帳とコミット**

`DIVERGENCE_MAP.md` に次の行を追記する（種別はすべて `mod (dcre-port)`）：
- `kernel/eventflag.h（dcre段階3a Task 5）` — 理由：Task 4 の `semaphore.h` 行と同型
  （`tmax_sflgid`／`QUEUE free_flgcb`／`aflginib_table[]` の extern 追加、`tnum_flg` の移設と
  `tnum_sflg` 新設、既存 `FLGID` の2レンジ版への**置換**、`#include <queue.h>` は既存で追加不要）
- `kernel/eventflag.c（dcre段階3a Task 5）` — 理由：「`#define tnum_flg` を削除（`.h` へ移設）。
  `QUEUE free_flgcb;` の定義を追加。`initialize_eventflag` の静的ループ境界を
  `tnum_flg` → `tnum_sflg` に変更し、既存のマスタ限定ブロックの中に動的スロット初期化を追加。
  **イベントフラグは非親和オブジェクトのため `iprcid`/`affinity`/`p_pcb` の充填や
  プロセッサ判定は一切行っていない**。`acre_flg`/`del_flg` を追加
  （dcre `eventflag.c:203-241`/`:246-284` の転写）。dcre からの意図的な逸脱4件：
  (1) glock の対化、(2) 空判定 `tnum_flg == 0` → `tnum_flg == tnum_sflg`、
  (3) `del_flg` の `CHECK_TSKCTX_UNL()`＋`p_runtsk` を `CHECK_TSKCTX_UNL_MYSTATE(&p_selftsk)`＋
  `p_selftsk != p_my_pcb->p_schedtsk` へ、
  (4) ★dcre `eventflag.c:257` の `CHECK_PAR(VALID_FLGID(flgid))`（E_PAR）を
  `CHECK_ID(VALID_FLGID(flgid))`（E_ID）へ — dcre 自身の不整合
  （`del_sem`/`del_mtx` は `CHECK_ID`、FMP3 の flg 系サービスコールも全て `CHECK_ID`）。
  `set_flg`/`clr_flg`/`wai_flg`/`pol_flg`/`twai_flg`/`ini_flg`/`ref_flg` の7関数に
  existence-before-state 規約の E_NOEXS 分岐を挿入（既存ロジックは字下げのみで byte-preserve、
  `clr_flg` の行末空白も保存）」
- `kernel/allfunc.h（dcre段階3a Task 5）`（既存行の理由欄に「`/* eventflag.c */` 節に
  `TOPPERS_acre_flg`/`TOPPERS_del_flg` を追加」を追記）
- `kernel/Makefile.kernel（dcre段階3a Task 5）`（同、「`eventflag =` 行に
  `acre_flg.o`/`del_flg.o` を追加」）
- `kernel/kernel_rename.def（dcre段階3a Task 5）` — 理由：「`# eventflag.c` 節に
  `free_flgcb`/`tmax_sflgid`/`aflginib_table` の3エントリを追加」
- `kernel/kernel_rename.h・kernel_unrename.h（dcre段階3a Task 5）` — 種別
  `mod (dcre-port, 生成物)`、理由：「再生成。各+3行・削除0行」

```bash
git add -A && git commit -m "feat(kernel): acre_flg/del_flg・free_flgcb・2レンジFLGID とE_NOEXS検査7箇所（dcre段階3a）"
```

---

### Task 6: mutex 層 — free_mtxcb・initialize・2レンジ MTXID・acre_mtx/del_mtx・E_NOEXS ×6

**推奨モデル:** 中位（sonnet）。段階3a で**最も難しい**タスク（`acre_mtx` の ceilpri 分岐と
`del_mtx` のロック中削除＝優先度復帰）。

**Files:**
- Modify: `kernel/mutex.h`（externs / `tnum_mtx`・`tnum_smtx` 移設 / `MTXID` 置換）
- Modify: `kernel/mutex.c`（`tnum_mtx` 重複削除 / `free_mtxcb` / `initialize_mutex` /
  `acre_mtx` / `del_mtx` / E_NOEXS ×6）
- Modify: `kernel/allfunc.h`（`/* mutex.c */` 節に2行）
- Modify: `kernel/Makefile.kernel`（`mutex =` 行に .o 2個）
- Modify: `kernel/kernel_rename.def` →（再生成）`kernel_rename.h` `kernel_unrename.h`
- Modify: `DIVERGENCE_MAP.md`

**Interfaces:**
- Consumes: Task 3 の `_kernel_tmax_smtxid` / `_kernel_amtxinib_table` / 予約 MTXCB /
  `T_CMTX` / `acre_mtx`・`del_mtx` の宣言。Task 1 の確認結果表（訂正B/C/F、
  `ini_mtx` の呼出し順、`remove_mutex`/`mutex_drop_priority` のシグネチャ）。
- Produces: `QUEUE free_mtxcb` / `tmax_smtxid` / `tnum_smtx` / `amtxinib_table[]` /
  2レンジ `MTXID(p_mtxcb)` / `ER_ID acre_mtx(const T_CMTX *)` / `ER del_mtx(ID)`。Task 7 が使う。

**★mtx 固有の相違（sem/flg と「同様」ではない点）:**
1. `initialize_mutex` はマスタ限定ブロックの冒頭で `mtxhook_check_ceilpri`/`mtxhook_release_all` を
   設定する（`mutex.c:133-134`）。**この2行より後ろに**動的スロット初期化を置く。
2. `acre_mtx` は `mtxatr == TA_CEILING` かどうかで**検査が分岐**する（`VALID_TPRI` vs `CHECK_VALIDATR`）。
3. `acre_mtx` は `ceilpri`（外部表現 `PRI`）を `INT_PRIORITY()` で**内部表現 `uint_t` へ変換**して
   `MTXINIB.ceilpri` へ入れる。
4. `del_mtx` は**ロック中でも成功**し、所有タスクのミューテックス連鎖から外して
   ceiling mutex なら優先度を復帰させる。
5. ★★**`MTX_CEILING(p_mtxcb)` は `p_mtxcb->p_mtxinib->mtxatr` を読む**（`mutex.c:117-119`）。
   したがって `del_mtx` では **`mtxatr = TA_NOEXS` を書く前に優先度復帰を済ませる**必要がある。
   順序を誤ると ceiling mutex の優先度が復帰しない（テストの手順5が検出する）。

**AID 無し構成の挙動は不変である**（`tnum_smtx == tnum_mtx`）。

- [ ] **Step 1: `kernel/mutex.h` — extern 追加・`tnum_*` 移設・`MTXID` の2レンジ置換**

`extern const ID tmax_mtxid;` のブロック（`mutex.h:80-83`）を次に置換：

```c
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
```

`extern const MTXINIB mtxinib_table[];` のブロック（`mutex.h:85-88`）の**直後**に追加：

```c
/*
 *  動的生成ミューテックスの初期化ブロックのエリア（kernel_cfg.c・RAM）
 */
extern MTXINIB	amtxinib_table[];
```

`extern MTXCB *const p_mtxcb_table[];`（`mutex.h:93`）の**直後**、
既存の `MTXID` マクロブロック（`mutex.h:95-99`）を次で**置換**する：

```c
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
```

★`#include <queue.h>` は `mutex.h:51` 付近に**既にある**（Task 1 Step 7 で確認済み）。
無ければ追加し、台帳の理由欄に書く。

- [ ] **Step 2: `kernel/mutex.c` — `tnum_mtx` の重複削除と `initialize_mutex` の改造**

`mutex.c:103-106` の

```c
/*
 *  ミューテックスの数
 */
#define tnum_mtx	((uint_t)(tmax_mtxid - TMIN_MTXID + 1))
```

を**削除**する（Step 1 で `mutex.h` に移した）。
`INDEX_MTX`/`get_mtxcb`（`:108-112`）と `MTXPROTO_MASK`/`MTXPROTO`/`MTX_CEILING`（`:114-119`）は
**そのまま残す**。

`#ifdef TOPPERS_mtxini` ブロック（`mutex.c:124-145`）を次で置換する：

```c
#ifdef TOPPERS_mtxini

/*
 *  使用していないミューテックス管理ブロックのリスト
 *
 *  MTXCBの先頭フィールドがQUEUE（wait_queue）なので，そのまま
 *  free-listのリンクに流用する．acre_mtxは取り出した直後に
 *  queue_initializeで作り直すため，TA_TPRI（優先度順待ちキュー）と
 *  干渉しない．
 */
QUEUE	free_mtxcb;

void
initialize_mutex(PCB *p_my_pcb)
{
	uint_t	i, j;
	MTXCB	*p_mtxcb;
	MTXINIB	*p_mtxinib;

	if (p_my_pcb->prcid == TOPPERS_MASTER_PRCID) {
		mtxhook_check_ceilpri = mutex_check_ceilpri;
		mtxhook_release_all = mutex_release_all;

		for (i = 0; i < tnum_smtx; i++) {
			p_mtxcb = p_mtxcb_table[i];
			queue_initialize(&(p_mtxcb->wait_queue));
			p_mtxcb->p_mtxinib = &(mtxinib_table[i]);
			p_mtxcb->p_loctsk = NULL;
		}

		/*
		 *  動的生成用スロットの初期化
		 *
		 *  ミューテックスはプロセッサ親和を持たない（MTXINIBに
		 *  iprcid/affinityが無く，MTXCBにp_pcbが無い）ため，段階2の
		 *  cyc/almのようなプロセッサ判定や充填は一切不要である．本関数は
		 *  元からマスタプロセッサ限定なので，そのブロックの中で続けて
		 *  初期化する．他プロセッサへの可視性は，本関数の呼出し後の
		 *  barrier_syncが保証する（段階1のfree_tcbと同じ論証）．
		 */
		queue_initialize(&free_mtxcb);
		for (j = 0; i < tnum_mtx; i++, j++) {
			p_mtxcb = p_mtxcb_table[i];
			p_mtxinib = &(amtxinib_table[j]);
			p_mtxinib->mtxatr = TA_NOEXS;
			p_mtxcb->p_mtxinib = ((const MTXINIB *) p_mtxinib);
			queue_insert_prev(&free_mtxcb, &(p_mtxcb->wait_queue));
		}
	}
}

#endif /* TOPPERS_mtxini */
```

（`p_loctsk` は動的スロットでは設定しない — `acre_mtx` が `NULL` を入れる。
dcre `mutex.c:164-171` も同じ。★`mtxhook_*` の2行は**マスタ限定ブロックの冒頭のまま**動かさない。）

- [ ] **Step 3: `kernel/mutex.c` に `acre_mtx` を追加**（`TOPPERS_mtxrela` 区画の直後、
  `TOPPERS_loc_mtx` 区画の直前）

★置き場所は dcre と同じ（dcre は `TOPPERS_mtxrela`（`:360-373`）の後に
`TOPPERS_acre_mtx`（`:378`））。`mutex_calc_priority`/`remove_mutex`/`mutex_raise_priority`/
`mutex_drop_priority`（`Inline` 関数群、`mutex.c:194-284`）より**後ろ**でなければ
`del_mtx` から `remove_mutex`/`mutex_drop_priority` が見えない。

```c
/*
 *  ミューテックスの生成［NGKI2022］
 */
#ifdef TOPPERS_acre_mtx

#ifndef LOG_ACRE_MTX_ENTER
#define LOG_ACRE_MTX_ENTER(pk_cmtx)
#endif /* LOG_ACRE_MTX_ENTER */

#ifndef LOG_ACRE_MTX_LEAVE
#define LOG_ACRE_MTX_LEAVE(ercd)
#endif /* LOG_ACRE_MTX_LEAVE */

ER_ID
acre_mtx(const T_CMTX *pk_cmtx)
{
	MTXCB	*p_mtxcb;
	MTXINIB	*p_mtxinib;
	ATR		mtxatr;
	PRI		ceilpri;
	ER		ercd;

	LOG_ACRE_MTX_ENTER(pk_cmtx);
	CHECK_TSKCTX_UNL();							/*［NGKI2023］［NGKI2024］*/

	mtxatr = pk_cmtx->mtxatr;
	ceilpri = pk_cmtx->ceilpri;

	if (mtxatr == TA_CEILING) {
		CHECK_PAR(VALID_TPRI(ceilpri));			/*［NGKI2037］*/
	}
	else {
		CHECK_VALIDATR(mtxatr, TA_TPRI);		/*［NGKI2025］*/
	}

	lock_cpu();
	acquire_glock();
	if (tnum_mtx == tnum_smtx || queue_empty(&free_mtxcb)) {
		ercd = E_NOID;							/*［NGKI2031］*/
	}
	else {
		p_mtxcb = ((MTXCB *) queue_delete_next(&free_mtxcb));
		p_mtxinib = (MTXINIB *)(p_mtxcb->p_mtxinib);
		p_mtxinib->mtxatr = mtxatr;
		p_mtxinib->ceilpri = INT_PRIORITY(ceilpri);

		queue_initialize(&(p_mtxcb->wait_queue));
		p_mtxcb->p_loctsk = NULL;				/*［NGKI2033］*/
		ercd = MTXID(p_mtxcb);
	}
	release_glock();
	unlock_cpu();

  error_exit:
	LOG_ACRE_MTX_LEAVE(ercd);
	return(ercd);
}

#endif /* TOPPERS_acre_mtx */
```

dcre（`mutex.c:378-423`）からの適応点は**2つだけ**：
(1) glock の対化、(2) 空判定 `tnum_mtx == 0` → `tnum_mtx == tnum_smtx`。
**検査の分岐（`mtxatr == TA_CEILING` の if/else）は dcre のまま**である。

★**既知の性質（dcre 由来・修正しない）:** `mtxatr != TA_CEILING` のとき `ceilpri` は
検査されないまま `INT_PRIORITY(ceilpri)` に通される（`ceilpri - TMIN_TPRI` が
負になれば `uint_t` へのラップが起きる）。しかし `MTX_CEILING()` が偽なので
`p_mtxinib->ceilpri` はどこからも読まれない。dcre とバイト等価な挙動であり、
**hardening を本段階でやらない**ことを台帳と progress.md に記録する
（段階1 deferred と同じ「ユーザ誤用経路」の扱い）。

**★書いてはいけないもの**（Constraint 4）：`p_mtxinib->iprcid` / `->affinity` / `p_mtxcb->p_pcb`。

- [ ] **Step 4: `kernel/mutex.c` に `del_mtx` を追加**（`acre_mtx` の直後）

```c
/*
 *  ミューテックスの削除［NGKI2056］
 */
#ifdef TOPPERS_del_mtx

#ifndef LOG_DEL_MTX_ENTER
#define LOG_DEL_MTX_ENTER(mtxid)
#endif /* LOG_DEL_MTX_ENTER */

#ifndef LOG_DEL_MTX_LEAVE
#define LOG_DEL_MTX_LEAVE(ercd)
#endif /* LOG_DEL_MTX_LEAVE */

ER
del_mtx(ID mtxid)
{
	MTXCB	*p_mtxcb;
	MTXINIB	*p_mtxinib;
	TCB		*p_loctsk;
	ER		ercd;
	TCB		*p_selftsk;
	PCB		*p_my_pcb;

	LOG_DEL_MTX_ENTER(mtxid);
	CHECK_TSKCTX_UNL_MYSTATE(&p_selftsk);		/*［NGKI2057］［NGKI2058］*/
	CHECK_ID(VALID_MTXID(mtxid));				/*［NGKI2059］*/
	p_mtxcb = get_mtxcb(mtxid);

	lock_cpu();
	acquire_glock();
	p_my_pcb = get_my_pcb();
	if (p_mtxcb->p_mtxinib->mtxatr == TA_NOEXS) {
		ercd = E_NOEXS;							/*［NGKI2060］*/
	}
	else if (mtxid <= tmax_smtxid) {
		ercd = E_OBJ;							/*［NGKI2062］*/
	}
	else {
		/*
		 *  待ちタスクがいても削除は成功し，待ちタスクはE_DLTで強制
		 *  解除される［NGKI2065］．init_wait_queueはMP対応済み
		 *  （wait.c:215-228）で，既存のini_mtxと同一の機構である．
		 */
		init_wait_queue(p_my_pcb, &(p_mtxcb->wait_queue));

		/*
		 *  ロック中でも削除できる．p_loctskがロックしているミューテッ
		 *  クスのリストから対象ミューテックスを削除し，優先度上限
		 *  ミューテックスなら現在優先度を復帰させる［NGKI2064］．
		 *  呼出し順・引数はFMP3の現物ini_mtx（mutex.c:605-614）に
		 *  合わせている（MP版のmutex_drop_priorityはp_my_pcbが先頭）．
		 *
		 *  ★MTX_CEILING(p_mtxcb)はp_mtxcb->p_mtxinib->mtxatrを読むため，
		 *    このブロックはmtxatrにTA_NOEXSを書く前に実行しなければ
		 *    ならない．順序を入れ替えると優先度が復帰しない．
		 */
		p_loctsk = p_mtxcb->p_loctsk;
		if (p_loctsk != NULL) {
			p_mtxcb->p_loctsk = NULL;
			(void) remove_mutex(p_loctsk, p_mtxcb);
			if (MTX_CEILING(p_mtxcb)) {
				mutex_drop_priority(p_my_pcb, p_loctsk,
										p_mtxcb->p_mtxinib->ceilpri);
			}
		}

		p_mtxinib = (MTXINIB *)(p_mtxcb->p_mtxinib);
		p_mtxinib->mtxatr = TA_NOEXS;			/*［NGKI2063］*/
		queue_insert_prev(&free_mtxcb, &(p_mtxcb->wait_queue));
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
	LOG_DEL_MTX_LEAVE(ercd);
	return(ercd);
}

#endif /* TOPPERS_del_mtx */
```

dcre（`mutex.c:428-479`）からの適応点は**4つ**：
1. glock の対化。
2. ★訂正C＝`CHECK_TSKCTX_UNL()` + `p_runtsk` を `CHECK_TSKCTX_UNL_MYSTATE(&p_selftsk)` +
   `p_selftsk != p_my_pcb->p_schedtsk` に（FMP3 の `ini_mtx` の流儀）。
3. `init_wait_queue` に `p_my_pcb` 引数、`mutex_drop_priority` に `p_my_pcb` 引数が付く
   （**MP 版のシグネチャ差異**。`mutex.c:271-284`／dcre は `mutex_drop_priority(TCB *, uint_t)`）。
4. ★訂正F＝dcre に無い `p_mtxcb->p_loctsk = NULL;` を入れる（FMP3 の `ini_mtx:608` に倣う。
   `remove_mutex` は退避済みローカル `p_loctsk` を使うので順序上安全）。

★**この関数の失敗モードを3つ意識すること**（テストの手順5/6 が全部検出する）：
- `mtxatr = TA_NOEXS` を優先度復帰より**先**に書く → ceiling mutex の優先度が戻らない。
- `init_wait_queue` を `queue_insert_prev` より**後**に呼ぶ → free-list のリンクが壊れる。
- `p_loctsk` の退避を忘れて `p_mtxcb->p_loctsk` を直接渡す → `NULL` 代入後に `NULL` を渡す。

- [ ] **Step 5: E_NOEXS 検査の挿入（mutex.c の6関数）**

判定式はすべて `p_mtxcb->p_mtxinib->mtxatr == TA_NOEXS`。
挿入位置は `acquire_glock()`（および直後の `p_my_pcb = get_my_pcb();` があればその後）の**直後**。

| 関数 | 現在の定義行 | 既存本体の形 | 挿入後 |
|---|---|---|---|
| `loc_mtx` | `mutex.c:365` | `if (raster) … else if (MTX_CEILING…) … else if (p_loctsk==NULL) … else if (p_loctsk==p_selftsk) … else {…}` | 先頭に `if (TA_NOEXS)`、既存 `if` を `else if` に |
| `ploc_mtx` | `:424` | 同型の `if` 連鎖 | 同上 |
| `tloc_mtx` | `:475` | 同型の `if` 連鎖 | 同上 |
| `unl_mtx` | `:539` | `if (p_mtxcb != p_selftsk->p_lastmtx) { E_OBJ } else {…}` | 同上 |
| `ini_mtx` | `:589` | 単文列（`init_wait_queue`／`p_loctsk` 処理／dispatch 判定） | `else { … }` で丸ごと包む |
| `ref_mtx` | `:639` | `pk_rmtx->htskid = …; pk_rmtx->wtskid = …; ercd = E_OK;` | `else { … }` |

★具体例（`unl_mtx`。`acquire_glock(); p_my_pcb = get_my_pcb();` の直後を置換）：

```c
	if (p_mtxcb->p_mtxinib->mtxatr == TA_NOEXS) {
		ercd = E_NOEXS;
	}
	else if (p_mtxcb != p_selftsk->p_lastmtx) {
		ercd = E_OBJ;
	}
	else {
		p_selftsk->p_lastmtx = p_mtxcb->p_prevmtx;
		if (MTX_CEILING(p_mtxcb)) {
			mutex_drop_priority(p_my_pcb, p_selftsk,
									p_mtxcb->p_mtxinib->ceilpri);
		}
		mutex_release(p_my_pcb, p_mtxcb);
		if (p_selftsk != p_my_pcb->p_schedtsk) {
			release_glock();
			dispatch();
			ercd = E_OK;
			goto unlock_and_exit;
		}
		ercd = E_OK;
	}
```

★具体例（`ini_mtx`。既存本体を `else { … }` で丸ごと包む）：

```c
	if (p_mtxcb->p_mtxinib->mtxatr == TA_NOEXS) {
		ercd = E_NOEXS;
	}
	else {
		init_wait_queue(p_my_pcb, &(p_mtxcb->wait_queue));
		p_loctsk = p_mtxcb->p_loctsk;
		if (p_loctsk != NULL) {
			p_mtxcb->p_loctsk = NULL;
			(void) remove_mutex(p_loctsk, p_mtxcb);
			if (MTX_CEILING(p_mtxcb)) {
				mutex_drop_priority(p_my_pcb, p_loctsk,
										p_mtxcb->p_mtxinib->ceilpri);
			}
		}
		if (p_selftsk != p_my_pcb->p_schedtsk) {
			release_glock();
			dispatch();
			ercd = E_OK;
			goto unlock_and_exit;
		}
		ercd = E_OK;
	}
```

**除外**: `initialize_mutex`、`mutex_check_ceilpri`（`TOPPERS_mtxchk`）、
`mutex_acquire`（`TOPPERS_mtxacq`）、`mutex_release`（`TOPPERS_mtxrel`）、
`mutex_release_all`（`TOPPERS_mtxrela`）— いずれも ID を取らない内部関数。
`acre_mtx`（ID を取らない）、`del_mtx`（自前で E_NOEXS を持つ）。
→ **mutex.c で E_NOEXS を挿入するのは上表の6関数だけ。FMP3 固有関数は無い。**

★`mutex_check_ceilpri` は `p_tcb->p_lastmtx` 連鎖と待ち対象を辿って
`MTX_CEILING(p_mtxcb)` を評価する（`mutex.c:161-178`）。**削除済み（TA_NOEXS）の
ミューテックスがこの連鎖に残ることは無い**（`del_mtx` が `remove_mutex` で外し、
`init_wait_queue` で待ちタスクを解除するため）。この論証を台帳の理由欄に1文書く。

- [ ] **Step 6: 配線（allfunc.h / Makefile.kernel）**

`kernel/allfunc.h` の `/* mutex.c */` 節（`:187-200`）の `#define TOPPERS_mtxrela` の直後に2行：
```c
#define TOPPERS_acre_mtx
#define TOPPERS_del_mtx
```
★`allfunc.h` の `/* mutex.c */` 節には `TOPPERS_mtxscan` / `TOPPERS_mtxdrop`（`:190-191`）という
**`mutex.c` に対応する `#ifdef` が存在しないエントリ**がある（pristine 既存の残骸）。
**触らない**（削除もしない）。触ると無関係な差分が出る。

`kernel/Makefile.kernel:100-101` を次に変更：
```
mutex = mtxini.o mtxchk.o mtxacq.o mtxrel.o mtxrela.o acre_mtx.o del_mtx.o \
		loc_mtx.o ploc_mtx.o tloc_mtx.o unl_mtx.o ini_mtx.o ref_mtx.o
```
**`KERNEL_FCSRCS` は触らない**（22個のまま）。

- [ ] **Step 7: rename 追加・再生成**

`kernel/kernel_rename.def` の `# mutex.c` 節（文字列を目印にする）を次に置換する：

```
# mutex.c
initialize_mutex
mutex_check_ceilpri
mutex_acquire
mutex_release
mutex_release_all
free_mtxcb
tmax_smtxid
amtxinib_table

```

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core/kernel && ruby ../utils/genrename.rb kernel && cd ..
git diff --stat kernel/kernel_rename.h kernel/kernel_unrename.h   # 期待: 各 +3 行・削除0
grep -n "free_mtxcb\|tmax_smtxid\|amtxinib_table" kernel/kernel_rename.h kernel/kernel_unrename.h
```
期待: `_kernel_free_mtxcb` / `_kernel_tmax_smtxid` / `_kernel_amtxinib_table` が実在。**削除行があれば不合格。**

- [ ] **Step 8: 全8構成ビルド + 等価性 + 混在サンプル**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
for p in musca_b1 musca_b1-2core rp2350_pico2 polarfire_soc_kit-qemu \
         kria_arm64 kria_arm64-1core kria_r5 kria_r5-2core; do
  cmake --build build/$p > /tmp/dcre3-t6-build-$p.log 2>&1; echo "$p build rc=$?"
  tools/cfg_equivalence.sh build/$p > /tmp/dcre3-t6-eq-$p.log 2>&1; echo "$p eq rc=$?"
done
cmake --build build/musca_b1-2core-tmix > /tmp/dcre3-t6-build-tmix.log 2>&1; echo "tmix build rc=$?"
tools/cfg_equivalence.sh build/musca_b1-2core-tmix > /tmp/dcre3-t6-eq-tmix.log 2>&1; echo "tmix eq rc=$?"
```
期待: 全て rc=0。**exit=2 は不合格**。
`test_dcre_mix.cfg` は `AID_MTX(1)` を含むので `acre_mtx`/`del_mtx` が実際にリンクされる。

- [ ] **Step 9: QEMU 起動（非退行）— musca_b1-2core と kria_r5-2core**

```bash
timeout -k 5 25 cmake --build build/musca_b1-2core --target run > /tmp/dcre3-t6-run-musca.log 2>&1
grep -c 'Processor [12] start\.' /tmp/dcre3-t6-run-musca.log     # 期待: 2
pgrep -a qemu                                                    # 期待: 何も出ない
```
```bash
timeout -k 5 25 cmake --build build/kria_r5-2core --target run > /tmp/dcre3-t6-run-r5.log 2>&1
grep -c 'Processor [12] start\.' /tmp/dcre3-t6-run-r5.log        # 期待: 2
pgrep -a qemu                                                    # 期待: 何も出ない
```
★`sample1` が静的ミューテックスを使うか（`grep -n "CRE_MTX" sample/sample1.cfg`）を先に見て、
使うなら「E_NOEXS 挿入6箇所が静的ミューテックスの動作を壊していない」ことの実証になる。
使わないなら、その事実（＝この Step は起動の非退行しか見ていない）を記録し、
**mutex の実動作の検査は Task 7 の test_dcre3 が唯一の砦である**ことを明記する。

- [ ] **Step 10: 台帳とコミット**

`DIVERGENCE_MAP.md` に次の行を追記する（種別はすべて `mod (dcre-port)`）：
- `kernel/mutex.h（dcre段階3a Task 6）` — Task 4/5 と同型（`tmax_smtxid`／`QUEUE free_mtxcb`／
  `amtxinib_table[]` の extern 追加、`tnum_mtx` の移設と `tnum_smtx` 新設、既存 `MTXID` の
  2レンジ版への**置換**）
- `kernel/mutex.c（dcre段階3a Task 6）` — 理由：「`#define tnum_mtx` を削除（`.h` へ移設）。
  `QUEUE free_mtxcb;` の定義を追加。`initialize_mutex` の静的ループ境界を
  `tnum_mtx` → `tnum_smtx` に変更し、既存のマスタ限定ブロックの中
  （`mtxhook_check_ceilpri`/`mtxhook_release_all` の設定より後ろ）に動的スロット初期化を追加。
  **ミューテックスは非親和オブジェクトのため `iprcid`/`affinity`/`p_pcb` の充填や
  プロセッサ判定は一切行っていない**。`acre_mtx`/`del_mtx` を
  `TOPPERS_mtxrela` 区画の直後に追加（dcre `mutex.c:378-423`/`:428-479` の転写。
  `remove_mutex`/`mutex_drop_priority` の `Inline` 定義より後ろでなければならない）。
  dcre からの意図的な逸脱4件：(1) glock の対化、(2) 空判定 `tnum_mtx == 0` →
  `tnum_mtx == tnum_smtx`、(3) `del_mtx` の `CHECK_TSKCTX_UNL()`＋`p_runtsk` を
  `CHECK_TSKCTX_UNL_MYSTATE(&p_selftsk)`＋`p_selftsk != p_my_pcb->p_schedtsk` へ、
  および MP 版シグネチャ（`init_wait_queue(p_my_pcb, …)`／
  `mutex_drop_priority(p_my_pcb, p_tcb, oldpri)`）への読み替え、
  (4) dcre に無い `p_mtxcb->p_loctsk = NULL;` を `del_mtx` に追加（FMP3 の `ini_mtx:608` に倣う）。
  ★`MTX_CEILING()` が `p_mtxinib->mtxatr` を読むため、`del_mtx` では優先度復帰を
  `mtxatr = TA_NOEXS` の**前**に行う順序制約がある。
  ★`mtxatr != TA_CEILING` のとき `ceilpri` が未検査のまま `INT_PRIORITY()` に通る点は
  dcre 由来の既知の性質で、`MTX_CEILING()` が偽なので読まれない＝本段階では hardening しない。
  ★削除済み（TA_NOEXS）のミューテックスが `p_lastmtx` 連鎖や待ち対象に残ることは無い
  （`del_mtx` が `remove_mutex` で外し `init_wait_queue` で待ちを解除するため）＝
  `mutex_check_ceilpri` は TA_NOEXS スロットを見ない。
  `loc_mtx`/`ploc_mtx`/`tloc_mtx`/`unl_mtx`/`ini_mtx`/`ref_mtx` の6関数に
  existence-before-state 規約の E_NOEXS 分岐を挿入（既存ロジックは字下げのみで byte-preserve）」
- `kernel/allfunc.h（dcre段階3a Task 6）`（既存行の理由欄に「`/* mutex.c */` 節に
  `TOPPERS_acre_mtx`/`TOPPERS_del_mtx` を追加。既存の `TOPPERS_mtxscan`/`TOPPERS_mtxdrop`
  （`mutex.c` に対応区画が無い pristine 既存の残骸）は**触っていない**」を追記）
- `kernel/Makefile.kernel（dcre段階3a Task 6）`（同、「`mutex =` 行に
  `acre_mtx.o`/`del_mtx.o` を追加」）
- `kernel/kernel_rename.def（dcre段階3a Task 6）` — 理由：「`# mutex.c` 節に
  `free_mtxcb`/`tmax_smtxid`/`amtxinib_table` の3エントリを追加」
- `kernel/kernel_rename.h・kernel_unrename.h（dcre段階3a Task 6）` — 種別
  `mod (dcre-port, 生成物)`、理由：「再生成。各+3行・削除0行」

```bash
git add -A && git commit -m "feat(kernel): acre_mtx/del_mtx（ロック中削除と優先度復帰）・free_mtxcb・2レンジMTXID とE_NOEXS検査6箇所（dcre段階3a）"
```

---

### Task 7: QEMU 回帰テスト test_dcre3

**推奨モデル:** 中位（sonnet）

**Files:**
- Create: `test/test_dcre3.c` `test/test_dcre3.cfg` `test/test_dcre3.h`
- Modify: `test/MANIFEST`（`test_dcre2.h` の直後に3行）・`test/testexec.rb`（`"dcre2"` の直後に1行）
- Modify: `DIVERGENCE_MAP.md`

**Interfaces:** Consumes Task 2〜6 の全成果。`syssvc/test_svc.h` の
`test_start` / `check_point`（= `check_point_prc(count, 0)`）/ `check_point_prc` /
`check_ercd` / `check_assert` / `check_finish`。

**★check_point の意味論（段階1 Task 6 で確定・段階2 Task 7 で再確認した事実）:**
`check_point_prc(count, prcid)` は `prcid > 0` のとき `check_count[prcid-1]` を、
`prcid == 0`（= `check_point()`）のとき `check_count[0]` を使う（`syssvc/test_svc.h:112`）。
すなわち **PRC1 の `check_point()` と PRC2 の `check_point_prc(n,2)` は独立したカウンタ**であり、
**PRC2 側の最初のチェックポイントは `check_point_prc(1, 2)`** になる。
各プロセッサのカウンタは 1 から単調増加でなければならず、崩れると
`## Unexpected check point` が出て失敗する。
★**同じ PRC1 上の TASK1 と TASK2 は同じカウンタ `check_count[0]` を共有する**ので、
2タスクにまたがる番号列が**通し**で単調増加になるよう振り付ける（本テストは
TASK1 が 1,2,4,6,7,8,9,10,11、TASK2 が 3,5）。

- [ ] **Step 1: 前提の現物確認（存在するものだけで書く）**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
sed -n '55,115p' syssvc/test_svc.h
grep -n "^CRE_SEM\|^CRE_FLG\|^CRE_MTX\|^CRE_TSK" kernel/kernel_api.def
grep -rn "CLS_PRC2" test/test_mmutex1.cfg test/test_spinlock1.cfg | head
grep -n "get_pri\|TSK_SELF\|TMAX_TPRI\|TMIN_TPRI" include/kernel.h | head
```
確認すること：
- `test_start` / `check_point_prc` / `check_finish` / `check_assert` / `check_ercd` /
  `check_point` が存在する（無いものは使わない）。
- `CRE_SEM #semid* { .sematr .isemcnt .maxsem }` / `CRE_FLG #flgid* { .flgatr .iflgptn }` /
  `CRE_MTX #mtxid* { .mtxatr +ceilpri? }`（ceilpri は省略可）。
- `CLS_PRC2` が既存テスト（`test_mmutex1.cfg:18` 等）で使われている＝
  `test_common1.cfg` 経由で使える。
- `get_pri(ID, PRI *)`（`include/kernel.h:307`）、`TSK_SELF`（`:581`）、
  `TMIN_TPRI = 1`（`:600`）、`TMAX_TPRI = 16`（`:601`）。
★食い違ったら**現物に合わせてテストを直し、直した事実を記録する**。

- [ ] **Step 2: `test/test_dcre3.h`**

```c
/*
 *		動的生成API（acre_sem/del_sem・acre_flg/del_flg・acre_mtx/del_mtx）
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

- [ ] **Step 3: `test/test_dcre3.cfg`**

```c
/*
 *		動的生成API（sem/flg/mtx）のテストの
 *		システムコンフィギュレーションファイル
 *
 *  $Id$
 */
INCLUDE("test_common1.cfg");

#include "test_dcre3.h"

CLASS(CLS_PRC1) {
	CRE_TSK(TASK1, { TA_ACT, 1, task1, MID_PRIORITY, STACK_SIZE, NULL });
	/*  TASK2: PRC1 上で E_DLT を受け取る待ちタスク（高優先度・起動は act_tsk）  */
	CRE_TSK(TASK2, { TA_NULL, 2, task2, HIGH_PRIORITY, STACK_SIZE, NULL });
	/*
	 *  静的な sem/flg/mtx．del_sem/del_flg/del_mtx の E_OBJ 対象であると
	 *  同時に，AID_SEM/AID_FLG/AID_MTX が「静的オブジェクトが1個以上ある
	 *  こと」を要求する（訂正E ガード）ため必須．
	 */
	CRE_SEM(SEM1, { TA_TPRI, 0, 1 });
	CRE_FLG(FLG1, { TA_TPRI, 0 });
	CRE_MTX(MTX1, { TA_TPRI });
}

CLASS(CLS_PRC2) {
	/*  TASK3: PRC2 上で E_DLT を受け取る待ちタスク（MP 経路の実証）  */
	CRE_TSK(TASK3, { TA_NULL, 3, task3, HIGH_PRIORITY, STACK_SIZE, NULL });
}

/*  AID_SEM/AID_FLG/AID_MTX はクラス外専用（Task 3 の E_RSATR 検査対象）  */
AID_SEM(2);
AID_FLG(1);
AID_MTX(2);
```
（`DEF_MPK` は**不要** — sem/flg/mtx はメモリプールを使わない。spec §1.3「E_NOMEM 経路なし」。）

- [ ] **Step 4: `test/test_dcre3.c`**（著作権ヘッダは `test/test_dcre2.c` と同形式で付ける）

```c
/*
 *		動的生成API（acre_sem/del_sem・acre_flg/del_flg・acre_mtx/del_mtx）
 *		のテスト
 *
 * 【テストの目的】
 *
 *	(A) acre_sem → sig/wai/pol の基本動作 → 休止資源での del_sem → E_NOEXS ×6
 *	(B) E_DLT 実証（同一プロセッサ）: 高優先度タスクを wai_sem で待たせ，
 *	    del_sem で E_DLT を受け取ることを check_ercd で確認
 *	(C) E_DLT 実証（別プロセッサ）: PRC2 のタスクを wai_sem で待たせ，
 *	    PRC1 から del_sem して E_DLT を受け取る（init_wait_queue の MP 経路）
 *	(D) スロット枯渇 E_NOID／静的オブジェクトへの del が E_OBJ／
 *	    パラメータ検査 E_PAR・E_RSATR／del → 再 acre で同一 ID（決定形）
 *	(E) flg: acre → set/clr/wai/pol/ini → del → E_NOEXS ×7，TA_CLR の実動作
 *	(F) mtx: acre(TA_CEILING) → loc → 現在優先度が上限へ上がる →
 *	    ★ロック中の del_mtx が成功 → 現在優先度がベース優先度へ復帰することを
 *	    get_pri で実測 → 削除済みであること（E_NOEXS ×6）
 *	(G) mtx エラー系（不正 ceilpri で E_PAR，未定義属性ビットで E_RSATR）と
 *	    スロット再利用
 *
 * 【使用リソース】
 *
 *	TASK1: 中優先度タスク，TA_ACT属性（静的・PRC1）
 *	TASK2: 高優先度タスク，TA_NULL属性（静的・PRC1．E_DLT を受け取る）
 *	TASK3: 高優先度タスク，TA_NULL属性（静的・PRC2．E_DLT を受け取る）
 *	SEM1/FLG1/MTX1: 静的なセマフォ／イベントフラグ／ミューテックス
 *	AID_SEM(2)/AID_FLG(1)/AID_MTX(2): 動的スロット
 *
 * 【チェックポイント】
 *
 *	PRC1（check_count[0]，TASK1 と TASK2 が共有）: 1..11 + check_finish(12)
 *	  TASK1 が 1,2,4,6,7,8,9,10,11／TASK2 が 3,5
 *	PRC2（check_count[1]，TASK3）: 1,2
 */

#include <kernel.h>
#include <t_syslog.h>
#include "syssvc/test_svc.h"
#include "kernel_cfg.h"
#include "test_dcre3.h"

/* DO NOT DELETE THIS LINE -- gentest depends on it. */

/*
 *  TASK2 / TASK3 が待つ動的セマフォの ID（TASK1 が設定してから act_tsk する）
 */
static volatile ID		dlt_semid;
static volatile bool_t	prc2_done;

/*
 *  PRC1 上で E_DLT を受け取るタスク
 */
void
task2(EXINF exinf)
{
	check_point(3);
	/*  資源が無いので待ちに入る．TASK1 の del_sem で E_DLT が返る．  */
	check_ercd(wai_sem(dlt_semid), E_DLT);
	check_point(5);
	/*  ext_tsk() で終了（戻ると E_SYS 相当の扱いになる）  */
	check_ercd(ext_tsk(), E_OK);
}

/*
 *  PRC2 上で E_DLT を受け取るタスク
 *
 *  PRC2 のチェックポイントカウンタは PRC1 と独立なので 1 から始まる．
 */
void
task3(EXINF exinf)
{
	check_point_prc(1, 2);
	check_ercd(wai_sem(dlt_semid), E_DLT);
	check_point_prc(2, 2);
	prc2_done = true;
	check_ercd(ext_tsk(), E_OK);
}

void
task1(EXINF exinf)
{
	T_CSEM	csem;
	T_CFLG	cflg;
	T_CMTX	cmtx;
	T_RSEM	rsem;
	T_RFLG	rflg;
	T_RMTX	rmtx;
	ER_ID	erid;
	ID		semid1, semid2, flgid1, mtxid1, mtxid2;
	FLGPTN	flgptn;
	PRI		tskpri;

	test_start(__FILE__);
	check_point(1);

	/*
	 *  1) acre_sem → sig/wai/pol の基本動作 → del_sem → E_NOEXS ×6
	 */
	csem.sematr = TA_TPRI;
	csem.isemcnt = 0U;
	csem.maxsem = 2U;
	erid = acre_sem(&csem);
	check_assert(erid > SEM1);		/*  動的IDは静的レンジの外＝2レンジSEMIDの直接検証  */
	semid1 = (ID) erid;

	check_ercd(ref_sem(semid1, &rsem), E_OK);
	check_assert(rsem.semcnt == 0U);
	check_assert(rsem.wtskid == TSK_NONE);
	check_ercd(sig_sem(semid1), E_OK);
	check_ercd(wai_sem(semid1), E_OK);			/*  資源1個あるので即取得  */
	check_ercd(pol_sem(semid1), E_TMOUT);		/*  資源0個  */
	check_ercd(ini_sem(semid1), E_OK);
	check_ercd(del_sem(semid1), E_OK);

	check_ercd(sig_sem(semid1), E_NOEXS);
	check_ercd(wai_sem(semid1), E_NOEXS);
	check_ercd(pol_sem(semid1), E_NOEXS);
	check_ercd(twai_sem(semid1, TMO_POL), E_NOEXS);
	check_ercd(ini_sem(semid1), E_NOEXS);
	check_ercd(ref_sem(semid1, &rsem), E_NOEXS);
	check_ercd(del_sem(semid1), E_NOEXS);
	check_point(2);

	/*
	 *  2) E_DLT 実証（同一プロセッサ PRC1）
	 *
	 *  TASK2 は TASK1 より高優先度なので act_tsk で即座に走り，
	 *  wai_sem で待ちに入ったところで TASK1 に戻ってくる．
	 */
	erid = acre_sem(&csem);						/*  isemcnt = 0  */
	check_assert(erid > SEM1);
	semid1 = (ID) erid;
	dlt_semid = semid1;

	check_ercd(act_tsk(TASK2), E_OK);			/*  → TASK2 が cp(3) を打って待つ  */
	check_point(4);
	check_ercd(ref_sem(semid1, &rsem), E_OK);
	check_assert(rsem.wtskid == TASK2);			/*  待ちタスクが実在する  */
	check_ercd(del_sem(semid1), E_OK);			/*  → TASK2 が E_DLT で起き cp(5) → ext_tsk  */
	check_point(6);
	check_ercd(del_sem(semid1), E_NOEXS);

	/*
	 *  3) E_DLT 実証（別プロセッサ PRC2）＝ init_wait_queue の MP 経路
	 *
	 *  TASK3 は PRC2 で並行に走るため，dly_tsk で待ちに入るのを待つ．
	 */
	erid = acre_sem(&csem);
	check_assert(erid > SEM1);
	semid1 = (ID) erid;
	dlt_semid = semid1;
	prc2_done = false;

	check_ercd(act_tsk(TASK3), E_OK);
	check_ercd(dly_tsk(TEST_TIME_PROC), E_OK);	/*  TASK3 が wai_sem に入るのを待つ  */
	check_ercd(ref_sem(semid1, &rsem), E_OK);
	check_assert(rsem.wtskid == TASK3);
	check_ercd(del_sem(semid1), E_OK);
	check_ercd(dly_tsk(TEST_TIME_PROC), E_OK);	/*  TASK3 が cp(2,2) を打つのを待つ  */
	check_assert(prc2_done);
	check_point(7);

	/*
	 *  4) スロット枯渇 E_NOID／静的への del は E_OBJ／パラメータ検査／再利用
	 */
	erid = acre_sem(&csem);
	check_assert(erid > SEM1);
	semid1 = (ID) erid;
	erid = acre_sem(&csem);
	check_assert(erid > SEM1);
	semid2 = (ID) erid;
	check_assert(semid1 != semid2);
	check_assert(acre_sem(&csem) == E_NOID);	/*  スロット2個を使い切った  */

	check_ercd(del_sem(SEM1), E_OBJ);			/*  静的生成オブジェクト  */

	check_ercd(del_sem(semid2), E_OK);			/*  空きが1個だけの状態を作る  */
	erid = acre_sem(&csem);
	check_assert(erid == semid2);				/*  FIFO/LIFO 不問で決定的  */
	check_ercd(del_sem(semid2), E_OK);
	check_ercd(del_sem(semid1), E_OK);

	csem.maxsem = 0U;							/*  1 <= maxsem を破る  */
	check_assert(acre_sem(&csem) == E_PAR);
	csem.maxsem = 2U;
	csem.isemcnt = 3U;							/*  isemcnt <= maxsem を破る  */
	check_assert(acre_sem(&csem) == E_PAR);
	csem.isemcnt = 0U;
	csem.sematr = TA_TPRI | 0x04U;				/*  未定義ビット → E_RSATR  */
	check_assert(acre_sem(&csem) == E_RSATR);
	csem.sematr = TA_TPRI;
	check_point(8);

	/*
	 *  5) flg: acre → set/clr/wai/pol/ini → del → E_NOEXS ×7
	 */
	cflg.flgatr = TA_TPRI | TA_WMUL | TA_CLR;
	cflg.iflgptn = 0x01U;
	erid = acre_flg(&cflg);
	check_assert(erid > FLG1);					/*  2レンジFLGIDの直接検証  */
	flgid1 = (ID) erid;

	check_ercd(ref_flg(flgid1, &rflg), E_OK);
	check_assert(rflg.flgptn == 0x01U);			/*  iflgptn が反映されている  */
	check_ercd(pol_flg(flgid1, 0x01U, TWF_ORW, &flgptn), E_OK);
	check_assert(flgptn == 0x01U);
	check_ercd(ref_flg(flgid1, &rflg), E_OK);
	check_assert(rflg.flgptn == 0U);			/*  TA_CLR でクリアされた  */
	check_ercd(set_flg(flgid1, 0x02U), E_OK);
	check_ercd(wai_flg(flgid1, 0x02U, TWF_ORW, &flgptn), E_OK);
	check_assert(flgptn == 0x02U);
	check_ercd(set_flg(flgid1, 0x03U), E_OK);
	check_ercd(clr_flg(flgid1, 0x01U), E_OK);
	check_ercd(ref_flg(flgid1, &rflg), E_OK);
	check_assert(rflg.flgptn == 0x01U);
	check_ercd(ini_flg(flgid1), E_OK);
	check_ercd(ref_flg(flgid1, &rflg), E_OK);
	check_assert(rflg.flgptn == 0x01U);			/*  iflgptn に戻る  */

	check_assert(acre_flg(&cflg) == E_NOID);	/*  AID_FLG(1) を使い切っている  */
	check_ercd(del_flg(FLG1), E_OBJ);			/*  静的生成オブジェクト  */
	check_ercd(del_flg(flgid1), E_OK);

	check_ercd(set_flg(flgid1, 0x01U), E_NOEXS);
	check_ercd(clr_flg(flgid1, 0U), E_NOEXS);
	check_ercd(wai_flg(flgid1, 0x01U, TWF_ORW, &flgptn), E_NOEXS);
	check_ercd(pol_flg(flgid1, 0x01U, TWF_ORW, &flgptn), E_NOEXS);
	check_ercd(twai_flg(flgid1, 0x01U, TWF_ORW, &flgptn, TMO_POL), E_NOEXS);
	check_ercd(ini_flg(flgid1), E_NOEXS);
	check_ercd(ref_flg(flgid1, &rflg), E_NOEXS);
	check_ercd(del_flg(flgid1), E_NOEXS);

	cflg.flgatr = TA_TPRI | 0x08U;				/*  未定義ビット → E_RSATR  */
	check_assert(acre_flg(&cflg) == E_RSATR);
	cflg.flgatr = TA_TPRI | TA_WMUL | TA_CLR;
	check_point(9);

	/*
	 *  6) mtx: acre(TA_CEILING) → loc → ★ロック中の del_mtx → 優先度復帰
	 */
	cmtx.mtxatr = TA_CEILING;
	cmtx.ceilpri = HIGH_PRIORITY;
	erid = acre_mtx(&cmtx);
	check_assert(erid > MTX1);					/*  2レンジMTXIDの直接検証  */
	mtxid1 = (ID) erid;

	check_ercd(get_pri(TSK_SELF, &tskpri), E_OK);
	check_assert(tskpri == MID_PRIORITY);		/*  ロック前はベース優先度  */
	check_ercd(loc_mtx(mtxid1), E_OK);
	check_ercd(get_pri(TSK_SELF, &tskpri), E_OK);
	check_assert(tskpri == HIGH_PRIORITY);		/*  上限優先度へ上がった  */
	check_ercd(ref_mtx(mtxid1, &rmtx), E_OK);
	check_assert(rmtx.htskid == TASK1);

	check_ercd(del_mtx(mtxid1), E_OK);			/*  ★ロック中でも削除できる  */
	check_ercd(get_pri(TSK_SELF, &tskpri), E_OK);
	check_assert(tskpri == MID_PRIORITY);		/*  ★ベース優先度へ復帰した  */

	check_ercd(loc_mtx(mtxid1), E_NOEXS);
	check_ercd(ploc_mtx(mtxid1), E_NOEXS);
	check_ercd(tloc_mtx(mtxid1, TMO_POL), E_NOEXS);
	check_ercd(unl_mtx(mtxid1), E_NOEXS);
	check_ercd(ini_mtx(mtxid1), E_NOEXS);
	check_ercd(ref_mtx(mtxid1, &rmtx), E_NOEXS);
	check_ercd(del_mtx(mtxid1), E_NOEXS);
	check_point(10);

	/*
	 *  7) mtx エラー系とスロット再利用
	 */
	cmtx.mtxatr = TA_CEILING;
	cmtx.ceilpri = TMIN_TPRI - 1;				/*  VALID_TPRI を破る（下限外）  */
	check_assert(acre_mtx(&cmtx) == E_PAR);
	cmtx.ceilpri = TMAX_TPRI + 1;				/*  VALID_TPRI を破る（上限外）  */
	check_assert(acre_mtx(&cmtx) == E_PAR);
	cmtx.mtxatr = TA_TPRI | 0x08U;				/*  未定義ビット → E_RSATR  */
	cmtx.ceilpri = HIGH_PRIORITY;
	check_assert(acre_mtx(&cmtx) == E_RSATR);

	cmtx.mtxatr = TA_TPRI;						/*  ceilpri は参照されない  */
	erid = acre_mtx(&cmtx);
	check_assert(erid > MTX1);
	mtxid1 = (ID) erid;
	erid = acre_mtx(&cmtx);
	check_assert(erid > MTX1);
	mtxid2 = (ID) erid;
	check_assert(mtxid1 != mtxid2);
	check_assert(acre_mtx(&cmtx) == E_NOID);	/*  スロット2個を使い切った  */

	check_ercd(del_mtx(MTX1), E_OBJ);			/*  静的生成オブジェクト  */

	check_ercd(del_mtx(mtxid2), E_OK);			/*  空きが1個だけの状態を作る  */
	erid = acre_mtx(&cmtx);
	check_assert(erid == mtxid2);				/*  FIFO/LIFO 不問で決定的  */
	check_ercd(del_mtx(mtxid2), E_OK);
	check_ercd(del_mtx(mtxid1), E_OK);
	check_point(11);

	check_finish(12);
}
```

**注意（実装者へ）:**
- `ext_tsk()` の返値検査（`check_ercd(ext_tsk(), E_OK)`）は「戻ってこない」ことが期待なので、
  `test/` の既存テストがどう書いているかを見て流儀を合わせる
  （`grep -n "ext_tsk" test/test_dcre1.c test/test_mtskman1.c`）。単に `ext_tsk();` でよい。
- `TSK_NONE`（`ref_sem` の `wtskid` が待ちタスク無しのとき返す値）が
  `include/kernel.h` に存在することを確認する。無ければ `check_assert(rsem.wtskid == TSK_NONE)` を落とす。
- `TMIN_TPRI - 1` は `PRI`（符号付き）なので 0 になる。`VALID_TPRI(0)` は偽＝E_PAR。
- `E_RSATR` の検査は `CHECK_VALIDATR` が使う属性ビットに依存する。
  ターゲット固有の追加属性ビットがある場合は使うビットを変える
  （`grep -n "TARGET_SEMATR\|TARGET_FLGATR\|TARGET_MTXATR" kernel/ include/ target/`）。
- `dly_tsk(TEST_TIME_PROC)` は 200ms。QEMU で PRC2 のタスクが起動して待ちに入るのに十分だが、
  ホストが遅い場合は `TEST_TIME_PROC` を大きくする（回数を固定していないので値を増やしても壊れない）。
- **チェックポイント番号は PRC1 が 1..11 + `check_finish(12)`、PRC2 が `(1,2)`・`(2,2)` の2個。
  各プロセッサ内で単調増加であることを QEMU 出力で確認する。**

- [ ] **Step 5: 登録**
  - `test/MANIFEST` の `test_dcre2.h`（`:43`）の直後に、アルファベット順で
    `test_dcre3.c` `test_dcre3.cfg` `test_dcre3.h` の3行
    （Task 2 で足した `test_dcre_mix.*` との並び順に注意 — `test_dcre3` < `test_dcre_mix`）。
  - `test/testexec.rb` の `"dcre2"    => { SRC: "test_dcre2" },`（`:91`）の直後に
    `"dcre3"    => { SRC: "test_dcre3" },`。

- [ ] **Step 6: ビルド・実行**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
cmake --preset musca_b1-2core -B build/musca_b1-2core-tdcre3 \
  -DFMP3_APPLNAME=test_dcre3 -DFMP3_APPLDIR=test \
  -DFMP3_SYSSVC_TARGET_C_FILES=$PWD/syssvc/test_svc.c > /tmp/dcre3-t7-conf.log 2>&1
echo "conf rc=$?"
cmake --build build/musca_b1-2core-tdcre3 > /tmp/dcre3-t7-build.log 2>&1; echo "build rc=$?"
tools/cfg_equivalence.sh build/musca_b1-2core-tdcre3 > /tmp/dcre3-t7-eq.log 2>&1; echo "eq rc=$?"
timeout -k 5 90 cmake --build build/musca_b1-2core-tdcre3 --target run \
  > /tmp/dcre3-t7-run.log 2>&1
grep 'TTSP_RESULT: PASS' /tmp/dcre3-t7-run.log
grep -c 'Check point' /tmp/dcre3-t7-run.log
grep 'Check point 1-2 passed\|Check point 2-2 passed' /tmp/dcre3-t7-run.log
pgrep -a qemu
```
期待:
- build rc=0、eq rc=0（**AID_SEM/AID_FLG/AID_MTX 有りの実構成で両エンジンがバイト一致**）。
- `TTSP_RESULT: PASS` が実在（rc は見ない — Constraint 9）。
- `Check point 1 passed.` 〜 `Check point 11 passed.` の11行 + PRC2 の
  `Check point 1-2 passed.` `Check point 2-2 passed.` の2行 = 計13行。
- `pgrep` の出力なし。

★`timeout` は 90 秒。本テストは `dly_tsk(200ms)` を2回使うだけなので実時間は短いが、
QEMU の起動と `-icount` 無しの実行揺らぎに余裕を持たせる。

- [ ] **Step 7: ★カーネル変異 negative control（生きた経路で）**

`kernel/semaphore.c` の `del_sem` の
`queue_insert_prev(&free_semcb, &(p_semcb->wait_queue));` を
**一時的にコメントアウト**して再ビルド・再実行する。

```bash
# 変異を入れてから：
cmake --build build/musca_b1-2core-tdcre3 > /dev/null 2>&1
timeout -k 5 90 cmake --build build/musca_b1-2core-tdcre3 --target run \
  > /tmp/dcre3-t7-neg.log 2>&1
grep -c 'TTSP_RESULT: PASS' /tmp/dcre3-t7-neg.log   # 期待: 0
grep 'Unexpected\|## ' /tmp/dcre3-t7-neg.log | head
pgrep -a qemu
```
期待: **PASS 行が出ない**こと。
機序：`AID_SEM(2)` なので、手順1の `del_sem` で1個目が返却されず、
手順2の `acre_sem` が2個目を取り、その `del_sem` でも返却されないため、
**手順3の `acre_sem` が `E_NOID` を返し `check_assert(erid > SEM1)` が失敗する**
（`## Assertion failed` 相当の行が出る）＝`del_sem` の free-list 返却が**生きた経路**である証拠。

変異を**復元**し、Step 6 を再実行して `TTSP_RESULT: PASS` に戻ることまで確認する。
**`TNUM_PRCID == 1` でしか通らない死んだ分岐への変異は不可**
（段階1 で2度やらかした型。本変異は 2 コア構成で必ず通る経路である）。

- [ ] **Step 8: ★2つ目の変異 negative control（del_mtx の優先度復帰）**

手順6（mtx の優先度復帰）が空虚でないことを実演する。
`kernel/mutex.c` の `del_mtx` の

```c
			if (MTX_CEILING(p_mtxcb)) {
				mutex_drop_priority(p_my_pcb, p_loctsk,
										p_mtxcb->p_mtxinib->ceilpri);
			}
```

を**一時的にコメントアウト**して：

```bash
cmake --build build/musca_b1-2core-tdcre3 > /dev/null 2>&1
timeout -k 5 90 cmake --build build/musca_b1-2core-tdcre3 --target run \
  > /tmp/dcre3-t7-neg2.log 2>&1
grep -c 'TTSP_RESULT: PASS' /tmp/dcre3-t7-neg2.log   # 期待: 0
grep 'Assertion failed' /tmp/dcre3-t7-neg2.log | head
pgrep -a qemu
```
期待: **PASS 行が出ない**こと。`check_assert(tskpri == MID_PRIORITY)`（手順6の
del_mtx 直後）が失敗する＝優先度復帰の検査が実際に効いている証拠。
変異を**復元**し、`TTSP_RESULT: PASS` に戻ることまで確認する。

★**この2つ目の control は省略しないこと。** `del_mtx` のロック中削除は段階3a で
唯一「dcre の転写に加えて FMP3 の MP シグネチャへの読み替えと順序制約」がある箇所であり、
テストが通っていても「優先度復帰の行が実は死んでいる」可能性を潰す必要がある。

- [ ] **Step 9: 非退行 — test_dcre1 / test_dcre2 / test_int2 の再実行**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
for t in dcre1 dcre2 int2; do
  d=build/musca_b1-2core-t$t
  [ -d $d ] || cmake --preset musca_b1-2core -B $d \
      -DFMP3_APPLNAME=test_$t -DFMP3_APPLDIR=test \
      -DFMP3_SYSSVC_TARGET_C_FILES=$PWD/syssvc/test_svc.c > /tmp/dcre3-t7-$t-conf.log 2>&1
  cmake --build $d > /tmp/dcre3-t7-$t-build.log 2>&1; echo "$t build rc=$?"
done
```
```bash
timeout -k 5 60 cmake --build build/musca_b1-2core-tdcre1 --target run > /tmp/dcre3-t7-d1-run.log 2>&1
grep 'TTSP_RESULT: PASS' /tmp/dcre3-t7-d1-run.log ; pgrep -a qemu
```
```bash
timeout -k 5 60 cmake --build build/musca_b1-2core-tdcre2 --target run > /tmp/dcre3-t7-d2-run.log 2>&1
grep 'TTSP_RESULT: PASS' /tmp/dcre3-t7-d2-run.log ; pgrep -a qemu
```
```bash
timeout -k 5 60 cmake --build build/musca_b1-2core-tint2 --target run > /tmp/dcre3-t7-i2-run.log 2>&1
grep 'TTSP_RESULT: PASS' /tmp/dcre3-t7-i2-run.log ; pgrep -a qemu
```
期待: 3本とも `TTSP_RESULT: PASS` が実在。
（★`test_dcre1` は静的セマフォ・イベントフラグを使う可能性がある。E_NOEXS 挿入19箇所が
既存の静的オブジェクトの動作を壊していないことの実証として重要。）

- [ ] **Step 10: 台帳とコミット**

`DIVERGENCE_MAP.md` に `test/test_dcre3.c` `test/test_dcre3.cfg` `test/test_dcre3.h`
（種別 `add (dcre-port)`）と、`test/MANIFEST` `test/testexec.rb` の既存行への追記を行って：

```bash
git add -A && git commit -m "test(dcre): 動的生成sem/flg/mtxの回帰テスト test_dcre3 を追加（2コアQEMU・E_DLTと優先度復帰を実測）"
```

---

### Task 8: 最終回帰と台帳整理

**推奨モデル:** 中位（sonnet）

**Files:**
- Modify: `DIVERGENCE_MAP.md`（掃除）・`.superpowers/sdd/progress.md`（記録）

**このタスクではコードを直さない。** 欠陥を見つけたら**記録して報告**し、修正は別コミットに切る。

- [ ] **Step 1: 全9プリセット configure+build**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
for p in musca_b1 musca_b1-2core rp2350_pico2 polarfire_soc_kit polarfire_soc_kit-qemu \
         kria_arm64 kria_arm64-1core kria_r5 kria_r5-2core; do
  cmake --preset $p > /tmp/dcre3-t8-conf-$p.log 2>&1; c=$?
  cmake --build build/$p > /tmp/dcre3-t8-build-$p.log 2>&1; b=$?
  echo "$p conf=$c build=$b"
done
```
期待: `polarfire_soc_kit`（実機プリセット）のみ SoftConsole ツールチェーン不在
（`fatal error: cannot read spec file 'nano.specs'`）で**既知の環境ギャップとして fail**。
それ**以外の8構成が exit=0**。

- [ ] **Step 2: 全8構成 + 派生3ビルドの `tools/cfg_equivalence.sh`**

```bash
for p in musca_b1 musca_b1-2core rp2350_pico2 polarfire_soc_kit-qemu \
         kria_arm64 kria_arm64-1core kria_r5 kria_r5-2core; do
  tools/cfg_equivalence.sh build/$p > /tmp/dcre3-t8-eq-$p.log 2>&1
  echo "$p eq=$?"
done
for d in musca_b1-2core-tmix musca_b1-2core-tdcre3; do
  tools/cfg_equivalence.sh build/$d > /tmp/dcre3-t8-eq-$d.log 2>&1
  echo "$d eq=$?"
done
```
期待: 全て 0。**2 は不合格**（Constraint 11）。
`-tmix` は**混在 AID 構成**、`-tdcre3` は **AID_SEM/AID_FLG/AID_MTX 全部入り構成**なので、
この2つが段階3a の cfg 変更を実構成で検査している本体である。

- [ ] **Step 3: QEMU 起動7構成（★プリセットごとに個別実行）**

段階1 Task 7 では `for` ループで全構成を1コマンドに詰めた結果、Bash ツールの
2分タイムアウトに当たり **qemu が孤児化した**。**1プリセット1コマンド**で実行し、
毎回 `pgrep` で残存を確認する。

```bash
timeout -k 5 25 cmake --build build/polarfire_soc_kit-qemu --target run > /tmp/dcre3-t8-run-polarfire.log 2>&1
grep -c 'Processor [1-4] start\.' /tmp/dcre3-t8-run-polarfire.log   # 期待: 4
pgrep -a qemu
```
```bash
timeout -k 5 25 cmake --build build/musca_b1 --target run > /tmp/dcre3-t8-run-musca1.log 2>&1
grep -c 'Processor 1 start\.' /tmp/dcre3-t8-run-musca1.log          # 期待: 1
pgrep -a qemu
```
```bash
timeout -k 5 25 cmake --build build/musca_b1-2core --target run > /tmp/dcre3-t8-run-musca2.log 2>&1
grep -c 'Processor [12] start\.' /tmp/dcre3-t8-run-musca2.log       # 期待: 2
pgrep -a qemu
```
```bash
timeout -k 5 25 cmake --build build/kria_arm64-1core --target run > /tmp/dcre3-t8-run-arm64-1.log 2>&1
grep -c 'Processor 1 start\.' /tmp/dcre3-t8-run-arm64-1.log         # 期待: 1
pgrep -a qemu
```
```bash
timeout -k 5 25 cmake --build build/kria_arm64 --target run > /tmp/dcre3-t8-run-arm64-4.log 2>&1
grep -c 'Processor [1-4] start\.' /tmp/dcre3-t8-run-arm64-4.log     # 期待: 4
pgrep -a qemu
```
```bash
timeout -k 5 25 cmake --build build/kria_r5 --target run > /tmp/dcre3-t8-run-r5-1.log 2>&1
grep -c 'Processor 1 start\.' /tmp/dcre3-t8-run-r5-1.log            # 期待: 1
pgrep -a qemu
```
```bash
timeout -k 5 25 cmake --build build/kria_r5-2core --target run > /tmp/dcre3-t8-run-r5-2.log 2>&1
grep -c 'Processor [12] start\.' /tmp/dcre3-t8-run-r5-2.log         # 期待: 2
pgrep -a qemu
```
（`rp2350_pico2` は QEMU にマシンモデルが無く `run` ターゲット自体が無い＝設計どおり。
`kria_r5-2core` は既知どおり rc=124 になるが、**両 Processor start 行が出ていれば合格**
＝rc 単独で判定しない（Constraint 9）。）
**各コマンドの後に `pgrep -a qemu` が何も出さないこと。** 出たら
`pkill -f qemu-system` で掃除し、その事実を記録する。

- [ ] **Step 4: 機能テスト4本の再実行**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
timeout -k 5 90 cmake --build build/musca_b1-2core-tdcre3 --target run > /tmp/dcre3-t8-tdcre3.log 2>&1
grep 'TTSP_RESULT: PASS' /tmp/dcre3-t8-tdcre3.log ; grep -c 'Check point' /tmp/dcre3-t8-tdcre3.log ; pgrep -a qemu
```
```bash
timeout -k 5 60 cmake --build build/musca_b1-2core-tdcre2 --target run > /tmp/dcre3-t8-tdcre2.log 2>&1
grep 'TTSP_RESULT: PASS' /tmp/dcre3-t8-tdcre2.log ; pgrep -a qemu
```
```bash
timeout -k 5 60 cmake --build build/musca_b1-2core-tdcre1 --target run > /tmp/dcre3-t8-tdcre1.log 2>&1
grep 'TTSP_RESULT: PASS' /tmp/dcre3-t8-tdcre1.log ; pgrep -a qemu
```
```bash
timeout -k 5 60 cmake --build build/musca_b1-2core-tint2 --target run > /tmp/dcre3-t8-tint2.log 2>&1
grep 'TTSP_RESULT: PASS' /tmp/dcre3-t8-tint2.log ; pgrep -a qemu
```
期待: 4本とも `TTSP_RESULT: PASS` が実在。`test_dcre3` は `Check point` 13行。

- [ ] **Step 5: エラー経路回帰マトリクス（既存12件 + 段階3a 新規6件 = 18件）**

★**`run.sh` は4引数形**（`<builddir> <cfg> <期待ercd> [EXTRA_CFLAGS]`）。
`#include "test_int2.h"` を含む cfg は**第4引数が必須**（付けないと rc=2）。
段階2 Task 8 ではブリーフの列に引数が抜けていて実装者が自力訂正した — 本計画では**全件に明記**する。

まず実在するファイルと突き合わせる（★下の列と食い違ったら、実在するものに合わせ、
**合わせた事実を記録する**）：

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
ls tools/cfg_error_tests/*.cfg | sort
```

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
R=tools/cfg_error_tests/run.sh
T=tools/cfg_error_tests
X="-I$PWD/test"
M=build/musca_b1-2core
$R build/polarfire_soc_kit-qemu $T/e_par_creisr_intno_keyerror.cfg   E_PAR;         echo "01:$?"
$R $M $T/musca_b1_e_rsatr_intno_affinity.cfg                          E_RSATR;      echo "02:$?"
$R build/kria_r5-2core $T/kria_r5_e_rsatr_intno_affinity.cfg          E_RSATR;      echo "03:$?"
$R $M $T/dcre_aid_in_class.cfg          E_RSATR "$X"; echo "04:$?"
$R $M $T/dcre_mpk_in_class.cfg          E_RSATR "$X"; echo "05:$?"
$R $M $T/dcre_mpk_zero.cfg              E_PAR   "$X"; echo "06:$?"
$R $M $T/dcre_mpk_double.cfg            E_OBJ   "$X"; echo "07:$?"
$R $M $T/dcre_mpk_misaligned.cfg        E_PAR   "$X"; echo "08:$?"
$R $M $T/dcre_aid_tsk_no_static.cfg     E_OBJ   "$X"; echo "09:$?"
$R $M $T/dcre_aid_cyc_in_class.cfg      E_RSATR "$X"; echo "10:$?"
$R $M $T/dcre_aid_alm_in_class.cfg      E_RSATR "$X"; echo "11:$?"
$R $M $T/dcre_aid_cyc_no_static.cfg     E_OBJ   "$X"; echo "12:$?"
```
```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
R=tools/cfg_error_tests/run.sh
T=tools/cfg_error_tests
X="-I$PWD/test"
M=build/musca_b1-2core
$R $M $T/dcre_aid_sem_in_class.cfg      E_RSATR "$X"; echo "13:$?"
$R $M $T/dcre_aid_flg_in_class.cfg      E_RSATR "$X"; echo "14:$?"
$R $M $T/dcre_aid_mtx_in_class.cfg      E_RSATR "$X"; echo "15:$?"
$R $M $T/dcre_aid_sem_no_static.cfg     E_OBJ   "$X"; echo "16:$?"
$R $M $T/dcre_aid_flg_no_static.cfg     E_OBJ   "$X"; echo "17:$?"
$R $M $T/dcre_aid_mtx_no_static.cfg     E_OBJ   "$X"; echo "18:$?"
```
期待: **18件すべて 0**（両エンジンが同じ ercd を同じ文言で検出）。
rc=2 は**前提未充足であり合格ではない**（EXTRA_CFLAGS の付け忘れか、
cfg が別のエラーで先に落ちている）。

★`ls` の結果に `rp2350_e_rsatr_intno_affinity.cfg` / `e_rsatr_inhno_affinity.cfg` /
`musca_b1_clsid_warning.cfg` のような**上の18件に無いファイル**が見つかった場合は、
「段階1/2 の回帰列に入っていなかったファイル」として**実行して結果を記録する**
（対象 builddir・期待 ercd は cfg 冒頭のコメントから読む。
`rp2350_*` は `build/rp2350_pico2`）。**列に無いから無視、はしない。**

★**hardening #1（TEPP_PRC マスタ bit 検査）の永続的なエラー回帰 cfg は存在しない**
（訂正H。全8プリセットで bit0 が立ち、`.cfg` からは `TOPPERS_TEPP_PRC` を変えられない）。
Task 2 Step 5 の mutation control が唯一の非空虚性の証拠であることを progress.md に明記する。

- [ ] **Step 6: `KERNEL_FCSRCS` 突き合わせ**（AGENTS.md §4。22個のまま不変のはず）

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
diff <(awk '/^KERNEL_FCSRCS/{f=1} f{print} f&&!/\\$/{exit}' kernel/Makefile.kernel \
       | grep -oE '[a-z_]+\.c' | sort -u) \
     <(grep -oE 'kernel/[a-z_]+\.c' CMakeLists.txt | sed 's#kernel/##' | sort -u)
echo "diff rc=$?"
```
期待: rc=0（差分なし）。段階3a は既存 `.c` にしか手を入れていないので変化しないはず。

- [ ] **Step 7: `DIVERGENCE_MAP.md` の完全性監査**

```bash
cd /home/honda/TOPPERS/FMP3/fmp3_core
git diff main...HEAD --name-only > /tmp/dcre3-t8-changed.txt
wc -l /tmp/dcre3-t8-changed.txt
while read f; do
  case "$f" in
    docs/*|tools/*|cmake/*|CMakeLists.txt|CMakePresets.json|cfg_py/*|.superpowers/*|kernel/*.py) continue;;
  esac
  grep -q -- "$f" DIVERGENCE_MAP.md || echo "MISSING: $f"
done < /tmp/dcre3-t8-changed.txt
```
期待: `MISSING:` が**1行も出ない**。
（除外パターンは段階2 Task 8 の裁定に従う＝`kernel/*.py` は Python cfg エンジンの派生
テンプレートで `DIVERGENCE_MAP.md:17` の「kernel/*.py（15個）」枠に記録済み。
`kernel/*.trb` は **pristine なので除外しない**。）

段階3a で触った pristine は次のとおり（段階1/2 分は既に記録済み）：
`kernel/kernel.trb`、`kernel/cyclic.trb`、`kernel/alarm.trb`、
`kernel/cyclic.c`、`kernel/alarm.c`、`kernel/kernel_api.def`、`include/kernel.h`、
`kernel/semaphore.h`、`kernel/semaphore.c`、`kernel/eventflag.h`、`kernel/eventflag.c`、
`kernel/mutex.h`、`kernel/mutex.c`、`kernel/allfunc.h`、`kernel/Makefile.kernel`、
`kernel/kernel_rename.def`＋再生成2、
`test/test_dcre3.*`、`test/test_dcre_mix.*`、`test/MANIFEST`、`test/testexec.rb`。
漏れが（あってはならないが）見つかったら**理由込みで**追記する。

あわせて次を反映する：
- **上流報告候補に d を追加するかを判断する**：dcre `eventflag.c:257` の
  `del_flg` が `CHECK_PAR(VALID_FLGID(flgid))`（E_PAR）を使っており、
  `del_sem`/`del_mtx`（`CHECK_ID`＝E_ID）および TOPPERS 標準と食い違う（訂正D）。
  **証拠は行番号つきで完備**しているので、既存候補 a/b/c と同じ書式で候補 d として記録する
  （送付するかはユーザ判断。b は段階2 で解消済みだが上流には未報告のまま）。
- **未 hardening の記録**：`acre_mtx` で `mtxatr != TA_CEILING` のとき `ceilpri` が
  未検査のまま `INT_PRIORITY()` に通る点（dcre 由来・`MTX_CEILING()` が偽なので読まれない）。

- [ ] **Step 8: `.superpowers/sdd/progress.md` へ段階3a 完了を記録し、コミット**

記録に含めること（**推測と事実を分ける**）：
- Task 1 の実装前確認7項目の結論と、それに基づく spec 訂正7件（A〜H のうち G/H は
  「構成可否の裁定」であって現物の誤りではないことを明示する）。
- 【事実】sem/flg/mtx は**非親和オブジェクト**であり、段階2 の Constraint 4
  （`iprcid`/`affinity`/`p_pcb` の充填）に相当するコードは**1行も書いていない**こと。
  free-list のリンクも tmevtb オーバーレイではなく `wait_queue` 直用のため、
  **段階2 訂正D（64bit で `callback` が上書きされる）は構造的に発生しなかった**こと。
- 【事実】`SEMID`/`FLGID`/`MTXID` は**既存マクロの2レンジ化置換**であり、
  段階2 の `CYCID`（新設）とは作業の性質が違うこと。
- 【事実】dcre からの意図的な逸脱の一覧：
  (1) 3つの `del_*` の `CHECK_TSKCTX_UNL_MYSTATE`（訂正C）、
  (2) `del_flg` の `CHECK_ID`（訂正D。dcre 側の不整合）、
  (3) `acre_sem` の恒真 `CHECK_PAR` 除去（訂正E。段階2 `acre_cyc` と同型）、
  (4) `del_mtx` の `p_loctsk = NULL`（訂正F。FMP3 `ini_mtx` に倣う）、
  (5) 返値型 `ER_UINT` → `ER_ID`（段階1/2 と同じ）、
  (6) glock の対化と空判定 `tnum_* == tnum_s*`（全 `acre_*` 共通）。
- 【事実】E_NOEXS 挿入は **19関数**（sem 6 / flg 7 / mtx 6）で、
  **段階2 の `msta_cyc`/`msta_alm` のような「上流に先例が無い類推適用」は1件も無かった**
  （FMP3 固有関数が sem/flg/mtx に存在しないため）。
- 【事実】`del_mtx` の順序制約（`MTX_CEILING()` が `mtxatr` を読むため優先度復帰は
  `TA_NOEXS` 書込みより前）と、それを Task 7 Step 8 の**2つ目の変異 control** で
  実演したこと。
- 【事実】hardening #1 の**永続的なエラー回帰 cfg は構成不能**であり
  （全プリセットで `TOPPERS_TEPP_PRC` の bit0 が立つ・`.cfg` から変更不能）、
  Task 2 Step 5 の条件反転 mutation control が唯一の非空虚性の証拠であること。
- 【推測含む・引き継ぎ課題】`acre_mtx` の `mtxatr != TA_CEILING` 時の未検査 `ceilpri`
  （dcre 由来。現状は読まれないので無害）。段階1 deferred #1・段階2 の
  `msta_cyc`/`msta_alm` のロック前 inib 読みと**同系統の「ユーザ誤用経路の hardening」課題**
  として引き継ぐ。段階3a では到達可能性の実証も修正も行っていない。
- 【段階3b（dtq/pdq/mpf）への引き継ぎ】
  - `AID_*` の cfg 共通枠組みは段階3a でも**per-object 変更ゼロ**で通った（4オブジェクト種目）。
    dtq/pdq/mpf は**管理領域（`dtqmb`/`pdqmb`/`mpf`/`mpfmb`）を伴う**ため
    per-object テンプレートの変更が**初めて必要になる**見込みである。
  - dtq/pdq/mpf は管理領域確保に `malloc_mpk` を要するため、**段階1最終レビュー Important #1
    （`del_tsk` 後のプール再利用と自終了タスクのスタック残余ウィンドウの理論的重なり）を
    3b 設計時に必ず再評価すること**（段階2・段階3a はいずれもプールを使わないため不発のまま）。
  - `TA_MBALLOC` の定義追加が要る（段階3 調査記録）。
- 【ISR への引き継ぎ】着手順は **3a → 3b → ISR**（裁定済み）。ISR は案B ハイブリッド + API 拡張。

```bash
git add -A && git commit -m "chore(dcre): 段階3aの最終回帰と台帳整理"
```

---

## Self-Review 済み事項（計画作成時の検証記録）

**spec 要件 → Task 対応:**

| spec | 内容 | Task |
|---|---|---|
| GC 1 | ブランチ・台帳 | 全 Task（各 Task の最終 Step） |
| GC 2 | スコープ（3b/ISR を含めない） | 計画全体の Files 一覧で境界を固定 |
| GC 3 | dcre 標準 API のみ | T3 Step 3-4 |
| **GC 4** | **プロセッサ親和なし・充填コードを書かない** | **T1 Step 7/8（ゲート）、T4/T5/T6 の各 Step 2-3 に明示的な禁止注記** |
| GC 5 | F-1 検証（両エンジン・exit 0 のみ合格） | T3 Step 5-10、T4-T7 の各ビルド Step、T8 Step 2 |
| GC 6 | CB はヒープ確保しない・free-list は wait_queue 直用 | T4/T5/T6 Step 1-2 |
| GC 7 | free-list は FIFO（再議しない） | T4/T5/T6 Step 2（`queue_insert_prev`）、T7 の「空き1個」形テスト |
| GC 8 | 汎用層・KERNEL_FCSRCS 不変 | T8 Step 6 |
| GC 9 | rc=124 単独で判定しない | T7 Step 6、T8 Step 3 |
| §1.1 | `T_CSEM`/`T_CFLG`/`T_CMTX` | T3 Step 3 |
| §1.2 | 6サービスコール宣言・機能コード | T3 Step 4（コードは既存＝T1 Step 1 で確定） |
| §1.3 | エラーコード（E_RSATR/E_PAR/E_NOID/E_NOEXS/E_OBJ・E_NOMEM 無し） | T4/T5/T6 の acre/del、T7 手順1/4/5/6/7 |
| §2 | del の意味論（E_DLT・ロック中 del_mtx・優先度復帰） | T4/T5/T6 の del_*、T7 手順2/3/6 + T7 Step 8 の変異 control |
| §3 | cfg 層（3行・per-object 変更ゼロ・訂正E ガードの自動適用） | T3（全体）、T1 Step 6 |
| §4 | カーネル層（free-list・initialize・2レンジ ID・E_NOEXS 19・配線） | T4/T5/T6（全体） |
| §5-1 | hardening: TEPP_PRC の cfg enforcement | T2 Step 2-5（★エラー回帰 cfg は構成不能＝訂正H） |
| §5-2 | hardening: p_pcb-stale コメント | T2 Step 6 |
| §5-3 | hardening: 混在 AID サンプル | T2 Step 7-8（cyc/alm）→ T3 Step 11（sem/mtx 拡張） |
| §5-4 | hardening: CHECK_PAR 逸脱の台帳追記 | T2 Step 9（+ T4 Step 10 で `acre_sem` の2件目） |
| §6 | MP 安全性（glock 下・残余ウィンドウ不存在） | T1 Step 3/7、T4-T6 の acre/del、T7 手順3（PRC2 の E_DLT） |
| §7 | test_dcre3 の8シナリオ | T7 Step 4（手順1-7）+ Step 7/8（変異 control ×2）+ Step 9（非退行） |
| §8 | 実装前確認7項目 | T1 Step 1-8 |
| §9 | 統治（8タスク・台帳・全構成回帰・引き継ぎ） | T8 |

**現物確認済み（計画作成時に実ファイルで確認した事実）:**
- `TFN_ACRE_SEM (-194)` / `TFN_ACRE_FLG (-195)` / `TFN_ACRE_MTX (-199)` /
  `TFN_DEL_SEM (-210)` / `TFN_DEL_FLG (-211)` / `TFN_DEL_MTX (-215)`
  （`include/kernel_fncode.h:135,136,139,147,148,151`）＝**6件とも既存**。
- `SEMID`/`FLGID`/`MTXID` が **既に存在**し既に inib ポインタ差分式であること
  （`semaphore.h:98-99` / `eventflag.h:97-98` / `mutex.h:98-99`）。
  CB は `SEMCB *const p_semcb_table[]`（`semaphore.h:93`）等の**ポインタ表**。
- 3つの INIB に `iprcid`/`affinity` が**無い**（`semaphore.h:61-65` / `eventflag.h:61-64` /
  `mutex.h:59-62`）。3つの CB に `p_pcb` が**無い**、かつ**先頭フィールドが `QUEUE wait_queue`**
  （`semaphore.h:74-78` / `eventflag.h:73-77` / `mutex.h:73-78`）。
- `initialize_semaphore`（`semaphore.c:118-136`）/ `initialize_eventflag`（`eventflag.c:126-144`）/
  `initialize_mutex`（`mutex.c:124-145`）が**既に全体マスタ限定**で、
  段階2 `initialize_cyclic` にあった `p_tevtcb == NULL` 判定を**持たない**こと。
- `tnum_sem`/`tnum_flg`/`tnum_mtx` が `.c` 側（`semaphore.c:107` / `eventflag.c:115` /
  `mutex.c:106`）にあり `.h` に無いこと（移設が要る）。
- `#include <queue.h>` が3つの `.h` に**既にある**こと（`semaphore.h:51` 他）。
- `init_wait_queue(PCB *, QUEUE *)`（`wait.h:249`、本体 `wait.c:215-228`）が
  **MP 対応済み**で `winfo.wercd = E_DLT` → `make_non_wait(p_my_pcb, p_tcb)` を行うこと。
- `remove_mutex(TCB *, MTXCB *)`（`mutex.c:223-237`）と
  `mutex_drop_priority(PCB *, TCB *, uint_t)`（`mutex.c:271-284`、**MP 版は p_my_pcb が先頭**）。
- `ini_mtx`（`mutex.c:586-631`）の呼出し順：`init_wait_queue` → `p_loctsk` 退避 →
  `p_mtxcb->p_loctsk = NULL` → `remove_mutex` → `MTX_CEILING` なら `mutex_drop_priority` →
  `p_selftsk != p_my_pcb->p_schedtsk` なら `release_glock(); dispatch();`。
- `unl_mtx`（`mutex.c:536-581`）が `mutex_drop_priority(p_my_pcb, p_selftsk, ceilpri)` →
  `mutex_release(p_my_pcb, p_mtxcb)` の順で呼ぶこと。
- `MTX_CEILING(p_mtxcb)` が `p_mtxcb->p_mtxinib->mtxatr & 0x03U == TA_CEILING`（`mutex.c:114-119`）
  ＝**`mtxatr` を読む**こと（del_mtx の順序制約の根拠）。
- `check_tskctx_unl_mystate`（`check.h:177-199`）が `check_tskctx_unl`（`check.h:146-171`）と
  **E_CTX 判定が厳密に等価**で `*pp_selftsk = p_my_pcb->p_runtsk` を足しただけであること。
- dcre `del_flg`（`eventflag.c:257`）だけが `CHECK_PAR`（E_PAR）で、
  `del_sem`（`:230`）`del_mtx`（`mutex.c:440`）は `CHECK_ID`（E_ID）であること。
- dcre `acre_sem`（`semaphore.c:189`）の `CHECK_PAR(0 <= isemcnt && ...)` が
  `uint_t` に対して恒真であること。
- dcre `del_mtx`（`mutex.c:450-471`）が `p_mtxcb->p_loctsk` を**クリアしない**こと。
- `TMAX_MAXSEM`（`include/kernel.h:659` = `UINT_MAX`、`kernel_sym.def:39`）、
  `VALID_TPRI`（`check.h:70`）、`INT_PRIORITY`（`task.h:70`、`mutex.c:47` が `task.h` を include）、
  `TA_TPRI`=0x01/`TA_WMUL`=0x02/`TA_CLR`=0x04/`TA_CEILING`=0x03（`include/kernel.h:506-511`）、
  `TA_NOEXS`（`kernel_impl.h:199`）、`TMIN_TPRI`=1/`TMAX_TPRI`=16（`:600-601`）、
  `TSK_SELF`=0（`:581`）、`get_pri`（`:307`）。
- cfg 共通枠組みが `self.obj_s` から `a{obj_s}inib_table` / `_kernel_a{obj_s}cb_<i>` /
  `_kernel_tmax_s{obj}id` / `TNUM_S{OBJ}ID` を**機械的に導く**こと
  （`kernel.py:122,177-181,225,233,254`）。sem/flg/mtx の各クラスは
  `super().__init__("sem", "semaphore")` 等で**カスタマイズ皆無**であること
  （`semaphore.py` は `prepare`/`generateInib` のみ）。
- 訂正E ガード（`kernel.py:145-152` / `kernel.trb:150-157`）と
  `has_aid` ブロック（`kernel.py:133-141` / `kernel.trb:138-146`）が
  **オブジェクト種別に依存しない**こと。
- `TOPPERS_TEPP_PRC` と `TOPPERS_MASTER_PRCID` が cfg から参照可能なシンボルであること
  （`kernel_sym.def:53-54`、使用例 `cyclic.py:70` / `kernel.py:587`）。
- `CMakeLists.txt:234,683` が `.cfg` と `.c` を**同じ `FMP3_APPLNAME` から導く**こと
  （＝cfg 単独バリアントは作れない＝訂正G の根拠）。
- `tools/cfg_error_tests/run.sh:40` の `EXTRA_CFLAGS="${4:-}"`（4引数形）。
- `tools/cfg_error_tests/` の実在ファイル15個（うち `dcre_aid_cyc_no_static.cfg` は
  `INCLUDE("test/test_common1.cfg")` + 第4引数の書式）。
- `test/MANIFEST:38-43` と `test/testexec.rb:90-91` の登録形式。
- `check_point(count)` = `check_point_prc(count, 0)` = `check_count[0]`（`test_svc.h:112`）。
- `CLS_PRC2` が既存テスト（`test/test_mmutex1.cfg:18` ほか）で使われていること。
- `kernel_api.def` の `CRE_SEM #semid* { .sematr .isemcnt .maxsem }`（`:2`）/
  `CRE_FLG #flgid* { .flgatr .iflgptn }`（`:3`）/ `CRE_MTX #mtxid* { .mtxatr +ceilpri? }`（`:6`）と、
  末尾の `AID_TSK .notsk` / `DEF_MPK { .mpksz &mpk? }` / `AID_CYC .nocyc` / `AID_ALM .noalm`。
- `kernel/Makefile.kernel` の `semaphore =`（`:86-87`）/ `eventflag =`（`:89-90`）/
  `mutex =`（`:100-101`）と `KERNEL_FCSRCS`（`:51-56`、22個）。
- `kernel/allfunc.h` の `/* semaphore.c */`（`:134-141`）/ `/* eventflag.c */`（`:143-152`）/
  `/* mutex.c */`（`:187-200`。★`TOPPERS_mtxscan`/`TOPPERS_mtxdrop` という
  `mutex.c` に対応区画が無いエントリが `:190-191` にある）。
- `kernel/kernel_rename.def` の `# semaphore.c`（`:64-65`）/ `# eventflag.c`（`:67-69`）/
  `# mutex.c`（`:87-92`）と、段階2 で拡張済みの `# cyclic.c`（`:106-112`）/ `# alarm.c`（`:114-120`）。
- 段階2 の `CYCID`（`kernel/cyclic.h:119-129`）と動的スロット初期化ブロック
  （`kernel/cyclic.c:152-174`）の実物（本計画の sem/flg/mtx 版はこれと同型）。

**未検証（実装者が最初に当たること）:**
- **3つの `.h` の `#include <queue.h>` の実在**（T1 Step 7）。`mutex.h` だけは
  計画作成時に行番号を確定できていない。無ければ追加し台帳に書く。
- **`TSK_NONE` の存在**（T7 Step 1）。`ref_sem` の `wtskid` 比較に使う。無ければその assert を落とす。
- **`TARGET_SEMATR`/`TARGET_FLGATR`/`TARGET_MTXATR` 相当のターゲット固有属性ビットの有無**
  （T7 Step 4 の `E_RSATR` テストで使う「未定義ビット」が衝突しないか）。
- **`sample/sample1.cfg` が `CRE_SEM`/`CRE_FLG`/`CRE_MTX` を含むか**（T3 Step 7 の positive control、
  T4 Step 9 / T6 Step 9 の「静的オブジェクトが壊れていないことの実証」の成立条件）。
  含まないなら `test_dcre_mix` 側の構成で代替し、代替した事実を記録する。
- **`test/test_dcre1.c` が静的 sem/flg を使うか**（T7 Step 9 の非退行の意味づけ）。
- **`ext_tsk()` の書き方**（T7 Step 4。`test/` の既存テストの流儀に合わせる）。
- **`tools/cfg_error_tests/` の実在ファイルと Task 8 Step 5 の18件列の一致**
  （段階2 Task 8 の記録に基づくが、列に無い3ファイルの扱いは T8 Step 5 で決める）。
- **Task 2 Step 5 の `sed` が意図した行だけを書き換えるか**（巻き込んだら手で編集する）。
- **Task 2 Step 5 の `git checkout --` 後に Step 4 の変更を入れ直したか**
  （入れ直し忘れると hardening #1 が入らないまま完了報告する事故になる）。
