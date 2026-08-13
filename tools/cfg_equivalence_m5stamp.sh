#!/usr/bin/env bash
#
#	cfg_equivalence_m5stamp.sh -- m5stamp_esp32p4_gcc の cfg 差分等価性検査
#
#  tools/cfg_equivalence.sh は CMake(ninja) の build ディレクトリから cfg の
#  コマンド行を取り出すが、m5stamp_esp32p4_gcc にはまだ CMake 層
#  （target.cmake / presets.json）が無い（Phase 1 の作業）。そこで本スクリプトは
#  classic フロー（configure.rb + make）で一度ビルドして artifacts
#  （cfg1_out.syms/.srec, fmp.syms/.srec）を作り、その上で
#    Ruby cfg（pristine cfg/cfg.rb + *.trb、オラクル）
#    Python cfg（cfg_py/cfg.py + *.py）
#  を独立に走らせて cfg1_out.c / kernel_cfg.c / kernel_cfg.h / offset.h を
#  バイト比較する。CMake 層ができたら本スクリプトは役目を終えて
#  tools/cfg_equivalence.sh へ一本化してよい。
#
#  使い方:
#    tools/cfg_equivalence_m5stamp.sh [workdir]          # 等価性検査
#    tools/cfg_equivalence_m5stamp.sh [workdir] --selftest
#        比較器が本当に差を検出することの実演（.py を 6 通りに壊して
#        すべて MISMATCH になることを確かめる。always-pass でないことの証拠）
#
#  前提:
#    - riscv32-esp-elf-gcc が PATH にあること（ESP-IDF 同梱のもの。
#      例: export PATH=$HOME/tools/espressif/tools/riscv32-esp-elf/\
#            esp-14.2.0_20241119/riscv32-esp-elf/bin:$PATH）
#    - ruby と python3
#
#  終了コード: 0=一致 / 1=不一致 / 2=実行前提が満たされていない
#
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK="${1:-${TMPDIR:-/tmp}/cfgeq_m5stamp}"
SELFTEST="${2:-}"
TDIR="${ROOT}/target/m5stamp_esp32p4_gcc"
ADIR="${ROOT}/arch/riscv_gcc"

command -v riscv32-esp-elf-gcc >/dev/null || {
    echo "riscv32-esp-elf-gcc が PATH にありません（ヘッダのコメント参照）" >&2; exit 2; }
command -v ruby >/dev/null    || { echo "ruby が要ります" >&2; exit 2; }
command -v python3 >/dev/null || { echo "python3 が要ります" >&2; exit 2; }

BUILD="${WORK}/classic"
INC="-I. -I${ROOT}/include -I${TDIR} -I${ADIR}/esp32p4 -I${ADIR}/common
     -I${ROOT}/arch/gcc -I${ROOT} -I${TDIR}/tools/fmp_app -I./gen -I${ROOT}/tecs_kernel"

#  classic ビルド（両パイプラインが借りる .syms/.srec を作るためだけに使う）
build_classic() {
    rm -rf "${BUILD}"; mkdir -p "${BUILD}"
    ( cd "${BUILD}" && ruby "${ROOT}/configure.rb" -T m5stamp_esp32p4_gcc -D "${ROOT}" -w \
        -S "syslog.o banner.o serial.o serial_cfg.o logtask.o chip_serial.o" \
        -a "${TDIR}/tools/fmp_app" -A fmp_app PRC_NUM=2 \
        -O "-DTOPPERS_OMIT_BSS_INIT -DTOPPERS_OMIT_DATA_INIT" \
      && make SECTION_OPTS="-fno-function-sections" ) > "${WORK}/classic.log" 2>&1
    return $?
}

#  片側のパイプラインを走らせる。$1 = ruby|py
run_engine() {
    local e="$1" dir="${WORK}/$1" eng tc rc
    rm -rf "${dir}"; mkdir -p "${dir}/objs" "${dir}/gen"
    cp "${BUILD}"/cfg1_out.syms "${BUILD}"/cfg1_out.srec \
       "${BUILD}"/fmp.syms "${BUILD}"/fmp.srec "${dir}/"
    if [ "$e" = ruby ]; then eng="ruby ${ROOT}/cfg/cfg.rb"; tc=trb
    else eng="python3 -B ${ROOT}/cfg_py/cfg.py"; tc=py; fi
    cd "${dir}" || return 9
    $eng --pass 1 --kernel fmp ${INC} \
        --api-table "${ROOT}/kernel/kernel_api.def" \
        --symval-table "${ROOT}/kernel/kernel_sym.def" \
        --symval-table "${ADIR}/common/core_sym.def" -M objs/cfg1_out_c.d \
        "${TDIR}/target_kernel.cfg" "${TDIR}/tools/fmp_app/fmp_app.cfg" >> log.txt 2>&1
    echo "pass1_rc=$?" >> rc.txt
    $eng --pass 2 --kernel fmp ${INC} \
        -C "${TDIR}/target_class.${tc}" -T "${TDIR}/target_kernel.${tc}" >> log.txt 2>&1
    echo "pass2_rc=$?" >> rc.txt
    $eng --pass 2 -O --kernel fmp ${INC} \
        -C "${TDIR}/target_class.${tc}" -T "${ADIR}/common/core_offset.${tc}" \
        --rom-symbol cfg1_out.syms --rom-image cfg1_out.srec >> log.txt 2>&1
    echo "pass2O_rc=$?" >> rc.txt
    $eng --pass 3 --kernel fmp -O ${INC} \
        -T "${TDIR}/target_check.${tc}" \
        --rom-symbol fmp.syms --rom-image fmp.srec >> log.txt 2>&1
    echo "pass3_rc=$?" >> rc.txt
    return 0
}

