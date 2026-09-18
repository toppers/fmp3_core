# DIVERGENCE_MAP — 上流 pristine への乖離台帳

上流：fmp3_archive（`UPSTREAM_VERSION` / `UPSTREAM_PRISTINE.txt` 参照）。
pristine を改変したら必ずここに記録する（マージ衝突解決の根拠になる）。

| path | 種別 | 内容・理由 | 上流報告 |
|------|------|-----------|----------|
| cfg/ | none | **無改変**（`git diff upstream main -- cfg` は空）。AGENTS.md §2 規則3（cfg 相当は `cfg_py/` で提供し CMake から呼ぶ、pristine の `cfg/` は使わない）を**文言・精神の両方で満たす**（2026-07-19、計画B Task 11 の cutover 完了により確定。下記「解消済み事項」参照）。`CMakeLists.txt` が呼ぶのは常に `cfg_py/cfg.py` であり，`cfg/cfg.rb` はコメント（`CMakeLists.txt:158-163`）にのみ現れる。`cfg/` は CMake のビルドグラフからは**完全に不使用**だが，`tools/cfg_equivalence.sh`（CMake 外）が差分等価性検査のオラクルとして引き続き呼ぶため，ファイル自体は削除しない。次の `git merge upstream` で `cfg/` に衝突が出た場合も，pristine 側の変更を素直に取り込んでよい（我々の改変は無い＝none） | - |
| target/ | remove | 使わない target を取り込まない（imx8mm_evk_arm64_gcc / raspberrypi_pico_gcc / stm32mp257f_dk_arm64_gcc / zcu102_arm64_gcc / zybo_gcc / zybo_z7_gcc）。allowlist は `tools/upstream_targets.txt`。復活させたい場合は 1 行足して `import_upstream.sh` 再実行 → `git merge upstream` | - |
| target/m5stamp_esp32p4_gcc | none | **無改変で取り込み済み**（2026-08-13、ESP32-P4 統合 Phase 0）。2026-07-19 の「本リポジトリでは管理しない」という決定（下記「解消済み事項」に旧記述を保存）を撤回し、`tools/upstream_targets.txt` に 1 行足して `import_upstream.sh` 再実行 → `git merge upstream` で取り込んだ。`arch/riscv_gcc/esp32p4`（chip 依存部）は元から allowlist の対象外＝取り込み済みだったので、これで P4 の chip 層・target 層が揃った。現時点で派生ファイル（`target.cmake` / `presets.json` / `*.py`）は無く、CMake ビルドは未対応（`FMP3_TARGET=m5stamp_esp32p4_gcc` は configure できない）。cfg テンプレートは pristine の `.trb` のみ | - |
| arch/riscv_gcc/common/arch.cmake | add | Makefile.core の CMake 版。上流の Makefile は残すが CMake ビルドからは参照しない | - |
| arch/riscv_gcc/polarfire_soc/chip.cmake | add | Makefile.chip の CMake 版。`-march` は上流の `rv64gc` ではなく `rv64imafdc`（ISA は同一。`rv64gc` は実在しない multilib ディレクトリ `rv64imafdc/lp64d` に解決され `crt0.o` が見つからないが、`rv64imafdc` は既定ディレクトリ `.` に解決される。ABI は `lp64d` のまま） | 未 |
| target/polarfire_soc_kit_gcc/{target.cmake,presets.json} | add | Makefile.target の CMake 版。Microchip SDK のソース16個を最終リンクに加える。FMP3_LDSCRIPT_VIA_DRIVER_T=ON を宣言する（picolibc.specs の %{!T:-Tpicolibc.ld} が -Wl,-T, では防げないため。汎用層 CMakeLists.txt 側のトグルを読む形に改めた＝計画A2 Task 1） | - |
| arch/arm_m_gcc/common/arch.cmake | add | Makefile.core の CMake 版。上流の Makefile は残すが CMake ビルドからは参照しない | - |
| arch/arm_m_gcc/musca_b1/chip.cmake | add | Makefile.chip の CMake 版。--gc-sections は上流に無いため使わない（polarfire の l2lim 制約がこのターゲットには無い） | - |
| target/musca_b1_gcc/{target.cmake,presets.json} | add | Makefile.target の CMake 版。QEMU 専用ターゲット（実機非対応）。FMP3_LDSCRIPT_VIA_DRIVER_T は設定しない（既定 OFF＝-Wl,-T, のままでよい） | - |
| kernel/*.py（15個） | add | asp3_core 1.7.1 cfg エンジン用 Python テンプレート。12個は `fmp3_pico_sdk`（FMP3 3.3.0）から流用、`interrupt.py`/`kernel_check.py`/`kernel.py` の3個は 3.3.0→3.4.0(20260719) の Ruby 差分をパッチ適用。計画B | - |
| arch/riscv_gcc/{common,polarfire_soc}/*.py（5個） | add | polarfire chip 層の Python テンプレート。前例なし・全数新規移植（asp3_core の riscv .py は単一プロセッサ版で別物、fmp3_pico_sdk に riscv 実装なし）。計画B | - |
| arch/arm_m_gcc/common/*.py（3個） | add | musca_b1 が使う ARM-M コア共通層。`fmp3_pico_sdk` の同名 `.py` は3.3.0時代の古い構造（ベクタテーブル生成がcore側、チェックが2テーブルindex方式）のため単純コピー不可と判明し、pristine現物の `.trb`（3.4.0）から書き直した。計画B | - |
| arch/arm_m_gcc/musca_b1/chip_kernel.py | add | musca_b1 chip 層。前例なし・全数新規（`fmp3_pico_sdk` に musca_b1 は無い）。計画B | - |
| target/polarfire_soc_kit_gcc/*.py（3個） | add | polarfire ターゲット層。前例なし・全数新規。計画B | - |
| arch/riscv_gcc/common/clic_kernel.py | add | ESP32-P4 の CLIC 依存部（pristine `clic_kernel.trb` の Python 版）。`plic_kernel.py` と同型。ESP32-P4 統合 Phase 0（2026-08-13） | - |
| arch/riscv_gcc/esp32p4/chip_kernel.py | add | esp32p4 chip 層（pristine `chip_kernel.trb` の Python 版）。polarfire 版との差は割込み線数（0..47）・`pid2cidx`（`prcid-1`）・include 先（`clic_kernel.py`）の3点のみ。ESP32-P4 統合 Phase 0（2026-08-13） | - |
| target/m5stamp_esp32p4_gcc/*.py（3個） | add | m5stamp ターゲット層。**`target/polarfire_soc_kit_gcc/*.py` とバイト同一で流用**（対応する `.trb` 3本が polarfire 版とバイト同一であることを `cmp` で実測。`target/rp2350_pico2_gcc/target_class.py` を musca_b1 版から流用したのと同じ扱い）。ESP32-P4 統合 Phase 0（2026-08-13） | - |
| tools/cfg_equivalence_m5stamp.sh | add | m5stamp 用の cfg 差分等価性検査。`tools/cfg_equivalence.sh` は CMake(ninja) の build から cfg コマンド行を取るが m5stamp はまだ CMake 層を持たないため、classic（configure.rb+make）で artifacts を作ってから Ruby/Python の両エンジンを回す。`--selftest` は .py を6通りに壊してすべて検出できることを実演する（always-pass でないことの証拠）。CMake 層ができたら `cfg_equivalence.sh` へ一本化して廃止してよい | - |
| target/musca_b1_gcc/*.py（3個） | add | musca_b1 ターゲット層。前例なし・全数新規。`target_kernel.py` は3.4.0で `core_kernel.trb` から移動してきたベクタテーブル／例外テーブル生成ロジックを含む。計画B | - |
| target/musca_b1_gcc/target_test.h | mod (dcre-port) | `test/test_dcre5.c`（dcre動的ISR回帰テスト）の自立化（ISR_SIO依存の解消、docs/qa-esp32s3-20260804-2.md Q-3）のため、`INTNO3`/`INTNO3_INTATR`/`INTNO3_INTPRI`/`intno3_clear()` を追加。`INTNO1`（IRQ60）・`INTNO_UNOPTED`（test/test_dcre5.h、IRQ61、意図的に未CFG_INT）と同じ「未接続の予備NVIC IRQ」の流儀で、次の予備IRQ62をPRC1に割り当てる（`(1U<<16)|(62U+16U)`、TA_ENAINT、intpri -2＝TMIN_INTPRI(-3)より内側でカーネル管理）。commit 2d77522/169b353（INTNO2追加→上流提供と判明し撤回）と同型の改変。musca_b1-2core実測でビルド・QEMU実行（TTSP_RESULT: PASS、Check point 13行）・`tools/cfg_equivalence.sh`（MATCH）を確認済み | 未 |
| target/rp2350_pico2_gcc/{target.cmake,presets.json} | add | Makefile.target の CMake 版。ARM-M（arm_m_gcc/rp2350 chip 層）。QEMU に RP2350/Pico のマシンモデルが無い（8.2.2/11.0.1 とも `-machine help` で確認済み）ため `FMP3_RUN_COMMAND` は定義しない＝`run` ターゲット自体を生成しない（ビルド専用、意図的）。計画C Task 1 | - |
| arch/arm_m_gcc/rp2350/{chip.cmake,chip_kernel.py} | add | rp2350 chip 層。前例なし・全数新規（`fmp3_pico_sdk` に rp2350 は無い）。計画C Task 1 | - |
| target/rp2350_pico2_gcc/*.py（3個: target_check.py/target_class.py/target_kernel.py） | add | rp2350 ターゲット層。`target_class.py` は musca_b1 版とバイト同一で流用（クラス構造がARM-M共通のため）。計画C Task 1 | - |
| target/kria_arm64_gcc/{target.cmake,presets.json} | add | Makefile.target の CMake 版（KRIA SOM Cortex-A53、QEMU xlnx-zcu102）。QEMU の `-serial` 割当ての罠（USE_XUART1でUART1へ出力するがserial個数指定を誤るとUART0へ繋がり出力が無音で消える）を踏まえ `-serial null -serial mon:stdio` を使用。4コア構成のsecondary-core起動もQEMU 11.0.1で確認済み。計画C Task 6/7 | - |
| arch/arm64_gcc/common/arch.cmake | add | Makefile.core の CMake 版。**ROM イメージ形式フック（`FMP3_DUMP_FORMAT dump`）をここで宣言**（Makefile.core:34 `DUMP = dump` の翻訳。kria_arm64 は `.dump` 形式）。計画C Task 6 | - |
| arch/arm64_gcc/common/*.py（4個: core_check.py/core_kernel.py/core_offset.py/gic_kernel.py） | add | arm64_gcc コア共通層の Python テンプレート。前例なし・全数新規移植（asp3_core に arm64/GIC 実装なし）。計画C Task 4 | - |
| arch/arm64_gcc/zynqmp/{chip.cmake,chip_kernel.py} | add | kria_arm64 chip 層（ZynqMP APU/Cortex-A53）。前例なし・全数新規。計画C Task 4/6 | - |
| target/kria_arm64_gcc/*.py（3個: target_check.py/target_class.py/target_kernel.py） | add | kria_arm64 ターゲット層。前例なし・全数新規。計画C Task 7 | - |
| target/kria_r5_gcc/{target.cmake,presets.json} | add | Makefile.target の CMake 版（KRIA SOM Cortex-R5F、QEMU xlnx-zcu102 RPUクラスタ）。1コア（lockstep相当）・2コア（split mode, `FMP3_PRC_NUM=2`）を`FMP3_PRC_NUM`で分岐（kria_arm64と同じ形）。2コアの`FMP3_RUN_COMMAND`は`-global xlnx-zynqmp.rpu-secondary-start=true`と`-device loader`2個を要する（Task 12で判明、Task 13で`target.cmake`に反映。詳細は下記「解消済み事項」参照）。計画C Task 10/13 | - |
| arch/arm_gcc/common/arch.cmake | add | Makefile.core の CMake 版。本ファイルに `DUMP` の定義は無い（現物確認済み）ため `FMP3_DUMP_FORMAT` は宣言せず既定の srec のまま（kria_r5 は srec のまま正しい）。計画C Task 8 | - |
| arch/arm_gcc/common/*.py（3個: core_check.py/core_kernel.py/core_offset.py） | add | arm_gcc（Cortex-R5F/GIC）コア共通層の Python テンプレート。前例なし・全数新規移植。計画C Task 8 | - |
| arch/arm_gcc/zynqmp_r5/{chip.cmake,chip_kernel.py} | add | kria_r5 chip 層（ZynqMP RPU/Cortex-R5F）。`chip_kernel.py` はプライベート割込み（intno 0〜31）のみプロセッサ番号で符号化する（`(prcid << 16) | intno`）構造（グローバル割込み32〜186はintno自体は符号化しない）。前例なし・全数新規。計画C Task 8/10 | - |
| target/kria_r5_gcc/*.py（3個: target_check.py/target_class.py/target_kernel.py） | add | kria_r5 ターゲット層。前例なし・全数新規。計画C Task 10 | - |
| kernel/kernel_api.def | mod (dcre-port) | 動的生成（dcre 段階1）静的API `AID_TSK .notsk` / `DEF_MPK { .mpksz &mpk? }` の2行を追加。dcre extension（`extension/dcre/kernel/kernel_api.def`）と同一の記法。cfg 両エンジン共用のためこの1ファイルの改変で足りる | - |
| kernel/kernel.trb | mod (dcre-port) | `KernelObject`（`@aidapi`/`@noobj`/`@inibList` の汎化と `generate()` の AID 集計・`TNUM_S*ID`/`_kernel_tmax_s*id`/動的 inib ブロック/予約 CB・ポインタ表末尾の追加）と末尾の `DEF_MPK` → `mpksz`/`mpk` 出力ブロックを追加。dcre kernel.trb（asp3_core 1.7.1 拡張）の DIFF を FMP3（プロセッサ/クラス概念あり）向けに翻案。段階1では `kernel_api.def` に `AID_TSK` のみ登録済みのため、`@aidapi` が `$cfgData` に無いオブジェクト（tsk 以外）は新規追加ブロックを完全にスキップし既存出力を厳密保持（Task 2 Step 7 の管理された差分許容リストがタスク+mpk 関連の5項目のみである根拠）。レビュー指摘（2026-08-03）を受け、`DEF_MPK` 出力ブロックの先頭に AID_TSK と同一規約（`params0.has_key?(:class)` → `error_ercd("E_RSATR", ...)`、文言 `DEF_MPK must not be within a class`）のクラス外専用検査を追加（設計書 §157 の要求。Python 側 `kernel.py` も同時に追加、`tools/cfg_error_tests/dcre_mpk_in_class.cfg` で回帰確認）。`checkAutoObjid` フックを追加（段階3a Task 2 hardening #1） | - |
| kernel/task.trb | mod (dcre-port) | `_kernel_torder_table` のサイズトークンを `TNUM_TSKID`（総数）から `TNUM_STSKID`（静的数）へ変更（dcre task.trb と同じ変更点）。最終レビュー指摘（2026-08-04）を受け、同箇所に `TNUM_STSKID` が `KernelObject.generate` の恒常出力に依存する旨のコメントを追記 | - |
| kernel/kernel_check.trb | mod (dcre-port) | パス3（メモリ検査）に `DEF_MPK` の mpk 整列・非NULL検査ブロックを追加（dcre kernel_check.trb 相当） | - |
| kernel/kernel_sym.def | mod (dcre-port) | パス3が参照する `CHECK_MPK_ALIGN`/`CHECK_MPK_NONNULL`/`CHECK_MB_ALIGN` の3シンボルを追加（dcre kernel_sym.def と同一の3行） | - |
| kernel/kernel_impl.h | mod (dcre-port) | `TA_NOEXS`（`:199`）直後に `TA_MEMALLOC`（`0x8000`）マクロを追加。`TOPPERS_MACRO_ONLY` ガード内に、Task 2 が生成する `_kernel_mpksz`/`_kernel_mpk` を束ねる `extern const size_t mpksz` / `extern MB_T *const mpk`（istksz/istk セクション直後）、`extern bool_t mpk_valid`（kerflg_table セクション直後）、`initialize_mempool`/`malloc_mempool`/`aligned_alloc_mempool`/`free_mempool` の4関数 extern と `malloc_mpk`/`aligned_alloc_mpk`/`free_mpk` の Inline 3関数（exit_kernel セクション直後）を追加。dcre（`extension/dcre/kernel/kernel_impl.h`）の同一ブロックを転記し、FMP3 の既存セクション構造（`istksz_table`/`kerflg_table`/`exit_kernel` 等）に合わせて配置を分割した（内容はdcreと同一）。段階1（Task 3）はTA_MBALLOC・TARGET_TSKATR等ACRE_TSK系マクロは対象外（別タスクの範囲） | - |
| kernel/check.h | mod (dcre-port) | 末尾（`#endif` 直前）に dcre `check.h` の `ALIGNED`/`STKSZ_ALIGN`/`INTPTR_ALIGN`/`FUNC_ALIGN`/`STACK_ALIGN`/`MPF_ALIGN`/`MB_ALIGN`/`FUNC_NONNULL` マクロ群を追加（`CHECK_*_ALIGN`/`CHECK_FUNC_NONNULL` 未定義時は `true` に落ちる形。対応する `CHECK_*_ALIGN` の値自体は base（`e2fe7ac`）時点で既に各 `arch/*/common/core_kernel_impl.h` に pristine として存在する。riscv は `CHECK_STKSZ_ALIGN`/`CHECK_INTPTR_ALIGN`/`CHECK_FUNC_ALIGN`/`CHECK_STACK_ALIGN`/`CHECK_MPF_ALIGN`/`CHECK_MPK_ALIGN`/`CHECK_MB_ALIGN` を全て（7個）持ち、arm_m は `CHECK_STKSZ_ALIGN`/`CHECK_FUNC_ALIGN`/`CHECK_STACK_ALIGN`/`CHECK_MPF_ALIGN`/`CHECK_MB_ALIGN` の5個を持つ（arm_m に欠けているのは `CHECK_INTPTR_ALIGN` と `CHECK_MPK_ALIGN` の2個のみで、これらは check.h の `#else` で `true` にフォールバックする）。Task 1/2 で追加したものではない） | - |
| kernel/startup.c | mod (dcre-port) | `bool_t mpk_valid;` の定義を `TOPPERS_sta_ker` ブロック冒頭に追加。`sta_ker()` の `barrier_sync(2)` 直後（マスタプロセッサのみ）に、`mpk`/`mpksz` からカーネルメモリプール領域を初期化し `mpk_valid` を設定する処理を追加。ファイル末尾（`TOPPERS_extkerhdr` の後）に dcre `startup.c` の `TOPPERS_kermem` ブロック（`MEMPOOLCB` 構造体・`align_pointer`・`initialize_mempool`・`malloc_mempool`・`aligned_alloc_mempool`・`free_mempool`、`OMIT_MEMPOOL_DEFAULT` ガード付き）をそのまま転記 | - |
| kernel/allfunc.h | mod (dcre-port) | `startup.c` セクションに `TOPPERS_kermem` を追加（dcre allfunc.h と同一）。本ファイルは Task 3 の Files 一覧に無いが、この CMake ビルドは常に `ALLFUNC` を定義する（`CMakeLists.txt` の `target_compile_definitions(fmp3 PRIVATE ALLFUNC)`）ため、`kernel_impl.h` が `allfunc.h` を include して全 `TOPPERS_xxx` を定義する経路のみが使われる。`TOPPERS_kermem` が無いと `startup.c` 末尾のメモリプール関数群が `#ifdef TOPPERS_kermem` に阻まれて一切コンパイルされず、`sta_ker()` からの `initialize_mempool` 呼び出しがリンクエラーになるため、ビルド可能性を保つ上で必須の追加 | - |
| kernel/kernel_rename.def | mod (dcre-port) | `# startup.c` 節に `mpk_valid`/`initialize_mempool`/`malloc_mempool`/`aligned_alloc_mempool`/`free_mempool` を追加（ブリーフ Step 4 の指示どおり）。加えて、`istksz_table`/`istk_table` 等の並びの直後に `mpksz`/`mpk` を追加（ブリーフの明示リストには無いが、`kernel_impl.h` の `extern const size_t mpksz`/`extern MB_T *const mpk` が Task 2 生成の `_kernel_mpksz`/`_kernel_mpk` に解決されるために必須。無いとリンクエラーになることを確認した上での追加） | - |
| kernel/kernel_rename.h, kernel/kernel_unrename.h | mod (dcre-port) | `utils/genrename.rb kernel` で `kernel_rename.def` から再生成（手編集不可）。上記7シンボル分の `#define`/`#undef` が増えた。再生成時、`kernel_rename.def` に元々存在しなかった `mtxhook_scan_ceilmtx`/`mutex_scan_ceilmtx`/`mutex_drop_priority` の3エントリが本改修と無関係に消えた（Task 3 着手前の pristine 状態で同じ genrename を走らせても同じ3行が消えることを確認済み＝既存の生成物ドリフト）。`mutex_scan_ceilmtx`/`mtxhook_scan_ceilmtx` は現在のソースに実体が存在せず、`mutex_drop_priority`（`mutex.c:272`）は `Inline`（static）関数のため外部シンボルとしてのリネームが元来不要であり、削除は無害と判断した | - |
| kernel/task.h | mod (dcre-port) | `p_tcb_table` extern（旧 `:297`）の直後に `extern QUEUE free_tcb`/`extern const ID tmax_stskid`/`extern TINIB atinib_table[]`/`#define tnum_stsk` を追加（dcre task.h 差分と同型、ブリーフ Step 1 のコードを転記）。`TSKID(p_tcb)` マクロを、`atinib_table`（動的生成タスク範囲）と `tinib_table`（静的生成タスク範囲）の2レンジを判定する版へ置換（dcre 相当、spec §4.3 の式のまま）。AID 未使用時は `tnum_stsk == tnum_tsk` となり2レンジ判定の動的分岐（`tnum_tsk - tnum_stsk == 0`）が常に偽になるため、常に静的レンジの式に落ちて挙動不変 | - |
| kernel/task.c | mod (dcre-port) | `TOPPERS_tskini` ブロック冒頭に `QUEUE free_tcb;` の定義を追加。`initialize_task` の静的タスク初期化ループの境界を `tnum_tsk` → `tnum_stsk` に変更（AID 無しでは `tnum_stsk == tnum_tsk` のため無改変）。ループ末尾に、マスタプロセッサのみで動くdcre 相当の動的スロット初期化ブロック（`free_tcb` の初期化、`atinib_table[j].tskatr = TA_NOEXS`、`p_tcb->p_pcb = get_pcb(1)` で PRC1 固定＝Constraint 4）を追加（ブリーフ Step 2 のコードを転記）。動的タスク数0のときはループが空振りするため既存挙動を保つ | - |
| arch/arm_m_gcc/common/core_kernel_impl.h | mod (dcre-port) | `TSKINICTXB` 定義（`:118`）を閉じる既存の `#endif /* TOPPERS_MACRO_ONLY */` の直後に、新規の `#ifndef TOPPERS_MACRO_ONLY` ブロックとして `init_tskinictxb`（stk_top/stk_bottom の初期化）・`tskinictxb_memalloc_ptr`（`free_mpk` へ渡す先頭番地の取得）の2つの Inline 関数を追加（ブリーフ Step 3 のコードを転記。dcre の `GenerateTskinictxb` と同じ stk_top=先頭番地/stk_bottom=末尾番地の約束）。既存コードからの呼び出しは無く（Task 4 時点では未使用のヘルパ関数）、既存の関数・マクロは無改変のため挙動不変 | - |
| kernel/kernel_rename.def（Task 4） | mod (dcre-port) | `# task.c` 節に `free_tcb`/`tmax_stskid`/`atinib_table` の3シンボルを追加（ブリーフ Step 4 の指示どおり。dcre の `kernel_rename.def` では `tmax_stskid`/`atinib_table` は `# kernel_cfg.c` 節にあるが、ブリーフが明示的に `# task.c` 節への追加を指示しているため従った。`genrename.rb` は `#` 行を単なる見出しコメントとして扱うだけで節の帰属はリネーム結果に影響しない） | - |
| kernel/kernel_rename.h, kernel/kernel_unrename.h（Task 4） | mod (dcre-port) | `utils/genrename.rb kernel` で再生成。上記3シンボル分の `#define _kernel_free_tcb` 等/`#undef` が `task.c` 節に増えただけで、他の差分（既知の3エントリ消失を含む）は今回発生しなかった（`git diff --stat` で `kernel_rename.h`/`kernel_unrename.h` とも3行追加のみを確認） | - |
| include/kernel.h（Task 5） | mod (dcre-port) | `t_rtsk`（旧`:136`）直前に `T_CTSK`（dcre 同一のフィールド並び：tskatr/exinf/task/itskpri/stksz/stk）を追加。`act_tsk` 宣言（旧`:212`）直前に `extern ER_ID acre_tsk(const T_CTSK *pk_ctsk) throw();`/`extern ER del_tsk(ID tskid) throw();` の2宣言を追加（機能コード `TFN_ACRE_TSK`/`TFN_DEL_TSK` は `include/kernel_fncode.h` に既存のため追加不要） | - |
| kernel/kernel_impl.h（Task 5） | mod (dcre-port) | Task 3 が追加した `TA_MEMALLOC` ブロックの直後に、dcre `extension/dcre/kernel/kernel_impl.h:151-161` と同一の `#ifndef TARGET_TSKATR #define TARGET_TSKATR 0U #endif` / `#ifndef TARGET_MIN_STKSZ #define TARGET_MIN_STKSZ 1U #endif` を追加。ブリーフの Files 一覧には無いが、`acre_tsk` の `CHECK_VALIDATR(tskatr, TA_ACT\|TA_NOACTQUE\|TARGET_TSKATR)`/`CHECK_PAR(stksz >= TARGET_MIN_STKSZ)` が参照する2マクロは、FMP3 では cfg 側（`kernel_sym.def`/`task.py`）にしか存在せず C コードとしては未定義だったため、コンパイルのために必須の追加と判断した（`arm64_gcc`/`arm_gcc` の `core_kernel_impl.h` は `TARGET_TSKATR` を独自定義済みだが `TARGET_MIN_STKSZ` はどのターゲットにも無い） | - |
| kernel/check.h（Task 5） | mod (dcre-port) | `CHECK_ID`（E_ID）と `CHECK_PAR`（E_PAR）の間に、dcre `check.h:205` と同一の `CHECK_VALIDATR(atr, valid_atr)`（E_RSATR）マクロを追加。ブリーフの Files 一覧には無いが、`acre_tsk` の `CHECK_VALIDATR(tskatr, TA_ACT\|TA_NOACTQUE\|TARGET_TSKATR)` が参照するマクロが FMP3 の check.h に存在しなかったため、コンパイルのために必須の追加と判断した | - |
| kernel/task_manage.c | mod (dcre-port) | `act_tsk` ブロック直前に `acre_tsk`/`del_tsk` をブリーフの全文どおり追加（free_tcb の FIFO pop/push、`iprcid=1`/`affinity=全プロセッサ`固定、`E_NOID`→`E_NOMEM`の順のエラー判定、`TA_MEMALLOC` 時の `USE_TSKINICTXB` 分岐）。加えて `act_tsk`/`mact_tsk`/`can_act`/`mig_tsk`/`get_tst`/`chg_pri`/`get_pri`/`chg_spr` の8関数に、`acquire_glock()` 直後の最初の状態判定として `if (p_tcb->p_tinib->tskatr == TA_NOEXS) { ercd = E_NOEXS; } else ...` を挿入（既存ロジックは字下げのみで else 節へ繰り込み、AID 無し構成では `tskatr` が `TA_NOEXS` に一致することが無いため挙動不変）。`mact_tsk`/`mig_tsk`/`chg_spr` は ASP3 dcre に対応物が無い FMP3 独自関数のため、`act_tsk`/`get_tst`/`chg_pri`/`get_pri`（dcre 実在）の配置パターンを転用した | - |
| kernel/task_refer.c | mod (dcre-port) | `ref_tsk` に E_NOEXS 検査を追加。dcre `task_refer.c` は `tstat = p_tcb->tstat;` 以降の全体を else 節に包む構造のため、FMP3 版でも `p_pcb`/`tstat` 取得から `subpri`/`actcnt`/`actprc`/`prcid` の取出しと `ercd = E_OK;` までを丸ごと else 節へ1段字下げして繰り込んだ（ロジック自体は無変更） | - |
| kernel/task_sync.c | mod (dcre-port) | `wup_tsk`/`can_wup`/`rel_wai`/`rsm_tsk` は dcre 同様、E_NOEXS 判定を `acquire_glock()` 直後の最初の分岐として追加。`sus_tsk` のみ dcre `task_sync.c`（`p_tcb == p_runtsk && !dspflg` を第1分岐のまま維持し E_NOEXS を第2分岐に置く実装）に倣い、FMP3 の同型分岐 `p_tcb == p_selftsk && !(p_my_pcb->dspflg)`（E_CTX）を第1分岐のまま残し、E_NOEXS をその直後の第2分岐として追加した（自タスクは TA_NOEXS になり得ないため分岐順は結果に影響しない） | - |
| kernel/task_term.c | mod (dcre-port) | `ras_ter`/`ter_tsk` に E_NOEXS 検査を追加。両関数とも ASP3 dcre では E_NOEXS が最初の分岐だが、FMP3 版は dcre に無い「異なるプロセッサに割り付けられているタスクならエラー」という `p_tcb->p_pcb != p_my_pcb` 分岐が既存の第1分岐として存在する。存在しないタスク（E_NOEXS）の判定を他の状態判定より優先させる方針で、E_NOEXS をその `p_pcb` 分岐より前・最初の分岐として追加した（dcre に対応物が無いため設計判断。プロセッサ違いでの呼び出しでも常に E_NOEXS が優先される） | - |
| kernel/allfunc.h（Task 5） | mod (dcre-port) | `task_manage.c` セクション先頭（`TOPPERS_act_tsk` 直前）に `TOPPERS_acre_tsk`/`TOPPERS_del_tsk` の2行を追加（dcre allfunc.h と同一の配置）。`TOPPERS_kermem` は Task 3 で追加済みのため重複追加はしていない | - |
| kernel/Makefile.kernel | mod (dcre-port) | `task_manage =` の .o 列挙の先頭に `acre_tsk.o del_tsk.o` を追記（上流形式の維持目的。`KERNEL_FCSRCS` は無改変。CMake の `ALLFUNC` ビルドはこの行を参照しないため実ビルドには影響しない） | - |
| include/kernel.h（Task 6） | mod (dcre-port) | `COUNT_MPF_T`/`ROUND_MPF_T` 定義ブロック（`:568-569`）の直後に `COUNT_MB_T(sz)`/`ROUND_MB_T(sz)` の2行を追加（dcre `extension/dcre/include/kernel.h:674-675` と同一内容、`TOPPERS_COUNT_SZ`/`TOPPERS_ROUND_SZ` は既存の `:558` 付近を再利用、`MB_T` は `t_stddef.h:131` に既存）。Task 2 の DEF_MPK codegen（`kernel/kernel.py`/`kernel.trb`）はこの2マクロを `kernel_cfg.c` へ出力するが、定義側の移植が漏れており、DEF_MPK 構成を初めて実コンパイルした test_dcre1（Task 6）のビルドで未定義エラーとして発覚した。`cfg_equivalence.sh` は両エンジンの生成文字列を diff するだけでコンパイルしないため、両エンジンが同じ欠落を共有すると検出できない（検証の盲点）。dcre 同領域にある `TCNT_DTQMB`/`TCNT_PDQMB`/`TCNT_MPFMB` 等は段階3（dtq/pdq/mpf）用のため追加していない | - |
| test/test_dcre1.c | add (dcre-port) | 動的生成API（acre_tsk/del_tsk/DEF_MPK）の QEMU 回帰テスト本体（Task 6）。musca_b1-2core 上で、acre→act→自然終了、AID_TSK(2) のスロット枯渇からの del による同一ID再利用（free-list は dcre 同様 FIFO、del_tsk は queue_insert_prev で末尾へ・acre_tsk は queue_delete_next で先頭からのため、空きスロットが1個だけの状態で検証）、E_NOID、del_tsk の E_OBJ（静的タスク／非休止タスク）、ter_tsk 経由の強制終了→休止→del成功、削除済みIDへの E_NOEXS、stk=NULL 自動確保時の E_NOMEM と成功、mact_tsk による全プロセッサ affinity の実証、の8シナリオを検証する。ブリーフのコードをほぼ転記したが、PRC2 側の `check_point_prc` が `syssvc/test_svc.c` の `check_count[prcid-1]`（プロセッサ毎に独立したカウンタ）を使う実装であることが実機ログで判明し、ブリーフが仮定していた「PRC1/PRC2 共通の単調増加番号（cp11 等）」ではなく、PRC2 側は本テスト内で最初のチェックポイントとして 1 から数え直す必要があったため、`check_point_prc(11, 2)` → `check_point_prc(1, 2)`、以降の PRC1 側 `check_point(12)`/`check_finish(13)` → `check_point(11)`/`check_finish(12)` へ番号を1つ詰めて修正した（QEMU実行ログで `## Unexpected check point 2-11.` を確認した上での修正）。★hardening パス Task 4 で **Task 1 のオーバーフロー検査1件（H-4：`acre_tsk` の `ROUND_STK_T(stksz)` 加算あふれ）の `E_PAR` を runtime で実演**（`stksz` は size_t 幅なので32bit/64bitどちらでも到達可能＝`#if` ガード不要。musca_b1-2core で実測 PASS・checkpoint 13 のまま）。変異 control C-3（`task_manage.c` の `stksz` あふれ検査削除）を実演済み：`check_assert(erid == E_PAR)` が `test_dcre1.c:218` で失敗（予測どおり）。C-4（`startup.c` の `alloc_mempool` の `aligned > limit` ガード削除）は tdcre1/tdcre4 双方で試したが**現構成（3プリセットのプール配置）から到達不能で control が空振りした**（TTSP_RESULT: PASS のまま変化なし）。「落ちなかったから安全」ではなく「この防御は現構成では到達不能な入力に対するもので runtime control を持たない」という記録。全変異とも復元後 `git diff --stat kernel/` が空であることを確認 | - |
| test/test_dcre1.cfg | add (dcre-port) | 上記テストのシステムコンフィギュレーションファイル。ブリーフの記述どおり、`AID_TSK(2)`/`DEF_MPK({ MPK_SIZE, NULL })` をクラス外（Task 2 の E_RSATR 検査対象の裏付け）に置く | - |
| test/test_dcre1.h | add (dcre-port) | 上記テストのヘッダ（優先度定数・`MPK_SIZE`・関数プロトタイプ）。`test_int2.h` と同型 | - |
| test/MANIFEST | mod (dcre-port) | `test_cpuexc9.c` の後・`test_dlynse.c` の前（アルファベット順）に `test_dcre1.c`/`test_dcre1.cfg`/`test_dcre1.h` の3行を追加 | - |
| test/testexec.rb | mod (dcre-port) | `"cpuexc10"` の後・`"dlynse"` の前に `"dcre1" => { SRC: "test_dcre1" },` を追加 | - |
| test/test_dcre2.c | add (dcre-port) | 動的生成API（acre_cyc/del_cyc/acre_alm/del_alm）の QEMU 回帰テスト本体（Task 7）。musca_b1-2core 上で、(A) acre_cyc(TNFY_HANDLER)→sta_cyc→発火→stp_cyc→del_cyc、(B) 動作中のままの del_cyc 成功（dcre意味論）、(C) 削除済みIDへの sta/stp/ref/msta_cyc の E_NOEXS、(D) E_PAR（cyctim=0／tmehdr=NULL／cycatr未定義ビット）と AID_CYC(2) 枯渇の E_NOID、(E) msta_cyc による PRC2 への移動と PRC2 側発火の実証（`sil_get_pid` で prcid==2 を確認）、(F) TNFY_SETVAR が notify_handler トランポリン経由で変数を設定すること、(G) alm 側の acre/sta/再sta/del と E_NOEXS/E_NOID、(H) 静的生成 CYC1/ALM1 への del が E_OBJ、の8シナリオを検証する。ブリーフのコードをそのまま実装した。ブリーフの Step 6 期待値コメントは PRC2 の初回チェックポイントを `Check point 1-2 passed.` としていたが、`syssvc/test_svc.c:136` の `syslog_2(LOG_NOTICE, "Check point %d-%d passed.", prcid, count)` は prcid が先・count が後のため実際の出力は `Check point 2-1 passed.` である（`check_point_prc(1, 2)` の呼び出し自体はブリーフ・実装ともに正しく、表示フォーマットの記述が逆だっただけ。QEMU実測ログで確認） | - |
| test/test_dcre2.cfg | add (dcre-port) | 上記テストのシステムコンフィギュレーションファイル。ブリーフの記述どおり、`CRE_CYC(CYC1, …)`/`CRE_ALM(ALM1, …)` を CLS_PRC1 内の静的オブジェクトとして置き、`AID_CYC(2)`/`AID_ALM(2)` をクラス外（Task 3 の E_RSATR 検査対象の裏付け）に置く。cyc/alm はメモリプールを使わないため `DEF_MPK` は無し | - |
| test/test_dcre2.h | add (dcre-port) | 上記テストのヘッダ（優先度定数・周期／アラーム時間定数・関数プロトタイプ）。`test_dcre1.h` と同型 | - |
| test/MANIFEST（Task 7） | mod (dcre-port) | `test_dcre1.h` の後・`test_dlynse.c` の前（アルファベット順）に `test_dcre2.c`/`test_dcre2.cfg`/`test_dcre2.h` の3行を追加。`test_dcre2.h` の後に `test_dcre_mix` 追加を追記（段階3a Task 2 hardening #3） | - |
| test/testexec.rb（Task 7） | mod (dcre-port) | `"dcre1"` の後・`"dlynse"` の前に `"dcre2" => { SRC: "test_dcre2" },` を追加。`"dcre2"` の後に `test_dcre_mix` 追加を追記（段階3a Task 2 hardening #3） | - |
| test/test_dcre_mix.c（dcre段階3a Task 2） | add (dcre-port) | AID_xxx 混在構成の cfg 等価性検査用サンプル。QEMU 実行は不要 | - |
| test/test_dcre_mix.cfg（dcre段階3a Task 2） | add (dcre-port) | AID_xxx 混在構成の cfg 等価性検査用サンプル。QEMU 実行は不要。**追記（段階3a Task 3）**：`AID_SEM(2)`/`AID_MTX(1)` と静的 `CRE_SEM`/`CRE_FLG`/`CRE_MTX` を追記し、混在の対象を sem/mtx へ拡張。`AID_FLG`/`AID_ALM` は意図的に書かない。**追記（段階3b Task 2）**：静的 `CRE_DTQ`/`CRE_PDQ`/`CRE_MPF` と `AID_DTQ(2)`/`AID_PDQ(1)`/`AID_MPF(1)` を追記。`AID_FLG`/`AID_ALM` は意図的に書かないまま。`sample/sample1.cfg` は静的 `CRE_PDQ`/`CRE_MPF` を持たないため本 cfg が段階3b の positive control 兼実コンパイル検査の媒体になる（`sample1.cfg` に `AID_PDQ`/`AID_MPF` を足すと訂正E ガードで E_OBJ になることを段階3b Task 1 Step 6 で実測済み）。混在説明コメント内の `acre_*/del_*` は `*/` の並びが C コメントを早期終了させるため `acre_* / del_*` とスペースを挟んだ（ブリーフの逐語表記からの意図的な最小逸脱） | - |
| test/test_dcre_mix.h（dcre段階3a Task 2） | add (dcre-port) | AID_xxx 混在構成の cfg 等価性検査用サンプル。QEMU 実行は不要 | - |
| arch/arm_m_gcc/common/core_rename.def | mod (upstream-gap-fix) | ファイル先頭の `# core_kernel_impl.c` 節の直前に `# core_kernel_impl.h` 節を新設し `sense_lock`/`unlock_cpu` の2エントリを追加。cfg 生成コードが参照する `_kernel_sense_lock`/`_kernel_unlock_cpu` が未リネームで多重ISR構成がリンク不能（arm64 の `core_rename.def` には同節が存在するが arm_m には節ごと欠落していた、pristine 由来の既存ギャップ）。`_kernel_lock_cpu` の未定義参照は出なかったため `lock_cpu` は追加していない（最小差分） | 報告候補（段階1最終レビュー 上流報告候補 b。★段階2 Task 2 で本乖離表の当該3行として修正済み＝候補 b は解消。上流への報告自体は「pristine 側の既存ギャップだった」事実として引き続き有効） |
| arch/arm_m_gcc/common/core_rename.h | mod (upstream-gap-fix) | 上記 `.def` の変更を `utils/genrename.rb core`（`arch/arm_m_gcc/common` で実行）により再生成（手編集ではない）。`#define sense_lock _kernel_sense_lock` / `#define unlock_cpu _kernel_unlock_cpu` の2行が core_kernel_impl.c 節の直前に追加された。**副作用として** `svc_handler` の1行がタブ+スペース混在からタブのみへ整形された（同一シンボル・同一リネーム先で意味的な差分ではない。今回の2エントリ追加とは無関係に、変更前の `.def` からの再生成でも同じ整形差分が再現することを確認済み＝pristine 側の生成物が現行 `genrename.rb` の出力と既に乖離していた既存ドリフト。手編集で温存する選択肢は Constraint 2（生成物は手編集禁止）に反するため、regeneration の結果をそのまま採用した） | 報告候補（段階1最終レビュー 上流報告候補 b。★段階2 Task 2 で本乖離表の当該3行として修正済み＝候補 b は解消。上流への報告自体は「pristine 側の既存ギャップだった」事実として引き続き有効） |
| arch/arm_m_gcc/common/core_unrename.h | mod (upstream-gap-fix) | 上記と同時に再生成。`#undef sense_lock` / `#undef unlock_cpu` の2行を追加。他の差分なし | 報告候補（段階1最終レビュー 上流報告候補 b。★段階2 Task 2 で本乖離表の当該3行として修正済み＝候補 b は解消。上流への報告自体は「pristine 側の既存ギャップだった」事実として引き続き有効） |
| kernel/kernel_api.def（dcre段階2 Task 3） | mod (dcre-port) | 静的API `AID_CYC .nocyc` / `AID_ALM .noalm` の2行を追加（`AID_TSK`/`DEF_MPK` の次）。dcre extension（`extension/dcre/kernel/kernel_api.def`）と同一の記法。**追記（段階3a Task 3）**：`AID_SEM .nosem` / `AID_FLG .noflg` / `AID_MTX .nomtx` の3行を追加（`AID_ALM` の次）。**追記（段階3b Task 2）**：`AID_DTQ .nodtq` / `AID_PDQ .nopdq` / `AID_MPF .nompf` の3行を追加（`AID_MTX` の次） | - |
| kernel/kernel.trb（dcre段階2 Task 3） | mod (dcre-port) | `generate()` の `numObjid` 算出直後に、★訂正E「AID_xxx が1個以上あるのに静的オブジェクトが0個」を `E_OBJ`（`"#{@aidapi} requires at least one #{@api} in the system"`）として弾くガードを追加。汎化済みの `KernelObject`（段階1）が `kernel/kernel_api.def` に登録済みの `@aidapi` を自動的に拾うため、`cyclic.trb`/`alarm.trb` 側の `@inibList["T_NFYINFO"]` 登録と合わせるだけで `AID_CYC`/`AID_ALM` が有効化される（`kernel.trb` 自体への追加改変はガード1ブロックのみ） | - |
| kernel/cyclic.trb（dcre段階2 Task 3） | mod (dcre-port) | `CyclicObject#initialize` に `@inibList["T_NFYINFO"] = "acyc_nfyinfo_table"` を追加（dcre `cyclic.trb:48-51` と同一）。動的生成 nfyinfo テーブルの登録のみで、`generateInib`/`prepare` 等の既存ロジックは無変更。`checkAutoObjid` オーバライドを追加（段階3a Task 2 hardening #1） | - |
| kernel/alarm.trb（dcre段階2 Task 3） | mod (dcre-port) | `AlarmObject#initialize` に `@inibList["T_NFYINFO"] = "aalm_nfyinfo_table"` を追加（dcre `alarm.trb:48-51` と同一）。他は cyclic.trb と同様に無変更。`checkAutoObjid` オーバライドを追加（段階3a Task 2 hardening #1） | - |
| include/kernel.h（dcre段階2 Task 3） | mod (dcre-port) | `MPF_T` の typedef（旧`:131`）直後・`T_CTSK`（旧`:136`）手前に、dcre `extension/dcre/include/kernel.h:133-198` と同一の `T_NFY_HDR`/`T_NFY_VAR`/`T_NFY_IVAR`/`T_NFY_TSK`/`T_NFY_SEM`/`T_NFY_FLG`/`T_NFY_DTQ`/`T_ENFY_VAR`/`T_ENFY_DTQ`/`T_NFYINFO` の型群を追加（★訂正A/B）。FMP3 に `T_NFYINFO` 型が元々存在しなかったため、cyclic/alarm の動的生成 nfyinfo テーブル（Task 3 の `kernel_cfg.c` 恒常出力）が参照する型を初めて定義した。`TMEHDR`/`FLGPTN`/`MODE`/`intptr_t` は既存定義を再利用するため追加の include は不要。**追記（段階2 Task 6）**：`t_rcyc` 手前に `T_CCYC`、`t_ralm` 手前に `T_CALM` を追加（dcre 同一のフィールド並び）。`sta_cyc`/`sta_alm` 宣言の直前にそれぞれ `extern ER_ID acre_cyc(const T_CCYC *pk_ccyc) throw();`/`extern ER del_cyc(ID cycid) throw();`、`extern ER_ID acre_alm(const T_CALM *pk_calm) throw();`/`extern ER del_alm(ID almid) throw();` を追加（機能コード `TFN_ACRE_CYC`/`TFN_DEL_CYC`/`TFN_ACRE_ALM`/`TFN_DEL_ALM` は `include/kernel_fncode.h` に既存のため追加不要。返値型は段階1の `acre_tsk` に揃えて `ER_ID` とした――dcre の `.c` 側は `ER_UINT` を使うが両者は `int_t` の別名であり、宣言と定義で揃っていれば問題ない意図的な逸脱）。**追記（段階3a Task 3）**：`t_rsem` 手前に `T_CSEM`、`t_rflg` 手前に `T_CFLG`、`t_rmtx` 手前に `T_CMTX` を追加。dcre `include/kernel.h:223-291` と同一。`sig_sem`/`set_flg`/`loc_mtx` 宣言の直前にそれぞれ `extern ER_ID acre_sem(const T_CSEM *pk_csem) throw();`/`extern ER del_sem(ID semid) throw();`、`extern ER_ID acre_flg(const T_CFLG *pk_cflg) throw();`/`extern ER del_flg(ID flgid) throw();`、`extern ER_ID acre_mtx(const T_CMTX *pk_cmtx) throw();`/`extern ER del_mtx(ID mtxid) throw();` の6宣言を追加（機能コードは `include/kernel_fncode.h` に既存のため追加不要。返値型のみ dcre の `ER_UINT` から `ER_ID` へ揃えた――段階1/2 と同じ意図的逸脱、`int_t` の別名で実体は同じ）。**追記（段階3b Task 2）**：`t_rdtq` 手前に `T_CDTQ`、`t_rpdq` 手前に `T_CPDQ`、`t_rmpf` 手前に `T_CMPF` を追加。dcre `include/kernel.h:246-252,258-266,285-291` とバイト一致（タブ幅を含む）。`snd_dtq`/`snd_pdq`/`get_mpf` 宣言の直前にそれぞれ `extern ER_ID acre_dtq(const T_CDTQ *pk_cdtq) throw();`/`extern ER del_dtq(ID dtqid) throw();`、`extern ER_ID acre_pdq(const T_CPDQ *pk_cpdq) throw();`/`extern ER del_pdq(ID pdqid) throw();`、`extern ER_ID acre_mpf(const T_CMPF *pk_cmpf) throw();`/`extern ER del_mpf(ID mpfid) throw();` の6宣言を追加（機能コードは `include/kernel_fncode.h` に既存のため追加不要。返値型のみ dcre の `ER_UINT` から `ER_ID` へ揃えた――段階1/2/3a と同じ意図的逸脱、`int_t` の別名で実体は同じ） | - |
| kernel/check.h（dcre段階2 Task 4） | mod (dcre-port) | `FUNC_NONNULL` ブロック（`:423-427`）の直後に `INTPTR_NONNULL(p_var)` を追加。dcre `extension/dcre/kernel/check.h:130-134` と同一内容（`CHECK_INTPTR_NONNULL` は既存の同名マクロと同様どのターゲットも定義していないため実質 `true`）。ブロックの前後関係は dcre（INTPTR_NONNULL が先・FUNC_NONNULL が後）と逆（FMP3 は FUNC_NONNULL が先・INTPTR_NONNULL が後）。段階1で `FUNC_NONNULL` 直後に置く配置がブリーフで既に指定されており、順序自体はどちらでも展開結果は同一のため機能的な差はない | - |
| kernel/kernel_impl.h（dcre段階2 Task 4） | mod (dcre-port) | `NFYHDR` の typedef（`:357-359`）直後に `extern ER check_nfyinfo(const T_NFYINFO *p_nfyinfo);` / `extern void notify_handler(EXINF exinf);` の2宣言を追加（dcre 同節と同一） | - |
| kernel/time_manage.c（dcre段階2 Task 4） | mod (dcre-port) | ファイル末尾（`TOPPERS_fch_hrt` 区画の直後）に dcre `extension/dcre/kernel/time_manage.c:220-296`（`TOPPERS_chknfy`）・`:302-375`（`TOPPERS_nfyhdr`）区画をそのまま転記（`diff -u` で差分ゼロを確認済み、Step 6 参照）。**転記のほかに、先頭の include 節へ `task.h`/`semaphore.h`/`eventflag.h`/`dataqueue.h` の4行を追加**（`check.h` の直後）。理由：転記した `check_nfyinfo` が使う `VALID_TSKID`/`VALID_SEMID`/`VALID_FLGID`/`VALID_DTQID`（`check.h`）はそれぞれ `tmax_tskid`/`tmax_semid`/`tmax_flgid`/`tmax_dtqid`（`task.h`/`semaphore.h`/`eventflag.h`/`dataqueue.h` の extern 宣言）を参照するが、FMP3 の既存 `time_manage.c` はこの4ヘッダを include していなかったため `_kernel_tmax_*` 未宣言でビルド不能だった（全8構成で発覚）。dcre `extension/dcre/kernel/time_manage.c:49-52` は元からこの4行を include しており、転記漏れではなく元々の include 節がこの4ヘッダを必要としていなかった（`chknfy`/`nfyhdr` 追加で初めて必要になった）ため、ブリーフの「check.h が include されていなければ追加し理由を書く」という指示を同種の欠落4件に適用した | - |
| kernel/allfunc.h（dcre段階2 Task 4） | mod (dcre-port) | `/* time_manage.c */` 節の `#define TOPPERS_fch_hrt` 直後に `TOPPERS_chknfy`/`TOPPERS_nfyhdr` の2行を追加（ALLFUNC ビルドで新区画を有効化するため必須。段階1 Task 3 の `TOPPERS_kermem` 追加と同じ理由） | - |
| kernel/kernel_impl.h（dcre段階3b Task 2） | mod (dcre-port) | 段階3b Task 2 で `TA_MBALLOC`（`UINT_C(0x4000)`・`#ifndef` ガード付き）を `TA_MEMALLOC` の直後に追加。dcre の同名定義と同一。cfg は本マクロを出力しないため生成物は不変（Step 7 の管理された差分検査で実証。`grep -c "TA_MBALLOC" kernel_cfg.c` = 0） | - |
| kernel/Makefile.kernel（dcre段階2 Task 4） | mod (dcre-port) | `time_manage =` の .o 列挙末尾に `chknfy.o nfyhdr.o` を追記（上流形式の維持目的。`KERNEL_FCSRCS` は無改変。CMake の `ALLFUNC` ビルドはこの行を参照しないため実ビルドには影響しない） | - |
| kernel/kernel_rename.def（dcre段階2 Task 4） | mod (dcre-port) | `# spin_lock.c` 節と `# cyclic.c` 節のあいだに `# time_manage.c` 節を新設し `check_nfyinfo`/`notify_handler` の2エントリを追加 | - |
| kernel/kernel_rename.h・kernel_unrename.h（dcre段階2 Task 4） | mod (dcre-port, 生成物) | 上記 `.def` の変更を `utils/genrename.rb kernel`（`kernel/` で実行）により再生成（手編集ではない）。`git diff --stat` は両ファイルとも追加のみ（削除0行）。`#define check_nfyinfo _kernel_check_nfyinfo` / `#define notify_handler _kernel_notify_handler`（`kernel_rename.h`）、`#undef check_nfyinfo` / `#undef notify_handler`（`kernel_unrename.h`）が、他の節と同じ「`/* time_manage.c */` コメント見出し＋定義＋空行」という genrename.rb 標準の出力形式で追加された（見出し行込みで各ファイル+6行だが、新規シンボルは2個のみ・既存行の変更なし） | - |
| kernel/cyclic.h（dcre段階2 Task 5） | mod (dcre-port) | `#include "time_event.h"` 直後に `#include <queue.h>` を追加（`kernel_impl.h` は `queue.h` を include していないため）。`extern const ID tmax_cycid;` ブロックを `tmax_scycid`（静的生成周期通知のID番号の最大値）extern と `QUEUE free_cyccb`（free-list、CYCCB先頭に領域が無いため tmevtb 領域を流用）extern に拡張。`cycinib_table[]` extern の直後に `acycinib_table[]`／`acyc_nfyinfo_table[]`（動的生成用、kernel_cfg.c・RAM）extern を追加。`p_cyccb_table[]` extern の直後に `tnum_cyc`／`tnum_scyc` マクロ（`tnum_cyc` は `cyclic.c` から移設）と、新設の `CYCID(p_cyccb)` マクロ（FMP3 の CYCCB はポインタ表のため dcre の配列添字式が使えず、段階1 `TSKID`（`task.h:324-328`）と同型の CYCINIB ポインタによる2レンジ判定式にした）を追加。AID 無し構成では `tnum_scyc == tnum_cyc` となり `CYCID` は常に静的レンジの式に落ちる＝挙動不変 | - |
| kernel/cyclic.c（dcre段階2 Task 5） | mod (dcre-port) | `#define tnum_cyc ...` を削除（`cyclic.h` へ移設したため重複削除。`INDEX_CYC`/`get_cyccb` は無変更）。`initialize_cyclic` の静的ループ境界を `tnum_cyc` → `tnum_scyc` に変更。`QUEUE free_cyccb;` の定義を追加。ループ末尾に、マスタプロセッサのみで動く動的スロット初期化ブロック（`free_cyccb` の `queue_initialize`、`acycinib_table[j].cycatr = TA_NOEXS`、`p_cyccb->p_pcb = p_my_pcb`（呼出し時点でマスタ自身＝`get_pcb(1)`と同義）、`queue_insert_prev` で free-list 末尾へ挿入＝FIFO）を追加。ブリーフのコードを転記。AID 無し構成では `tnum_scyc == tnum_cyc` のため動的ループは空振りし、静的ループの境界変更も無影響＝挙動不変 | - |
| kernel/alarm.h（dcre段階2 Task 5） | mod (dcre-port) | cyclic.h と同型の変更。`#include <queue.h>` 追加。`tmax_almid` ブロックに `tmax_salmid` extern と `QUEUE free_almcb` extern を追加。`alminib_table[]` extern 直後に `aalminib_table[]`／`aalm_nfyinfo_table[]` extern を追加。`p_almcb_table[]` extern 直後に `tnum_alm`／`tnum_salm` マクロと `ALMID(p_almcb)` マクロ（CYCID と同型の2レンジ判定式）を追加。AID 無し構成では `tnum_salm == tnum_alm` となり挙動不変 | - |
| kernel/alarm.c（dcre段階2 Task 5） | mod (dcre-port) | cyclic.c と同型の変更。`#define tnum_alm ...` を削除（`alarm.h` へ移設）。`initialize_alarm` の静的ループ境界を `tnum_alm` → `tnum_salm` に変更。`QUEUE free_almcb;` の定義を追加。ループ末尾に、マスタプロセッサのみで動く動的スロット初期化ブロック（`free_almcb` の `queue_initialize`、`aalminib_table[j].almatr = TA_NOEXS`、`queue_insert_prev` によるFIFO free-list挿入）を追加。AID 無し構成では `tnum_salm == tnum_alm` のため挙動不変 | - |
| kernel/kernel_rename.def（dcre段階2 Task 5） | mod (dcre-port) | `# cyclic.c` 節に `free_cyccb`/`tmax_scycid`/`acycinib_table`/`acyc_nfyinfo_table` の4エントリ、`# alarm.c` 節に `free_almcb`/`tmax_salmid`/`aalminib_table`/`aalm_nfyinfo_table` の4エントリを追加（ブリーフ Step 5 の指示どおり）。生成名 `_kernel_tmax_scycid` 等は `kernel.trb`（Task 3 で追加した `AID_CYC`/`AID_ALM` 汎化ロジック `kernel.trb:186-188`）が出力する `_kernel_tmax_s#{@obj}id` と一致することを確認済み | - |
| kernel/kernel_rename.h・kernel_unrename.h（dcre段階2 Task 5） | mod (dcre-port, 生成物) | `utils/genrename.rb kernel`（`kernel/` で実行）で再生成（手編集ではない）。`git diff --stat` は両ファイルとも各+8行・削除0行（`# cyclic.c`/`# alarm.c` 節にそれぞれ4エントリの `#define`/`#undef` が追加されただけ、他の差分なし） | - |
| kernel/cyclic.c（dcre段階2 Task 6） | mod (dcre-port) | `TOPPERS_cycini` 区画の直後・`TOPPERS_sta_cyc` 区画の直前に `acre_cyc`/`del_cyc` を追加（ブリーフのコードを転記）。dcre からの適応点は4つ：(1) `lock_cpu`/`acquire_glock`・`release_glock`/`unlock_cpu` の対化（giant-lock 規約）、(2) 空判定を `tnum_cyc == 0` → `tnum_cyc == tnum_scyc`（FMP3 は静的分が別レンジのため）、(3) `iprcid`/`affinity`/`p_pcb` に `TOPPERS_MASTER_PRCID`/`TOPPERS_TEPP_PRC` を充填（Global Constraint 4、訂正C：`(1U<<TNUM_PRCID)-1` ではなく `TOPPERS_TEPP_PRC` を使う）、(4) free-list のリンクに `tmevtb` 領域を転用しているため 64bit 環境で `callback`/`arg` が上書きされている分の再設定（★訂正D）。さらに `sta_cyc`/`msta_cyc`/`stp_cyc`/`ref_cyc` の4関数に、`acquire_glock()` 直後・既存の状態判定の最初の分岐として `if (p_cyccb->p_cycinib->cycatr == TA_NOEXS) { ercd = E_NOEXS; } else { …既存本体… }` を挿入（段階1の existence-before-state 規約と同型。既存ロジックは字下げのみでbyte-preserve）。**`msta_cyc` は dcre に存在しない FMP3 固有関数であり上流に先例が無い**――`sta_cyc` への類推適用（段階1の `mig_tsk`/`ras_ter`/`ter_tsk` と同じ扱い）。`msta_cyc` はロック取得前に `p_cyccb->p_cycinib->iprcid`/`affinity` を読んで `CHECK_PRCID`/`CHECK_MIG` しており、TA_NOEXS スロットでは `affinity` が未定義値になり得るが、段階1の `mig_tsk` と同じ既知課題（ユーザ誤用経路の hardening、段階1最終レビューの deferred #1）として本段階でも触らない。なお `acre_cyc` の範囲検査は dcre `cyclic.c` の `CHECK_PAR(0 <= cycphs && cycphs <= TMAX_RELTIM)` から**恒真条件 `0 <= cycphs` を落として** `CHECK_PAR(cycphs <= TMAX_RELTIM)` としている（`RELTIM` は符号なしのため意味的にヌルな比較。dcre からの意図的な逸脱・意味は同一）。`del_cyc` に `p_pcb`-stale の不変量コメントを追加（コード不変。段階3a Task 2 hardening #2） | - |
| kernel/alarm.c（dcre段階2 Task 6） | mod (dcre-port) | cyclic.c と同型の変更。`TOPPERS_almini` 区画の直後に `acre_alm`/`del_alm` を追加（同じ4適応点。`iprcid`/`affinity` に `TOPPERS_MASTER_PRCID`/`TOPPERS_TEPP_PRC`、free-list 転用領域の `callback`/`arg` 再設定＝訂正D）。`sta_alm`/`msta_alm`/`stp_alm`/`ref_alm` の4関数に同型の E_NOEXS 挿入（`p_almcb->p_alminib->almatr == TA_NOEXS` 判定、既存ロジックは字下げのみでbyte-preserve）。`msta_alm` も `msta_cyc` と同じ理由で CHECK_PRCID/CHECK_MIG の hardening は本段階で対処しない。`del_alm` に `p_pcb`-stale の不変量コメントを追加（コード不変。段階3a Task 2 hardening #2） | - |
| kernel/allfunc.h（dcre段階2 Task 6） | mod (dcre-port) | `/* cyclic.c */` 節の `TOPPERS_cycini` 直後に `TOPPERS_acre_cyc`/`TOPPERS_del_cyc`、`/* alarm.c */` 節の `TOPPERS_almini` 直後に `TOPPERS_acre_alm`/`TOPPERS_del_alm` の各2行を追加（ALLFUNC ビルドで新規関数を有効化するため必須） | - |
| kernel/Makefile.kernel（dcre段階2 Task 6） | mod (dcre-port) | `cyclic =` の .o 列挙に `acre_cyc.o del_cyc.o` を、`alarm =` の .o 列挙に `acre_alm.o del_alm.o` を追記（上流形式の維持目的。`KERNEL_FCSRCS` は無改変。CMake の `ALLFUNC` ビルドはこの行を参照しないため実ビルドには影響しない） | - |
| kernel/semaphore.h（dcre段階3a Task 4） | mod (dcre-port) | `tmax_ssemid`／`QUEUE free_semcb`／`aseminib_table[]` の extern を追加。`tnum_sem` を `semaphore.c` から移設し `tnum_ssem` を新設。既存の `SEMID(p_semcb)` マクロを段階2 の `CYCID` と同型の2レンジ版へ置換（新設ではない）。AID 無し構成では `tnum_sem == tnum_ssem` となり従来と同一の式に落ちる＝挙動不変。`#include <queue.h>` は元から `:51` にあるため追加不要（cyclic.h とは異なる） | - |
| kernel/semaphore.c（dcre段階3a Task 4） | mod (dcre-port) | `#define tnum_sem` を削除（`.h` へ移設）。`QUEUE free_semcb;` の定義を追加。`initialize_semaphore` の静的ループ境界を `tnum_sem` → `tnum_ssem` に変更し、既存のマスタ限定ブロックの中に動的スロット初期化（`queue_initialize(&free_semcb)`、`aseminib_table[j].sematr = TA_NOEXS`、`queue_insert_prev` で FIFO 挿入）を追加。セマフォは非親和オブジェクトのため `iprcid`/`affinity`/`p_pcb` の充填やプロセッサ判定は一切行っていない（段階2 cyc/alm との相違）。`acre_sem`/`del_sem` を追加（dcre `semaphore.c:170-214`/`:219-257` の転写）。dcre からの意図的な逸脱3件：(1) glock の対化、(2) 空判定 `tnum_sem == 0` → `tnum_sem == tnum_ssem`、(3) `del_sem` の `CHECK_TSKCTX_UNL()`＋`p_runtsk` を `CHECK_TSKCTX_UNL_MYSTATE(&p_selftsk)`＋`p_selftsk != p_my_pcb->p_schedtsk` へ（FMP3 の `ini_flg`/`ini_mtx` の流儀に合わせた。`check_tskctx_unl_mystate` は `check_tskctx_unl` と E_CTX 判定が厳密に等価＝`check.h:177-199`）。さらに `acre_sem` の範囲検査は dcre の `CHECK_PAR(0 <= isemcnt && isemcnt <= maxsem)` から恒真条件 `0 <= isemcnt` を落として `CHECK_PAR(isemcnt <= maxsem)` としている（`uint_t` に対する意味的にヌルな比較。段階2 `acre_cyc` の `0 <= cycphs` と同型の意図的逸脱・意味は同一）。`sig_sem`/`wai_sem`/`pol_sem`/`twai_sem`/`ini_sem`/`ref_sem` の6関数に existence-before-state 規約の E_NOEXS 分岐を挿入（既存ロジックは字下げのみで byte-preserve） | - |
| kernel/allfunc.h（dcre段階3a Task 4） | mod (dcre-port) | `/* semaphore.c */` 節の `#define TOPPERS_semini` の直後に `TOPPERS_acre_sem`/`TOPPERS_del_sem` の2行を追加（ALLFUNC ビルドで新規関数を有効化するため必須） | - |
| kernel/Makefile.kernel（dcre段階3a Task 4） | mod (dcre-port) | `semaphore =` の .o 列挙の先頭（`semini.o` の次）に `acre_sem.o del_sem.o` を追加（上流形式の維持目的。`KERNEL_FCSRCS` は無改変。CMake の `ALLFUNC` ビルドはこの行を参照しないため実ビルドには影響しない） | - |
| kernel/kernel_rename.def（dcre段階3a Task 4） | mod (dcre-port) | `# semaphore.c` 節に `free_semcb`/`tmax_ssemid`/`aseminib_table` の3エントリを追加 | - |
| kernel/kernel_rename.h・kernel_unrename.h（dcre段階3a Task 4） | mod (dcre-port, 生成物) | `utils/genrename.rb kernel` で再生成（手編集ではない）。`git diff --stat` は両ファイルとも各+3行・削除0行（`# semaphore.c` 節に3エントリの `#define`/`#undef` が追加されただけ、他の差分なし） | - |
| kernel/eventflag.h（dcre段階3a Task 5） | mod (dcre-port) | Task 4 の `semaphore.h` 行と同型（`tmax_sflgid`／`QUEUE free_flgcb`／`aflginib_table[]` の extern 追加、`tnum_flg` の移設と `tnum_sflg` 新設、既存 `FLGID` の2レンジ版への置換、`#include <queue.h>` は既存で追加不要） | - |
| kernel/eventflag.c（dcre段階3a Task 5） | mod (dcre-port) | `#define tnum_flg` を削除（`.h` へ移設）。`QUEUE free_flgcb;` の定義を追加。`initialize_eventflag` の静的ループ境界を `tnum_flg` → `tnum_sflg` に変更し、既存のマスタ限定ブロックの中に動的スロット初期化を追加。イベントフラグは非親和オブジェクトのため `iprcid`/`affinity`/`p_pcb` の充填やプロセッサ判定は一切行っていない。`acre_flg`/`del_flg` を追加（dcre `eventflag.c:203-241`/`:246-284` の転写）。dcre からの意図的な逸脱4件：(1) glock の対化、(2) 空判定 `tnum_flg == 0` → `tnum_flg == tnum_sflg`、(3) `del_flg` の `CHECK_TSKCTX_UNL()`＋`p_runtsk` を `CHECK_TSKCTX_UNL_MYSTATE(&p_selftsk)`＋`p_selftsk != p_my_pcb->p_schedtsk` へ、(4) ★dcre `eventflag.c:257` の `CHECK_PAR(VALID_FLGID(flgid))`（E_PAR）を `CHECK_ID(VALID_FLGID(flgid))`（E_ID）へ — dcre 自身の不整合（`del_sem`/`del_mtx` は `CHECK_ID`、FMP3 の flg 系サービスコールも全て `CHECK_ID`）。`set_flg`/`clr_flg`/`wai_flg`/`pol_flg`/`twai_flg`/`ini_flg`/`ref_flg` の7関数に existence-before-state 規約の E_NOEXS 分岐を挿入（既存ロジックは字下げのみで byte-preserve、`clr_flg` の行末空白も保存） | 報告候補（段階3a Task 8 上流報告候補 d。dcre `eventflag.c:257` の `del_flg` だけが `CHECK_PAR(VALID_FLGID(flgid))`（E_PAR）を使っており、`del_sem`（`semaphore.c:230`）・`del_mtx`（`mutex.c:440`）は `CHECK_ID`（E_ID）で、FMP3 の flg 系サービスコール自身も全て `CHECK_ID`。dcre 自身の不整合であり、証拠は行番号つきで完備。送付するかはユーザ判断（候補 b は段階2 で解消済みだが上流には未報告のまま、と同じ扱い）。★段階3b Task 3 で `del_dtq`（`kernel/dataqueue.c:413`）も同型の `CHECK_PAR`→`CHECK_ID` 不整合と判明し、候補 d は「`del_flg` と `del_dtq` の2件」へ拡張された（詳細は本表 `kernel/dataqueue.c（dcre段階3b Task 3）` 行を参照）） |
| kernel/allfunc.h（dcre段階3a Task 5） | mod (dcre-port) | `/* eventflag.c */` 節の `#define TOPPERS_flgcnd` の直後に `TOPPERS_acre_flg`/`TOPPERS_del_flg` の2行を追加（ALLFUNC ビルドで新規関数を有効化するため必須） | - |
| kernel/Makefile.kernel（dcre段階3a Task 5） | mod (dcre-port) | `eventflag =` の .o 列挙に `acre_flg.o`/`del_flg.o` を追加（上流形式の維持目的。`KERNEL_FCSRCS` は無改変。CMake の `ALLFUNC` ビルドはこの行を参照しないため実ビルドには影響しない） | - |
| kernel/kernel_rename.def（dcre段階3a Task 5） | mod (dcre-port) | `# eventflag.c` 節に `free_flgcb`/`tmax_sflgid`/`aflginib_table` の3エントリを追加 | - |
| kernel/kernel_rename.h・kernel_unrename.h（dcre段階3a Task 5） | mod (dcre-port, 生成物) | `utils/genrename.rb kernel` で再生成（手編集ではない）。`git diff --stat` は両ファイルとも各+3行・削除0行（`# eventflag.c` 節に3エントリの `#define`/`#undef` が追加されただけ、他の差分なし） | - |
| kernel/mutex.h（dcre段階3a Task 6） | mod (dcre-port) | Task 4/5 と同型（`tmax_smtxid`／`QUEUE free_mtxcb`／`amtxinib_table[]` の extern 追加、`tnum_mtx` の移設と `tnum_smtx` 新設、既存 `MTXID` の2レンジ版への置換）。`#include <queue.h>` は元から `:49` にあるため追加不要 | - |
| kernel/mutex.c（dcre段階3a Task 6） | mod (dcre-port) | `#define tnum_mtx` を削除（`.h` へ移設）。`QUEUE free_mtxcb;` の定義を追加。`initialize_mutex` の静的ループ境界を `tnum_mtx` → `tnum_smtx` に変更し、既存のマスタ限定ブロックの中（`mtxhook_check_ceilpri`/`mtxhook_release_all` の設定より後ろ）に動的スロット初期化を追加。ミューテックスは非親和オブジェクトのため `iprcid`/`affinity`/`p_pcb` の充填やプロセッサ判定は一切行っていない。`acre_mtx`/`del_mtx` を `TOPPERS_mtxrela` 区画の直後に追加（dcre `mutex.c:378-423`/`:428-479` の転写。`remove_mutex`/`mutex_drop_priority` の `Inline` 定義より後ろでなければならない）。dcre からの意図的な逸脱4件：(1) glock の対化、(2) 空判定 `tnum_mtx == 0` → `tnum_mtx == tnum_smtx`、(3) `del_mtx` の `CHECK_TSKCTX_UNL()`＋`p_runtsk` を `CHECK_TSKCTX_UNL_MYSTATE(&p_selftsk)`＋`p_selftsk != p_my_pcb->p_schedtsk` へ、および MP 版シグネチャ（`init_wait_queue(p_my_pcb, …)`／`mutex_drop_priority(p_my_pcb, p_tcb, oldpri)`）への読み替え、(4) dcre に無い `p_mtxcb->p_loctsk = NULL;` を `del_mtx` に追加（FMP3 の `ini_mtx:606-614` に倣う。`remove_mutex` は退避済みローカル `p_loctsk` を使うので順序上安全）。★`MTX_CEILING()` が `p_mtxinib->mtxatr` を読むため、`del_mtx` では優先度復帰を `mtxatr = TA_NOEXS` の前に行う順序制約がある（実装済み・確認済み）。★`mtxatr != TA_CEILING` のとき `ceilpri` が未検査のまま `INT_PRIORITY()` に通る点は dcre 由来の既知の性質で、`MTX_CEILING()` が偽なので読まれない＝本段階では hardening しない。★削除済み（TA_NOEXS）のミューテックスが `p_lastmtx` 連鎖や待ち対象に残ることは無い（`del_mtx` が `remove_mutex` で外し `init_wait_queue` で待ちを解除するため）＝`mutex_check_ceilpri` は TA_NOEXS スロットを見ない。`loc_mtx`/`ploc_mtx`/`tloc_mtx`/`unl_mtx`/`ini_mtx`/`ref_mtx` の6関数に existence-before-state 規約の E_NOEXS 分岐を挿入（既存ロジックは字下げのみで byte-preserve） | - |
| kernel/allfunc.h（dcre段階3a Task 6） | mod (dcre-port) | `/* mutex.c */` 節の `#define TOPPERS_mtxrela` の直後に `TOPPERS_acre_mtx`/`TOPPERS_del_mtx` の2行を追加（ALLFUNC ビルドで新規関数を有効化するため必須）。既存の `TOPPERS_mtxscan`/`TOPPERS_mtxdrop`（`mutex.c` に対応する `#ifdef` 区画が無い pristine 既存の残骸）は触っていない | - |
| kernel/Makefile.kernel（dcre段階3a Task 6） | mod (dcre-port) | `mutex =` の .o 列挙の `mtxrela.o` の次に `acre_mtx.o`/`del_mtx.o` を追加（上流形式の維持目的。`KERNEL_FCSRCS` は無改変。CMake の `ALLFUNC` ビルドはこの行を参照しないため実ビルドには影響しない） | - |
| kernel/kernel_rename.def（dcre段階3a Task 6） | mod (dcre-port) | `# mutex.c` 節に `free_mtxcb`/`tmax_smtxid`/`amtxinib_table` の3エントリを追加 | - |
| kernel/kernel_rename.h・kernel_unrename.h（dcre段階3a Task 6） | mod (dcre-port, 生成物) | `utils/genrename.rb kernel` で再生成（手編集ではない）。`git diff --stat` は両ファイルとも各+3行・削除0行（`# mutex.c` 節に3エントリの `#define`/`#undef` が追加されただけ、他の差分なし） | - |
| test/test_dcre3.c（dcre段階3a Task 7） | add (dcre-port) | 動的生成API（acre_sem/del_sem・acre_flg/del_flg・acre_mtx/del_mtx）の QEMU 回帰テスト本体（Task 7）。musca_b1-2core 上で、(A) acre_sem→sig/wai/pol の基本動作→休止資源での del_sem→E_NOEXS ×6、(B) 同一プロセッサ（PRC1）の E_DLT 実証（TASK2 を wai_sem で待たせてから del_sem、check_ercd で E_DLT を確認）、(C) 別プロセッサ（PRC2）の E_DLT 実証（TASK3 を wai_sem で待たせ、PRC1 から del_sem。init_wait_queue の MP 経路）、(D) スロット枯渇 E_NOID／静的オブジェクト（SEM1）への del が E_OBJ／パラメータ検査 E_PAR・E_RSATR／del→再acreで同一ID（FIFO/LIFO不問の決定形）、(E) flg: acre→set/clr/wai/pol/ini→del→E_NOEXS ×7・TA_CLR の実動作、(F) mtx: acre(TA_CEILING)→loc→現在優先度が上限(HIGH_PRIORITY)へ上がる→★ロック中の del_mtx が成功→現在優先度がベース(MID_PRIORITY)へ復帰することを get_pri で実測→削除済み確認(E_NOEXS ×6)、(G) mtx エラー系（不正ceilpriでE_PAR、未定義属性ビットでE_RSATR）とスロット再利用、の7シナリオを検証する。ブリーフのコードをそのまま実装したが、`ext_tsk()` の呼び出しは `check_ercd(ext_tsk(), E_OK)` ではなく `test_dcre1.c`/`test_dcre2.c` の既存流儀（`ext_tsk();` 単体呼び出し）に合わせた。QEMU実測で、PRC2 の `check_point_prc(1,2)`/`check_point_prc(2,2)` の表示は test_dcre2 と同じ理由（`syssvc/test_svc.c:136` の引数順が prcid 先・count 後）で `Check point 2-1 passed.`/`Check point 2-2 passed.` となり、`Check point` 系ログの総行数は PRC1(1..11 + check_finish(12) が "Check point 12 passed." も出す＝12行) + PRC2(2行) = 14行であることを確認した（ブリーフの見積り「13行」は check_finish 自身のログ出力分を数え漏れていた）。get_pri/TSK_SELF/TMIN_TPRI/TMAX_TPRI/TSK_NONE の実引用行番号はブリーフの想定（`include/kernel.h:307`等）とは異なる（`:323`/`:603`/`:622`/`:623`/`:604`）が、値・意味は一致するためテスト内容に影響なし | - |
| test/test_dcre3.cfg（dcre段階3a Task 7） | add (dcre-port) | 上記テストのシステムコンフィギュレーションファイル。ブリーフの記述どおり、`CRE_SEM(SEM1, …)`/`CRE_FLG(FLG1, …)`/`CRE_MTX(MTX1, …)` を CLS_PRC1 内の静的オブジェクトとして置き、`TASK2`(PRC1)/`TASK3`(PRC2) を E_DLT 受信用の高優先度待ちタスクとして配置、`AID_SEM(2)`/`AID_FLG(1)`/`AID_MTX(2)` をクラス外（E_RSATR 検査対象の裏付け）に置く。sem/flg/mtx はメモリプールを使わないため `DEF_MPK` は無し | - |
| test/test_dcre3.h（dcre段階3a Task 7） | add (dcre-port) | 上記テストのヘッダ（優先度定数・`TEST_TIME_PROC`・関数プロトタイプ）。`test_dcre2.h` と同型 | - |
| test/MANIFEST（dcre段階3a Task 7） | mod (dcre-port) | `test_dcre2.h` の後・`test_dcre_mix.c` の前（アルファベット順、`_` (0x5F) > `3` (0x33) のため `test_dcre3` が先）に `test_dcre3.c`/`test_dcre3.cfg`/`test_dcre3.h` の3行を追加 | - |
| test/testexec.rb（dcre段階3a Task 7） | mod (dcre-port) | `"dcre2" => { SRC: "test_dcre2" },` の後・`"dcremix"` の前に `"dcre3" => { SRC: "test_dcre3" },` を追加 | - |
| kernel/dataqueue.h（dcre段階3b Task 3） | mod (dcre-port) | `tmax_sdtqid`／`QUEUE free_dtqcb`／`adtqinib_table[]` の extern を追加。`tnum_dtq` を `dataqueue.c` から移設し `tnum_sdtq` を新設。既存の `DTQID(p_dtqcb)` マクロを段階2 の `CYCID`・段階3a の `SEMID` と同型の**2レンジ版へ置換**（新設ではない）。AID 無し構成では `tnum_dtq == tnum_sdtq` となり従来と同一の式に落ちる＝挙動不変。`#include <queue.h>` は元から `:51` にあるため追加不要 | - |
| kernel/dataqueue.c（dcre段階3b Task 3） | mod (dcre-port) | `#define tnum_dtq` を削除（`.h` へ移設）。`QUEUE free_dtqcb;` の定義を追加。`initialize_dataqueue` の静的ループ境界を `tnum_dtq` → `tnum_sdtq` に変更し、**既存のマスタ限定ブロックの中に**動的スロット初期化（`queue_initialize(&free_dtqcb)`、`adtqinib_table[j].dtqatr = TA_NOEXS`、`queue_insert_prev` で FIFO 挿入）を追加。**データキューは非親和オブジェクトのため `iprcid`/`affinity`/`p_pcb` の充填やプロセッサ判定は一切行っていない**。`acre_dtq`/`del_dtq` を追加（dcre `dataqueue.c:339-397`/`:402-444` の転写）。dcre からの意図的な逸脱4件：(1) glock の対化、(2) 空判定 `tnum_dtq == 0` → `tnum_dtq == tnum_sdtq`、(3) `del_dtq` の `CHECK_TSKCTX_UNL()`＋`p_runtsk` を `CHECK_TSKCTX_UNL_MYSTATE(&p_selftsk)`＋`p_selftsk != p_my_pcb->p_schedtsk` へ、(4) ★dcre `dataqueue.c:413` の `CHECK_PAR(VALID_DTQID(dtqid))`（E_PAR）を `CHECK_ID(VALID_DTQID(dtqid))`（E_ID）へ — dcre 自身の不整合（`del_pdq`/`del_mpf` は `CHECK_ID`、FMP3 の dtq 系サービスコールも全て `CHECK_ID`）。段階3a 訂正D（`del_flg`）の同型2件目であり、上流報告候補 d を拡張する。★`del_dtq` は **`TA_MBALLOC` のビット検査と `free_mpk` を `dtqatr = TA_NOEXS` の書込みより前**に置く順序制約がある（`TA_NOEXS` は `((ATR)(-1))`＝全ビット1なので、後に置くと `(dtqatr & TA_MBALLOC) != 0U` が必ず真になり、ユーザ供給の管理領域まで `free_mpk` に渡してしまう）。dcre `dataqueue.c:427-430` も同じ順序。★`acre_dtq` はユーザ供給の `dtqmb` を**受理**する（`TA_MBALLOC` を立てない）。cfg 側の静的生成が `dtqmb != NULL` を E_NOSPT で弾くのとは非対称だが dcre 忠実である。`dtqcnt` の範囲検査は dcre に無いので**書いていない**。`snd_dtq`/`psnd_dtq`/`tsnd_dtq`/`fsnd_dtq`/`rcv_dtq`/`prcv_dtq`/`trcv_dtq`/`ini_dtq`/`ref_dtq` の9関数に existence-before-state 規約の E_NOEXS 分岐を挿入。うち8関数は既存ロジックを字下げのみで byte-preserve したが、**`fsnd_dtq` だけは dcre に倣って構造変更した**：ロック取得前の `CHECK_ILUSE(p_dtqcb->p_dtqinib->dtqcnt > 0U)` を削除し、ロック内の E_NOEXS の次の分岐 `else if (!(p_dtqcb->p_dtqinib->dtqcnt > 0U)) { ercd = E_ILUSE; }` へ移した（dcre `dataqueue.c:604-606`）。理由は E_NOEXS ゲートより前に削除済みスロットの INIB を読まないため。返値・意味論は不変 | 報告候補（上流報告候補 d の拡張。`del_dtq` も `del_flg` と同型の `CHECK_PAR`→`CHECK_ID` 不整合を持つ） |
| kernel/allfunc.h（dcre段階3b Task 3） | mod (dcre-port) | `/* dataqueue.c */` 節の `#define TOPPERS_dtqrcv` の直後に `TOPPERS_acre_dtq`/`TOPPERS_del_dtq` の2行を追加（ALLFUNC ビルドで新規関数を有効化するため必須） | - |
| kernel/Makefile.kernel（dcre段階3b Task 3） | mod (dcre-port) | `dataqueue =` の .o 列挙に `acre_dtq.o`/`del_dtq.o` を追加（上流形式の維持目的。`KERNEL_FCSRCS` は無改変。CMake の `ALLFUNC` ビルドはこの行を参照しないため実ビルドには影響しない） | - |
| kernel/kernel_rename.def（dcre段階3b Task 3） | mod (dcre-port) | `# dataqueue.c` 節に `free_dtqcb`/`tmax_sdtqid`/`adtqinib_table` の3エントリを追加 | - |
| kernel/kernel_rename.h・kernel_unrename.h（dcre段階3b Task 3） | mod (dcre-port, 生成物) | `utils/genrename.rb kernel` で再生成（手編集ではない）。`git diff --stat` は両ファイルとも各+3行・削除0行（`# dataqueue.c` 節に3エントリの `#define`/`#undef` が追加されただけ、他の差分なし） | - |
| kernel/check.h（dcre段階3b Task 4） | mod (dcre-port) | `VALID_TPRI` の直後に `VALID_DPRI(dpri)`（dcre `check.h:71` と同一）を追加。`acre_pdq` の `maxdpri` 検査に必要で、FMP3 の pristine には存在しなかった（既存の `snd_pdq` 等は直書きしており、それは書き換えていない）。段階1 で `CHECK_VALIDATR` を追加したのと同じ前例 | - |
| kernel/pridataq.h（dcre段階3b Task 4） | mod (dcre-port) | Task 3 の `dataqueue.h` 行と同型（`tmax_spdqid`／`QUEUE free_pdqcb`／`apdqinib_table[]` の extern 追加、`tnum_pdq` の移設と `tnum_spdq` 新設、既存 `PDQID` の2レンジ版への**置換**、`#include <queue.h>` は既存で追加不要） | - |
| kernel/pridataq.c（dcre段階3b Task 4） | mod (dcre-port) | `#define tnum_pdq` を削除（`.h` へ移設）。`QUEUE free_pdqcb;` の定義を追加。`initialize_pridataq` の静的ループ境界を `tnum_pdq` → `tnum_spdq` に変更し、既存のマスタ限定ブロックの中に動的スロット初期化を追加。**優先度データキューは非親和オブジェクトのため `iprcid`/`affinity`/`p_pcb` の充填やプロセッサ判定は一切行っていない**。`acre_pdq`/`del_pdq` を追加（dcre `pridataq.c:316-379`/`:384-426` の転写）。dcre からの意図的な逸脱2件：(1) glock の対化、(2) 空判定 `tnum_pdq == 0` → `tnum_pdq == tnum_spdq`（`del_pdq` の ID 検査は dcre も `CHECK_ID` なので `del_dtq` のような訂正は不要）。★`del_pdq` は **`TA_MBALLOC` のビット検査と `free_mpk` を `pdqatr = TA_NOEXS` の書込みより前**に置く順序制約がある（`TA_NOEXS` は全ビット 1 なので後に置くと必ず真になる）。`snd_pdq`/`psnd_pdq`/`tsnd_pdq`/`rcv_pdq`/`prcv_pdq`/`trcv_pdq`/`ini_pdq`/`ref_pdq` の8関数に existence-before-state 規約の E_NOEXS 分岐を挿入。うち5関数は字下げのみで byte-preserve したが、**`snd_pdq`/`psnd_pdq`/`tsnd_pdq` は dcre に倣って構造変更した**：ロック取得前の `CHECK_PAR(TMIN_DPRI <= datapri && datapri <= p_pdqcb->p_pdqinib->maxdpri)` を `CHECK_PAR(TMIN_DPRI <= datapri)` に短縮し、`maxdpri` との比較をロック内の `else if (datapri > p_pdqcb->p_pdqinib->maxdpri) { ercd = E_PAR; }` へ移した（dcre `pridataq.c:444,450-452` ほか）。理由は E_NOEXS ゲートより前に削除済みスロットの INIB を読まないため。返値・意味論は不変 | - |
| kernel/allfunc.h（dcre段階3b Task 4） | mod (dcre-port) | `/* pridataq.c */` 節に `TOPPERS_acre_pdq`/`TOPPERS_del_pdq` を追加 | - |
| kernel/Makefile.kernel（dcre段階3b Task 4） | mod (dcre-port) | `pridataq =` 行に `acre_pdq.o`/`del_pdq.o` を追加 | - |
| kernel/kernel_rename.def（dcre段階3b Task 4） | mod (dcre-port) | `# pridataq.c` 節に `free_pdqcb`/`tmax_spdqid`/`apdqinib_table` の3エントリを追加 | - |
| kernel/kernel_rename.h・kernel_unrename.h（dcre段階3b Task 4） | mod (dcre-port, 生成物) | 再生成。各+3行・削除0行 | - |
| kernel/mempfix.h（dcre段階3b Task 5） | mod (dcre-port) | Task 3/4 と同型（`tmax_smpfid`／`QUEUE free_mpfcb`／`ampfinib_table[]` の extern 追加、`tnum_mpf` の移設と `tnum_smpf` 新設、既存 `MPFID` の2レンジ版への**置換**） | - |
| kernel/mempfix.c（dcre段階3b Task 5） | mod (dcre-port) | `#define tnum_mpf` を削除（`.h` へ移設）。`QUEUE free_mpfcb;` の定義を追加。`initialize_mempfix` の静的ループ境界を `tnum_mpf` → `tnum_smpf` に変更し、既存のマスタ限定ブロックの中に動的スロット初期化を追加（free-list のリンクは `wait_queue`）。**固定長メモリプールは非親和オブジェクトのため `iprcid`/`affinity`/`p_pcb` の充填やプロセッサ判定は一切行っていない**。`acre_mpf`/`del_mpf` を追加（dcre `mempfix.c:199-279`/`:284-328` の転写）。dcre からの意図的な逸脱2件：(1) glock の対化、(2) 空判定 `tnum_mpf == 0` → `tnum_mpf == tnum_smpf`（`del_mpf` の ID 検査は dcre も `CHECK_ID` なので訂正不要）。★`acre_mpf` は**2段確保**（①`mpf == NULL` なら `malloc_mpk(ROUND_MPF_T(blksz) * blkcnt)` + `TA_MEMALLOC`、②`mpfmb == NULL` なら `malloc_mpk(sizeof(MPFMB) * blkcnt)` + `TA_MBALLOC`）で、**②失敗時は `pk_cmpf->mpf == NULL` を条件に①を `free_mpk` で巻き戻す**（判定にローカル `mpf` を使うと①で上書きされていて区別できない。dcre `mempfix.c:250` と同一）。`MPFINIB.blksz` には `ROUND_MPF_T(blksz)`（丸めた値）を入れる。E_NOMEM のとき free-list から CB を取り出していない（2段とも成功してから `queue_delete_next`）。★`del_mpf` は **`TA_MEMALLOC`/`TA_MBALLOC` の2つのビット検査と `free_mpk` を `mpfatr = TA_NOEXS` の書込みより前**に置く順序制約がある（`TA_NOEXS` は `((ATR)(-1))`＝全ビット1なので、後に置くと2条件とも必ず真になり、ユーザ供給の領域まで解放してしまう）。dcre `mempfix.c:308-314` も同じ順序。`get_mpf`/`pget_mpf`/`tget_mpf`/`rel_mpf`/`ini_mpf`/`ref_mpf` の6関数に existence-before-state 規約の E_NOEXS 分岐を挿入。うち5関数は字下げのみで byte-preserve したが、**`rel_mpf` は dcre に倣って構造変更した**：ロック取得前の `CHECK_PAR` 4件を削除し、ロック内の E_NOEXS の次に `blkoffset`/`blkidx` の計算と4条件の `||` 結合による `E_PAR` 分岐を置いた（dcre `mempfix.c:487-495`）。これは **ercd の問題ではなくメモリ安全性の修正**である（削除済みプールでは `p_mpfinib->p_mpfmb` が `free_mpk` 済みの番地で、ロック前にデリファレンスすると解放済み領域を読む）。4条件の評価順は dcre のままで、範囲外 `blkidx` は第3条件の短絡により第4条件へ到達しない | - |
| kernel/allfunc.h（dcre段階3b Task 5） | mod (dcre-port) | `/* mempfix.c */` 節に `TOPPERS_acre_mpf`/`TOPPERS_del_mpf` を追加 | - |
| kernel/Makefile.kernel（dcre段階3b Task 5） | mod (dcre-port) | `mempfix =` 行に `acre_mpf.o`/`del_mpf.o` を追加 | - |
| kernel/kernel_rename.def（dcre段階3b Task 5） | mod (dcre-port) | `# mempfix.c` 節に `free_mpfcb`/`tmax_smpfid`/`ampfinib_table` の3エントリを追加 | - |
| kernel/kernel_rename.h・kernel_unrename.h（dcre段階3b Task 5） | mod (dcre-port, 生成物) | 再生成。各+3行・削除0行 | - |
| test/test_dcre4.c（dcre段階3b Task 6） | add (dcre-port) | 動的生成API（acre_dtq/del_dtq・acre_pdq/del_pdq・acre_mpf/del_mpf）の QEMU 回帰テスト本体。musca_b1-2core 上で、(A) acre_dtq→psnd/prcv/fsnd の実通信→del_dtq→E_NOEXS×9（dtqcnt==0の管理領域なしdtqも含む）、(B) E_NOMEM と E_NOID の順序実証（同じ過大 dtqcnt が空きスロットありでは E_NOMEM・枯渇では E_NOID になることと、E_NOMEM 後も free-list が減っていないことを直後の acre×2 成功で確認）、(C) 送信待ち E_DLT（TASK2・PRC1）と受信待ち E_DLT（TASK3・PRC2、init_wait_queue の MP 経路）の両方、(D) del→再acre後のキュー状態リセット確認、(E) pdq: 優先度順配送・maxdpri のロック内検査・del→E_NOEXS×8・VALID_DPRI 検査、(F) dtq のスロット枯渇 E_NOID／静的オブジェクトへの del が E_OBJ／空き1個からの決定的再acre／ユーザ供給 dtqmb 経路、(G) mpf: 自動確保→pget/get/rel→ブロック内容の読み書き実証→未返却ブロックを持ったままの del→E_NOEXS×6→ブロック領域が入らない大きさでの E_NOMEM→MPF_CYCLES(=16)回の acre/del 反復でプールが実際に返っていることの実証、の8シナリオを検証する。ブリーフのコードをそのまま実装。`user_dtqmb`（`DTQMB` 型）は `kernel/dataqueue.h` がカーネル内部ヘッダで `<kernel.h>` 経由では見えないため、ブリーフの代替指示どおり `static intptr_t user_dtqmb_area[USER_DTQCNT];` を用いて `cdtq.dtqmb = (void *) user_dtqmb_area;` とした（`DTQMB` は `intptr_t data;` 1個の構造体でサイズ・アライン等価）。`check_assert(data == 0x10 && datapri == 1)` 型の `&&` 結合は `test_pdq1.c` に前例が無かったため、ブリーフの注意書きに従い1行ずつの `check_assert` に分割した。QEMU実測で `Check point` 系ログは PRC1(1..12 の12行 + check_finish(13) 自身の1行=13行) + PRC2(2行) = 15行であり、ブリーフの事前見積り「15」と一致した。★カーネル変異 negative control 2件を実施：(1) `kernel/dataqueue.c` の `del_dtq` の `queue_insert_prev(&free_dtqcb, ...)` をコメントアウトすると、`AID_DTQ(2)` の2スロットが手順1で枯渇し、手順2の `check_assert(acre_dtq(&cdtq) == E_NOMEM)`（`test_dcre4.c:226`）が「実際は E_NOID が返る」ために失敗する（ブリーフの予測は `check_assert(erid > DTQ1)` での失敗だったが、同じ free-list 枯渇機序の別の断面。ブリーフの許容規定により実測を正とした）。TTSP_RESULT: PASS は出ない。(2) `kernel/mempfix.c` の `del_mpf` の `TA_MEMALLOC` 分岐内の `free_mpk(p_mpfinib->mpf);` をコメントアウトすると、`Check point 11 passed.` は出るが `Check point 12 passed.` は出ず、`check_assert(erid > MPF1)`（`test_dcre4.c:519`、MPF_CYCLES ループ内）がブリーフの予測どおりの行で失敗する。両方とも変異を復元し `TTSP_RESULT: PASS`（15行）に戻ることを確認した（`git diff` で `kernel/dataqueue.c`/`kernel/mempfix.c` の差分ゼロを確認）。★シナリオD（旧称「滞留データの破棄」）はレビューで訂正：`acre_dtq` は生成のたびに無条件で `count = 0; head = 0; tail = 0;` をリセットする（`kernel/dataqueue.c:396-398`）ため、「del → 再 acre → 空」の検査は del_dtq が滞留データをどう扱おうと必ず通る。すなわち本検査が実証するのは acre 側のキュー状態リセットのみであり、del_dtq 側の滞留データ破棄そのものは公開 API から観測不能（count が全読出しをゲートするため）で判別できない — 既知の検査限界としてコメント・テスト目的欄を書き換えた（テスト自体の削除は不要）。★hardening パス Task 2 で `acre_mpf` の**2 段確保・②失敗時の巻き戻し**経路の runtime 検査を追加（`blkcnt=400/blksz=4` で①成功・②失敗させ、直後に `blkcnt=200/blksz=4` が成功することで①の解放を観測する。bump allocator の `count==0` リセットを梃子にした間接観測であり、`free_mpk` の呼出しそのものは公開 API から観測できない）。変異 control（`pk_cmpf->mpf` → ローカル `mpf`）でケースB が倒れることを実演済み。checkpoint は増やしていない（15 のまま）。★hardening パス Task 4 で **Task 1 のオーバーフロー検査4件の `E_PAR` を runtime で実演**（(a) `acre_dtq` の `dtqcnt*sizeof(DTQMB)`、(b) `acre_pdq` の `pdqcnt*sizeof(PDQMB)`、(c) `acre_mpf` の `ROUND_MPF_T(blksz)*blkcnt`、(d) `acre_mpf` の `ROUND_MPF_T(blksz)` 自体の丸めあふれ。いずれも32bit限定なので `#if SIZE_MAX == UINT_MAX` で囲み、`#if` が生きていることを `check_assert(sizeof(size_t) == sizeof(uint_t))` で担保。musca_b1-2core で実測 PASS・checkpoint 15 のまま）。変異 control C-1（`dataqueue.c` の `dtqcnt` 検査削除）／C-2（`mempfix.c` の `blksz` 丸め検査削除）を実演済み：C-1 は (a) の `check_assert` が`test_dcre4.c:572` で失敗。C-2 は (d) の `check_assert` が `test_dcre4.c:587` で失敗するが、**タスクレビューで「ブリーフの予測『②で E_NOMEM』を検証せずに転記していた」ことが指摘され、再検証（一時計装＋逆アセンブル）で実際の機序を確定した**：guard(a)削除後、guard(b)の `CHECK_PAR(blkcnt <= SIZE_MAX/ROUND_MPF_T(blksz))` は除数が非定数（実行時値）のため GCC が umull による桁あふれ検査へ変換しており、`ROUND_MPF_T(UINT_MAX)`＝0（丸め加算のあふれ）のときは `blkcnt*0` が桁あふれせず判定を素通りする（`kernel/mempfix.c:237-242` の既存コメントが予告していた経路そのもの）。その結果 `mempfix.c:278` の `malloc_mpk(ROUND_MPF_T(blksz)*blkcnt)` も同じ0を渡されて**即座に成功**し、`acre_mpf` は E_NOMEM を経由せず**正常終了（実測 erid=2）**する——`check_assert(erid == E_PAR)` はこの「見た目は正常終了だが blksz=0 の壊れたプールができている」不正な成功を捕まえて落ちる。C-2 は空振り（guard(b)による代替検出）ではなく実際に検出しているという結論に変わりはないが、「②で E_NOMEM」の記述は誤りだったので訂正する。両変異とも復元後 `git diff --stat kernel/` が空であることを確認 ★mempool freelist 化（本行の startup.c 置換と同一コミット）で【算術】コメント（`sizeof(MEMPOOLCB)` 12→16・実効容量 2036→2032・残量 436→428・ヘッダ4B/割付け）と MPF_CYCLES ループの実証コメントを新レイアウトへ更新（コード・check_assert は無変更、checkpoint 15 のまま。ケースA のロールバックは count==0 backstop 経由で旧実装とビット単位で同一の全域リセットになるため判定は不変 — spec §7.3 の机上トレースどおり QEMU 実測 PASS で確認） | - |
| test/test_dcre4.cfg（dcre段階3b Task 6） | add (dcre-port) | 上記テストのシステムコンフィギュレーションファイル。ブリーフの記述どおり、`CRE_DTQ(DTQ1, …)`/`CRE_PDQ(PDQ1, …)`/`CRE_MPF(MPF1, …)` を CLS_PRC1 内の静的オブジェクトとして置き、`TASK2`(PRC1)/`TASK3`(PRC2) を E_DLT 受信用の高優先度待ちタスクとして配置、`AID_DTQ(2)`/`AID_PDQ(1)`/`AID_MPF(1)` をクラス外（E_RSATR 検査対象の裏付け）に置く。**`test_dcre3.cfg` と異なり `DEF_MPK({ MPK_SIZE, NULL })` が必須**（dtq/pdq/mpf の管理領域は `malloc_mpk` から取るため、`DEF_MPK` が無いと `mpk_valid` が偽になり `dtqcnt > 0` の `acre_dtq` がすべて E_NOMEM になる） | - |
| test/test_dcre4.h（dcre段階3b Task 6） | add (dcre-port) | 上記テストのヘッダ（優先度定数・`MPK_SIZE`/`MPF_BLKCNT`/`MPF_BLKSZ`/`MPF_CYCLES`/`USER_DTQCNT`/`TEST_TIME_PROC`・関数プロトタイプ）。`MPK_SIZE = 2048` は正常系の1周分（272B前後）は入るが、`del_mpf` の `TA_MEMALLOC` 解放が生きていない場合の `MPF_CYCLES(=16)` 周分の累積（4KB超）は入らないよう選定（negative control 2 の成立条件） ★mempool freelist 化で MPK_SIZE 選定根拠コメントを新実装（バンプ＋完全一致再利用、1周 272B→280B、backstop で頭打ち）へ更新（値は 2048 のまま、negative control の成立条件も維持：del_mpf の TA_MEMALLOC 解放を落とすと freelist に 256B エントリが載らないため 7〜8 周で E_NOMEM になる関係は保存される） | - |
| test/MANIFEST（dcre段階3b Task 6） | mod (dcre-port) | `test_dcre3.h` の後・`test_dcre_mix.c` の前（アルファベット順、`test_dcre3` < `test_dcre4` < `test_dcre_mix`）に `test_dcre4.c`/`test_dcre4.cfg`/`test_dcre4.h` の3行を追加 | - |
| test/testexec.rb（dcre段階3b Task 6） | mod (dcre-port) | `"dcre3" => { SRC: "test_dcre3" },` の後・`"dcremix"` の前に `"dcre4" => { SRC: "test_dcre4" },` を追加 | - |
| kernel/kernel_api.def（ISR段階 Task 2） | mod (dcre-port) | `AID_MPF .nompf` の次に `AID_ISR .noisr` / `ENA_DYNISR .intno*` の2行を追加。`AID_ISR .noisr` は既存の `AID_xxx` 群と同じ形（パラメータ名 `noisr` は共通枠組みの `self.noobj = "no" + obj`（`kernel/kernel.py:121`）に一致させる必須の命名）。`ENA_DYNISR` は FMP3 独自の静的 API（dcre には無い）で、案B-2 ハイブリッド（ENA_DYNISR で明示した intno だけをキュー方式にする）の opt-in 印である。`*`（KEYPAR）により同一 intno への重複記述が共通部（`cfg_py/pass2.py:385-391`）で自動的に E_OBJ になり、`cfgData["ENA_DYNISR"]` が intno 値をキーとする dict になる（`dcre_dynisr_duplicated.cfg` で回帰確認、ENA_DYNISR 固有のコードは1行も要らない）。両エンジン共用の1ファイルのため、この変更で「両エンジン同時変更」の一部を満たす | - |
| kernel/interrupt.trb（ISR段階 Task 2） | mod (dcre-port) | (a) 冒頭の `kernel_cfg.h` への `TNUM_ISRID`/各ISRIDマクロの出力（3文）を削除し、末尾の `IsrObject.new.generate()`（共通枠組み）へ移動する旨のコメントへ置換。(b) `CRE_ISR` 検査ループの直後・`isr_flag = {}` の直前に、`ENA_DYNISR` のエラーチェック（クラス内必須＝E_RSATR、intno有効範囲＝E_PAR、CFG_INT必須＝E_OBJ、CFG_INTと同一クラス必須＝E_RSATR、カーネル管理外intno拒否＝E_OBJ、DEF_INH競合＝E_OBJ）と、★訂正E の2ガード（ENA_DYNISR≥1かつ静的CRE_ISR=0はE_OBJ／AID_ISR≥1かつdynIsrList=0はE_OBJ）、および `isr_queue_list`/`isr_queue_table` の生成を追加。(c) inthdr 生成ループ内側、`if isrParamsList.size > 0` の直前に opt-in intno（`$isrQueueHeader` にある intno）の枝を追加：DEF_INH相当の登録＋`_kernel_call_isr` 1行だけの inthdr 生成＋`next`（`next` を落とすと opt-in intno に静的ISRもある場合に同名関数が二重定義されコンパイルエラーになる）。(d) inthdr 生成ループの直後に `IsrObject`（`generateInib` は `p_isr_queue` を opt-in 済みなら `$isrQueueHeader` の値、さもなくば `NULL` にする）と `isrorder_table`（isrid昇順・`TNUM_SISRID==0` は `TOPPERS_EMPTY_LABEL`）を追加。dcre からの意図的な逸脱3件：①キューを作るのは `ENA_DYNISR` された intno だけ（dcre は全 intno）、②`isrorder_table` は isrid 昇順（dcre は挿入順。opt-in の有無で `.cfg` の呼出し順序が変わらないようにするため）、③`isrorder_table` に `TNUM_SISRID==0` の `TOPPERS_EMPTY_LABEL` ガードを追加（dcre は `[0]` 配列になる）。既存のインライン連鎖生成の枝は1文字も変えていない（`test_int2` の生成物がバイト一致することで実証済み、Task 2 Step 11）。**ISR段階でdeferredだった『カーネル管理外intpri×ENA_DYNISR→E_OBJ』の回帰cfgをhardeningパスTask4で追加**（`tools/cfg_error_tests/dcre_dynisr_unmanaged_intpri.cfg`。musca_b1は`INTPRI_CFGINT_VALID`(-8..-1)⊋`TMIN_INTPRI`(-3)なので構成できる。両エンジンとも`E_OBJ`が2回出る＝`ENA_DYNISR`自身の分岐と`AID_ISR requires at least one ENA_DYNISR`の連鎖） | - |
| kernel/interrupt.py（ISR段階 Task 2） | mod (dcre-port) | `.trb` と同じ4箇所（(a)〜(d)）を移植。Ruby は `$isrQueueHeader`（グローバル）にする必要があるが（`IsrObject#generateInib` から参照するためスコープ外を避ける）、Python はモジュール変数 `isrQueueHeader` で足りる。両エンジンの出力文字列は完全に同一（`cfg_equivalence.sh` がバイト比較、8構成全てで `RESULT = MATCH`）。**ISR段階でdeferredだった『カーネル管理外intpri×ENA_DYNISR→E_OBJ』の回帰cfgをhardeningパスTask4で追加**（`.trb` の行に記載の内容と同一。両エンジン`rc=0`・文言 IDENTICAL） | - |
| include/kernel.h（ISR段階 Task 2） | mod (dcre-port) | `t_rspn`（`T_RSPN`）の直後・「サービスコールの宣言」コメントの直前に `T_CISR`（dcre `include/kernel.h:322-328` とバイト一致、`diff` rc=0 で確認済み）を追加。「割込み管理機能」コメント直後・`dis_int` 宣言の直前に `extern ER_ID acre_isr(const T_CISR *pk_cisr) throw();` / `extern ER del_isr(ID isrid) throw();` の2宣言を追加（dcre `include/kernel.h:481-482` と同一）。返値型は dcre 自身が `ER_ID` を使っているため、段階1〜3b の `ER_UINT`→`ER_ID` のような逸脱は発生しない。機能コード（`TFN_ACRE_ISR`/`TFN_DEL_ISR`）は Task 1 で既存確認済みのため `include/kernel_fncode.h` は無改変 | - |
| kernel/kernel_impl.h（ISR段階 Task 2） | mod (dcre-port) | `TMIN_SPNID`（`:194`）の直後に `TMIN_ISRID`（割込みサービスルーチンIDの最小値）を追加。dcre は `TMIN_ALMID` の直後に置くが、FMP3 は `TMIN_ALMID` の後ろに `TMIN_SPNID` が既にあるため、既存行の間へ割り込ませず `TMIN_SPNID` の直後（＝ブロック末尾）に足して差分を1行に閉じた。`TARGET_TSKATR` ブロック（`:209-211`）の直後に `#ifndef TARGET_ISRATR #define TARGET_ISRATR 0U #endif`（dcre `kernel_impl.h:155-157` と同一）を追加。`kernel/kernel_sym.def:73` の `TARGET_ISRATR,,,defined(TARGET_ISRATR),0` により cfg 側も同じ 0 を得るため、cfg 生成物（`kernel_cfg.h`/`kernel_cfg.c`）は1バイトも変わらないことを実測で確認済み（Task 2 Step 6：`diff -r` は cfg1_out.db/.dump 等の再コンパイル副産物のみ差分あり、`kernel_cfg.h`/`kernel_cfg.c` は rc=0） | - |
| test/test_dcre_mix.h（ISR段階 Task 2） | mod (dcre-port) | `extern void task1(EXINF exinf);` の直後に、混在構成のISRが使う `extern void mix_isr1(EXINF exinf);` を追加（`target_test.h` は既に include 済みで `INTNO1`/`INTNO1_INTATR`/`INTNO1_INTPRI` を提供する） | - |
| test/test_dcre_mix.c（ISR段階 Task 2） | mod (dcre-port) | `task1` の直後に `mix_isr1`（中身は空）を追加。QEMU では実行しない（ビルド・cfg等価性検査のみ） | - |
| test/test_dcre_mix.cfg（ISR段階 Task 2） | mod (dcre-port) | 8家族目（ISR）として `CFG_INT(INTNO1, …)`/`CRE_ISR(MIX_ISR1, …)`/`ENA_DYNISR(INTNO1)` を `CLASS(CLS_PRC1)` ブロック末尾（`CRE_MPF` の次）に追加し、`AID_ISR(2)` をクラス外（末尾）に追加。同一 `kernel_cfg.c` の中で `MIX_ISR1`（キュー方式・`_kernel_call_isr` 1行）と `ISR_SIO`（`serial.cfg` 由来、インライン連鎖）が共存することを実測確認（Task 2 Step 14）。★混在説明コメント中の `acre_*/del_*` は、`*/` の並びが C コメントを早期終了させるため（段階3a Task 2 と同じ罠、当該行に前例あり）`acre_* / del_*` とスペースを挟んだ（ブリーフの逐語表記からの意図的な最小逸脱。最初の実行でこの罠に落ち、両エンジンとも `syntax error` で `pass1` から MISMATCH になったことを実測してから修正した） | - |
| kernel/interrupt.h（dcre動的ISR Task 3） | mod (dcre-port) | ISR のランタイムオブジェクトを新設した（FMP3 には従来存在しなかった）。`#include <queue.h>` を追加し、`ISRQCB`（★dcre に無い FMP3 の新設型。キューごとの enqueue 世代番号 `isrseq` を持つ）・`ISRINIB`・`ISRCB`（★dcre の2フィールドに `isrseq` と `running` を追加）・`ISR_ENTRY` の4型、`tnum_isr_queue`/`isr_queue_list`/`isr_queue_table`/`free_isrcb`/`tmax_isrid`/`tmax_sisrid`/`isrinib_table`/`aisrinib_table`/`isrorder_table`/`p_isrcb_table` の extern、`tnum_isr`/`tnum_sisr`、2レンジ `ISRID`、`initialize_isr`/`call_isr` の宣言を追加。`ISRID` は dcre の配列差分式（`interrupt.h:126`）ではなく段階2 `CYCID`・段階3a `SEMID`・段階3b `DTQID` と同型の INIB ポインタ差分の2レンジ式にした（FMP3 の CB はポインタ表経由の named static であるため）。**Task 5 レビュー指摘：`ISRQCB` コメント（`:61-63`）が Task 3 時点の「キュー空で isrseq を 0 へ戻す」挙動を記述したまま残っていたのを、`enqueue_isr` の単調カウンタ化（Task 5 コントローラ裁定、`interrupt.c:172-183` の修正済みコメントおよび spec 訂正Gと同内容）に合わせて書き換えた**。Codex 外部レビュー指摘による保証スコープの明確化（機構は変更なし・意味論は起動開始時点集合）：`ISRQCB` コメント（`:61-65`）を，単調 `isrseq` の保証が `isrpri >= cur`（走査位置と同じかそれより後）の位置に限ることが分かるよう書き換えた | - |
| kernel/interrupt.c（dcre動的ISR Task 3） | mod (dcre-port) | `LOG_ISR_ENTER`/`LOG_ISR_LEAVE` のデフォルト定義、`INDEX_ISR`/`get_isrcb`、`ISR_KEY_GT`（★dcre に無い。走査の安定キー比較）、`enqueue_isr`（dcre `interrupt.c:182-195` の形。isrseq はキューの生存期間を通じて単調増加。★Task 3 時点では「キューが空になったとき isrseq を 0 へ戻す」実装だったが、Task 5 でコントローラが裁定して撤回した。理由は Task 5 の行を参照）、`search_isr_queue`（dcre `:267-293` の転写。型が `ISRQCB *` に変わるだけ）、`QUEUE free_isrcb;`、`initialize_isr`（dcre `:207-231` + マスタ限定化 + `p_isr_queue == NULL` のスキップ）、`call_isr` の暫定版（Task 4 で置換）を追加。**ISR は非親和オブジェクトであり `iprcid`/`affinity`/`p_pcb` の充填は1行も書いていない**。`initialize_isr` がマスタ限定なのは共有データの初期化だからで、他プロセッサへの可視性は呼出し後の `barrier_sync` が保証する。**Task 4 で `call_isr` を MP 対応版へ全面的に書き換えた。** dcre `interrupt.c:240-260` の素朴な単方向走査は「走査と acre/del が時間的に排他」という単一プロセッサの性質に依存しており FMP3 では成立しないため、転写ではなく新規設計である。(1) キュー参照はジャイアントロック下、(2) ISR 本体はロック外、(3) 次の ISR は `(isrpri, isrseq)` の辞書式順序でロック再取得後に再決定（ポインタ／ISRID はスロット再利用で曖昧になるため使えない。Codex 指摘 #1）、(4) 実行中は `p_isrcb->running` に自コアのビットを立てる（`del_isr` の quiesce 用。Codex 指摘 #2）、(5) ロック復元は `call_cyclic`（`cyclic.c:541-549`）と同じ3分岐、(6) 走査終了時は必ず CPU ロック解除状態で戻る（インライン連鎖は最後の ISR が残したロックを解除しないという既存の差があるが、キュー方式ではこれを解消している。差が及ぶのは `ENA_DYNISR` された intno のみ）。`running` をビットマップ（カウンタでない）で実装できる根拠は `doc/porting.txt:790,807`（割込み受付時に IPM を受け付けた割込みの優先度へ設定し、同一 intno＝同一優先度の再入を含めてマスクする）に実測で確認した。`get_my_pcb()` をロック前に呼んでよい根拠は `kernel/interrupt.trb:607-609`「割込みハンドラは，他のプロセッサへマイグレートすることはない」（ブリーフの行番号 452-456 とは現物のファイルでずれていたため file:line を実測し直した）。`kernel/interrupt.c` の include に `"spin_lock.h"`（`force_unlock_spin` の宣言。`cyclic.c` と同じ要件）を追加した。`test_int2` を一時的に opt-in させて実行し、チェックポイント（1〜6）の順序・exinf（isr1=1/isr2=2/isr3=3）・ロック復元（C-1/C-2 の `check_assert`）がインライン連鎖のときと同一であることを実測で確認した（確認後 `test_int2.cfg` は復元済み・`git diff --stat` 差分ゼロ）。**Task 5 で `acre_isr`（dcre `interrupt.c:303-352` の転写）と `del_isr`（同 `:361-392` の転写 + quiesce の新設）を追加。** dcre からの意図的な逸脱 5 件：(1) glock の対化、(2) 空判定 `tnum_isr == 0` → `tnum_isr == tnum_sisr`、(3) ★`acre_isr` から `CHECK_PAR(VALID_INTNO_CREISR(intno))` を**削除**した（FMP3 の `VALID_INTNO` は `(prcid, intno)` の2引数で呼出しコアに依存しうるため。intno の検証は cfg が生成するグローバル適格 intno 表の二分探索のみで行い、範囲外・CFG_INT 無し・未 ENA_DYNISR・DEF_INH 競合はすべて E_OBJ になる。dcre は範囲外を E_PAR にするが、FMP3 ではコア非依存に区別する手段が存在しない。Codex 指摘 #3 への対応）、(4) ★`del_isr` に **quiesce ループ**を新設した（`running != 0` の間、glock と CPU ロックを解放し `delay_for_interrupt` を挟んで取り直す。`wait.c:126-131` と同一の5行）。これにより「`del_isr` が E_OK を返した時点で対象 ISR は実行中でなく以後実行されない」という dcre 単一コアと同等の意味論を MP でも保証する。Codex 指摘 #2 への対応、(5) ★`del_isr` の `isratr = TA_NOEXS` を **quiesce の前**に置いた（quiesce 中はロックを解放するため、後に置くと同一 isrid への2本目の `del_isr` が削除済みエントリに `queue_delete` を再実行してキューを壊す）。段階3bの『属性の読みは TA_NOEXS の書込みより前』という不変量は ISR には適用されない — ISR の `isratr` はビット検査にもマスク比較にも使われず、`== TA_NOEXS` の同値比較と代入だけだからである（現物確認済み）。★`del_isr` の `CHECK_ID`（E_ID）は dcre もそうであり逸脱ではない。上流報告候補 d（`del_flg`/`del_dtq` の `CHECK_PAR`）は**拡張しない**。**Task 4 レビュー起因の追加修正（コントローラ裁定）：`enqueue_isr` の「キューが空になったとき isrseq を 0 へ戻す」分岐を全削除した。** isrseq はキューの生存期間を通じて単調増加する。理由：走査中（call_isr がジャイアントロックを外して ISR 本体を呼んでいる間）にキューが一時的に空になり、その隙に `acre_isr` された ISR は isrseq が 0 から振り直されるため、走査側の継続キー `cur` より小さくなり、同一の割込み起動では拾われない（次の割込みまで取りこぼす）。これは spec §5 が保証する「同一起動内での拾い上げ」に反する（安全側の脱落ではなく仕様違反）。単調カウンタであればこの保証が無条件に回復する（ドレイン後の enqueue は必ず任意の走査中 cur より大きい値を得る）。u32 のラップには1つのキューへシステム寿命の間に 2^32 回 enqueue する必要があり、実用上到達不能である（リセットを正当化していたのと同じ論法を、リセットではなくラップの受容に転用する）。**ISR段階で未整備だった「走査中の完全ドレイン→enqueue」の回帰カバレッジを hardening パス Task 3 で解消**（`test_dcre5` 手順9・変異control付き。`enqueue_isr` のコメントにも回帰テストの所在を追記。コードは1バイトも変えていない） | - |
| kernel/check.h（dcre動的ISR Task 3） | mod (dcre-port) | `VALID_ISRID`・`VALID_ISRPRI`・`CHECK_OBJ` を追加。3つとも dcre `check.h:64,73-74,235-240` と同一。段階3b の `VALID_DPRI` 追加と同じ前例 | - |
| kernel/allfunc.h（dcre動的ISR Task 3） | mod (dcre-port) | `/* interrupt.c */` 節に `TOPPERS_isrini`/`TOPPERS_isrcal` を追加。**Task 5 で `TOPPERS_acre_isr`/`TOPPERS_del_isr` を追加** | - |
| kernel/Makefile.kernel（dcre動的ISR Task 3） | mod (dcre-port) | `interrupt =` 行に `isrini.o`/`isrcal.o` を追加。`KERNEL_FCSRCS` は不変。**Task 5 で `interrupt =` 行に `acre_isr.o`/`del_isr.o` を追加**（`KERNEL_FCSRCS` は22個のまま不変） | - |
| kernel/kernel_rename.def（dcre動的ISR Task 3） | mod (dcre-port) | `# interrupt.c` 節に `initialize_isr`/`call_isr`/`free_isrcb`、`# kernel_cfg.c` 節に `tmax_isrid`/`tmax_sisrid`/`isrinib_table`/`aisrinib_table`/`p_isrcb_table`/`isrorder_table`/`tnum_isr_queue`/`isr_queue_list`/`isr_queue_table` を追加（計12）。★`call_isr` のリネームは必須である（cfg が生成する inthdr が `_kernel_call_isr(...)` を呼ぶため） | - |
| kernel/kernel_rename.h・kernel_unrename.h（dcre動的ISR Task 3） | mod (dcre-port, 生成物) | `utils/genrename.rb kernel` で再生成（手編集ではない）。各+12行・削除0行 | - |
| test/test_dcre5.c（dcre動的ISR Task 6） | add (dcre-port) | 動的生成API（acre_isr/del_isr）の QEMU 回帰テスト本体。musca_b1-2core・8手順・変異control 3件（quiesce／走査の安定キー／free-list返却）。(A) `ENA_DYNISR` された `INTNO1` で静的ISR2本（`ISR_S4`/`ISR_S2`、isrpri 4/2）だけの呼出し順序がisrpri昇順（記述順ではない）であること（訂正Iのinvariant）、(B) 静的と動的（isrpri 1/3/5）の混在順序、(C) 同一isrpriの動的ISRがacre順（isrseq昇順）に呼ばれること、(D) ★走査中（PRC1のISR内で待ち合わせ）に別コア（PRC2のTASK3）が同一isrpriのdel/acreを行っても取りこぼし・二重実行が起きないこと（Codex指摘#1）、(E) ★PRC2で実行中のISR（`long_isr`）をPRC1の`del_isr`が完了を待って戻ること（quiesce・Codex指摘#2）、(F) E_OBJ（未ENA_DYNISRのintno／範囲外intno／静的ISRの削除）・E_PAR（isrpri範囲外）・E_RSATR・E_ID・E_NOID・E_NOEXS・E_CTX、(G) 削除後は呼ばれなくなること、をブリーフのコードでほぼ逐語実装した。**ブリーフからの変更点（実測で発覚・記録）**：(1) `(char)(intptr_t) exinf` の二重キャストは冗長（`include/t_stddef.h:108` で `EXINF` は既に `typedef intptr_t EXINF;`）なため `(char) exinf` の単一キャストに簡略化（`-Wall` で警告なしを確認）。(2) ★**起動レースを実測で発見・修正**：`test_start()`（`syssvc/test_svc.c`）は `check_count[]` を全プロセッサ分ゼロクリアするが、`TASK3`（PRC2）が独立カウンタへ最初の `check_point_prc(1, 2)` を打つタイミングがTASK1（PRC1）の `test_start()` 呼び出しより先行する場合があり（2コア同時起動の非決定性、実測でログ順が `Check point 2-1 passed.` → `Check point 1 passed.` となるケースを確認）、その場合 `test_start()` がPRC2側のカウンタを黙って0に巻き戻し、後段の `check_point_prc(2, 2)` が `## Unexpected check point 2-2.` で失敗し `TTSP_RESULT: PASS` が出ないことを実測確認した。対策として `task1_ready`（volatile bool_t）を新設し、TASK1が `test_start()` 直後に真にし、TASK3は最初の `check_point_prc(1, 2)` の前に（`SPIN_LIMIT` 上限つきで）これを待つハンドシェイクを追加した。修正後は3回連続実行で `Check point` 系ログ12行・`TTSP_RESULT: PASS` を確認した（ブリーフには無い、実測発見・実装追加）。QEMU実測で `Check point` 系ログは PRC1(1..9 の9行 + `check_finish(10)` 自身の1行=10行) + PRC2(2-1/2-2の2行) = 12行であり、ブリーフの事前見積り「12」と一致した。★カーネル変異negative control 3件を実施し、いずれも復元後 `git diff --stat kernel/interrupt.c` 差分ゼロ・`TTSP_RESULT: PASS`（12行）へ戻ることを確認した：(1) `del_isr` の quiesceループ（`while (p_isrcb->running != 0U) { ... }`）をコメントアウト→`Check point 5 passed.` は出るが `Check point 6 passed.` は出ず、`check_assert(long_finished)`（`test_dcre5.c:446`）が失敗、PASSなし（ブリーフの予測どおり）。(2) `ISR_KEY_GT` を `(((pri1) > (pri2)))` へ退化（isrseq第2キーを無効化）→`Check point 3 passed.` は出るが `Check point 4 passed.` は出ず、`check_assert(isr_log_is("2ABC4"))`（`test_dcre5.c:381`）が失敗、PASSなし（ブリーフの予測どおり。Codex指摘#1が現実の欠陥であることの実演）。(3) `del_isr` の `queue_insert_prev(&free_isrcb, ...)`（free-list返却）をコメントアウト→スロット4個（`AID_ISR(4)`）のうち手順2で3個使い切ったまま返却されないため、手順3の2個目の `acre_isr`（'B'）でE_NOID枯渇し `check_assert(erid > ISR_S4)`（`test_dcre5.c:374`）が失敗、PASSなし（ブリーフの予測は手順6での枯渇だったが、実際は手順3で1手順早く枯渇した。free-list返却が効いていないことに起因するassertである点は一致しており、ブリーフの許容規定どおり実測を正とした）。**★レビュー指摘（Important・記録追加）**：negative control 2（isrseqタイブレーク）が検証するのは「同一isrpriの順序がisrseqタイブレークに依存すること」のみであり、Task 5のenqueue_isr単調カウンタ化（リセット撤去）が直した「走査中の完全ドレイン→enqueueで新エントリが取りこぼされない」性質はカバーしていない。本テストのINTNO1キューは静的ISR_S4/ISR_S2が常駐し走査中に完全に空にならない（手順4のハンドシェイクでもenqueue時点でA/C/S4が残存）ため、Task 5以前の旧リセット挙動（キューが空になったときだけisrseqを0へ戻す）でも本テストは全パスしてしまう。現行コードの正しさ自体は`interrupt.c:188-205`のコード検査で確認済みだったが、当該性質（isrseq単調化の完全ドレイン前提）の回帰カバレッジは**hardening パス Task 3 で解消済み**（`test_dcre5` 手順9に追加。INTNO2＝静的ISRゼロの動的専用キューを使い、走査中に完全ドレインさせてから同一isrpriでacreする。旧リセット分岐を戻すと手順9が倒れることを変異controlで実演済み）。**Codex 外部レビュー指摘による保証スコープの明確化（機構は変更なし・意味論は起動開始時点集合）**：`enqueue_isr` コメント（`interrupt.c:165-186`）の「単調isrseqにより同一起動内で取りこぼされない」という記述が，`ISR_KEY_GT`（isrpri第一キーの辞書式比較）の下では`isrpri >= cur`の走査位置にしか成立しない過大主張だったのを訂正した。curより高優先（isrpriが小さい）位置に入る新規ISRは本起動では実行されず次回の割込みから有効になる——これは「起動開始時点で登録済みのISR集合を処理する」意味論（静的インライン連鎖と同一）どおりであり仕様である。同旨のスコープ明確化をspec（`docs/superpowers/specs/2026-08-04-fmp3-dcre-isr-design.md`訂正G追記・§5）・`docs/dynamic-creation.md`（§3.5・§9.1）・`test/test_dcre5.c`（手順3コメント）にも反映した。**hardening パス Task 3**：上記の未整備ギャップ（負のcontrol 2 が isrseq単調化の完全ドレイン前提を検査していない件）を閉じるため、`TASK2`（PRC1・HIGH・TA_NULL）・`drain_isr_a`/`drain_isr_b`（ともに INTNO2・isrpri 1・同一isrpri）・`CMD_FIRE_DRAIN` を追加し、手順9として「INTNO2（静的ISRゼロの動的専用キュー）へ A のみを acre・発火 → A の実行中に PRC1 から del_isr(A) して走査中にキューを完全ドレイン → quiesce のロック解放窓で TASK2 が B を acre_isr（空キューへenqueue）→ A が完了して走査再開 → B が同一起動内で（2度目の ras_int なしに）拾われる」を実測した。TASK2 を A の本体からではなく TASK1 自身が act_tsk し TASK2 は最初に dly_tsk(`DRAIN_DELAY`=10ms) で眠る設計にすることで、「TASK1 が del_isr に入る前に TASK2 が acre してしまう」早すぎる横取りを避けている（万一早すぎてもキューが空にならず変異controlが検出する設計）。★変異control（`enqueue_isr` へ旧リセット分岐を一時的に再導入）で `check_assert(dr_b_fired)` が予測どおり失敗（`TTSP_RESULT: FAIL`、最終checkpointが9で停止）することを実演し、本手順が空虚でないことを確認した。checkpoint数は12→13（PRC1側が1..9→1..10 + `check_finish`が10→11）に変わった。**test_dcre5自立化（ISR_SIO依存の解消）**：downstream（esp32_s3）がテストビルドから`syssvc/serial.cfg`を除外するため`ISR_SIO`未定義でビルドできない問題（docs/qa-esp32s3-20260804-2.md Q-3）を受け、`check_assert(erid > ISR_SIO)`（旧:424）と`check_ercd(del_isr(ISR_SIO), E_OBJ)`（旧:578）を本ファイル内の3本目の静的ISR`ISR_SELF`（新設ハンドラ`static_isr_self`、INTNO3、発火しない）へ差し替え。動的ID>静的IDの2レンジ分割・del_isrの静的拒否という検証内容自体は保たれる（クロスファイル性のみ後退）。musca_b1-2core実測でCheck point行数13のままPASS維持を確認、`tools/cfg_equivalence.sh`もMATCH | - |
| test/test_dcre5.cfg（dcre動的ISR Task 6） | add (dcre-port) | 上記テストのシステムコンフィギュレーションファイル。ブリーフの記述どおり、`CLS_PRC1` に `CRE_TSK(TASK1, …)`・`CFG_INT(INTNO1, …)`・`CRE_ISR(ISR_S4, …)`（isrpri 4）・`CRE_ISR(ISR_S2, …)`（isrpri 2、記述順をisrpri順と食い違わせて呼出し順序を検出可能にする。`test_int2` と同じ流儀）・`ENA_DYNISR(INTNO1)` を、`CLS_PRC2` に `CRE_TSK(TASK3, …)`・`CFG_INT(INTNO2, …)`（静的ISRなし・動的専用）・`ENA_DYNISR(INTNO2)` を置き、クラス外に `AID_ISR(4)`（手順6のE_NOID枯渇がちょうど4個で起きるよう調整済み）を置く。ISRは固定長CB配列でメモリプールを使わないため `DEF_MPK` は無し（`test_dcre2.cfg`/`test_dcre3.cfg` と同型）。**hardening パス Task 3**：`CLS_PRC1` の `CRE_TSK(TASK1, …)` 直後に `CRE_TSK(TASK2, { TA_NULL, 2, task2, HIGH_PRIORITY, STACK_SIZE, NULL })`（休止起動・TASK1より高優先度）を追加。`CLS_PRC2` の `INTNO2` ブロックは無変更（静的ISRゼロ＋ENA_DYNISRのままが手順9の前提）。**test_dcre5自立化**：`CLS_PRC1` の `ENA_DYNISR(INTNO1)` 直後に `CFG_INT(INTNO3, …)`・`CRE_ISR(ISR_SELF, { TA_NULL, 'X', INTNO3, static_isr_self, 6 })` を追加（ENA_DYNISRしない・発火しない静的専用ISR。ISR_SIO依存解消のため。INTNO3は`target/musca_b1_gcc/target_test.h`に新設） | - |
| test/test_dcre5.h（dcre動的ISR Task 6） | add (dcre-port) | 上記テストのヘッダ（優先度定数・`INTNO_UNOPTED`/`INTNO_BAD`・`ISR_LOG_SIZE`/`SPIN_LIMIT`/`LONG_ISR_SPIN`・`CMD_*`・関数プロトタイプ）。ブリーフの記述をそのまま採用（`LONG_ISR_SPIN = 2000000U` で変異control(1)の成立を実測確認したため増量は不要だった）。**hardening パス Task 3**：`CMD_FIRE_DRAIN`（手順9の指令）・`DRAIN_DELAY`（10ms、TASK2の初期dly_tskの長さ。実測でこの値のまま変異controlが倒れることを確認したため増量は不要だった）・`task2`/`drain_isr_a`/`drain_isr_b` のextern宣言を追加。**test_dcre5自立化**：`static_isr_self` の extern 宣言を追加（`static_isr`/`dyn_isr` と並べて配置） | - |
| test/MANIFEST（dcre動的ISR Task 6） | mod (dcre-port) | `test_dcre4.h` の後・`test_dcre_mix.c` の前（アルファベット順、`test_dcre4` < `test_dcre5` < `test_dcre_mix`）に `test_dcre5.c`/`test_dcre5.cfg`/`test_dcre5.h` の3行を追加 | - |
| test/testexec.rb（dcre動的ISR Task 6） | mod (dcre-port) | `"dcre4" => { SRC: "test_dcre4" },` の後・`"dcremix"` の前に `"dcre5" => { SRC: "test_dcre5" },` を追加 | - |
| test/test_dcre_mix_ma.c（ISR最終レビュー Important #3 対応） | add (dcre-port) | マルチaffinityクラス（`CLS_ALL_PRC1`）上の `ENA_DYNISR` 生成検査用サンプル。`test_dcre_mix.c` と同型（build-only、実行時は即 `check_finish(1)`）。QEMU 実行は不要 | - |
| test/test_dcre_mix_ma.cfg（ISR最終レビュー Important #3 対応） | add (dcre-port) | 上記のシステムコンフィギュレーションファイル。`CFG_INT`/`CRE_ISR(MA_ISR1)`/`ENA_DYNISR` を，affinityPrcList が2以上のクラス（`CLS_ALL_PRC1`）の囲みの中に置く。**kria_r5-2core専用**（他4ターゲットは NGKI5184／per-prcid intno 符号化により E_RSATR になることを実測確認済み。詳細は上記「既知・対処しない事項」の 2026-08-04 エントリ参照）。`AID_ISR(1)` は `MA_ISR1` 自身で訂正E ガードを満たす | - |
| test/test_dcre_mix_ma.h（ISR最終レビュー Important #3 対応） | add (dcre-port) | 上記のヘッダ（`task1`/`ma_isr1` のプロトタイプ）。`test_dcre_mix.h` と同型 | - |
| test/MANIFEST（ISR最終レビュー Important #3 対応） | mod (dcre-port) | `test_dcre_mix.h` の後・`test_dlynse.c` の前（アルファベット順、`.` (0x2E) < `_` (0x5F) のため `test_dcre_mix.h` < `test_dcre_mix_ma.c`）に `test_dcre_mix_ma.c`/`test_dcre_mix_ma.cfg`/`test_dcre_mix_ma.h` の3行を追加 | - |
| test/testexec.rb（ISR最終レビュー Important #3 対応） | mod (dcre-port) | `"dcremix" => { SRC: "test_dcre_mix" },` の後・`"dlynse"` の前に `"dcremixma" => { SRC: "test_dcre_mix_ma" },` を追加 | - |
| kernel/startup.c（hardening） | mod (dcre-port) | `align_pointer`（旧 `:470-474`）を廃止し、`malloc_mempool`/`aligned_alloc_mempool` を共通の `alloc_mempool`（`uintptr_t` 算術）へ再構成した。**dcre 原形の `((char *) limit) - ((char *) brk) >= size` は `ptrdiff_t` と `size_t` の混在比較**で、アラインメント調整が `limit` を越えた場合に負のポインタ差が巨大な符号なし値へ変換され、プール外の番地を成功として返す（**上流報告候補 c の現物**）。あわせて `align_pointer` の加算あふれも塞いだ。★これは dcre からの**意図的な逸脱**であり、上流の欠陥の修正である。呼出し側（`acre_*`）のサイズ検査と**一体で**入れている（片方だけでは穴が残る — 段階3b 最終レビューの裁定）。dcre（`extension/dcre/kernel/startup.c:238-278`）と現物比較のうえ同一の欠陥を確認済み（本ブランチ固有の劣化ではない） | **要**（候補 c を「観察」から「修正済み・要報告」へ格上げ） |
| kernel/dataqueue.c（hardening） | mod (dcre-port) | `acre_dtq` に `CHECK_PAR(dtqcnt <= (SIZE_MAX / sizeof(DTQMB)));` を追加（H-1）。★dcre（`extension/dcre/kernel/dataqueue.c`）にこの検査は無い。`dtqcnt`（`uint_t`）と `sizeof(DTQMB)`（`size_t`）の積が32bitターゲットであふれると、`dtqcnt` 個の `DTQMB` が入らない領域の確保に成功してしまい `enqueue_data` がプール外を破壊する。64bitターゲットでは積があふれないため検査は恒真（無害） | - |
| kernel/pridataq.c（hardening） | mod (dcre-port) | `acre_pdq` に `CHECK_PAR(pdqcnt <= (SIZE_MAX / sizeof(PDQMB)));` を追加（H-2）。★dcre にこの検査は無い。根拠は `acre_dtq` の同じ検査と同一（`pdqcnt`×`sizeof(PDQMB)` の32bitあふれ→`enqueue_pridata` のプール外破壊） | - |
| kernel/mempfix.c（hardening） | mod (dcre-port) | `acre_mpf` に3本の `CHECK_PAR` を追加（H-3）：(a) `blksz <= SIZE_MAX - (sizeof(MPF_T) - 1)`（`ROUND_MPF_T(blksz)` の丸め加算あふれ防止）、(b) `blkcnt <= SIZE_MAX / ROUND_MPF_T(blksz)`（ブロック領域確保の積あふれ）、(c) `blkcnt <= SIZE_MAX / sizeof(MPFMB)`（管理領域確保の積あふれ）。★dcre（`extension/dcre/kernel/mempfix.c`）にこれらの検査は無い。(a)を(b)より先に置くのは、(b)の除数が0にならないことを保証するため（`blksz != 0` は直前で検査済み） | - |
| kernel/task_manage.c（hardening） | mod (dcre-port) | `acre_tsk` に `CHECK_PAR(stksz <= (SIZE_MAX - (sizeof(STK_T) - 1)));` を追加（H-4）。★dcre（`extension/dcre/kernel/task_manage.c`）にこの検査は無い。`acre_tsk` には乗算は無いが `ROUND_STK_T(stksz)` の丸め加算は32bit/64bitどちらでもあふれうる（`stksz` は `size_t`）。あふれると丸め結果が0付近へ落ち、`aligned_alloc_mpk` が「成功」してゼロ長スタックのタスクができる | - |
| kernel/startup.c（mempool freelist 化） | mod (dcre-port, 機能拡張) | 既定メモリプール実装（`OMIT_MEMPOOL_DEFAULT` ブロック）を「バンプ＋完全一致サイズ再利用」へ置換。各割付けの直前に `MPHDR`（`size_t` 1個＝要求サイズ無加工）を置き、解放されたユーザ領域を `FREEBLK`（単方向 LIFO）へ転用。alloc は freelist を「サイズ完全一致＋ポインタ実アライン適合」で先頭から走査し、無ければ従来の bump（`brk + sizeof(MPHDR)` のあふれ検査をガード列に追加）。free は O(1) push、`count==0` の全域リセット（brk 先頭戻し）は backstop として維持し freelist も同時にクリア。`MEMPOOLCB` は 12B→16B（32bit）、割付け1件あたり +4B/+8B のヘッダ。外部シグネチャ4関数・`kernel_rename.def`・`allfunc.h`・`OMIT_MEMPOOL_DEFAULT` フックは不変。dcre の原形は「全解放まで再利用しない」単純バンプで、長寿命割付が1つでも残ると create/delete 反復でアリーナが単調に痩せて E_NOMEM に到達する（esp32_s3 実機で同一タスクIDが1ブート中6回再利用される実測パターンが動機）。これを完全一致再利用で除去する（ユーザ裁定2件：既定実装を置換／first-fit・分割・サイズクラス化は不採用。`docs/superpowers/specs/2026-08-04-fmp3-mempool-freelist-design.md`）。★64bit ターゲットでは `sizeof(MPFMB)*1 = 4B < sizeof(FREEBLK) 8B` の割付けで FREEBLK 書込みがユーザ領域末尾を越えるが、はみ出し先は次ヘッダ直前の死領域に必ず収まる（`sizeof(FREEBLK) == sizeof(MPHDR)` による。spec §3 の「最小要求はポインタ幅」という記述はこの mpfmb 経路を見落としており、計画時所見2として訂正・論証をコード内コメントに記録）。★レビュー訂正（2026-08-04）：上記 FREEBLK はみ出しコメントの当初の下界「user + size + sizeof(MPHDR)」は反例（64bit・size=4・alignment=8 で実際は user + sizeof(MPHDR) に到達）により誤りと判明し、「header 番地は常に sizeof(MPHDR) の倍数」という帰納的性質からの正しい下界 user + sizeof(MPHDR) へコード内コメントを訂正した（結論の非オーバーラップ自体は変わらないが、タイトな下界の論証が誤っていたため式を訂正）。★既知の潜在的制約（意図的に未ガード）：同一番地の二重 free_mempool は自己循環（`next` が自身を指す）を生み、以後の alloc 走査でサイズ一致要求への多重払い出し（エイリアシング）またはサイズ不一致要求での無限ループ（ロックアップ）を起こす——旧 bump 実装の二重解放（count 帳簿の狂いのみ）より質的に悪い失敗モード。現行呼出し側（mempfix.c/dataqueue.c/pridataq.c/task_manage.c）は全数確認済みで二重解放しない（潜在＝将来の呼出し側追加時のリスク）。O(n) メンバーシップ走査は無駄なコスト・先頭のみの簡易検査は偽の安心のため、ランタイム検査は入れない裁定（`free_mempool` のコード内コメント参照）。★最終レビューで発見・修正（コード変更あり）：FREEBLK はみ出し論証は「次の割付けのヘッダが存在する」ことを前提にしており、プール終端（`limit`）の扱いが未検証だった。ユーザ供給 `mpk`（`CHECK_MB_ALIGN`=4 にしか丸められない）は64bitターゲットで `mpksz ≡ 4 (mod 8)` になりえ、`limit` ちょうどに着地する4B割付け（`sizeof(MPFMB)*1`）の解放時 FREEBLK 書込み（8B）が `limit` を4B越えてプール外を破壊しうる。`initialize_mempool` で `limit` を `sizeof(MPHDR)` の倍数へ切り下げる構造的な修正で解消（自動生成プール・既存 `MPK_SIZE` はすでに整列済みのため在来挙動は不変。spec §3【裁定 2026-08-04・最終レビュー】参照） | - （不具合修正ではなく拡張のため上流報告しない） |
| test/test_dcre6.c（mempool freelist 化） | add (dcre-port) | カーネルメモリプールの完全一致フリーリスト再利用の QEMU 回帰テスト本体（musca_b1-2core 専用・32bit 机上算術前提を `check_assert(sizeof(size_t) == 4U)` で固定）。(A) 長寿命 dtq（4B）保持下で同一サイズ acre_mpf/del_mpf × 16 周が E_NOMEM にならない（旧 bump 実装では 8 周目で必然的に E_NOMEM＝変異 control で再現する positive control）、(B) del/再 acre をまたいだ pget_mpf ブロック番地の一致（完全一致再利用の直接観測）、(C) サイズ不一致（8B 要求 vs freelist{256B,16B}）が bump を前進させることを「残量ちょうど 1728B の fill 成功」と「直後 4B 要求の E_NOMEM」の挟み撃ちで観測、(D) 全解放後の 2000B 割付成功による count==0 backstop（brk リセット＋freelist クリア）の実証、の4フェーズ・checkpoint 6。変異 control（`alloc_mempool` の freelist 一致判定への `0 &&` 挿入）で 8 周目の `check_assert(erid > MPF1)` が予測どおり倒れることを実演し、復元後 `git diff --stat kernel/` が空であることを確認 | - |
| test/test_dcre6.cfg（mempool freelist 化） | add (dcre-port) | 上記テストのシステムコンフィギュレーションファイル。CLS_PRC1 に TASK1 と静的 DTQ1/MPF1（AID_* の訂正E ガード要件。cfg 静的確保のためプール不使用）、クラス外に AID_DTQ(4)（フェーズCで dtq 3本同時生存＋境界検査の第4スロットが必要——3以下だと境界が E_NOMEM でなく E_NOID になる）・AID_MPF(1)・DEF_MPK({ MPK_SIZE, NULL }) | - |
| test/test_dcre6.h（mempool freelist 化） | add (dcre-port) | 上記テストのヘッダ。MPK_SIZE=2048・MPF_BLKCNT=4/MPF_BLKSZ=64/MPF_CYCLES=16・LONG/PROBE/FILL/BOUND/BIG_DTQCNT=1/2/432/1/500 と、全数値の机上算術（実効容量 2032・1周 280B・旧実装 8 周目 E_NOMEM・fill 1732B ちょうど・リセット後 2004B）のコメント | - |
| test/MANIFEST（mempool freelist 化） | mod (dcre-port) | `test_dcre5.h` の後・`test_dcre_mix.c` の前（アルファベット順）に `test_dcre6.c`/`test_dcre6.cfg`/`test_dcre6.h` の3行を追加 | - |
| test/testexec.rb（mempool freelist 化） | mod (dcre-port) | `"dcre5"` の後・`"dcremix"` の前に `"dcre6" => { SRC: "test_dcre6" },` を追加 | - |

