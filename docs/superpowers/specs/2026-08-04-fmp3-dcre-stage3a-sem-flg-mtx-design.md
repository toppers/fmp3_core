# FMP3 動的生成API 段階3a（sem/flg/mtx）設計書

**Goal:** `feature/dynamic-creation` ブランチ上で、セマフォ（`acre_sem`/`del_sem` +
`AID_SEM`）・イベントフラグ（`acre_flg`/`del_flg` + `AID_FLG`）・ミューテックス
（`acre_mtx`/`del_mtx` + `AID_MTX`）の動的生成を dcre 忠実移植で実現する。
あわせて段階2最終レビューの hardening 4件を実装する。

**参照:**
- 段階1 spec: `2026-08-03-fmp3-dynamic-creation-design.md`（§4.5 共通基盤）
- 段階2 spec: `2026-08-04-fmp3-dcre-stage2-cyc-alm-design.md`（実行済み前例）
- dcre 原典: `/home/honda/TOPPERS/ASP3/asp3_3.7/extension/dcre/kernel/`
  `semaphore.{h,c}`・`eventflag.{h,c}`・`mutex.{h,c}`（現行ソースが正、DIFF は参考）
- 調査記録: 6オブジェクト構造調査（2026-08-04、コントローラ ledger 参照）

**スコープ外:** dtq/pdq/mpf（段階3b — 管理領域の malloc_mpk と TA_MBALLOC を伴うため、
プール再利用と自終了スタック残余ウィンドウの裁定〔段階1最終レビュー Important #1〕を
3b 設計時に行ってから着手）。ISR（別計画・方式は案B ハイブリッドで裁定済み）。

---

## Global Constraints（段階1/2 から継承。実装中に再解釈しない）

1. ブランチ `feature/dynamic-creation`。main へマージしない。pristine 改変は
   `DIVERGENCE_MAP.md` に記録（種別 `mod (dcre-port)`、上流報告欄 `-`）。
2. 段階3a = sem/flg/mtx の acre/del + AID 3種 + 段階2 hardening 4件のみ。
   dtq/pdq/mpf・ISR・段階4的な何かを含めない。
3. API 面は dcre 標準のみ：`T_CSEM`/`T_CFLG`/`T_CMTX`（dcre include/kernel.h:223-291 と
   同一）。独自 API なし。`AID_*` はクラス外専用（E_RSATR）。
4. **プロセッサ親和なし**: sem/flg/mtx の INIB に iprcid/affinity は存在しない
   （現物確認済み — semaphore.h:61-78 等）。Constraint 4（PRC1 固定）の類推は**不要**。
   充填コードを書かないこと（書いたらそれはバグ）。
5. 検証 = F-1：Ruby オラクルと Python 製品の両エンジン同時変更、
   `tools/cfg_equivalence.sh` exit 0 のみ合格（2 は前提未充足で不合格）。
6. CB はヒープ確保しない。free-list のリンクは **CB 先頭の wait_queue
   （既存 QUEUE フィールド）を直接流用**（dcre semaphore.c:145-161 と同一。
   cyclic の tmevtb オーバーレイ技法は**使わない** — 不要）。
7. free-list は FIFO（del=queue_insert_prev / acre=queue_delete_next）。裁定済み・再議しない。
8. 汎用層（CMakeLists.txt・fmp3_core.cmake・cfg_py/pass1.py・pass2.py）不変。
   KERNEL_FCSRCS 不変（acre/del は既存 .c に入る）。
9. rc=124 単独を成功判定にしない。パイプで成否判定しない。QEMU はプリセット個別実行＋
   pgrep 残存確認。

---

## 1. API 定義

### 1.1 パケット型（dcre include/kernel.h:223-291 と同一。T_CALM の後に置く）

```c
typedef struct t_csem {
	ATR			sematr;		/* セマフォ属性 */
	uint_t		isemcnt;	/* セマフォの初期資源数 */
	uint_t		maxsem;		/* セマフォの最大資源数 */
} T_CSEM;

typedef struct t_cflg {
	ATR			flgatr;		/* イベントフラグ属性 */
	FLGPTN		iflgptn;	/* イベントフラグの初期ビットパターン */
} T_CFLG;

typedef struct t_cmtx {
	ATR			mtxatr;		/* ミューテックス属性 */
	PRI			ceilpri;	/* ミューテックスの上限優先度 */
} T_CMTX;
```

