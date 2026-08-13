# ESP32-P4 OpenOCD / JTAG / GDB デバッグ手順

- 対象ボード: M5Stamp ESP32P4（チップ内蔵 USB-JTAG, USB ID `303a:1001`）
- 最終更新: 2026年07月18日（本手順は実機 ESP32-P4 rev v1.3 で実行し確認した）

## 要点

- ESP32-P4 は**チップ内蔵 USB-JTAG** を持つ．外付け JTAG プローブ・配線は不要で，
  USB ケーブル 1 本でデバッグできる．
- **Espressif フォーク `openocd-esp32` が必要**（ESP-IDF のツールインストールで導入される）．
  upstream の OpenOCD は ESP32-P4 非対応（`board/esp32p4-builtin.cfg` を持たない）．
- HP デュアルコアは `esp32p4.hp.cpu0` / `esp32p4.hp.cpu1` の 2 ターゲットとして見え，
  halt group にまとめられる（片方を halt すると両方 halt する）．
- **複数の ESP ボードを接続している場合は `adapter serial` の指定が必須**（後述．
  省略すると OpenOCD は無関係なボードを掴む）．

---

## 1. OpenOCD の準備

### 1.1 入手

ESP-IDF のツールインストール（`install.sh`）で `openocd-esp32` が
`$IDF_TOOLS_PATH/tools/openocd-esp32/<version>/` 以下に導入される．`export.sh` を
source すると PATH に入る．導入手順は target_user.md「開発環境の準備」を参照．

```sh
. $IDF_PATH/export.sh
openocd --version      # → Open On-Chip Debugger v0.12.0-esp32-... と表示されること
```

### 1.2 ★ upstream OpenOCD を掴む罠

システムに upstream の OpenOCD（例: `/usr/local/bin/openocd`，ディストリ配布版）が
入っていると，`export.sh` を source していない場合にそちらが先に見つかる．upstream 版は
`board/esp32p4-builtin.cfg` を持たないため，設定ファイルが見つからないエラーになる．

```sh
which openocd          # ← openocd-esp32 のパスを指していることを必ず確認する
openocd --version      # ← "…-esp32-…" が付かない版は ESP32-P4 非対応
```

バージョン文字列に `-esp32-` が含まれない場合は upstream 版である．

### 1.3 USB アクセス権限（udev ルール）

一般ユーザで USB-JTAG を開けるようにするため，openocd-esp32 に同梱の udev ルールを
導入する．**ルールは openocd-esp32 の `share/openocd/contrib/` にある**．

```sh
# <openocd-esp32> は $IDF_TOOLS_PATH/tools/openocd-esp32/<version>/openocd-esp32
sudo cp <openocd-esp32>/share/openocd/contrib/60-openocd.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger
```

導入しない場合は OpenOCD が USB デバイスを開けず，sudo での実行が必要になる．

---

## 2. 接続対象ボードの指定（複数ボード接続時は必須）

内蔵 USB-JTAG のアダプタシリアルは**ボードの MAC アドレス文字列**である．
`adapter serial` で対象を明示する．

```sh
# 接続中のボードとシリアルを列挙（ESP 系は vid:pid=303a:1001）
for d in /dev/ttyACM*; do
  echo "$d $(udevadm info -q property -n $d | grep -oE 'ID_SERIAL_SHORT=[^ ]*' | cut -d= -f2)"
done

openocd -c "adapter serial 30:ED:A0:EA:98:0E" -f board/esp32p4-builtin.cfg
```

**`adapter serial` を省くと，OpenOCD は最初に見つかった `303a:1001` のデバイスを掴む．**
ESP32-P4 以外の ESP チップを掴むと，JTAG IDCODE 不一致で次のように失敗する
（実機で発生した例．P4 は `0x00012c25`，別チップを掴むと別の値になる）．

```
Warn : JTAG tap: esp32p4.tap0       UNEXPECTED: 0x00017c25 …
Error: JTAG tap: esp32p4.tap0  expected 1 of 1: 0x00012c25 …
```

**注意: `/dev/ttyACM<N>` の番号は接続順で変わる**（ボードの抜き差しや再起動で入れ替わる）．
番号でボードを識別せず，必ずシリアル（MAC）で同定すること．`adapter serial` による
指定はポート番号に依存しないため，この点でも安全である．