**★恒常出力の受容（ISR段階 Task 2、訂正D）**：ISR にはランタイムオブジェクトが
無かったため、`AID_ISR` を `kernel_api.def` に登録した時点で（`cfgData` が登録済み
API のキーを常に持つ＝`cfg_py/pass2.py:181-186`）、静的 `CRE_ISR` を1個以上持つ全構成に
ISRINIB 表・ISRCB 実体・`p_isrcb_table`・`isrorder_table`・`tmax_isrid`/`tmax_sisrid`/
`TNUM_SISRID`・`initialize_isr` の起動時呼出しが恒常的に加わる（`build/musca_b1-2core`
の `kernel_cfg.c` 管理された差分、Task 2 Step 10 の12項目の許容リストと完全一致することを
実測確認済み）。**ディスパッチ経路（`_kernel_inthdr_*` 本体）はバイト不変**であり
（`test_int2` 構成、Task 2 Step 11）、加わるのはデータと起動時の O(N) ループ1本だけである。
「前」のベースラインは Task 2 Step 1 で実測済み：`build/musca_b1-2core/fmp`
（変更前・HEAD f1f1d53）＝ text=58960 / data=32 / bss=24748 バイト（`size` コマンド実測）。

**★ROM増分の「後」実測（Task 3 Step 9、訂正D完了）**：Task 3 完了後の
`build/musca_b1-2core/fmp` ＝ text=59452 / data=32 / bss=24820 バイト。
増分は **text +492 / data +0 / bss +72** バイト。この構成の `_kernel_isrinib_table`
（`build/musca_b1-2core/generated/kernel_cfg.c`）は静的ISR3個（`ISR_SIO`・
`INTNO1_ISR`・`INTNO2_ISR`）、動的スロット0個（`AID_ISR` 未使用、`TNUM_ISRID
== TNUM_SISRID`）、`tnum_isr_queue == 0`（`ENA_DYNISR` 未使用構成のため
`isr_queue_table`/`isr_queue_list` は空）。見積りとの整合：bss +72 ≒
`ISRCB`×3（`sizeof(ISRCB)`＝QUEUE(8)+ポインタ(4)+isrseq(4)+running(4)＝20バイト、
32bit環境）＝60バイト＋`QUEUE free_isrcb`（8バイト）＝68バイトに整列パディング数バイトを
加えた値で桁は一致。text +492 バイトは `isrinib_table`（`sizeof(ISRINIB)`×3＝
20×3＝60）＋`isrorder_table`（4×3＝12）＋`p_isrcb_table`（4×3＝12）に加え、
`initialize_isr`/`enqueue_isr`/`search_isr_queue`/`call_isr`（暫定版）の
コード本体（残り約400バイト）で説明でき、桁の誤りは無い。