### 1.2 サービスコール

```c
extern ER_ID	acre_sem(const T_CSEM *pk_csem) throw();
extern ER		del_sem(ID semid) throw();
extern ER_ID	acre_flg(const T_CFLG *pk_cflg) throw();
extern ER		del_flg(ID flgid) throw();
extern ER_ID	acre_mtx(const T_CMTX *pk_cmtx) throw();
extern ER		del_mtx(ID mtxid) throw();
```

機能コード（TFN_ACRE_SEM 等6個）は kernel_fncode.h に既存（**訂正A**: 実装前確認で確定済み）:
TFN_ACRE_SEM(-194):135 / TFN_ACRE_FLG(-195):136 / TFN_ACRE_MTX(-199):139 /
TFN_DEL_SEM(-210):147 / TFN_DEL_FLG(-211):148 / TFN_DEL_MTX(-215):151。

### 1.3 エラーと検査（dcre 準拠）

- `acre_sem`: CHECK_VALIDATR(sematr, TA_TPRI)、**訂正E**: dcre の検査のうち uint_t に
  対して恒真の下限 `0 <= isemcnt` のみ落とし、**`isemcnt <= maxsem` は維持**、
  `1 <= maxsem && maxsem <= TMAX_MAXSEM` も維持（dcre semaphore.c:188-189 準拠）、E_NOID。
- `acre_flg`: CHECK_VALIDATR(flgatr, TA_TPRI|TA_WMUL|TA_CLR)、E_NOID。
- `acre_mtx`: `mtxatr == TA_CEILING` なら VALID_TPRI(ceilpri) を検査、それ以外は
  CHECK_VALIDATR(mtxatr, TA_TPRI)（dcre mutex.c:395-410 の分岐そのまま）。E_NOID。
- `del_*` 共通: E_NOEXS（TA_NOEXS）→ **訂正D**: `del_flg` も CHECK_ID（E_ID）、E_PAR ではない
  （dcre の del_sem/del_mtx および FMP3 の flg 系多数派と同型。dcre の del_flg 自身は CHECK_PAR で非一貫 — 上流報告候補 d）
  → E_OBJ（静的 = id <= tmax_s*id）→ 成功。
  **E_NOMEM 経路なし**（3オブジェクトともプール不使用）。

## 2. del の意味論（dcre 忠実 — ユーザ承認済み 2026-08-04）

- **待ちタスクがいても削除は成功**し、待ちタスクは E_DLT で強制解除する。
  実装は FMP3 既存の `init_wait_queue(p_my_pcb, &wait_queue)`（wait.c:215-228、
  **MP 対応済み**・既存 ini_* と同一機構）を呼ぶだけ。新規の解除機構は書かない。
- **del_mtx はロック中でも成功**（dcre mutex.c:430-475 と同一）。ロック中の場合の手順は
  FMP3 の ini_mtx（mutex.c:606-614）と同一順序:
  ① `p_loctsk = p_mtxcb->p_loctsk;`（ローカルへ退避）
  ② `p_mtxcb->p_loctsk = NULL;`（CB フィールドのクリア — **訂正F**: dcre は省くが ini_mtx に倣う）
  ③ `remove_mutex(p_loctsk, p_mtxcb)`（②の後でも①のローカルは非 NULL なので安全）
  ④ ceiling mutex なら `mutex_drop_priority(p_my_pcb, p_loctsk, ...)` で優先度復帰
     （MP 版は p_my_pcb が先頭引数）
  **重要**: `MTX_CEILING(p_mtxcb)` は `mtxatr` を `MTXPROTO_MASK(0x03)` で
  マスクした値の比較であり、`TA_NOEXS`(=-1) もマスク後は `TA_CEILING` に
  一致してしまう。先に `mtxatr = TA_NOEXS` を書くと、非 ceiling ミューテッ
  クスまで stale な `ceilpri` で `mutex_drop_priority` 経路に入ってしまう
  ため、④の優先度復帰（ceiling 判定を含む）を `mtxatr = TA_NOEXS` の書込み
  より前に行う。
