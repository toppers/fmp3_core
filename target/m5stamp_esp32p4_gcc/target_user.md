# M5Stamp ESP32P4 ターゲット依存部 ユーザーズマニュアル
- 作成者: 本田晋也
- 最終更新: 2026年06月29日

# ドキュメントの位置づけ

このドキュメントは，TOPPERS/FMP3カーネルの M5Stamp ESP32P4 ターゲット依存部を
使用するために必要な事項を説明するものである．

# 概要

本ターゲット依存部（GNU開発環境向け）は，TOPPERS/FMP3カーネルを，ESP32-P4 を
搭載した M5Stamp ESP32P4 モジュール上で動作させるためのものである．

- M5Stamp ESP32P4: https://shop.m5stack.com/products/m5stamp-esp32p4-module
- SoC: ESP32-P4（デュアルコア RISC-V HP コア＋LP コア．本移植は **HP コア2基**を
  対象とし LP コアは使用しない）．HP コアは RV32IMAFC，CLIC モード固定．

ターゲット略称等は次の通り．

	ターゲット略称：m5stamp_esp32p4_gcc
	システム略称：m5stamp_esp32p4
	開発環境略称：gcc

## ターゲット依存部の構成

本ターゲット依存部は，チップ依存部として ESP32-P4 チップ依存部を，コア依存部として
RISC-Vコア依存部（CLIC 依存部含む）を使用している．

	target/
		m5stamp_esp32p4_gcc/		本ターゲット依存部
			tools/					ビルド・実機実行・テストのツール一式（方式(a)）

	arch/
		riscv_gcc/common/			RISC-Vコア依存部（CLIC 依存部 clic_kernel_impl 等を含む）
		riscv_gcc/esp32p4/			ESP32-P4 チップ依存部
		riscv_gcc/doc/				RISC-V/CLIC 依存部のドキュメント
		gcc/						GCC開発環境依存部

## 統合方式（方式(a)）

ESP32-P4 はベアメタル単独ブートではなく **ESP-IDF** のブート・初期化を前提とする．
本移植は **方式(a)** を採る：FMP3 を静的ライブラリ libfmp3.a にまとめ，ESP-IDF
アプリ（ローダ）へ静的リンクして実機で動かす．

- ABI は ilp32f（IDF と一致）．
- `.data`/`.bss` の初期化は IDF 起動が行うため start.S の自前初期化は抑止する
  （TOPPERS_OMIT_BSS_INIT / TOPPERS_OMIT_DATA_INIT）．
- FMP3 の全 text を内部 RAM（IRAM）へ集める（flash MMU 窓との overlap 回避，
  asm の近距離 jal を同一 IRAM 内に収めるため）．

IDF 起動 → app_main（ローダ）が toppers_start を参照 → FMP3 のエントリへ，という
流れでカーネルが起動する．詳細は target/m5stamp_esp32p4_gcc/idf_image_integration.md，
チップ固有の知見は arch/riscv_gcc/esp32p4/chip_design.md を参照．

## メモリ配置とキャッシュ

方式(a)の最終イメージにおける FMP3 の配置は以下のとおり（アドレス・サイズは
fmp_app ビルドの実測代表値．サイズはアプリにより変わる）．

| 何 | セクション | アドレス | 内容 |
|---|---|---|---|
| FMP3 本体（カーネル＋アプリの全 text） | `.iram0.text` | 0x4FF0_0000（内部 SRAM / HP L2MEM） | 約 80KB．**RAM 実行**（XIP ではない） |
| IDF ローダ殻（app_main 等） | `.flash.text` | 0x4000_0020（flash XIP） | キャッシュ経由実行 |
| FMP3 .data（`kernel_data_CLS_*`・タスクスタック `stack_CLS_*` 含む） | `.dram0.data` | 0x4FF1_3980 付近（L2MEM） | IDF 起動が flash→RAM コピーで初期化 |
| 非タスク/アイドルスタック（istack/idstack） | `.dram0.data` 内 | 0x4FF19F40 付近 | 同上 |
| .bss | `.dram0.bss` / `.dram1.bss` | 0x4FF2_0740 / 0x4FF4_0000（sram_high） | IDF 起動がゼロ初期化 |
| **FMP3 の rodata**（`istkpt_table` 等の const テーブル） | `.flash.rodata` | 0x4002_xxxx（**flash XIP 上**） | キャッシュ経由の読出し |