**★ROM増分の最終確認（ISR段階 Task 7、HEAD 99d11f5）**：Task 3 の記録は「暫定版
`call_isr`」時点のスナップショットであり、その後 Task 4（`call_isr` の MP対応版への
全面書き直し）・Task 5（`acre_isr`/`del_isr` の追加、quiesce 新設）で恒常リンクされる
コードが増えたため、Task 3 の増分だけでは HEAD の実際の footprint を表さない。
Task 7 時点で `size build/musca_b1-2core/fmp` を再実測した：
text=60756 / data=32 / bss=24820。Task 3 からの追加増分は **text +1304 / data +0 /
bss +0** バイト（bss 不変＝`ISRCB`/`free_isrcb` 等のデータ構造は Task 3 で確定済みで
Task 4/5 は関数本体のみを追加したため、という理解と整合）。ベースライン
（変更前 f1f1d53）からの総増分は **text +1796 / data +0 / bss +72** バイト。
本受容（訂正D）はこの最終値に対して有効であることをここで確定する。

種別: add=追加 / patch=部分改変 / replace=置換 / remove=削除 / none=無改変（差分ゼロだが，
運用上の注意が必要なため記録目的で本表に載せている。現状 `cfg/` のみ）

## 既知・対処しない事項

- **mpf ブロック領域の受容済みウィンドウ**（段階3b最終レビューで特定・
  `docs/superpowers/specs/2026-08-04-fmp3-dcre-stage3b-dtq-pdq-mpf-design.md` §1/§5 訂正済み）。
  `get_mpf` が返す生ポインタはアプリがロック外で使用し、`del_mpf` は貸出中でも
  ブロック領域ごと解放する（dcre 忠実・アプリ契約）ため、E_NOEXS ゲートは
  stale ポインタへの参照を防げない。段階1 §2.3 の自終了スタック窓と同類の
  受容済みウィンドウとして受容（管理領域3種＝p_dtqmb/p_pdqmb/p_mpfmb は
  ウィンドウ・フリーが成立し，この点と区別する）。対処はスコープ外。