- 待ち解除で起きたタスクの再スケジュールは init_wait_queue / make_non_wait が面倒を見る
  （既存機構）。del_* 側でディスパッチ判断が必要かは **訂正C**: 3つの `del_*` とも
  `CHECK_TSKCTX_UNL_MYSTATE(&p_selftsk)` を使う（ini_flg/ini_mtx に統一）（§8-3）。

## 3. cfg 層

- `kernel_api.def` に dcre と同一の3行: `AID_SEM .nosem` / `AID_FLG .noflg` / `AID_MTX .nomtx`。
- **訂正E**: **per-object テンプレート（semaphore.trb/.py 等）の変更はゼロ**
  （全て汎用 KernelObject 継承であることを実装前確認で確定済み。dcre 自身も per-object
  テンプレートを持たない。各オブジェクトで `super().__init__("sem", "semaphore")` 等で統一）。
- **訂正E**: ガード（AID>0 かつ静的0個 → cfg エラー E_OBJ）は KernelObject.generate() の
  中の `self.aidapi` / `self.api` のみを見ており、オブジェクト種別に依存しない。
  3オブジェクトにも自動適用される。各オブジェクトで発火することのエラー回帰 cfg を追加
  （`dcre_aid_sem_no_static.cfg` 等、in-class E_RSATR 回帰と合わせて計6ケース）。
- 管理された差分（AID 無し構成）・positive control（AID 有りの両エンジンバイト一致）・
  実コンパイル検査（AID 有り構成の実リンク — 段階2 訂正B の教訓）・negative control は
  段階2 Task 3 と同じ型。

## 4. カーネル層

- 各 .c に `QUEUE free_semcb;` 等を定義（dcre と同名・同位置）。
- `initialize_semaphore` 等（**現行は既にマスタのみループ** — semaphore.c:126-133）:
  既存静的ループの境界を `tnum_sem` → `tnum_ssem` に変え、直後に動的スロット節
  （TA_NOEXS 化 + `queue_insert_prev(&free_semcb, &(p_semcb->wait_queue))`）を追加。
  **プロセッサフィルタも p_pcb 充填も無し**（非親和オブジェクト）。
- **訂正B**: 2レンジ ID マクロ: SEMID/FLGID/MTXID を段階2の CYCID と同型で**置換**
  （現行は既に inib ポインタ差分式で存在。実装前確認 §8-2 で確定済み。
  既存マクロを置換して 2レンジ化。`tnum_sem` 等は `.c` から `.h` へ移設が必要。
  CYCID の p_*inib ポインタ判定・(ID) キャスト・`tnum_* - tnum_s*` 境界の形を踏襲）。
- E_NOEXS 挿入（**19関数**、acquire_glock 直後の最初の分岐、既存ロジックは else 連鎖へ
  バイト保存で繰り込み）:
  - semaphore.c: sig_sem / wai_sem / pol_sem / twai_sem / ini_sem / ref_sem（6）
  - eventflag.c: set_flg / clr_flg / wai_flg / pol_flg / twai_flg / ini_flg / ref_flg（7）
  - mutex.c: loc_mtx / ploc_mtx / tloc_mtx / unl_mtx / ini_mtx / ref_mtx（6）
  - FMP3 固有関数は無し（msta 型の類推問題は今回発生しない — 調査確認済み）。
- 配線: allfunc.h に TOPPERS_acre_sem 等6行、Makefile.kernel の各オブジェクト行に .o、
  kernel_rename.def に free_semcb/tmax_ssemid/aseminib_table（flg/mtx も同型）を追加し
  再生成（正確なシンボル名は cfg 出力（kernel.trb の `a#{@obj_s}inib_table`）に合わせる）。

## 5. 段階2 hardening（本計画に折り込み・独立タスク化）

