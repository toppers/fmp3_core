# m5stamp_esp32p4_gcc ツール（方式(a)：ESP-IDF 静的リンク＋実機ローダ）

ESP32-P4（M5Stamp ESP32P4）で TOPPERS/FMP3 を実機ビルド・書込み・テストするための
ツール一式．FMP3 を静的ライブラリ（libfmp3.a）にまとめ，ESP-IDF アプリへリンクして
実機（HP RISC-V 2コア）で動かす「方式(a)」を実装する．

## 構成

	tools/
		fmp_loader/			ESP-IDF プロジェクト（FMP3 を取り込むローダ）
			CMakeLists.txt		IDF プロジェクト定義
			main/
				CMakeLists.txt	libfmp3.a のビルド（build_fmp3_lib.sh 呼出し）と
								リンク．app_main が toppers_start を参照して芋づる
								式に FMP3 をリンクする
				fmp_loader.c	IDF app_main（FMP3 を起動）
			sdkconfig / sdkconfig.defaults
			build_fmp3_lib.sh	FMP3(configure.rb/make) を libfmp3.a にまとめる．
								全 text を IRAM へ回すリネームを施す
			run_fmp_test.sh		FMP3 の test/（test_*.c）を実機で走らせる runner
			run_ttsp_api.sh		本物の TTSP3 適合性テストを実機で走らせる runner
		fmp_app/				最小周期タスクアプリ（first light 用．dly_tsk で周期
							syslog → タイマ tick/スケジューラ/logtask を end-to-end 検証）
		fmp_hwlp_app/			HWLP（ハードウェアループ）コプロセッサのコンテキスト save/restore
							実証アプリ（設計は pie_hwlp_design.md）
		fmp_perf1v_app/			perf1（タスク切換え時間計測．test/perf1.c 改造）のベクタ命令
							使用版．切換え直後に PIE ベクタ命令を1回使う
		fmp_pie_app/			PIE（ベクタ）コプロセッサのコンテキスト save/restore（eager方式）
							実証アプリ
		fmp_pie_mig2_app/		lazy PIE の他タスク移行（pattern B/C/D）を跨いだコンテキスト
							正当性の実証アプリ
		fmp_pie_mig_app/		lazy PIE の mig_tsk（自タスク移行）を跨いだコンテキスト正当性の
							実証アプリ
		fmp_pie_perf_app/		lazy PIE の非オーナ初回ベクタ命令における例外処理時間の
							計測アプリ
		fmp_pie_raster_app/		lazy PIE における ras_ter（強制終了要求）経由の終了処理の
							release_context 正当性の実証アプリ
		fmp_pie_term_app/		lazy PIE におけるタスク終了時の所有権解放（release_context）
							正当性の実証アプリ
		fmp_tls_app/			TLS（スレッドローカルストレージ）初期化の検証アプリ．picolibc
							rand() の種が各タスクで独立していることを確認

FMP3 ルート（`<fmp3>`）は各スクリプトが自身の位置から導出する（自己完結）．
本ツールは `<fmp3>/target/m5stamp_esp32p4_gcc/tools/` に置かれる前提．

## 前提

- ESP-IDF（v5.5 系で確認）．ツールチェーン・GDB・OpenOCD は IDF の `install.sh` で
  導入される．導入手順は `../target_user.md`「開発環境の準備」を参照．
  - **`IDF_TOOLS_PATH` は導入時と使用時で必ず同じ値にすること**（食い違うと IDF が
    別の Python 仮想環境を参照し，ビルドが正常に走らない）．未設定時の既定は `~/.espressif`．
- Ruby（FMP3 の configure.rb / *.trb 用）．

## ビルド・実機実行

```sh
. $IDF_PATH/export.sh

cd <fmp3>/target/m5stamp_esp32p4_gcc/tools/fmp_loader
idf.py build                 # libfmp3.a(方式a) をビルドし IDF アプリへリンク
idf.py -p /dev/ttyACM0 flash monitor
```

**書込み先のポートは必ず対象ボードのものを指定する．** `/dev/ttyACM<N>` の番号は
接続順で変わるため，複数ボードを接続している場合はシリアル（＝MAC 文字列）で同定する．

```sh
for d in /dev/ttyACM*; do
  echo "$d $(udevadm info -q property -n $d | grep -oE 'ID_SERIAL_SHORT=[^ ]*' | cut -d= -f2)"
done
```

`idf.py build` の一部として `build_fmp3_lib.sh` が走り，`fmp_app` を FMP3 へ静的リンク
した `fmp3_build/libfmp3.a` を生成する（main/CMakeLists.txt の add_custom_command）．

## テスト

```sh
cd <fmp3>/target/m5stamp_esp32p4_gcc/tools/fmp_loader
./run_fmp_test.sh 2 test_mtrans2 test_raster2 ...   # PRC_NUM=2 で SMP テスト
```

- PASS マーカー: `All check points passed.`（計測系は `TTSP_RESULT: DONE`）
- FAIL マーカー: `## Unexpected` / `## Assertion` / `TTSP_RESULT: FAIL`
- SMP の canary は `mtrans2`/`raster2`（dispatch-IPI storm による livelock を露呈）．
  intermittent なため `mtrans2` は複数回回すこと（チップ依存部の知見は arch の
  chip_design.md，真因と対策は mtrans2_lost_wakeup_analysis.md 参照）．

**★ 注意: `run_fmp_test.sh` は書込み先・モニタ先を `/dev/ttyACM0` に固定している**
（環境変数での上書き手段は無い）．対象ボードが別のポートに現れる環境では，実行前に
スクリプトの該当箇所を書き換えること．

本物の TTSP3 適合性は `run_ttsp_api.sh`（外部 TTSP3 スイートを `TTSP3_DIR` で指定）．

利用手順は `../target_user.md`，実機デバッグ（JTAG/GDB）は
`../esp32p4_openocd_jtag.md`，イメージ統合の詳細は `../idf_image_integration.md`，
CLIC やチップ固有の知見は `arch/riscv_gcc/esp32p4/chip_design.md`，
`arch/riscv_gcc/doc/clic_design.md` を参照．