- **`ld: ... has a LOAD segment with RWX permissions` 警告**（`cfg1_out` / `fmp` リンク時）。
  pristine の Microchip リンカスクリプト（`target/polarfire_soc_kit_gcc/sdk/.../mpfs-lim.ld` が
  全領域を rwx 宣言）に起因する無害な警告であり，**意図的に対処しない**（分離しようとする方が
  pristine への不要な改変を生み危険）。`cfg1_out` は実行しないため実害は無く，`fmp` も QEMU 実機で
  正常起動を確認済み。

- **`FMP3_CHECK_TRB_FILES`（pass3・`fmp3_cfg_check()` が使う `target_check.py`/`core_check.py`/
  `kernel_check.py` の連鎖）は `cmake/trb_depends.cmake` の `fmp3_trb_closure()` の対象外**
  （`fmp3_trb_closure()` は `FMP3_OFFSET_TRB_FILES`／`FMP3_KERNEL_CFG_TRB_FILES` にしか
  呼ばれていない。`CMakeLists.txt:400-403`）。したがって `core_check.py`／`kernel_check.py`
  を編集しても pass3 の POST_BUILD が無警告に再実行されない可能性がある
  （計画A2 Task 5 で kernel_cfg/offset 側に見つかった同型の Critical と同じ性質の隙間だが、
  pass3 側は未修正のまま残っている）。実害は「pass3 が古いチェックロジックのまま実行され続ける」
  ことであり、ビルド失敗はしない（気づきにくい）。**対処は本計画のスコープ外**（cfg パイプラインの
  依存追跡強化は計画A/A2/Bのいずれの目的でもない）。将来 `core_check.py`／`kernel_check.py` を
  編集する際は、`ninja -C <builddir> -t clean && ninja -C <builddir>` でクリーンビルドして
  変更を確実に反映させること。