- text の IRAM 配置は build_fmp3_lib.sh の objcopy（`.text`→`.iram1.fmptext`）で行う．
  FMP3 独自セクション（`.kernel_data_CLS_*`/`.stack_CLS_*`）は `.data.*` へリネームし，
  IDF の sections.ld（`*(.data .data.*)`）による配置・初期化に乗せる．

**キャッシュは ON**（IDF ブートローダ/スタートアップが有効化した状態を引き継ぐ．
FMP3 側ではキャッシュを操作しない）．

- L2 キャッシュ 128KB（`CONFIG_CACHE_L2_CACHE_128KB`），ライン 64B．L1 ライン 64B．
- ESP32-P4 は**内部 SRAM（L2MEM）へのアクセスも L1 キャッシュ経由**
  （`SOC_CACHE_INTERNAL_MEM_VIA_L1CACHE`）．FMP3 の text・データ・スタックは
  L1 キャッシュされて動作する．HP 2 コア間のコヒーレンスはハードウェアが担保する
  （FMP3 側にキャッシュ同期処理は無く，SMP カーネルテスト全 PASS で確認済み）．
- flash XIP 領域（IDF ローダ殻の text と FMP3 の rodata）は L1＋L2 キャッシュ経由．
  キャッシュミス時は flash アクセスのレイテンシ（他コアとの MSPI 競合を含む）を
  踏むため，ハードリアルタイム性の観点では FMP3 rodata が flash 上にあることに留意
  （必要なら rodata も RAM へ寄せる改造は可能）．

# 開発環境の準備

本ターゲット依存部は **ESP-IDF を前提**とする（ブート・初期化を IDF に委ねる方式(a)．
「統合方式」節を参照）．ESP-IDF 本体は利用者が任意の場所に導入する．以下では
`~/tools/esp-idf` に置く例を示すが，場所は任意である．

## 必要なもの

- **ESP-IDF**（**v5.5 系**で動作確認）．RISC-V ツールチェーン・GDB・OpenOCD は
  すべて IDF のツールインストールで導入されるため，個別に用意する必要はない．
  - ツールチェーン: `riscv32-esp-elf`（IDF v5.5 では GCC 14.2.0 を固定）
  - GDB: `riscv32-esp-elf-gdb`（ツールチェーンとは**別パッケージ**）
  - OpenOCD: `openocd-esp32`（Espressif フォーク．upstream 版は ESP32-P4 非対応）
- **Ruby**（FMP3 の configure.rb / *.trb 用．3.2 系で確認）
- 動作確認は M5Stamp ESP32P4 実機で行っている（内蔵 USB-Serial/JTAG 経由）

## 導入手順

```sh
# 1) ESP-IDF の取得（v5.5 系）
git clone -b release/v5.5 --recursive https://github.com/espressif/esp-idf.git ~/tools/esp-idf

# 2) ツール一式の導入（ESP32-P4 向け）
export IDF_PATH=$HOME/tools/esp-idf
export IDF_TOOLS_PATH=$HOME/tools        # ← 導入時と使用時で必ず同じ値にする（下記注意）
cd $IDF_PATH && ./install.sh esp32p4

# 3) 環境の有効化（ビルド・書込み・デバッグの各セッションで実行）
. $IDF_PATH/export.sh
```

**★ `IDF_TOOLS_PATH` は導入時と使用時で必ず同じ値にすること．** 値が食い違うと
IDF が別の Python 仮想環境を参照し，ツールが見つからない・ビルドが正常に走らない
といった分かりにくい失敗になる（実際に踏んだ事例がある）．未設定時の IDF 既定値は
`~/.espressif` である．ツールは `$IDF_TOOLS_PATH/tools/<tool>/<version>/` に入る．

導入されたツールの確認:

```sh
riscv32-esp-elf-gcc --version   # → 14.2.0
riscv32-esp-elf-gdb --version   # → GNU gdb (esp-gdb) …
openocd --version               # → …-esp32-… （"-esp32-" が付かない版は P4 非対応）
```

## チップリビジョンに関する注意

ESP32-P4 の初期シリコン（rev v1.x）は，ESP-IDF 既定の最小リビジョン指定では弾かれる．
本ターゲットの `tools/fmp_loader/sdkconfig.defaults` では
`CONFIG_ESP32P4_SELECTS_REV_LESS_V3` / `CONFIG_ESP32P4_REV_MIN_100` を指定して
これを回避している（動作確認は rev v1.3 の実機で実施）．