compare() {
    local fail=0
    if ! diff -q "${WORK}/ruby/rc.txt" "${WORK}/py/rc.txt" >/dev/null; then
        echo "  [DIFF] 終了コード列"; diff "${WORK}/ruby/rc.txt" "${WORK}/py/rc.txt"; fail=1
    else
        echo "  [OK]   終了コード列 ($(tr '\n' ' ' < "${WORK}/ruby/rc.txt"))"
    fi
    local f
    for f in cfg1_out.c kernel_cfg.c kernel_cfg.h offset.h; do
        if [ ! -f "${WORK}/ruby/$f" ] || [ ! -f "${WORK}/py/$f" ]; then
            echo "  [MISSING] $f (ruby=$([ -f "${WORK}/ruby/$f" ] && echo y || echo n) py=$([ -f "${WORK}/py/$f" ] && echo y || echo n))"
            fail=1; continue
        fi
        if cmp -s "${WORK}/ruby/$f" "${WORK}/py/$f"; then
            echo "  [OK]   $f ($(stat -c%s "${WORK}/ruby/$f") bytes)"
        else
            echo "  [DIFF] $f"; diff "${WORK}/ruby/$f" "${WORK}/py/$f" | head -20; fail=1
        fi
    done
    return $fail
}

one_shot() { run_engine ruby; run_engine py; compare; }

mkdir -p "${WORK}"
echo "cfg_equivalence_m5stamp.sh: work dir = ${WORK}"
build_classic || { echo "classic ビルドに失敗（${WORK}/classic.log）" >&2; exit 2; }
echo "== 等価性検査 =="
if one_shot; then echo "RESULT = MATCH"; RC=0; else echo "RESULT = MISMATCH"; RC=1; fi

if [ "${SELFTEST}" = "--selftest" ]; then
    echo
    echo "== selftest（比較器が差を検出することの実演）=="
    st_fail=0
    #  「対象ファイル|sed式」。すべて MISMATCH になれば比較器は生きている。
    for spec in \
        "${ADIR}/common/clic_kernel.py|s/0xffU/0xfeU/" \
        "${ADIR}/esp32p4/chip_kernel.py|s/range(0, 48)/range(0, 47)/" \
        "${ADIR}/esp32p4/chip_kernel.py|s/return (pid - 1)\$/return (pid)/" \
        "${TDIR}/target_kernel.py|s/IncludeTrb(\"chip_kernel.py\")/IncludeTrb(\"chip_kernel_BOGUS.py\")/" \
        "${TDIR}/target_class.py|0,/clsData/s/clsData/clsDataX/" \
        "${TDIR}/target_check.py|s/IncludeTrb(\"core_check.py\")/IncludeTrb(\"core_check_BOGUS.py\")/" \
    ; do
        f="${spec%%|*}"; e="${spec#*|}"
        cp "$f" "$f.cfgeqbak"
        sed -i "$e" "$f"
        if cmp -s "$f" "$f.cfgeqbak"; then
            echo "  [BAD] 変異が空振り（control 不成立）: $f : $e"; st_fail=1
            mv "$f.cfgeqbak" "$f"; continue
        fi
        if one_shot >/dev/null 2>&1; then
            echo "  [BAD] 変異しても MATCH（見逃し）: $(basename "$f") : $e"; st_fail=1
        else
            echo "  [OK]  変異を検出: $(basename "$f") : $e"
        fi
        mv "$f.cfgeqbak" "$f"
    done
    #  変異を戻したあとに再び MATCH になることまで確かめる
    if one_shot >/dev/null 2>&1; then echo "  [OK]  復元後に MATCH"; else echo "  [BAD] 復元後も MISMATCH"; st_fail=1; fi
    [ $st_fail -eq 0 ] && echo "SELFTEST = ALL PASS" || { echo "SELFTEST = FAIL"; RC=1; }
fi
exit ${RC}