- **`cfg_py/cfg.py` の `pass3()` が持つ非対称性: Ruby の `$timeStampFileName` グローバルに相当する
  Python 側の状態が pass3 の名前空間フィルタに阻まれ、`check.timestamp` が書かれない。**
  `kernel/kernel_check.py:57` は `timeStampFileName = "check.timestamp"` をtrb実行時の名前空間
  （`ns`）に設定するが、`cfg_py/cfg.py` の `pass3()` は `ns` から `g` へのマージを
  `g.update({k: v for k, v in ns.items() if k in g.get("globalVars", [])})`（`cfg_py/cfg.py:545-546`）
  という**明示的な allowlist（`globalVars`）越しにしか行わない**のに対し、`kernel_check.py` は
  `timeStampFileName` を `globalVars` に登録しない。したがって `main()` の
  `if g.get("timeStampFileName"): open(...)`（`cfg_py/cfg.py:782-783`）は常に偽のまま終わり、
  Python 経路では `check.timestamp` が一度も書かれない（Ruby の `cfg/cfg.rb:749-750` は
  `$timeStampFileName` が真のグローバル変数のため、この非対称性を持たない）。
  **製品ビルドへの影響を確認した**：`CMakeLists.txt` の `fmp3_cfg_check()`（pass3 を呼ぶ
  `add_custom_command(TARGET fmp POST_BUILD ...)`）は `check.timestamp` を `OUTPUT` として
  宣言しておらず、CMake のビルドグラフはこのファイルの有無を一切参照しない。実際に
  `build/{musca_b1,musca_b1-2core,polarfire_soc_kit-qemu,*-libonly}/generated/` を
  全数 `find` したが `check.timestamp` はどこにも存在せず（`kernel_cfg.timestamp`／
  `offset.timestamp`／`cfg1_out.timestamp` という別物の CMake 自身の追跡ファイルとは別）、
  一方で `fmp.syms`／`fmp.srec`（pass3 実行の副産物）は全構成で生成されていた＝pass3 自体は
  実行されているが `check.timestamp` だけが欠落している状態を実測で確認した。
  **結論：製品ビルドの成否・正しさには影響しない**（誰も読んでいない孤立した副産物が欠けているだけ）。
  Task 6 で発見時点では影響未確認だったが、Task 13 で影響なしと確定した。念のため
  pristine（`kernel/kernel_check.trb` 由来の翻訳）は未修正のまま残す（挙動を変えると
  差分等価性検査の前提が変わるため、修正するなら計画外で別途検討）。

