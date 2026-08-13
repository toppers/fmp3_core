# -*- coding: utf-8 -*-
#
#    パス2の生成スクリプトのチップ依存部（ESP32-P4 用）
#
#  $Id: chip_kernel.py (converted from chip_kernel.trb by Claude Opus 5) $
#

#
#  使用できる割込み番号とそれに対応する割込みハンドラ番号
#
INTNO_VALID = {}
INHNO_VALID = {}
#  CLIC 線番号 0..47（CLIC_TNUM_INTNO=48 に対応）
clic_intno_list = list(range(0, 48))
for prcid in range(1, TNUM_PRCID + 1):
    INTNO_VALID[prcid] = []
    INHNO_VALID[prcid] = []
    for intno in clic_intno_list:
        INTNO_VALID[prcid].append(intno)
        INHNO_VALID[prcid].append((prcid << 16) | intno)


#
#  プロセッサID(1オリジン)からコンテキストINDEXへの変換
#    ESP32-P4: cidx = mhartid = prcid - 1（master=0）
#
def pid2cidx(pid):
    return (pid - 1)


#
#  生成スクリプトのコア依存部
#
IncludeTrb("core_kernel.py")

#
#  生成スクリプトのCLIC依存部
#
IncludeTrb("clic_kernel.py")