### ★罠: `adapter serial` は大文字・小文字を区別する

`udevadm` の `ID_SERIAL_SHORT` は**大文字**（`30:ED:A0:EA:98:0E`）で返る．一方
esptool などは**小文字**（`30:ed:a0:ea:98:0e`）で MAC を表示するため，そちらを
コピーすると一致せず次のように失敗する（実機で発生）．

```
Info : No device matches the serial string
Error: esp_usb_jtag: could not find or open device!
Error: [esp32p4.hp.cpu0] Unsupported DTM version: -1
Error: [esp32p4.hp.cpu0] Could not identify target type.
```

`udevadm` の出力をそのまま使うこと（`Makefile.target` の `make openocd` /
`make jtaggdb` は自動的にそうしている）．

正常に接続できると次のようになる（実機ログ）．

```
Info : esp_usb_jtag: serial (30:ED:A0:EA:98:0E)
Info : JTAG tap: esp32p4.tap0 tap/device found: 0x00012c25 (mfg: 0x612 (Espressif Systems), part: 0x0012, ver: 0x0)
Info : [esp32p4.hp.cpu0] Core 0 made part of halt group 1.
Info : [esp32p4.hp.cpu0]  XLEN=32, misa=0x40901125
Info : [esp32p4.hp.cpu1] Core 1 made part of halt group 1.
Info : [esp32p4.hp.cpu0] starting gdb server on 3333
```

`misa=0x40901125` は RV32IMAFC（＋非標準拡張）を示す．

---

## 3. GDB によるデバッグ

### 3.1 GDB の実体

GDB は**ツールチェーンとは別パッケージ**として導入される．

```
$IDF_TOOLS_PATH/tools/riscv32-esp-elf-gdb/<version>/riscv32-esp-elf-gdb/bin/riscv32-esp-elf-gdb
```

`export.sh` を source すれば `riscv32-esp-elf-gdb` として PATH に入る（確認に用いた版:
`GNU gdb (esp-gdb) 16.2_20250324`）．

### 3.2 ★ 読み込むべき ELF（方式(a)特有）

本ターゲットは方式(a)（FMP3 を libfmp3.a にまとめ ESP-IDF アプリへ静的リンク）を採るため，
**FMP3 のシンボルは最終成果物である `build/fmp_loader.elf` に入っている**．
configure.rb/make が生成する `fmp` ELF ではなく，こちらを GDB に読ませること．

### 3.3 接続手順

ターミナル A（OpenOCD 常駐．gdb サーバは 3333 番で待受）:

```sh
cd <fmp3>/target/m5stamp_esp32p4_gcc/tools/fmp_loader
openocd -c "adapter serial <ボードのMAC>" -f board/esp32p4-builtin.cfg
```

ターミナル B（GDB）:

```sh
cd <fmp3>/target/m5stamp_esp32p4_gcc/tools/fmp_loader
riscv32-esp-elf-gdb -ex "target extended-remote :3333" \
                    -ex "monitor halt" \
                    build/fmp_loader.elf
```

実機で確認した動作例:

```
(gdb) printf "PC=%#x\n", $pc
PC=0x4ff05318
(gdb) info symbol $pc
sil_dly_nse1 in section .iram0.text
(gdb) info line *irc_begin_ipi
Line 97 of ".../arch/riscv_gcc/esp32p4/chip_kernel_impl.c"
    starts at address 0x4ff03f4c <irc_begin_ipi>
```

FMP3 のチップ依存部・コア依存部のシンボルがソース行まで解決できる．

### 3.4 ★ FMP3 の text は IRAM 上にある

方式(a)では FMP3 の全 text を内部 RAM（IRAM）へ集める（`build_fmp3_lib.sh` が
`.text` を `.iram1.fmptext` へリネームする）．最終イメージでは **`.iram0.text`
（`0x4FF0_0000` 付近）**に配置され，**flash XIP ではなく RAM 実行**である．

- ブレークポイントは RAM 上のためハードウェア資源を消費せずソフトブレークが使える．
- 逆に，flash 上のコード（IDF ローダ殻の `.flash.text`）とはアドレス帯が異なる
  （`0x4000_0020` 付近）．`info symbol` の section 表示で区別できる．