- **`target/musca_b1_gcc/target_class.py`（`target_class.trb` 翻訳）の `clsid` は、
  `tools/cfg_equivalence.sh` の差分等価性検査では検証できない構造的な盲点がある**
  （計画B Task 8・9 で発見）。`clsid` は musca_b1（ARM-M・NVIC）では警告文言にしか現れず
  生成物（`kernel_cfg.c`／`kernel_cfg.h`）のバイト列には出力されない
  （`SecnameKernelData` が空文字列を返すため、セクション名にも漏れない）。
  さらに1コア構成では両クラスが `initPrc=1`／`affinityPrcBitmap=1` に縮退し差の出る
  フィールドが無くなるため、2コア構成でも「全部一致」の意味を過大評価しないこと。
  **polarfire（RISC-V・PLIC）は対象外**：`clsid` が `kernel_cfg.c` のセクション名
  （`.kernel_data_CLS_PRC1` 等）に漏れるため、既存のバイト比較で実質的に覆われている。
  `clsid` の意味的な正しさは `tools/cfg_error_tests/`（警告メッセージ経由の文言比較）で
  部分的に補っているが、`cfg_equivalence.sh` 単体の「一致」は `clsid` については無検証である
  ことを踏まえて読むこと。

- **（2026-07-19 外部レビュー指摘）コンパイルオプションの結合順が上流と逆。** 現物を確認した：
  上流 `sample/Makefile:188` は `COPTS := -O2 $(COPTS)`（`-O2` を**先頭に**追加し、
  target 側が `Makefile.target` 等で後から積む `COPTS` はその**後ろ**に来る＝GCCの
  「最後に指定した `-O` が勝つ」規則により target 側が `-O2` を上書き可能）。一方
  `CMakeLists.txt` は `include(${FMP3_TARGET_DIR}/target.cmake)`（`:81`）で
  `FMP3_COMPILE_OPTIONS` に target 固有の最適化オプションが積まれた**後**に
  `list(APPEND FMP3_COMPILE_OPTIONS -g -Wall -O2)`（`:137`）を実行しており、
  `-O2` が target.cmake の指定より**後**＝上書きする側になる。上流と逆順。
  現5ターゲットの `target.cmake` はいずれも独自の `-O` を指定しない（`FMP3_COMPILE_OPTIONS`
  に `-Os`/`-O0` 等を積んでいない）ため実害は無いが、将来 target.cmake 側で
  `-Os`/`-O0` 等に上書きしたいターゲットを追加すると、上流と違って上書きできない
  （常に `-O2` に戻される）ことになる。対処は本レビュー対応のスコープ外と判断し記録に留める。