1. **Constraint 4 の cfg enforcement**: AID_CYC/AID_ALM > 0 のとき
   `TOPPERS_TEPP_PRC` にマスタプロセッサの bit が立っていることを両エンジンで検査
   （不成立なら cfg エラー）。エラー回帰 cfg 追加。将来ターゲットでの
   free_cyccb BSS ゼロ → queue_delete_next NULL 参照を予防（段階2最終レビュー Minor 1）。
2. del_cyc/del_alm に「p_pcb は free-list 滞在中 stale のままで、acre_* が無条件に
   再設定する」暗黙不変量の1行コメントを追加（段階2最終レビュー triage ①）。
3. **訂正G**: 混在 AID（AID_CYC>0 / AID_ALM=0 等）の equivalence サンプル cfg（同 Minor 3）。
   段階3a の AID 追加で組合せが増えるため、**sem/flg/mtx の混在サンプルも1ケース Task 2-3 に追加**
   （cfg 単独バリアント = CMake の constraint で作成不可）。
4. **訂正H**: TEPP_PRC 検査のエラー回帰 cfg は構成不能（全8プリセットでマスタ bit が
   立っており、.cfg から TOPPERS_TEPP_PRC は変更できない）。代替として
   **条件反転の mutation control** を行う: kernel.py / kernel.trb に追加する
   TEPP_PRC 検査の条件を一時的に反転し、従来通るはずの musca_b1-2core の cfg が
   両エンジンとも同一文言のエラーで失敗することを実証してから復元する
   （検査が空虚でないことの実演）。この結果、Task 8 のエラー行列は 12+6=18 件。
5. DIVERGENCE_MAP の cyclic.c 行へ CHECK_PAR 恒真条件除去の半文追記（同 Minor 4）。

## 6. MP 安全性

- 全 acre/del は lock_cpu + acquire_glock 下（段階1/2 と同一規約）。
- 待ちタスクの解除（E_DLT）は giant lock 下で init_wait_queue が行い、既存 ini_* と
  同一経路 — 新規の窓は生じない。
- del_mtx の優先度復帰も giant lock 下（既存 unl_mtx の経路と同じ）。
- **残余ウィンドウ**: sem/flg/mtx はタイマ・スタック・プールと無関係のため、段階1 §2.3 /
  段階2 §5.2 で扱った類の解放後参照ウィンドウは**構造的に存在しない**（CB は cfg 予約
  静的領域で、free-list 復帰後も E_NOEXS ゲートが全 API を遮断する）。
  spec としてこの不存在を明示し、レビューで反証を試みること。

## 7. テスト（test_dcre3、musca_b1-2core、AID_SEM(2)/AID_FLG(1)/AID_MTX(2)）

**テスト構成:** `TASK1` (MID/PRC1)、`TASK2` (HIGH/PRC1)、`TASK3` (HIGH/PRC2)。
静的オブジェクト: `SEM1`・`FLG1`・`MTX1` 各1個。

1. sem: acre → sig/wai の基本動作 → del（休止資源での削除）→ E_NOEXS
2. **E_DLT 実証**: HIGH 優先度タスク（PRC1 の TASK2／PRC2 の TASK3）を wai_sem で待たせ、
   del_sem → 待ちタスクが E_DLT を受け取ることを check_ercd で確認（同一プロセッサ／
   別プロセッサの両経路）
3. 枯渇 E_NOID（sem 2個使用中に3個目）、静的 sem への del → E_OBJ
4. flg: acre → set/wai → del、E_NOEXS
5. mtx: acre(TA_CEILING) → loc → **ロック中 del_mtx 成功** → 所有タスクの現在優先度が
   ベース優先度へ復帰したことを get_pri で実測 → 解放済みであること（再 loc 不可 E_NOEXS）
6. mtx エラー系: TA_CEILING で不正 ceilpri → E_PAR、acre 再利用（del → 再 acre 同一 ID、
   全スロット使用→1個del の決定形）
7. **カーネル変異 negative control**: del_sem の queue_insert_prev を殺し、再 acre が
   E_NOID → FAIL → 復元 → PASS
