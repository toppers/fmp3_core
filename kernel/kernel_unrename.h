/* This file is generated from kernel_rename.def by genrename. */

/* This file is included only when kernel_rename.h has been included. */
#ifdef TOPPERS_KERNEL_RENAME_H
#undef TOPPERS_KERNEL_RENAME_H

/*
 *  startup.c
 */
#undef barrier_sync
#undef exit_kernel
#undef dispatch_handler
#undef ext_ker_handler
#undef mpk_valid
#undef initialize_mempool
#undef malloc_mempool
#undef aligned_alloc_mempool
#undef free_mempool

/*
 *  task.c
 */
#undef free_tcb
#undef tmax_stskid
#undef atinib_table
#undef initialize_task
#undef search_schedtsk
#undef update_schedtsk_dsp
#undef make_runnable
#undef make_non_runnable
#undef make_dormant
#undef make_active
#undef change_priority
#undef change_subprio
#undef rotate_ready_queue
#undef task_terminate
#undef migrate_self
#undef migrate_activate_self

/*
 *  taskhook.c
 */
#undef mtxhook_check_ceilpri
#undef mtxhook_release_all

/*
 *  wait.c
 */
#undef make_wait_tmout
#undef wait_dequeue_wobj
#undef wait_complete
#undef wait_tmout
#undef wait_tmout_ok
#undef wobj_make_wait
#undef wobj_make_wait_tmout
#undef init_wait_queue

/*
 *  time_event.c
 */
#undef boundary_evttim
#undef current_evttim
#undef current_hrtcnt
#undef monotonic_evttim
#undef systim_offset
#undef initialize_tmevt
#undef tmevt_up
#undef tmevt_down
#undef update_current_evttim
#undef set_hrt_event
#undef set_hrt_event_handler
#undef tmevtb_register
#undef tmevtb_enqueue
#undef tmevtb_enqueue_reltim
#undef tmevtb_dequeue
#undef check_adjtim
#undef tmevt_lefttim
#undef signal_time

/*
 *  semaphore.c
 */
#undef initialize_semaphore
#undef free_semcb
#undef tmax_ssemid
#undef aseminib_table

/*
 *  eventflag.c
 */
#undef initialize_eventflag
#undef check_flg_cond
#undef free_flgcb
#undef tmax_sflgid
#undef aflginib_table

/*
 *  dataqueue.c
 */
#undef initialize_dataqueue
#undef enqueue_data
#undef force_enqueue_data
#undef dequeue_data
#undef send_data
#undef force_send_data
#undef receive_data
#undef free_dtqcb
#undef tmax_sdtqid
#undef adtqinib_table

/*
 *  pridataq.c
 */
#undef initialize_pridataq
#undef enqueue_pridata
#undef dequeue_pridata
#undef send_pridata
#undef receive_pridata
#undef free_pdqcb
#undef tmax_spdqid
#undef apdqinib_table

/*
 *  mutex.c
 */
#undef initialize_mutex
#undef mutex_check_ceilpri
#undef mutex_acquire
#undef mutex_release
#undef mutex_release_all
#undef free_mtxcb
#undef tmax_smtxid
#undef amtxinib_table

/*
 *  mempfix.c
 */
#undef initialize_mempfix
#undef get_mpf_block
#undef free_mpfcb
#undef tmax_smpfid
#undef ampfinib_table

/*
 *  spin_lock.c
 */
#undef initialize_spin_lock
#undef force_unlock_spin

/*
 *  time_manage.c
 */
#undef check_nfyinfo
#undef notify_handler

/*
 *  cyclic.c
 */
#undef initialize_cyclic
#undef call_cyclic
#undef free_cyccb
#undef tmax_scycid
#undef acycinib_table
#undef acyc_nfyinfo_table

/*
 *  alarm.c
 */
#undef initialize_alarm
#undef call_alarm
#undef free_almcb
#undef tmax_salmid
#undef aalminib_table
#undef aalm_nfyinfo_table

/*
 *  interrupt.c
 */
#undef initialize_interrupt
#undef initialize_isr
#undef call_isr
#undef free_isrcb

/*
 *  exception.c
 */
#undef initialize_exception

/*
 *  kernel_cfg.c
 */
#undef kerflg_table
#undef p_pcb_table
#undef initialize_object
#undef inirtnbb_table
#undef terrtnbb_table
#undef subprio_primap
#undef tmax_tskid
#undef tinib_table
#undef torder_table
#undef p_tcb_table
#undef tmax_semid
#undef seminib_table
#undef p_semcb_table
#undef tmax_flgid
#undef flginib_table
#undef p_flgcb_table
#undef tmax_dtqid
#undef dtqinib_table
#undef p_dtqcb_table
#undef tmax_pdqid
#undef pdqinib_table
#undef p_pdqcb_table
#undef tmax_mtxid
#undef mtxinib_table
#undef p_mtxcb_table
#undef tmax_mpfid
#undef mpfinib_table
#undef p_mpfcb_table
#undef tmax_spnid
#undef spninib_table
#undef p_spncb_table
#undef tmax_cycid
#undef cycinib_table
#undef p_cyccb_table
#undef tmax_almid
#undef alminib_table
#undef p_almcb_table
#undef tmax_isrid
#undef tmax_sisrid
#undef isrinib_table
#undef aisrinib_table
#undef p_isrcb_table
#undef isrorder_table
#undef tnum_isr_queue
#undef isr_queue_list
#undef isr_queue_table
#undef tnum_def_inhno
#undef inhinib_table
#undef tnum_cfg_intno
#undef intinib_table
#undef tnum_def_excno
#undef excinib_table
#undef p_tevtcb_table
#undef p_tmevt_heap_table
#undef istksz_table
#undef istk_table
#undef istkpt_table
#undef idstk_table
#undef idstkpt_table
#undef mpksz
#undef mpk


#include "target_unrename.h"

#endif /* TOPPERS_KERNEL_RENAME_H */