- **（2026-08-04 dcre段階3b Task 7 追記／2026-08-04 hardening Task 1 で解消済み）
  `acre_dtq`/`acre_pdq`/`acre_mpf` は乗算の桁あふれを検査しない（dcre 由来・未 hardening）。**
  `acre_dtq`/`acre_pdq` は管理領域サイズ
  `sizeof(DTQMB) * dtqcnt` / `sizeof(PDQMB) * pdqcnt` を、`acre_mpf` はブロック領域サイズ
  `ROUND_MPF_T(blksz) * blkcnt` を、いずれも `dtqcnt`/`pdqcnt`/`blkcnt`/`blksz` の上限検査を
  行わずに `malloc_mpk` へ渡す（`kernel/dataqueue.c`・`kernel/pridataq.c`・`kernel/mempfix.c`、
  段階3b Task 3/4/5）。`size_t` で桁あふれすると `malloc_mpk` に想定外に小さい値が渡り、
  本来収まらないはずの確保が**成功してしまう**（その後 `enqueue_data` 等が範囲外を書く）。
  `kernel/startup.c` の `malloc_mempool` が持つ符号混在比較（上流報告候補 c。
  `((char *)limit - (char *)brk) >= size` が `ptrdiff_t >= size_t`）と組み合わさると、
  ユーザが巨大な `dtqcnt`/`pdqcnt`/`blkcnt` を渡す誤用経路で悪化しうる。段階1 deferred #1
  （`mact_tsk`/`mig_tsk` のロック前 `p_tinib` 読み）・段階3a `acre_mtx` の未検査 `ceilpri` と
  同系統の「ユーザ誤用経路の hardening」課題として引き継ぐ。**段階3b では到達可能性の実証も
  修正も行っていない。**
  ★**hardening パス Task 1（2026-08-04）でこのオーバーフロー未検査（H-1〜H-3、`acre_dtq`/
  `acre_pdq`/`acre_mpf`）と、上で述べた `malloc_mempool` の符号混在比較（H-5）を**一体で**
  修正した。**片方だけ直すと穴が残る**（サイズ検査だけならアラインメント調整で `brk` が
  `limit` を越えたときの符号なし化誤判定が残り、符号比較だけならオーバーフローで小さくなった
  `size` が正当に「入る」と判定されて成功してしまう）ため、同一コミットで両方を直した。
  あわせて `acre_tsk` の `ROUND_STK_T(stksz)` 丸めあふれ（H-4。乗算ではなく丸め加算だが同系統の
  危険）にも検査を追加した。詳細は本表末尾の H-1〜H-5 の行を参照。この事項は解消済みであり、
  引き継ぐ「未 hardening」課題ではなくなった

- **（2026-08-04 ISR段階 Task 7 追記）`kernel/interrupt.trb:338-346`（Python 側は
  `interrupt.py` の対応行）の `isrorder_table` 生成には、他の表（`isrinib_table`／
  `p_isrcb_table` 等）と異なり `TNUM_SISRID == 0` のガードが**必要だった**という
  事実そのものは新しい観察ではなく（Task 2 で `TOPPERS_EMPTY_LABEL` ガードを実装済み、
  本表 `kernel/interrupt.trb（ISR段階 Task 2）` 行の逸脱③を参照）、ここで記録するのは
  **dcre 自身の該当箇所（`extension/dcre/kernel/interrupt.trb:338-346` 相当）にこの
  ガードが無い**という観察である。dcre のまま静的 ISR が 0 個の構成（`AID_ISR` のみで
  `CRE_ISR` が無い等）に適用すると `const ID _kernel_isrorder_table[TNUM_SISRID] = { };`
  ＝ゼロ長配列＋空初期化子（GNU 拡張）を出力する。GCC では警告なくコンパイルが通るため
  **現行バグではなく**、他のツールチェーンへ dcre を移植する際の可搬性の懸念に留まる。
  したがって上流報告候補には加えない（候補は 4 件のまま）。FMP3 側は既に
  `TOPPERS_EMPTY_LABEL` でガード済みのため実害はない。

- **（2026-08-04 ISR段階 Task 7 追記）`acre_isr` は `intno` の範囲検査を持たない
  （訂正A・本表 `kernel/interrupt.c（dcre動的ISR Task 3）` 行の逸脱(3)を参照）。**
  検査は cfg が生成するグローバル適格 intno 表の二分探索（`search_isr_queue` 相当）
  のみで行われ、これは**十分である**と判断している（表は cfg が生成する閉じた集合であり、
  範囲外の値が表に載ることはないため、実行時の追加検査は論理的に不要）。ただし
  **dcre は範囲外 intno を `E_PAR` で報告するのに対し、FMP3 は `E_OBJ`
  （範囲外・CFG_INT 無し・未 ENA_DYNISR・DEF_INH 競合を区別しない一括の ercd）で
  報告する**という ercd の非互換がある。FMP3 では `(prcid, intno)` の2引数を取る
  `VALID_INTNO` がコアに依存しうるため、dcre のようにコア非依存の `E_PAR` 検査を
  維持する手段が存在しない（Codex 指摘 #3 への対応として意図的に削除した）。
  dcre 向けに書かれたアプリケーションを FMP3 へ移植する際、`acre_isr` の異常系で
  ercd の差（`E_PAR` ではなく `E_OBJ` が返る）が観測されうることを非互換として明記する。
  未 hardening ではなく設計上の意図的帰結であり、対処はスコープ外。

- **（2026-08-04 ISR段階 Task 7・上流報告候補の最終確認）上流報告候補は 4 件
  （a=段階1、b=解消済み・未報告、c=`malloc_mempool` の符号混在比較（**本ブランチでは
  hardening パス Task 1（2026-08-04）で修正済み。上流へは未報告**）、d=`del_flg`/
  `del_dtq` の `CHECK_PAR`→`CHECK_ID` 不整合2件）のまま、ISR 段階では増えていない。**
  ISR 段階で `del_isr` が候補 d と同型（`CHECK_PAR` の使用）になっていないかを確認した
  結果、`del_isr` は dcre 自身が `CHECK_ID`（E_ID）であり（本表 `kernel/interrupt.c
  （dcre動的ISR Task 3）` 行に記録済み）、候補 d が求める「dcre 自身の `CHECK_PAR`/
  `CHECK_ID` 不整合」には該当しない。**3件目を探して見つからなかった、という事実**
  （見つけて拡張しなかったのではない）として記録する。

- **（2026-08-04 hardening パス Task 5・台帳自己整合の訂正）上の「上流報告候補は 4 件」
  （本行群 16:16 コミット 5a8ddb4）は，同日 16:44 コミット 5fa1285（本表下方
  「kria_r5 だけがこの検査を持たない」の段落）で見つかった** `arch/arm_gcc/zynqmp_r5/
  chip_kernel.trb` の `IncludeTrb("gic_kernel.trb")` 欠落（pristine 自身の R5 ポートが
  最初から持つ非対称。詳細は下記該当段落）**を候補として数えないまま据え置かれていた。
  この欠落は「pristine 側の既存ギャップ」であり性質上 a〜d と同種の上流報告候補であるため，
  **上流報告候補は正しくは 5 件（a=段階1、b=解消済み・未報告、c=`malloc_mempool` の
  符号混在比較＝hardening パス Task 1（2026-08-04）で修正済み・上流へは未報告、
  d=`del_flg`/`del_dtq` の `CHECK_PAR`→`CHECK_ID` 不整合2件、e=kria_r5 の
  `TargetCheckCfgInt` 欠落＝`chip_kernel.trb` の `IncludeTrb` 非対称）** に訂正する。
  hardening パス自体はこの件数を増やしていない（e は ISR 段階内の既存発見の数え忘れであり，
  本パスが新規に見つけたものではない）。候補 c だけが本パスで「観察」から
  「修正済み・要報告」へ状態が変わった。

## 解消済み事項

- **（2026-08-13 解消）`target/m5stamp_esp32p4_gcc` を取り込まない、という 2026-07-19 の決定。**
  旧記述（台帳の該当行にあったもの）はこうだった:
  「ESP32-P4 のターゲット依存部は本リポジトリでは管理しない。`fmp3_esp_idf`（別リポジトリ）が
  chip 依存部・ターゲット依存部の両方を管理する方針にユーザが決定したため（2026-07-19）。
  `arch/riscv_gcc/esp32p4`（chip 依存部）は上流追従の差分を見られる利点を保つため
  pristine のまま残す＝除外対象外。」
  ESP32-P4 統合 Phase 0 の着手にあたり、この方針を撤回して取り込んだ。理由は、
  chip 層（`arch/riscv_gcc/esp32p4`）だけを追従して target 層を別リポジトリに置くと、
  上流が両者を同時に変えたときの整合を機械で確かめられないため。取り込みは
  `tools/upstream_targets.txt` に 1 行足して `import_upstream.sh` を再実行し
  `git merge upstream`（AGENTS.md §2 規則6 の手順そのまま。衝突ゼロ、66 ファイル add のみ）。
  取り込んだ内容は fmp3_archive `882bbe42`（= `polarfire_soc_kit_gcc-20260729`、main の
  現 pin と同一）であり、TOPPERS svn `https://dev.toppers.jp/svn/fmp3/branches/3.4` の
  r593 作業コピーと `diff -rq` でバイト一致することを実測した（svn 側にのみ
  `E_PACKAGE`＝リリースパッケージ生成用の制御ファイルがあり、これは公開パッケージには
  入らないので取り込まない）。
  **cfg_py 層は同日に追従済み**（下記の add 行 4 件）。**未了**: CMake 層
  （`target/m5stamp_esp32p4_gcc/{target.cmake,presets.json}` と
  `arch/riscv_gcc/esp32p4/chip.cmake`）は未作成＝`FMP3_TARGET=m5stamp_esp32p4_gcc` は
  configure できない。なお m5stamp の実機成果物は FMP3 単体の ELF ではなく
  `libfmp3.a` を ESP-IDF アプリ（`tools/fmp_loader`）へ静的リンクしたもの
  （`tools/fmp_loader/build_fmp3_lib.sh`）なので、汎用 CMake 層（`fmp` 実行ファイルを
  リンクする形）へどう載せるかは設計判断が要る。これは Phase 1 の作業。

- **（2026-07-19 解消）`cfg_py/cfg.py`（計画Aの中身）が pristine の `cfg/cfg.rb` へ委譲する薄いシムであった件。**
  計画B（`docs/superpowers/plans/2026-07-19-fmp3-cmake-b-python-cfg.md`）で asp3_core 1.7.1 の
  本物の Python cfg エンジンへ差し替えた。`.trb`（Ruby）テンプレート30ファイル・3963行を
  `.py`（Python）へ全数移植し、pristine の Ruby cfg（VERSION 1.7.1、オラクルと版が一致）との
  差分等価性検査（`tools/cfg_equivalence.sh`）で `cfg1_out.c`／`offset.h`／`kernel_cfg.c`／
  `kernel_cfg.h` の一致を polarfire・musca_b1（1コア・2コア）の全構成で確認済み。
  製品ビルドの `CFG_SCRIPT_DEPS` は `cfg_py/*.py` の5ファイルのみを指し、
  pristine の `cfg/*.rb` は CMake のビルドグラフに一切含まれない
  （AGENTS.md §2 規則3を文言・精神の両方で満たす）。Ruby は `tools/cfg_equivalence.sh`
  （CMake外）からのみ呼ばれるオラクルとして残る。以下の記述は、**再調査を避けるための記録として
  残す**（削除しない。計画A2 の HardFault 解消記録と同じ扱い）。

  <details>
  <summary>解消前の記録（期限付きの逸脱としての原文）</summary>

  `cfg_py/cfg.py`（計画Aの中身。Task 3）：AGENTS.md §2 規則3「pristine の `cfg/` は使わない。
  cfg 相当は `cfg_py/`（Python）で提供し、CMake から呼ぶ」の**文言は満たす**（CMake が呼ぶのは
  常に `cfg_py/cfg.py`）が、**精神には抵触する**（`cfg_py/cfg.py` は pristine の `cfg/cfg.rb` へ
  委譲する薄いシムであり、実行されるのは結局 Ruby 版である）。CMake パイプラインの正しさを、
  テンプレート Python 移植のバグと切り離して検証するための足場。
  解消条件：計画B（`cfg_py/` への asp3_core 1.7.1 エンジン移植とテンプレート移植）の完了時に、
  シムを本物のエンジンへ差し替える。以降 Ruby は `tools/cfg_equivalence.sh`（CMake 外）からのみ
  呼ぶオラクルとして残す。この行が残っている間は AGENTS.md §6 の完了条件を満たさない。

  </details>

- **（2026-07-19 解消）`target/musca_b1_gcc/target_timer.c` の HRT 状態がプロセッサ別でなく、
  2コア SMP が起動直後に HardFault で停止していた件。** 下記「未解決事項」に記録していた
  問題は、上流 20260719（`rp2350_pico2_gcc-20260719`、pin `b59797f14dedcb07020f96895903ca7fcd14a4af`）
  で**上流自身により修正済み**。我々が 2026-07-19 付で報告した上流報告書
  （`docs/upstream-reports/2026-07-19-musca_b1-2core-hardfault.md`）の指摘と一致する修正。
  `hrt_base`/`hrt_reload`/`hrt_last`/`hrt_fresh` が `static volatile` の単一変数から
  `[TNUM_PRCID]` の配列（`target_timer.c:58,63,68,75`）に変わり、`get_my_prcidx()` で
  プロセッサ別に添字アクセスするよう全アクセス箇所（9箇所）が改修された。
  `target_timer.h` の「単一プロセッサ前提」の記述も削除された。
  QEMU（`musca-b1`）で2コアビルドを再検証し、`Processor 1 start.` / `Processor 2 start.`
  の2行が出て HardFault なく走行を継続することを確認済み（回帰確認ログ参照）。
  以下の記述は、**再調査を避けるための記録として残す**（削除しない）。

  <details>
  <summary>解消前の記録（未解決事項としての原文）</summary>

  - **`target/musca_b1_gcc/target_timer.c` の HRT 状態がプロセッサ別でない疑いがあり、
    2コア SMP が起動直後に HardFault で停止する。** `target_timer.c` は 2コアSMP対応の
    `musca_b1_gcc` 依存部の一部だが、HRT の内部状態 `hrt_base`（:47）・`hrt_reload`（:52）は
    `static volatile` のグローバル変数として**1組しか**持たない（プロセッサ番号でインデックス
    された配列等になっていない）。一方 `target/musca_b1_gcc/target_kernel.h:59` は2コア時
    `TOPPERS_TEPP_PRC = 0x3`（PRC1・PRC2 の両方が時間イベント処理プロセッサ）と定義し、
    `target/musca_b1_gcc/target_user.txt` は「各コア内蔵の SysTick を…使用する
    （`target_timer.c`）」と書いている＝設計意図は per-core SysTick／per-core HRT 状態のはず。
    新旧 QEMU（11.0.1 / 8.2.2）の両方で、同一箇所・同一 PC のクラッシュを再現しており
    QEMU の版依存ではない（詳細: `.superpowers/sdd/a2-task-6-report.md`）。
    **ただし** `target_user.txt:13` は「この2コア構成を用いて FMP3 の2コア SMP を QEMU 上で
    検証するためのターゲットである」と明記しており、上記の状態不足と矛盾する。
    我々が踏んでいない前提条件（例：ビルド／cfg 側の別の設定）がある可能性は排除できないため、
    **「上流のバグ」と断定はせず、「強い証拠がある未解決事項」として記録する**。
    `target_timer.c` 自体は本タスクの方針により**未修正**（構造変更を伴うため、修正はユーザの
    判断待ち）。1コア構成（`musca_b1` プリセット）はこの問題の影響を受けず正常動作する。

  </details>

