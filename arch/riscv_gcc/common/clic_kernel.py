# -*- coding: utf-8 -*-
#
#   TOPPERS/FMP Kernel
#       Flexible MultiProcessor Kernel
#
#   Copyright (C) 2024-2026 by Embedded and Real-Time Systems Laboratory
#               Graduate School of Information Science, Nagoya Univ., JAPAN
#
#   利用条件は TOPPERS ライセンス（plic_kernel.trb と同一）．無保証．
#
#  $Id: clic_kernel.py (converted from clic_kernel.trb by Claude Opus 5) $
#

#
#  パス2の生成スクリプトの割込みコントローラ依存部（ESP32-P4 CLIC 用）
#  plic_kernel.trb を置き換える．
#

#
#  CLIC 割込みターゲットコンテキストINDEXテーブル
#    CLIC では割込み優先度マスクは自コアのメモリマップドレジスタのため
#    本テーブルは実質未使用だが，I/F互換のため生成する．
#
kernelCfgC.comment_header("CLIC Interrupt target Context Index Table")
kernelCfgC.add("const uint8_t _kernel_clic_target_cidx_table[CLIC_TNUM_INTNO + 1] = {")
for index, intno in enumerate(clic_intno_list):
    if index > 0:
        kernelCfgC.add(",")
    kernelCfgC.append(f"\t/* 0x{intno:05d} */ ")
    if intno in cfgData["CFG_INT"]:
        kernelCfgC.append(
            f"{pid2cidx(clsData[cfgData['CFG_INT'][intno]['class']]['initPrc'])}U")
    else:
        kernelCfgC.append("0xffU")
kernelCfgC.add()
kernelCfgC.add2("};")


#
#  CFG_INTのターゲット依存のチェック
#
def TargetCheckCfgInt(params):
    if ((params["intno"] >> 16) == 0) \
            and (clsData[params["class"]]["affinityPrcBitmap"]
                 != (1 << (clsData[params["class"]]["initPrc"] - 1))):
        error_ercd("E_RSATR", params, "%%intno is configured "
                   "to be accepted by more than one processors, "
                   "which is not supported on this target.")