### 3.5 ★ `info threads` は FMP3 のタスクを表示しない

OpenOCD の RTOS 対応が ESP-IDF の FreeRTOS を認識するため，`info threads` には
FreeRTOS 側のスレッド（`IDLE` / `main` 等）が並ぶ．**これは FMP3 のタスクではない．**

```
  Id   Target Id                                                   Frame
* 1    Thread … "IDLE" (Name: IDLE)                                sil_dly_nse1 () at …
  2    Thread … "main" (Name: main, State: Running @CPU0)          sil_dly_nse1 () at …
```

FMP3 のタスク状態を見るには，GDB からカーネルのデータ構造（TCB 等）を直接参照するか，
`syslog` 出力・アプリ側の計装を用いる．

### 3.6 デュアルコア（2 ハート）の扱い

両コアは halt group にまとめられ，片方の halt で両方が止まる．OpenOCD 側で
`monitor targets` を用いるとターゲット一覧と現在の選択が確認できる．

**既知の不具合**: 一部の openocd-esp32 で
`[esp32p4.hp.cpu1] Fatal: Failed to read s0`（openocd-esp32 Issue #377）が出る．
新しめの openocd-esp32 を用いること．

---

### 3.7 ★★ ブレークポイントはリセットを跨がせないこと（最重要）

**`monitor reset halt` の直後に張ったブレークポイントは，消えることがある．**

- **原因**: `monitor reset halt` の後，ブート途中でハートがリセットされ
  （OpenOCD が `Hart unexpectedly reset!` を出力），**RISC-V デバッグトリガ
  ＝ハードウェアブレークポイントが消える**．GDB が再挿入するより先に実行が
  当該番地を通過するとブレークせず走り去る．
- **実測（同一条件の反復）**:

  | 手順 | 結果 |
  |---|---|
  | `monitor reset halt` ＋ `thb toppers_start` ＋ `continue` | **3/5 でしか停止しない** |
  | リセットせず `monitor halt` で attach してから `break` | **5/5 で安定** |

  `flushregs` / `maintenance flush register-cache` /
  `set breakpoint always-inserted on` のいずれでも解消しなかった．
- **ブート自体は壊れていない**: `reset halt` → `resume` では
  `TOPPERS/FMP3 Kernel Release 3.4.0 ...` のバナーから `Processor 1/2 start.`，
  テスト完走までシリアルに出力される（実機で確認）．あくまでブレークポイントが
  消えるのが問題である．

したがって **§3.3 のとおり `monitor halt`（attach のみ・リセットしない）を既定とする**．
同梱の `gdb.ini` もこの方針で書かれている．

#### 起動（`toppers_start`）そのものを追いたい場合

方式(a)の起動連鎖は実測で次のとおり:

```
#0  toppers_start ()    at arch/riscv_gcc/common/start.S:66      ← FMP3 入口（.iram0.text, 0x4ff02840）
#1  app_main ()         at tools/fmp_loader/main/fmp_loader.c
#2  main_task ()        at esp-idf/components/freertos/app_startup.c
#3  vPortTaskWrapper () at esp-idf/.../portable/riscv/port.c
```

ここで停止させる手順は次のとおりだが，上記のレースにより**毎回は停止しない**
（実測 3/5）．**停止しなければ繰り返すこと．**

```
(gdb) monitor reset halt
(gdb) maintenance flush register-cache
(gdb) thb toppers_start
(gdb) continue
```

確実に停止させたい場合は，アプリ側に待ちループ（例: `volatile int dbg_wait = 1;
while (dbg_wait) ;`）を置き，attach 後にデバッガから `set var dbg_wait = 0` で
抜ける方法が確実である．

### 3.8 ハードウェアブレークポイントは 1 コア 3 個

OpenOCD の起動ログに `[esp32p4.hp.cpu0] Found 3 triggers` と出るとおり，
**RISC-V デバッグトリガは 1 コアあたり 3 個**しかない．

FMP3 の text は `.iram0.text`（RAM 実行）にあるため**通常の `break`
（ソフトウェアブレークポイント）が使える**（§3.4）．`hbreak` / `thb` は
flash XIP 上のコード（IDF ローダ殻の `.flash.text`）に限って使い，
3 個の枠を使い切らないようにすること．