# ターゲット定義事項の規定

本ターゲット依存部は，RISC-Vコア依存部・CLIC 依存部・ESP32-P4 チップ依存部を
用いて実装されている．これらにおけるターゲット定義事項の規定は
「RISC-V依存部 ユーザーズマニュアル」および arch/riscv_gcc/doc/clic_design.md，
arch/riscv_gcc/esp32p4/chip_user.md を参照すること．

# ドライバ関連の情報

## タイマドライバ

高分解能タイマは CLINT の Machine Timer で実現している（コアローカルの mtimecmp）．
ESP32-P4 の mtime は mtimectl の MTIME_EN で明示有効化する（chip_initialize）．

## シリアルインタフェースドライバ

ESP32-P4 内蔵 UART を用いる（ポーリング出力）．ボーレート 115200bps，8bit，
パリティなし，ストップ1bit．システムログの低レベル出力も同 UART を用いる．

# システム構築・実機実行・テスト

ビルド・書込み・テストのツールは `target/m5stamp_esp32p4_gcc/tools/` にある
（手順の詳細は tools/README.md）．要点のみ示す．

## ビルド

```sh
. $IDF_PATH/export.sh                  # 導入手順は「開発環境の準備」を参照

cd <fmp3>/target/m5stamp_esp32p4_gcc/tools/fmp_loader
idf.py build                           # 方式(a): libfmp3.a をビルドし IDF アプリへリンク
```

`idf.py build` の一部として `build_fmp3_lib.sh` が走り，FMP3 を configure.rb/make で
ビルドして `libfmp3.a` にまとめる．既定のアプリは `tools/fmp_app`，既定の PE 数は
`PRC_NUM=2`（SMP）である．

## 書込み対象ボードの同定（重要）

内蔵 USB-Serial/JTAG のシリアル番号は**ボードの MAC アドレス文字列**である．
**`/dev/ttyACM<N>` の番号は接続順で変わる**（抜き差しや再起動で入れ替わる）ため，
複数のボードを接続している場合は必ずシリアルで同定してから書き込む．

```sh
for d in /dev/ttyACM*; do
  echo "$d $(udevadm info -q property -n $d | grep -oE 'ID_SERIAL_SHORT=[^ ]*' | cut -d= -f2)"
done
```

## 書込みと実行

ビルドディレクトリからは `Makefile.target` のターゲットが使える．**対象ボードを
シリアル（MAC）で指定でき，指定を省いて複数台が繋がっている場合は候補を列挙して
中断する**ため，誤書込みを防げる．

```sh
make flash   SERIAL=30:ED:A0:EA:98:0E   # esptool で書込み（build/flash_args に従う）
make console SERIAL=30:ED:A0:EA:98:0E   # カーネル出力を見る
make jtaggdb SERIAL=30:ED:A0:EA:98:0E   # JTAG/GDB でデバッグ（esp32p4_openocd_jtag.md 参照）
```

ESP-IDF のツールを直接使う場合は次のとおり．

```sh
idf.py -p /dev/ttyACM0 flash monitor    # ポートは上記で同定したものを指定する
```

esptool を直接用いる場合は，ビルド時に生成される `build/flash_args` を渡す
（オフセット等はこのファイルが正）．

```sh
cd build
python -m esptool --chip esp32p4 --port /dev/ttyACM0 --baud 460800 \
       --before default_reset --after hard_reset write_flash @flash_args
```

方式(a)のイメージ構成（`build/flash_args` の内容．`--flash_mode dio --flash_freq 80m
--flash_size 2MB`）:

| オフセット | ファイル | 内容 |
|---|---|---|
| `0x2000`  | `bootloader/bootloader.bin` | ESP-IDF 2nd ブートローダ |
| `0x8000`  | `partition_table/partition-table.bin` | パーティションテーブル |
| `0x10000` | `fmp_loader.bin` | FMP3 を静的リンクした IDF アプリ |

## デバッグ（JTAG / GDB）

ESP32-P4 は内蔵 USB-JTAG を持ち，USB ケーブル 1 本で OpenOCD/GDB デバッグができる．
**FMP3 のシンボルは最終成果物 `build/fmp_loader.elf` に含まれる**（configure.rb/make が
生成する `fmp` ELF ではない）．複数ボード接続時の対象指定，dual-hart の扱い，
IRAM 実行の注意などは **esp32p4_openocd_jtag.md** を参照．

## テスト