- **（2026-07-19 解消）`kernel/interrupt.py` の `INTNO_VALID_ALL` 等3変数が
  `list(set(...))` で構築され、Ruby版（`.values.flatten.uniq`、出現順保存）と違い順序不定
  だった件。** 外部レビュー指摘。現状はこの3変数が `in` によるメンバーシップ判定にしか
  使われておらず（本ファイル中で確認済み）実害は無かったが、将来これらを列挙生成に使うと
  Rubyと出力順が食い違いうる潜在的な地雷だったため、`list(dict.fromkeys(...))`
  （出現順を保った重複除去、Rubyの`.uniq`と同義）に置き換えて解消した
  （`kernel/interrupt.py`、修正3のコミットと同時に対応）。

- **（2026-07-19 解消／事前調査を否定）`chip_el3_initialize()`（`arch/arm64_gcc/zynqmp/
  chip_kernel_impl.c:55-81`）の System Timestamp Generator（`0xFF260000`）への無条件書き込みが
  QEMU 11.0.1 で同期外部アボートを起こす、という Task 4-6 着手前の事前調査の**強い状況証拠は，
  実行して確認した結果，再現しなかった**。**pristine は無改変のまま。**

  実測（`/home/honda/qemu-build/qemu-11.0.1/build-a64/qemu-system-aarch64`、`-M
  xlnx-zcu102,secure=on -smp 1|4 -m 2G -nographic -d guest_errors,unimp`）：
  1コア・4コアいずれも `timeout 20` で rc=124（タイムアウトによる正常継続）、
  デバッグログ（`-d guest_errors,unimp`）に `0xff260000`（STG）・`0xffd80000`
  （事前調査でもう一つのリスクとして挙げた PMU_GLOBAL）付近の "Unassigned mem"・
  外部アボートに類する記録は**一切無い**（`gic_dist_writeb: Bad offset ...` という
  GIC ディストリビュータへのバイト単位アクセス警告のみで、非致命的）。

  **実際に「バナーが1行も出ない」原因は別にあった**：本ターゲットは
  `USE_XUART1`（`target.cmake:83`、`Makefile.target:100-115` 由来）でコンソールを
  UART1（`0xFF010000`）に出す。QEMU の `hw/arm/xlnx-zynqmp.c`（`uart_addr =
  {0xFF000000, 0xFF010000}`、`serial_hd(i)` を `uart[i]` に割り当て）は `-serial` 引数の
  **個数**でどの UART にどのチャデブを繋ぐか決まるため、`-serial mon:stdio` を1個しか
  渡さないと index0＝UART0 に接続され、カーネルが実際に書き込む UART1 にはバックエンドが
  無くコンソール出力が**エラーも出さず黙って消える**（QEMU は UART1 自体はハードウェアとして
  正しく実装しているため `guest_errors` にも `unimp` にも掛からない）。`-serial null -serial
  mon:stdio`（UART0=null, UART1=mon:stdio）に直したところ、1コア・4コアとも
  `TOPPERS/FMP3 Kernel Release 3.4.0 for KR260 ...` のバナー・`Processor 1..4 start.`
  （4コアは4行）・サンプルタスクの周期出力まで到達することを確認した。

  加えて、`target.cmake` の `QEMU_SYSTEM_AARCH64_KRIA` の既定パス
  （`/home/honda/qemu-build/install/bin/qemu-system-aarch64`）も実機には存在せず
  （`install/bin` には musca_b1 用の `qemu-system-arm` のみがインストール済みで、
  aarch64 の 11.0.1 バイナリはビルドツリー `qemu-11.0.1/build-a64/` に置かれたまま
  未インストールだった）、無指定だと PATH 上の QEMU 8.2.2 にフォールバックしていた。
  ビルドツリーのパスも候補に加えるよう修正した。

  以上2件を `target/kria_arm64_gcc/target.cmake`（derived、pristine ではない）で
  修正した：(1) `-serial mon:stdio` → `-serial null -serial mon:stdio`、
  (2) `QEMU_SYSTEM_AARCH64_KRIA` の既定探索候補に `qemu-11.0.1/build-a64/
  qemu-system-aarch64` を追加。**`chip_kernel_impl.c` 等 pristine 側は一切変更していない**
  （`TOPPERS_USE_QEMU` ガードは不要と判断・未追加）。4コア構成（PMU_GLOBAL 経由の
  secondary core 起動）についても、同じ実測で `Processor 1/2/3/4 start.` の4行が
  問題なく出力されることを確認済みで、事前調査が挙げたもう一つのリスク
  （PMU_GLOBAL ポーリング）も実害としては顕在化しなかった。

- **（2026-07-19 検証完了）`kria_r5_gcc`（Cortex-R5F, RPUクラスタ）のQEMU実行検証
  （Task 12、上流`runqu`レシピの翻訳）。1コア（lockstep相当）・2コア（split mode）とも
  pristine 無改変で動作を確認した。**

  QEMUバージョン：`/home/honda/qemu-build/qemu-11.0.1/build-a64/qemu-system-aarch64
  --version` → `QEMU emulator version 11.0.1`（brief記載値と一致）。

  1コア（`kria_r5` プリセット、`target.cmake` の `FMP3_RUN_COMMAND` そのまま＝上流
  `runqu` の翻訳）：`timeout 20 qemu-system-aarch64 -M xlnx-zcu102 -smp 6 -m 2G
  -nographic -global xlnx-zynqmp.boot-cpu=rpu-cpu[0] -global
  cortex-r5f-arm-cpu.mp-affinity=0 -device loader,file=build/kria_r5/fmp,cpu-num=4
  -serial null -serial mon:stdio -d guest_errors,unimp` → rc=124（タイムアウトに
  よる正常継続）。`TOPPERS/FMP3 Kernel Release 3.4.0 for KR260 <Cortex-R5F> ...`
  バナー・`Processor 1 start.`・サンプルタスクの周期出力（20秒で44回)まで到達。
  `-d guest_errors,unimp` のログに `RPU_RPU_GLBL_CNTL`（`0xFF9A0000`）付近の
  異常アクセスは一切出力されなかった。**brief Step 3 の`TOPPERS_USE_QEMU`ガードは
  不要と判断し、pristine（`target_kernel_impl.c`）は無改変のまま。**

  2コア（`kria_r5-2core` プリセット、split mode, `FMP3_PRC_NUM=2`）：**brief Step 4 の
  提案コマンド（`-device loader` を `cpu-num=4`/`cpu-num=5` に2個並べるだけ）は
  そのままでは動かないことを実測で確認した**（brief記載の通り上流に前例が無い構成で
  あり，確信度が低いとの前置きは正しかった／このコマンド自体は誤り）。このコマンドで
  実行すると `timeout 20` で rc=124 になるが，バナーが**1行も**出力されない
  （1コアの rc=124＝正常継続とは似て非なる「無反応のハング」）。原因切り分けのため
  `-device loader` を `cpu-num=4` のみ（2コアビルドのバイナリを1コア同様に単独起動）
  にしても同じく無反応であることを確認し，2個目の `-device loader` 自体が原因では
  ないと判定した。`kernel/startup.c` の `barrier_sync()`（`TOPPERS_barsync`,
  複数箇所で `barrier_sync(1)`〜`barrier_sync(7)` を呼ぶ）が `TNUM_PRCID`（=2）個の
  プロセッサ全員の到達を待つ設計のため，RPU1（PRC2）が実行を始めない限り RPU0（PRC1）
  はバナー出力前の最初のバリアで無期限に停止する，と推測した。

  QEMU側の原因を特定：`hw/arm/xlnx-zynqmp.c`（`/home/honda/qemu-build/qemu-11.0.1/
  hw/arm/xlnx-zynqmp.c:242-267`）は，`boot-cpu` に指定されなかった RPU を既定で
  `start-powered-off=true`（電源断状態で生成）にする。コード中コメント：
  「Secondary CPUs start in powered-down state. When the "rpu-secondary-start"
  machine property is set, they instead start running from reset together with
  the boot CPU, which allows running an SMP guest on the RPU cluster under QEMU
  (there is no model of the LPD/CRL reset registers that the guest would
  otherwise use to release them).」（同ファイル254-260行）。すなわち実機では
  ソフトウェア（ブートローダ等）が LPD/CRL のリセット制御レジスタを叩いて RPU1 を
  解放するが，QEMU の xlnx-zynqmp モデルはそのレジスタ群を実装していないため，
  ゲスト側の操作では RPU1 を起こせない。`rpu-secondary-start`
  （`DEFINE_PROP_BOOL`、同ファイル949-950行）という machine property を立てる
  ことでのみ RPU1 が RPU0 と同時に reset から動き出す。

  `-global xlnx-zynqmp.rpu-secondary-start=true` を追加した以下のコマンドで
  再実行したところ，`Processor 1 start.` / `Processor 2 start.` の2行・
  両プロセッサのサンプルタスク周期出力（`TASK1_1`/`TASK2_1`）が安定して継続する
  ことを確認した（20秒実行で両プロセッサとも進行を継続、40秒（`timeout -k 5 40`）の
  再実行でも同様に PRC1/PRC2 とも カウント100まで進行し，フォールト・停止なし。
  `ps -eo pid,comm | grep qemu-system` で残存プロセス無しも確認）：
  ```
  qemu-system-aarch64 -M xlnx-zcu102 -smp 6 -m 2G -nographic \
      -global xlnx-zynqmp.boot-cpu=rpu-cpu[0] \
      -global xlnx-zynqmp.rpu-secondary-start=true \
      -global cortex-r5f-arm-cpu.mp-affinity=0 \
      -device loader,file=build/kria_r5-2core/fmp,cpu-num=4 \
      -device loader,file=build/kria_r5-2core/fmp,cpu-num=5 \
      -serial null -serial mon:stdio -d guest_errors,unimp
  ```
  musca_b1 の2コア事例（HardFaultで停止）と異なり，**kria_r5 の2コアは真の意味で
  安定動作した**。pristine 側の改修は一切不要だった（`target_kernel_impl.c` は
  無改変）。**`target.cmake` の `FMP3_RUN_COMMAND`（1コア用）はこのタスクの
  scope外につき未変更のまま**（brief の Files 節が `DIVERGENCE_MAP.md` のみを
  変更対象としているため）。2コア用の `cmake --build ... --target run` を
  今後整備する場合は，上記コマンド（`-device loader` 2個＋
  `-global xlnx-zynqmp.rpu-secondary-start=true`）を `target.cmake` 側に
  追加する対応が必要になる（現状のまま `kria_r5-2core` プリセットで
  `--target run` を実行すると，1コア用の `FMP3_RUN_COMMAND` が使われるため
  上記の「無反応のハング」を踏む）。

  回帰確認：他5ターゲット（`musca_b1`／`musca_b1-2core`／`polarfire_soc_kit-qemu`／
  `kria_arm64`／`kria_arm64-1core`／`rp2350_pico2`）を全て `rm -rf build/<preset>`
  してから configure・build し直し，全て configure rc=0・build rc=0・`fmp`
  生成物ありを確認した（regression無し）。

- **（2026-07-19 解消／Task 13）上記「2コア用の `cmake --build ... --target run` を
  今後整備する場合」の対応を実施した。** `target/kria_r5_gcc/target.cmake` の
  `FMP3_RUN_COMMAND` を `FMP3_PRC_NUM STREQUAL "2"` で分岐させ（`kria_arm64_gcc/
  target.cmake` の `FMP3_PRC_NUM` 分岐と同じ形）、2コア側に
  `-global xlnx-zynqmp.rpu-secondary-start=true` と `-device loader` 2個
  （`cpu-num=4`／`cpu-num=5`）を積んだ。修正後 `cmake --build build/kria_r5-2core
  --target run`（`timeout -k 5 25`）を実測：`Processor 1 start.` / `Processor 2 start.`
  の2行、`TASK1_1`/`TASK2_1` の周期出力が130行（20秒）、`rc=124`（タイムアウトに
  よる正常継続）、残存QEMUプロセス無しを確認した。修正前（1コア用の
  `FMP3_RUN_COMMAND` がそのまま `kria_r5-2core` にも使われていた状態）で
  バナー0行のまま `rc=124`（無反応のハング）になることは Task 12 が実測済み
  （上記ブロック）であり、本修正はその際に Task 12 自身が「今後整備する場合の
  対応」として書き残した変更点をそのまま適用したもの。**Task 13 では修正前の
  状態への revert-and-retest は行っていない**（Task 12 の実測記録を根拠として
  採用し，修正後の動作のみを新規に実測した。この点は推測ではなく，実施しな
  かったことの明示）。`target.cmake` は derived ファイル（pristine ではない）
  のため pristine 側の改変は無く、`target_kernel_impl.c` 等は無改変のまま。

- **（2026-07-19 解消／Task 13）`kria_r5` 用の `E_RSATR` エラーテスト variant
  （Task 11 が「follow-up gap」として未着手のまま残していたもの）を新規作成した。**
  `tools/cfg_error_tests/kria_r5_e_rsatr_intno_affinity.cfg`：`arch/arm_gcc/
  zynqmp_r5/chip_kernel.py:15-31` が「プライベート」割込み（intno 0〜31）だけを
  `(prcid << 16) | intno` でプロセッサ番号に符号化する構造を踏まえ、既存の
  IPI dispatch 用（intno 0〜3）と衝突しない未使用のプライベート範囲 intno=20 を
  選び、`CLS_PRC1`（割付け可能PRC1のみ）の囲みの中で PRC2 用に符号化された
  `(2 << 16) | 20` を `CFG_INT` することで、musca_b1/rp2350 の既存 variant と
  同型の affinity 不整合を作った。`kria_r5-2core` に対して実行し，
  ruby/python 両エンジンで `E_RSATR`（メッセージ文言も IDENTICAL）を確認
  （`tools/cfg_error_tests/run.sh build/kria_r5-2core \
  tools/cfg_error_tests/kria_r5_e_rsatr_intno_affinity.cfg "E_RSATR"` → exit=0）。
  negative control として `kria_r5`（1コア）に対して同じ `.cfg` を実行すると
  `E_PAR`（`INTNO_VALID[2]` 自体が存在しないため）になることも確認した
  （同コマンドの第3引数を `"E_PAR"` に変えて exit=0）。`tools/` は derived
  ファイルのため `DIVERGENCE_MAP.md` への記録義務は無いが，pristine の
  構造理解（chip_kernel.py の符号化ルール）に基づくテスト資産のため記録目的で
  ここに残す。

- **（2026-07-19 解消／Task 6・7・13）`kria_arm64_gcc` を追加する際に必要になった
  ROM イメージ形式フック（`FMP3_DUMP_FORMAT`）は Task 6/7 で
  `arch/arm64_gcc/common/arch.cmake` に実装済みだったが，同じ問題が
  `musca_b1_gcc`／`rp2350_pico2_gcc`（どちらも `arch/arm_m_gcc` コア共通層）
  にも存在することは「対処しない事項」として記録されたまま Task 13 まで
  未対応だった。** Task 13 で現物を再確認した：`arch/arm_m_gcc/common/
  Makefile.core:31` は `arch/arm64_gcc/common/Makefile.core:34` と同じ
  無条件代入 `DUMP = dump` を持つ（`sample/Makefile:133-134` の
  `ifndef DUMP: DUMP = srec` を include 順で後から上書きする、同一の理屈）。
  `arch/arm_m_gcc/common/arch.cmake` に `set(FMP3_DUMP_FORMAT dump)` を追加し
  （`arch/arm64_gcc/common/arch.cmake` と同じ場所・同じ書式）、`musca_b1_gcc`／
  `musca_b1-2core`／`rp2350_pico2_gcc` を srec から dump へ切り替えた。
  DUMPOPTS：`musca_b1_gcc`／`rp2350_pico2_gcc` の `Makefile.target` は
  `DUMPOPTS` を定義しない（`kria_arm64_gcc` のみ定義。現物確認済み）ため，
  上流でもフィルタ無し（全セクション）の `objdump -s` になる。CMake 汎用層は
  `FMP3_DUMPOPTS` 未定義時に既定で空文字列（`CMakeLists.txt:96-97`）にするため，
  ここで何も宣言しなければ上流と同じ挙動になる（実際に何も宣言していない）。
  影響確認：`rm -rf build/{musca_b1,musca_b1-2core,rp2350_pico2}` からの
  クリーンビルドで `generated/cfg1_out.dump`（`.srec` ではない）が生成される
  ことを確認し，`tools/cfg_equivalence.sh`（拡張子 `srec`／`dump` を自動判定，
  Task 6 で既に両対応済み）・`tools/cfg_error_tests/run.sh`（同じく Task 7 で
  両対応済み）を実行し，3構成とも exit=0（MATCH／RESULT=OK）を確認した。
  `kria_r5_gcc`（`arch/arm_gcc` コア共通層）は `arch/arm_gcc/common/
  Makefile.core` に `DUMP` の定義自体が無いことを確認済み（Task 8）のため対象外
  ＝現状の srec のままで正しい。全6ターゲットのROMイメージ形式は
  上流Makefileの実際の挙動と一致した：srec = polarfire・kria_r5、
  dump = musca_b1×2・rp2350・kria_arm64×2。

## 未解決事項（強い証拠はあるが断定はしない）

- **（2026-07-19 発見）`target/musca_b1_gcc/target_timer.c` の `hrt_clear_event_body()`
  （:228-233）が、2コア構成の PRC2（非タイムマスタ）で「割込みを発生させない」つもりの
  クリアを、SysTick が表現できる最大区間（24bit・40MHz ≒ 0.4194秒、`hrt_program(HRT_MAX_TICKS)`、
  `target_timer.h:52`）での再武装として実装しているため、既定の `sample1` 構成
  （PRC2 側に周期タイムイベントが1つも起動されない構成）では**約0.42秒ごとに不要な
  SysTick 割込みが発生し続ける**。`kernel/time_event.c:756-758` の
  `LOG_NOTICE("no time event is processed in hrt interrupt on PRC%d.")` が
  20秒の実行で47回（当方の再現では22秒で52回、25秒で59回）、**全て PRC2 でのみ**出力される
  ことを実測で確認済み（実測した通知間隔の平均 0.41947秒は、
  `HRT_MAX_TICKS/CPU_CLOCK_HZ = 16777215/40000000 = 0.419430秒`という計算値と一致）。
  機序は特定できたと判断している（詳細・上流への質問:
  `docs/upstream-reports/2026-07-19-musca_b1-2core-hardfault.md` の
  「上流20260719修正後に残る事象」節）。**致命的ではない**（カーネルは走行を継続し、
  実イベント登録時に SysTick は毎回上書きされるため周期タスクの精度には影響しないと
  コード読解で判断しているが、実機での精度実測は未実施）。この `hrt_clear_event_body()`
  自体は 20260719 の per-core 化改修で**変更されていない**（`git diff` で確認済み）。
  すなわち20260719以前から存在した挙動だが、それまでは PRC2 が起動直後に HardFault で
  停止していたため観測される機会が無く、HardFault 修正によって初めて表面化した。
  **上流のバグと断定はせず**、`hrt_clear_event_body()` の「クリア」の意図
  （SysTick に真の無期限停止手段が無いことへの次善策か、見落としか）を上流に確認したい
  未解決事項として記録する。pristine は未改変（調査は既存のビルド成果物の実行のみで行った）。

- **（2026-07-19 発見・未調査／Task 13）`kria_r5-2core` の QEMU 実行でも、上記と同型に
  見える `LOG_NOTICE("no time event is processed in hrt interrupt on PRC%d.")` が
  観測された。** ただし出力されるプロセッサが**PRC1**である点が musca_b1（PRC2）と
  逆である。Task 13 の Step 4 回帰確認（`cmake --build build/kria_r5-2core --target run`、
  `timeout -k 5 25`、20秒分の出力）で7回観測した（すべて `on PRC1.`）。musca_b1 は
  ARM-M の SysTick（`target_timer.c`）、kria_r5 は ZynqMP の TTC（`ttc_hrt.c`）と
  実装するハードウェアタイマが異なり、`hrt_clear_event_body()` 相当の実装が同じ機序を
  持つかどうかは**未確認**（コードは読んでいない）。カーネルは20秒間 PRC1/PRC2 双方の
  `TASK1_1`/`TASK2_1` 周期出力を継続しており（130行）、致命的でないことは実測で確認したが、
  根本原因の調査はTask 13のスコープ外（本タスクの4件の既知欠陥のいずれにも該当しない）
  のため行っていない。**事実（7回・PRC1・非致命的）と、musca_b1と「同型かもしれない」という
  推測は分けて記録する**。pristine は未改変（調査は既存のビルド成果物の実行のみで行った）。

- **（2026-08-04 発見／ISR最終レビュー Important #3 対応）`CFG_INT`（したがって同一
  クラス制約により `ENA_DYNISR` も）は，intno の符号化方式に関わらず，**現時点で
  サポートされている全5ターゲットのうち kria_r5 を除く4つで**，affinityPrcList が
  2以上のクラスの中に書くと E_RSATR になる。** musca_b1/rp2350_pico2 は intno 自体が
  per-prcid 符号化（`kernel/interrupt.py:151-164` の汎用検査が捕まえる。訂正Hが
  最初に確認した事実）だが，**kria_arm64（GIC）・polarfire_soc_kit（PLIC）は
  intno がグローバル（プロセッサ間で同一値）でも同じく止まる**——
  `arch/arm64_gcc/common/gic_kernel.py` / `arch/riscv_gcc/common/plic_kernel.py` の
  `TargetCheckCfgInt`（NGKI5184：「複数プロセッサでの割込み受付は動作するはずだが
  未テストのため現状サポートしない」というコメントつきの明示的な E_RSATR）が原因。
  これは dcre / ISR 移植とは無関係な，既存のアーキ層の制約であり，本ブランチが
  持ち込んだものではない（`gic_kernel.py`/`plic_kernel.py` は計画C Task 4/A で
  新規移植した際に，対応する pristine `.trb`（`arch/arm64_gcc/common/gic_kernel.trb`／
  `arch/riscv_gcc/common/plic_kernel.trb`）の `TargetCheckCfgInt` をそのまま
  移植したものであり，動作は pristine と同一）。
  **kria_r5（arm_gcc/zynqmp_r5）だけがこの検査を持たない**。原因を pristine の
  ソースまで遡って確認した：`arch/arm64_gcc/zynqmp/chip_kernel.trb:29` は
  `IncludeTrb("gic_kernel.trb")` するが，`arch/arm_gcc/zynqmp_r5/chip_kernel.trb`
  （pristine，全39行）には対応する `IncludeTrb` が**存在しない**——`core_kernel.trb`
  しか読み込まない。すなわちこれは**upstream の R5 ポート自体が最初から持っていない
  非対称**であり，`target/kria_r5_gcc/*.py`（計画C Task 8/10，本 DIVERGENCE_MAP
  33-35行目）の Python 移植は pristine の構造を忠実に反映したもので，移植側の
  見落としではない。結果として **`test/test_dcre_mix_ma.cfg`（`CLS_ALL_PRC1`，
  affinityPrcList=[1, 2]）は kria_r5-2core でのみ生成が通り**，
  `build/kria_r5-2core-tmixma/generated/kernel_cfg.c` の
  `_kernel_inh_table_prc1[0x28]`／`_kernel_inh_table_prc2[0x28]` の両方が同一の
  `_kernel_inthdr_40` を指し，`_kernel_inthdr_40` は同一の `_kernel_isr_queue_table[0]`
  （`_kernel_tnum_isr_queue == 1`）を呼ぶことを実測で確認した（`cfg_equivalence.sh`
  は同構成で `RESULT = MATCH`）。**runtime での同一キュー2コア並行走査は今回も
  未実証のまま**（QEMU実行はこの追加のスコープ外，コード検査では健全）。
  この非対称自体（kria_r5 に NGKI5184 相当の安全網が無いこと）は，将来 kria_r5-2core
  で「動作するはずだが未テスト」の機能を意図せず使ってしまう余地として記録する
  価値はあるが，**アーキ層（pristine）の変更が要る話であり，本 ISR 移植のスコープ外**
  のため，コード変更は行わない。pristine は本追加では未改変（新規ファイルの追加と
  台帳記載のみ）

## 期限付きの逸脱

| 対象 | 内容 | 解消条件 |
|---|---|---|
| （現在なし） | 2026-07-19 時点で期限付きの逸脱は無い。`cfg_py/cfg.py` の逸脱は計画B完了で解消済み（上記「解消済み事項」参照） | - |
