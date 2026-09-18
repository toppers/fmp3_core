# -*- coding: utf-8 -*-
#
#   TOPPERS/FMP Kernel
#       Toyohashi Open Platform for Embedded Real-Time Systems/
#       Flexible MultiProcessor Kernel
# 
#   Copyright (C) 2015 by FUJI SOFT INCORPORATED, JAPAN
#   Copyright (C) 2015-2019 by Embedded and Real-Time Systems Laboratory
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
#  $Id: task.py (converted from task.trb by Claude Code Sonnet 4.6) $
# 

#
#		タスク管理モジュールの生成スクリプト
#

class TaskObject(KernelObject):
    def __init__(self):
        super().__init__("tsk", "task", "t")

    def prepare(self, key, params):
        params.setdefault("stk", "NULL")

        # tskatrが無効の場合（E_RSATR）
        if (params["tskatr"]
                & ~(TA_ACT | TA_NOACTQUE | TARGET_TSKATR)) != 0:
            error_illegal_id("E_RSATR", params, "tskatr", "tskid")

        # itskpriが有効範囲外の場合（E_PAR）
        if not (TMIN_TPRI <= params["itskpri"]
                and params["itskpri"] <= TMAX_TPRI):
            error_illegal_id("E_PAR", params, "itskpri", "tskid")

        # stkszが小さすぎる場合（E_PAR）
        if params["stksz"] < TARGET_MIN_STKSZ:
            error_wrong_id("E_PAR", params, "stksz", "tskid", "too small")

        # スタック領域の設定
        if params["stk"] == "NULL":
            stk_name = f"_kernel_stack_{params['tskid']}"
            stk_secname = SecnameStack(params["class"])
            params["tinib_stksz"] = AllocStack(
                stk_name, params["stksz"], stk_secname)
            params["tinib_stk"] = stk_name
        else:
            # stkがNULLでない場合
            if (params["stksz"] & (CHECK_STKSZ_ALIGN - 1)) != 0:
                error_wrong_id("E_PAR", params, "stksz", "tskid",
                               "not aligned")
            params["tinib_stksz"] = params["stksz"]
            params["tinib_stk"] = f"(void *)({params['stk']})"

        # ターゲット依存の処理
        if "TargetTaskPrepare" in globals():
            TargetTaskPrepare(key, params)

    def generateInib(self, key, params):
        if USE_TSKINICTXB:
            tskinictxb = GenerateTskinictxb(key, params)
        else:
            tskinictxb = (f"{params['tinib_stksz']}, "
                          f"{params['tinib_stk']}")
        cls = clsData[params["class"]]
        return (f"({params['tskatr']}), (EXINF)({params['exinf']}), "
                f"(TASK)({params['task']}), "
                f"INT_PRIORITY({params['itskpri']}), "
                f"{tskinictxb}, "
                f"{cls['initPrc']}, "
                f"0x{cls['affinityPrcBitmap']:x}")


# タスクが1つも登録されていない場合
if len(cfgData["CRE_TSK"]) == 0:
    error("no task is registered")

#
#  タスク管理に関する情報の生成
#
kernelCfgC.comment_header("Task Management Functions")
TaskObject().generate()

# タスク生成順序テーブルの生成
# TNUM_STSKID は kernel_api.def に AID_TSK が登録されているとき KernelObject.generate
# が常に出力する（恒常出力）。AID_TSK の行を .def から外すとここが未定義参照になる。
kernelCfgC.add("const ID _kernel_torder_table[TNUM_STSKID] = { ")
kernelCfgC.append("\t")
for index, (_, params) in enumerate(cfgData["CRE_TSK"].items()):
    if index > 0:
        kernelCfgC.append(", ")
    kernelCfgC.append(f"{params['tskid']}")
kernelCfgC.add()
kernelCfgC.add2("};")

#
#  ENA_SPRの処理
#
subprio_tskpri = {}
for _, params in cfgData["ENA_SPR"].items():
    tskpri_val = params["tskpri"].val

    # (TMIN_TPRI <= tskpri && tskpri <= TMAX_TPRI)でない場合
    if not (TMIN_TPRI <= tskpri_val and tskpri_val <= TMAX_TPRI):
        error_illegal("E_PAR", params, "tskpri")

    if tskpri_val not in subprio_tskpri:
        subprio_tskpri[tskpri_val] = params["tskpri"]
    else:
        warning_api(params,
                    f"tskpri `{params['tskpri']}' in ENA_SPR"
                    f" is same with {subprio_tskpri[tskpri_val]}")

    # クラスの囲みの中に記述されている場合
    if "class" in params:
        error_illegal_id("E_RSATR", params, "class", "tskpri")

# サブ優先度を用いてスケジュールする優先度のビットマップ
kernelCfgC.append("const uint16_t _kernel_subprio_primap = ")
if len(subprio_tskpri) > 0:
    kernelCfgC.append("(")
    for index, (_, tskpri) in enumerate(subprio_tskpri.items()):
        if index > 0:
            kernelCfgC.append(" | ")
        kernelCfgC.append(f"PRIMAP_BIT(INT_PRIORITY({tskpri}))")
    kernelCfgC.add2(");")
else:
    kernelCfgC.add2("0U;")