### 3.9 Makefile からの起動

ビルドディレクトリでは次のターゲットが使える（`Makefile.target`）．
対象ボードは `SERIAL`（USB シリアル＝MAC）で指定する．

```sh
make jtaggdb SERIAL=30:ED:A0:EA:98:0E   # OpenOCD をパイプ内包した GDB（常駐不要・gdb.ini 適用）
make openocd SERIAL=30:ED:A0:EA:98:0E   # OpenOCD を前面起動（別端末から :3333 へ接続する場合）
make console SERIAL=30:ED:A0:EA:98:0E   # カーネル出力（USB-CDC シリアル）
make flash   SERIAL=30:ED:A0:EA:98:0E   # esptool で書込み
```

`SERIAL` を省略すると，Espressif 製デバイスが 1 台だけならそれを使い，
**複数あれば候補を列挙して中断する**（別ボードを誤って掴まないため）．

`make jtaggdb` は OpenOCD を GDB の子プロセスとして起動する（`gdb_port pipe`）ので
OpenOCD を別途常駐させる必要がない．手で同じことを行う場合:

```sh
riscv32-esp-elf-gdb \
  -ex 'target extended-remote | openocd -c "gdb_port pipe; log_output openocd.log" \
       -c "adapter serial 30:ED:A0:EA:98:0E" -f board/esp32p4-builtin.cfg' \
  -x <target>/gdb.ini \
  build/fmp_loader.elf
```

---

## 4. フラッシュ書込み（JTAG 経由）

通常の書込みは `idf.py flash`（esptool 経由・シリアル）で行う（target_user.md 参照）．
JTAG 経由で書き込む場合は openocd-esp32 独自コマンド `program_esp` を用いる．
オフセットは方式(a)のイメージ構成に合わせること（`build/flash_args` に記載される）．

```sh
openocd -c "adapter serial <MAC>" -f board/esp32p4-builtin.cfg \
  -c "program_esp build/fmp_loader.bin 0x10000 verify reset exit"
```

ブートローダ・パーティションテーブルも含めて書く場合は，それぞれ `0x2000` /
`0x8000` へ書き込む（`build/flash_args` の内容と一致させる）．実機の
`build/flash_args` は次の内容であった．

```
--flash_mode dio --flash_freq 80m --flash_size 2MB
0x2000 bootloader/bootloader.bin
0x10000 fmp_loader.bin
0x8000 partition_table/partition-table.bin
```

**注**: 本書で実機確認したのは esptool 経由の書込み（`make flash`）であり，
上記 `program_esp` による JTAG 経由書込みは**未検証**である．

---

## 5. SMP デバッグ時の注意（実機で得た知見）

- **JTAG halt は livelock を解いてしまう**．IPI storm などによるハング状態を観測する
  目的では，halt した時点で現象が消えることがある．ハング判定には RAM 上のカウンタを
  用いた計装を併用すること．
- **CLINT はコアローカル**である．JTAG から他コアの CLINT レジスタを読むことはできない
  （自コア基準のアドレスとして解決されるため）．
- **CLIC のレジスタ領域（`0x2080_xxxx`）は OpenOCD の `mdw` / GDB のメモリリードで
  常に 0 を返す**という計測アーチファクトが確認されている．CLIC の状態は
  デバッガのメモリリードで確認せず，ターゲット上のコードで読んで出力すること．
- **リセットの種別で再現率が変わる**（Heisenbug）．`idf.py flash` に伴うフルリセットは
  完走しやすく，RTS リセットは wedge しやすい，という傾向が観測されている．

---

## 参考 URL

- ESP-IDF P4 JTAG Debugging:
  https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-guides/jtag-debugging/index.html
- ESP-IDF JTAG Tips and Quirks:
  https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/jtag-debugging/tips-and-quirks.html
- openocd-esp32 フォーク: https://github.com/espressif/openocd-esp32
- openocd-esp32 cpu1 read s0 エラー Issue #377:
  https://github.com/espressif/openocd-esp32/issues/377
- OpenOCD Flash Commands: https://openocd.org/doc/html/Flash-Commands.html

以上