8. 非退行: test_dcre1 / test_dcre2 / test_int2 PASS 維持

## 8. 実装前確認リスト（計画 Task 1 で現物確認 ✅ PASS）

1. ✅ TFN_ACRE_SEM/DEL_SEM/ACRE_FLG/DEL_FLG/ACRE_MTX/DEL_MTX の kernel_fncode.h 既存有無
2. ✅ FMP3 の SEMID/FLGID/MTXID マクロの現行実装（有無・inib ベースか）→ 2レンジ化の形を確定
3. ✅ FMP3 の ini_sem/ini_flg/ini_mtx の実装（init_wait_queue の呼び方・ディスパッチ判断・
   mutex の remove_mutex/mutex_drop_priority 呼び出し順）— del_* はこれを正として組む
4. ✅ TMAX_MAXSEM 等の定数と VALID_TPRI の存在
5. ✅ dcre の acre_sem/del_sem/acre_flg/del_flg/acre_mtx/del_mtx 本体の転写元行番号確定
6. ✅ 訂正E ガードが sem/flg/mtx で発火することの事前確認（オブジェクト種依存の穴がないこと）
7. ✅ wait_queue を free-list リンクに使う際、TA_TPRI（優先度順キュー）の初期化状態と
   干渉しないこと（acre 時に queue_initialize で作り直す dcre の手順を確認）
8. ✅ initialize_semaphore/eventflag/mutex がマスタプロセッサ限定であること（ゲート確認）

## 9. 統治

- 8タスク構成（段階2と同型: 実装前確認 → hardening → cfg → sem → flg → mtx →
  test_dcre3 → 最終回帰）を想定。計画で確定。
- 全 pristine 編集は DIVERGENCE_MAP。equivalence 全構成維持。上流報告候補は従来3件のまま。

---

## 10. 実装前確認の結果（2026-08-04 実測）

### 10.1 実装前確認リスト（8項目全PASS）

| Step | 項目 | 検証コマンド | 実測結果 | 判定 |
|---|---|---|---|---|
| 1 | TFN_* 6個既存 | `grep TFN_ACRE_SEM... kernel_fncode.h` | 6個全部見つかり：ACRE_SEM(-194):135 / ACRE_FLG(-195):136 / ACRE_MTX(-199):139 / DEL_SEM(-210):147 / DEL_FLG(-211):148 / DEL_MTX(-215):151 | ✅ Pass（訂正A確定） |
| 2 | SEMID/FLGID/MTXID マクロ | `grep "define SEMID/FLGID/MTXID" -A2` | 3マクロとも既存、inib ポインタ差分式。CB はポインタ表。tnum_* は .c にある | ✅ Pass（訂正B確定：置換・.h 移設） |
| 3 | ini_sem/flg/mtx 実装 | `sed -n '330,366p' semaphore.c` 等 | ini_sem は CHECK_TSKCTX_UNL()（ini_flg/ini_mtx は MYSTATE）。ini_mtx 優先度復帰は init_wait_queue→p_loctsk=NULL→remove_mutex→mutex_drop_priority順 | ✅ Pass（訂正C/F確定） |
| 4 | 定数マクロ | `grep TMAX_MAXSEM check.h` 等 | TMAX_MAXSEM/VALID_TPRI/INT_PRIORITY/TA_*/TA_NOEXS/CHECK_* 全部所在確定 | ✅ Pass（追加移植不要） |
| 5 | dcre 転写元行範囲 | `grep -n "^#ifdef TOPPERS_" dcre/kernel/*.c` | 9個全部確定：semini 132-165 / acre_sem 170-214 / del_sem 219-257 / flgini 140-173 / acre_flg 203-241 / del_flg 246-284 / mtxini 138-174 / acre_mtx 378-423 / del_mtx 428-479 | ✅ Pass（計画値と完全一致） |
| 6 | 訂正E ガード | `sed -n '125,155p' kernel.py` + クラス定義 | ガードは KernelObject.generate() の self.aidapi / self.api のみ見る、種別非依存。Sem/Flg/Mtx クラス定義は共通枠組み継承のみ（per-object テンプレート変更ゼロ） | ✅ Pass（訂正E確定） |
| 7 | wait_queue free-list・TA_TPRI | `sed -n '61,99p' semaphore.h` 等 + dcre | CB 先頭全部 wait_queue。INIB に iprcid/affinity なし。CB に p_pcb なし。dcre acre で queue_initialize 再初期化（TA_TPRI と干渉なし） | ✅ Pass（Global Constraint 4 確認） |
| 8 | initialize_* マスタ限定 | `sed -n '118,136p' semaphore.c` 等 | 3関数とも `if (p_my_pcb->prcid == TOPPERS_MASTER_PRCID)` で全体がマスタ限定。プロセッサフィルタなし | ✅ Pass（ゲート条件通過） |