```sh
cd <fmp3>/target/m5stamp_esp32p4_gcc/tools/fmp_loader
./run_fmp_test.sh 2 test_mtrans2 test_raster2 ...   # PRC_NUM=2 で SMP テスト
```

**★ 注意: `run_fmp_test.sh` は書込み先・モニタ先を `/dev/ttyACM0` に固定している**
（環境変数による上書き手段は無い）．前述のとおりポート番号は接続順で変わるため，
複数のボードを接続している環境では，実行前にスクリプトの該当箇所を対象ボードの
ポートに書き換えること．意図しないボードへ書き込む事故を防ぐため，実行前に必ず
シリアル（MAC）で対象を確認する．

- SMP の canary は mtrans2/raster2（dispatch-IPI storm による livelock を露呈する．
  intermittent なため mtrans2 は複数回回す）．
- 割込み管理機能テスト int1 は **実行可能**（CLIC はソフトから IP を立てられ ras_int
  をサポートするため．PLIC ターゲットでは非対応だった点が異なる）．
- 本物の TTSP3 適合性は run_ttsp_api.sh（外部 TTSP3 スイートを TTSP3_DIR で指定）．

# リファレンス

## ディレクトリ構成・ファイル構成

	target/m5stamp_esp32p4_gcc/
		MANIFEST					個別パッケージのファイルリスト
		Makefile.target				Makefileのターゲット依存部
		m5stamp_esp32p4_kit.h		ターゲットのハードウェア資源の定義
		target_asm.inc				ターゲット依存部のアセンブリ言語用マクロ定義
		target_cfg1_out.h			cfg1_out.cのリンクに必要なスタブの定義
		target_check.trb			kernel_check.trbのターゲット依存部
		target_class.trb			ターゲット依存部のクラス定義
		target_ipi.h				プロセッサ間割込みのターゲット依存部
		target_kernel.cfg			カーネル実装のコンフィギュレーションファイル
		target_kernel.h				kernel.hのターゲット依存部
		target_kernel.trb			kernel.trbのターゲット依存部
		target_kernel_impl.c		カーネル実装のターゲット依存部
		target_kernel_impl.h		カーネル実装のターゲット依存部に関する定義
		target_rename.def / .h		ターゲット依存部の内部識別名のリネーム
		target_serial.cfg / .h		シリアルドライバのターゲット依存部
		target_sil.h				sil.hのターゲット依存部
		target_stddef.h				t_stddef.hのターゲット依存部
		target_syssvc.h				システムサービスのターゲット依存定義
		target_test.h				テストプログラムのターゲット依存定義
		target_timer.h				タイマドライバを使用するための定義
		target_unrename.h			ターゲット依存部の内部識別名のリネーム解除
		target_user.md				本マニュアル
		esp32p4_openocd_jtag.md		OpenOCD/JTAG/GDB デバッグ手順
		idf_image_integration.md	ESP-IDF × FMP3 イメージ統合（方式(a)）
		tools/						ビルド・実機実行・テストのツール一式
			fmp_loader/				ESP-IDF プロジェクト＋スクリプト
			fmp_app/				first light 用アプリ
			README.md				ツールの使い方

## バージョン履歴

- 2026/07/18
  - リリースに向けてドキュメントを整備．
    - 「開発環境の準備」を新設し，ESP-IDF の取得・`install.sh` によるツール導入
      （ツールチェーン／GDB／OpenOCD）・`IDF_TOOLS_PATH` を導入時と使用時で一致させる
      注意・チップリビジョン回避策（`CONFIG_ESP32P4_SELECTS_REV_LESS_V3` 等）を記載．
    - 書込み手順を具体化（対象ボードのシリアルによる同定，`flash_args` に基づく
      オフセット表，esptool 直接実行の例）．`run_fmp_test.sh` がポートを固定している
      点の注意を追記．
    - デバッグ（JTAG/GDB）の入口を追加し esp32p4_openocd_jtag.md へ誘導．
  - 誤りの訂正: `IDF_TOOLS_PATH` の例示値を修正（旧記述の値は Python 仮想環境の
    不一致を招く既知の落とし穴であった）．

- 2026/06/29
  - polarfire_soc_kit_gcc を基に M5Stamp ESP32P4（ESP32-P4 / RV32 / CLIC / SMP）
    向けに新規作成．方式(a)（ESP-IDF 静的リンク）のツールを tools/ に同梱し，
    ターゲット単体で自己完結するよう構成．

以上
