/*
 *		AID_xxx 混在構成の cfg 等価性検査用サンプル
 */

#include <kernel.h>
#include "target_test.h"

#define MID_PRIORITY	10

#ifndef STACK_SIZE
#define	STACK_SIZE		4096
#endif /* STACK_SIZE */

#ifndef MPK_SIZE
#define MPK_SIZE		4096
#endif /* MPK_SIZE */

#ifndef TOPPERS_MACRO_ONLY
extern void	task1(EXINF exinf);
#endif /* TOPPERS_MACRO_ONLY */

/*  混在構成に載せる静的ISRとENA_DYNISRの対象（INTNO1はtarget_test.hが定義）  */
#ifndef TOPPERS_MACRO_ONLY
extern void	mix_isr1(EXINF exinf);
#endif /* TOPPERS_MACRO_ONLY */
