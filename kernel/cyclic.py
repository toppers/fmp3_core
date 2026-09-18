# -*- coding: utf-8 -*-
#
#   TOPPERS/FMP Kernel
#       Toyohashi Open Platform for Embedded Real-Time Systems/
#       Flexible MultiProcessor Kernel
# 
#   Copyright (C) 2015 by FUJI SOFT INCORPORATED, JAPAN
#   Copyright (C) 2015-2021 by Embedded and Real-Time Systems Laboratory
#               Graduate School of Information Science, Nagoya Univ., JAPAN
# 
#   上記著作権者は，以下の(1)〜(4)の条件を満たす場合に限り，本ソフトウェ
#   ア（本ソフトウェアを改変したものを含む．以下同じ）を使用・複製・改
#   変・再配布（以下，利用と呼ぶ）することを無償で許諾する．
#   (1) 本ソフトウェアをソースコードの形で利用する場合には，上記の著作
#       権表示，この利用条件および下記の無保証規定が，そのままの形でソー
#       スコード中に含まれていること．
#   (2) 本ソフトウェアを，ライブラリ形式など，他のソフトウェア開発に使
#       用できる形で再配布する場合には，再配布に伴うドキュメント（利用
#       者マニュアルなど）に，上記の著作権表示，この利用条件および下記
#       の無保証規定を掲載すること．
#   (3) 本ソフトウェアを，機器に組み込むなど，他のソフトウェア開発に使
#       用できない形で再配布する場合には，次のいずれかの条件を満たすこ
#       と．
#     (a) 再配布に伴うドキュメント（利用者マニュアルなど）に，上記の著
#         作権表示，この利用条件および下記の無保証規定を掲載すること．
#     (b) 再配布の形態を，別に定める方法によって，TOPPERSプロジェクトに
#         報告すること．
#   (4) 本ソフトウェアの利用により直接的または間接的に生じるいかなる損
#       害からも，上記著作権者およびTOPPERSプロジェクトを免責すること．
#       また，本ソフトウェアのユーザまたはエンドユーザからのいかなる理
#       由に基づく請求からも，上記著作権者およびTOPPERSプロジェクトを
#       免責すること．
# 
#   本ソフトウェアは，無保証で提供されているものである．上記著作権者お
#   よびTOPPERSプロジェクトは，本ソフトウェアに関して，特定の使用目的
#   に対する適合性も含めて，いかなる保証も行わない．また，本ソフトウェ
#   アの利用により直接的または間接的に生じたいかなる損害に関しても，そ
#   の責任を負わない．
# 
#  $Id: cyclic.py (converted from cyclic.trb by Claude Code Sonnet 4.6) $
# 

#
#		周期通知機能の生成スクリプト
#

class CyclicObject(KernelObject):
    def __init__(self):
        super().__init__("cyc", "cyclic")
        self.inibList["T_NFYINFO"] = "acyc_nfyinfo_table"

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

    def prepare(self, key, params):
        # cycatrが無効の場合（E_RSATR）
        if (params["cycatr"] & ~(TA_STA)) != 0:
            error_illegal_id("E_RSATR", params, "cycatr", "cycid")

        # cyctimが有効範囲外の場合（E_PAR）
        if not (0 < params["cyctim"] and params["cyctim"] <= TMAX_RELTIM):
            error_illegal_id("E_PAR", params, "cyctim", "cycid")

        # cycphsが有効範囲外の場合（E_PAR）
        if not (0 <= params["cycphs"] and params["cycphs"] <= TMAX_RELTIM):
            error_illegal_id("E_PAR", params, "cycphs", "cycid")

        # 通知情報の処理
        params["nfyhdr"] = f"_kernel_cychdr_{params['cycid']}"
        generateNotifyHandler(key, params, "cycid")

        # 割り付け可能プロセッサが時刻管理プロッサのみでない場合（E_RSATR）
        if ((clsData[params["class"]]["affinityPrcBitmap"] | TOPPERS_TEPP_PRC)
                != TOPPERS_TEPP_PRC):
            error_illegal_id("E_RSATR", params, "class", "cycid")

    def generateInib(self, key, params):
        cls = clsData[params["class"]]
        return (f"({params['cycatr']}), "
                f"(intptr_t)({params['par1']}), {params['nfyhdr']}, "
                f"({params['cyctim']}), ({params['cycphs']}), "
                f"{cls['initPrc']}, "
                f"0x{cls['affinityPrcBitmap']:x}")


#
#  周期通知に関する情報の生成
#
kernelCfgC.comment_header("Cyclic Notification Functions")
CyclicObject().generate()