### 10.2 訂正A~H 確定表

| 訂正 | 対象§ | 内容 | 実装前確認§ | Status |
|---|---|---|---|---|
| **A** | 1.2 | TFN_ACRE_SEM(-194) 等6個 kernel_fncode.h 既存確定 | §8-1 | ✅ 確定 |
| **B** | 4 | ID マクロは置換（新規定義でなく）、tnum_* を .h へ移設 | §8-2 | ✅ 確定 |
| **C** | 2 | 3つの del_* とも CHECK_TSKCTX_UNL_MYSTATE を使用 | §8-3 | ✅ 確定 |
| **D** | 1.3 | del_flg も CHECK_ID（E_ID）、E_PAR でなく | §8-3 | ✅ 確定 |
| **E** | 3/8-6 | AID>0 かつ静的0個で E_OBJ エラー、per-object テンプレート変更ゼロ | §8-6 | ✅ 確定 |
| **F** | 2 | del_mtx に p_loctsk = NULL を入れる（優先度復帰前） | §8-3 | ✅ 確定 |
| **G** | 5 | 混在AID サンプル cfg は Task 2-3 で追加（sem/flg/mtx も含める） | §5 | ✅ 確定 |
| **H** | 5 | TEPP_PRC テスト cfg 構成不能→mutation control 代替（18 件） | §5 | ✅ 確定 |

### 10.3 dcre 転写元行範囲（後続 Task 参照用）

| 関数・型 | dcre ファイル | 区画 | 行範囲 |
|---|---|---|---|
| initialize_semaphore + free_semcb | semaphore.c | TOPPERS_semini | 132-165 |
| acre_sem | semaphore.c | TOPPERS_acre_sem | 170-214 |
| del_sem | semaphore.c | TOPPERS_del_sem | 219-257 |
| initialize_eventflag + free_flgcb | eventflag.c | TOPPERS_flgini | 140-173 |
| acre_flg | eventflag.c | TOPPERS_acre_flg | 203-241 |
| del_flg | eventflag.c | TOPPERS_del_flg | 246-284 |
| initialize_mutex + free_mtxcb | mutex.c | TOPPERS_mtxini | 138-174 |
| acre_mtx | mutex.c | TOPPERS_acre_mtx | 378-423 |
| del_mtx | mutex.c | TOPPERS_del_mtx | 428-479 |
| T_CSEM/T_CFLG/T_CMTX | include/kernel.h | — | 225-291（含む） |

### 10.4 後続 Task への引き継ぎ項目

- **(a) TFN_* 6件の既存値**: Step 1 表参照
- **(b) SEMID/FLGID/MTXID の現行実装形**: Step 2 確認（ポインタ差分式・CB はポインタ表）
- **(c) ini_sem/ini_flg/ini_mtx の init_wait_queue 呼出し流儀・ディスパッチ判断**: Step 3 確認
- **(d) remove_mutex / mutex_drop_priority のシグネチャ・呼出し順**: Step 3 で確認（ini_mtx の優先度復帰順序参照）
- **(e) TMAX_MAXSEM/VALID_TPRI/INT_PRIORITY 所在**: Step 4 確認
- **(f) dcre 転写元行範囲 6組**: §10.3 転写元行範囲表参照
- **(g) 訂正E ガードが sem/flg/mtx で発火**: Step 6 で確認（per-object テンプレート変更ゼロ）
