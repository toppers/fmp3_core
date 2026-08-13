# ESP-IDF ブートローダ × FMP3 イメージ統合 調査

対象: ESP32-P4（RV32IMAFC, デュアル HP コア）/ M5Stamp ESP32P4
ESP-IDF: v5.5 / esptool 4.12.dev2
FMP3: FMP3 ツリー（本ドキュメントが含まれるツリー）

**採用方式: (a) 最小 IDF アプリをローダ殻にする** — FMP3 を `libfmp3.a` として IDF ビルドに静的リンクし，`app_main` から `toppers_start` へ制御移譲．イメージ形式・appdesc・flash セグメント 2 本・SHA/checksum・mmap 整列は IDF/esptool が全自動処理する．

方式(b)「完全独立 ELF + elf2image」は棄却．主理由: `bootloader_utility.c:842` の `assert(rom_index==2)`（flash セグメントちょうど 2 本前提）を RAM-only イメージが踏む．

---

## 方式(a) vs (b) 選定要点

| 観点 | (a) ローダ殻 ← 採用 | (b) 完全独立 ← 棄却 |
|---|---|---|
| イメージ形式 | IDF/esptool 全自動（appdesc, 2 segments, SHA, mmap 整列） | 自前 `.ld` で全責任 |
| flash 2 セグメント assert | 自動的に満たす | **要対応**（最大の落とし穴） |
| リンカスクリプト | IDF 既定流用 | 新規作成必須 |
| FMP3 start.S | 変更不要 | 変更不要（ただし load 番地/シンボル整合が必要） |
| デバッグ | IDF monitor/panic 流用可 | 自前 |

---

## ブートローダ jump 時点の状態

IDF 2nd ステージブートローダが app へ jump する時点:
- **core0 のみ稼働**，core1 はリセット保持（起動は app 責務）
- **初期化済み**: クロック/PLL，cache，flash MMU，UART
- **未設定（app 責務）**: sp，gp，BSS/data，mtvec（実機値 `0x4FF00003` = CLIC 固定），FPU FS，core1 起動
- jump は引数なし `call entry_addr` のみ

---

## start.S / IDF 差分と対応（方式(a)）

| 項目 | IDF jump 時点 | FMP3 が期待 | 対応 |
|---|---|---|---|
| 実行コア | core0 のみ | master (hartid 0 を master に) | `TOPPERS_MASTER_PRCID`/`my_pidx` を P4 用に再定義 |
| hartid 採番 | core0=0, core1=1 | PolarFire は 1..4，`pidx=hartid-1` | **chip 層で `my_pidx=mhartid`（master=0）** |
| sp | bootloader 値 | istkpt_table から自前設定 | start.S が設定（istkpt_table を cfg 生成） |
| mtvec | CLIC 0x4FF00003 | カーネルが後で設定 | CLIC モードのベクタ/mtvt 設定をチップ依存部で |
| BSS/data | 未初期化 | start.S が master で初期化 | IDF リンカ配置に合わせ `__sbss_*/__bss_*/__idata_*/__data_*` を整合，または `TOPPERS_OMIT_*` で IDF 側に委譲 |
| FPU | 未設定 | start.S が Initial に | start.S が処理 |
| 2 コア目 | リセット保持 | 各コアが `toppers_start` 進入想定 | **app が core1 を起動し `toppers_start` へ誘導**（`ets_set_appcpu_boot_addr` + unstall）→ start_sync で rendezvous |

FMP3 自身は core1 を起こさない（PolarFire は MSS SDK が起こす）．ESP32-P4 では IDF/ROM の core1 起動経路をチップ依存部で実装する必要がある．これが FMP3 SMP 起動の要．

---

## 実機メモリ配置（M5Stamp ESP32P4）

flash 2MB，dio，80MHz，single-app partition（factory 0x10000, 1MB），PSRAM 無効（初期）．

| 領域 | アドレス | サイズ |
|---|---|---|
| TCM | 0x3010_0000 | 8KB |
| flash mmap (IROM/DROM) | 0x4000_0020〜 | — |
| HP L2MEM sram_low | 0x4FF0_0000 | 約 180KB (0x2CBD0) |
| HP L2MEM sram_high | 0x4FF4_0000 | 384KB (0x60000) |
| heap_end | 0x5000_0000 | — |
| PSRAM 窓（無効） | 0x4800_0000 | 64MB |

初期は PSRAM 無効・内蔵 SRAM 完結で進める．モジュール PSRAM 実サイズは未記録．

---

## 統合手順（方式(a)）

1. IDF プロジェクト殻を新設（`set-target esp32p4`，`set-target esp32p4`，2MB/dio/80m）．
2. FMP3 を `configure.rb -T m5stamp_esp32p4_gcc ... && make` で静的ライブラリ化（`-march=rv32imafc_zicsr_zifencei_xesppie -mabi=ilp32f`，hard-float ABI で IDF と一致）．`main/CMakeLists.txt` に `target_link_libraries` / `add_prebuilt_library` で取り込む．最初は FMP3 を `-nostdlib` 寄りで自己完結させ，IDF libc とのシンボル衝突を避ける．
3. `app_main`（または `esp_startup_start_app` の weak 上書き）から `toppers_start` へジャンプ．最初は single-core（PRC_NUM=1）でカーネル起動 → syslog 確認．
4. core1 起動経路（クロック EN + リセット解除 + `ets_set_appcpu_boot_addr` + unstall）をチップ依存部に実装し，core1 を `toppers_start` へ誘導 → SMP（PRC_NUM=2）で start_sync rendezvous 確認．
5. `idf.py build` がイメージ形式・appdesc・2 segments・SHA・mmap 整列を自動処理．書込みは `esptool ... write_flash 0x2000 bootloader 0x8000 ptable 0x10000 app`．

---

## リスク一覧

| 優先度 | 内容 | 対応 |
|---|---|---|
| 高 | core1 起動は app 責務（FMP3 は起こさない）．未実装だと SMP 不可 | チップ依存部で `ets_set_appcpu_boot_addr` + unstall 実装 |
| 高 | hartid 採番/master 定義が PolarFire デフォルトと異なる | chip 層で `my_pidx=mhartid`，`TOPPERS_MASTER_PRCID=0`，`TNUM_PRCID=2` |
| 中 | BSS/data 初期化の責務分担（IDF リンカ配置と FMP3 シンボルの整合） | `__idata_*/__bss_*` シンボル整合，または `TOPPERS_OMIT_*` で IDF に委譲 |
| 中 | ABI/libc/specs 整合（FMP3 を IDF ビルドに混ぜる際） | FMP3 を `-nostdlib` 寄りで自己完結させる |
| 低 | PSRAM 無効・flash 2MB 制約，モジュール PSRAM 実サイズ未記録 | 初期は内蔵 SRAM 完結 |
| 低 | USB-Serial/JTAG 喪失（USB/クロック周辺再初期化で ACM 消失） | FMP3 でその周辺を触らない |
