/* -*- mode: c; c-basic-offset: 8; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) 2004 Cluster File Systems, Inc.
 *   Author: Jacob Berkman <jacob@clusterfs.com>
 *
 *   This file is part of Lustre, http://www.lustre.org.
 *
 *   Lustre is free software; you can redistribute it and/or
 *   modify it under the terms of version 2 of the GNU General Public
 *   License as published by the Free Software Foundation.
 *
 *   Lustre is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with Lustre; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#define DEBUG_SUBSYSTEM S_PORTALS

#include <libcfs/kp30.h>
#include <libcfs/libcfs.h>
#include <libcfs/linux/portals_compat25.h>



struct lc_watchdog {
        struct timer_list lcw_timer; /* kernel timer */
        struct list_head  lcw_list;
        struct timeval    lcw_last_touched;
        struct task_struct *lcw_task;

        void (*lcw_callback)(struct lc_watchdog *,
			     struct task_struct *,
			     void *data);
        void *lcw_data;

        int lcw_pid;
        int lcw_time; /* time until watchdog fires, in ms */

        enum {
                LC_WATCHDOG_DISABLED,
                LC_WATCHDOG_ENABLED,
                LC_WATCHDOG_EXPIRED
        } lcw_state;
};

/*
 * The dispatcher will complete lcw_start_completion when it starts,
 * and lcw_stop_completion when it exits.
 * Wake lcw_event_waitq to signal timer callback dispatches.
 */
static struct completion lcw_start_completion;
static struct completion lcw_stop_completion;
static wait_queue_head_t lcw_event_waitq;

/*
 * Set this and wake lcw_event_waitq to stop the dispatcher.
 */
enum {
        LCW_FLAG_STOP = 0
};
static unsigned long lcw_flags = 0;

/*
 * Number of outstanding watchdogs.
 * When it hits 1, we start the dispatcher.
 * When it hits 0, we stop the distpatcher.
 */
static __u32         lcw_refcount = 0;
static DECLARE_MUTEX(lcw_refcount_sem);

/*
 * List of timers that have fired that need their callbacks run by the
 * dispatcher.
 */
static spinlock_t       lcw_pending_timers_lock = SPIN_LOCK_UNLOCKED;
static struct list_head lcw_pending_timers = \
        LIST_HEAD_INIT(lcw_pending_timers);

static struct task_struct *lcw_lookup_task(struct lc_watchdog *lcw)
{
        struct task_struct *tsk;
        unsigned long flags;
        ENTRY;

        read_lock_irqsave(&tasklist_lock, flags);
        tsk = find_task_by_pid(lcw->lcw_pid);
        read_unlock_irqrestore(&tasklist_lock, flags);
        if (!tsk) {
                CWARN("Process %d was not found in the task list; "
                      "watchdog callback may be incomplete\n", lcw->lcw_pid);
        } else if (tsk != lcw->lcw_task) {
                tsk = NULL;
                CWARN("The current process %d did not set the watchdog; "
                      "watchdog callback may be incomplete\n", lcw->lcw_pid);
        }

        RETURN(tsk);
}

static void lcw_cb(unsigned long data)
{
        struct lc_watchdog *lcw = (struct lc_watchdog *)data;
        struct task_struct *tsk;
        unsigned long flags;

        ENTRY;

        if (lcw->lcw_state != LC_WATCHDOG_ENABLED) {
                EXIT;
                return;
        }

        lcw->lcw_state = LC_WATCHDOG_EXPIRED;

        CWARN("Watchdog triggered for pid %d: it was inactive for %dus\n",
              lcw->lcw_pid, (lcw->lcw_time * 1000) / HZ);

        tsk = lcw_lookup_task(lcw);
        if (tsk != NULL)
                portals_debug_dumpstack(tsk);

        spin_lock_irqsave(&lcw_pending_timers_lock, flags);
        if (list_empty(&lcw->lcw_list)) {
                list_add(&lcw->lcw_list, &lcw_pending_timers);
                wake_up(&lcw_event_waitq);
        }
        spin_unlock_irqrestore(&lcw_pending_timers_lock, flags);

        EXIT;
}

static int is_watchdog_fired(void)
{
        unsigned long flags;
        int rc;

        if (test_bit(LCW_FLAG_STOP, &lcw_flags))
                return 1;

        spin_lock_irqsave(&lcw_pending_timers_lock, flags);
        rc = !list_empty(&lcw_pending_timers);
        spin_unlock_irqrestore(&lcw_pending_timers_lock, flags);
        return rc;
}

static int lcw_dispatch_main(void *data)
{
        int rc = 0;
        unsigned long flags;
        struct lc_watchdog *lcw;
        struct task_struct *tsk;

        ENTRY;

        kportal_daemonize("lc_watchdogd");

        SIGNAL_MASK_LOCK(current, flags);
        sigfillset(&current->blocked);
        RECALC_SIGPENDING;
        SIGNAL_MASK_UNLOCK(current, flags);

        complete(&lcw_start_completion);

        while (1) {
                wait_event_interruptible(lcw_event_waitq, is_watchdog_fired());
                CDEBUG(D_INFO, "Watchdog got woken up...\n");
                if (test_bit(LCW_FLAG_STOP, &lcw_flags)) {
                        CDEBUG(D_INFO, "LCW_FLAG_STOP was set, shutting down...\n");

                        spin_lock_irqsave(&lcw_pending_timers_lock, flags);
                        rc = !list_empty(&lcw_pending_timers);
                        spin_unlock_irqrestore(&lcw_pending_timers_lock, flags);
                        if (rc) {
                                CERROR("pending timers list was not empty at "
                                       "time of watchdog dispatch shutdown\n");
                        }
                        break;
                }

                spin_lock_irqsave(&lcw_pending_timers_lock, flags);
                while (!list_empty(&lcw_pending_timers)) {

                        lcw = list_entry(lcw_pending_timers.next,
                                         struct lc_watchdog,
                                         lcw_list);
                        list_del_init(&lcw->lcw_list);
                        spin_unlock_irqrestore(&lcw_pending_timers_lock, flags);

                        CDEBUG(D_INFO, "found lcw for pid %d\n", lcw->lcw_pid);

                        if (lcw->lcw_state != LC_WATCHDOG_DISABLED) {
                                /*
                                 * sanity check the task against our
                                 * watchdog
                                 */
                                tsk = lcw_lookup_task(lcw);
                                lcw->lcw_callback(lcw, tsk, lcw->lcw_data);
                        }

                        spin_lock_irqsave(&lcw_pending_timers_lock, flags);
                }
                spin_unlock_irqrestore(&lcw_pending_timers_lock, flags);
        }

        complete(&lcw_stop_completion);

        RETURN(rc);
}

static void lcw_dispatch_start(void)
{
        int rc;

        ENTRY;
        LASSERT(lcw_refcount == 1);

        init_completion(&lcw_stop_completion);
        init_completion(&lcw_start_completion);
        init_waitqueue_head(&lcw_event_waitq);

        CDEBUG(D_INFO, "starting dispatch thread\n");
        rc = kernel_thread(lcw_dispatch_main, NULL, 0);
        if (rc < 0) {
                CERROR("error spawning watchdog dispatch thread: %d\n", rc);
                EXIT;
                return;
        }
        wait_for_completion(&lcw_start_completion);
        CDEBUG(D_INFO, "watchdog dispatcher initialization complete.\n");

        EXIT;
}

static void lcw_dispatch_stop(void)
{
        ENTRY;
        LASSERT(lcw_refcount == 0);

        CDEBUG(D_INFO, "trying to stop watchdog dispatcher.\n");

        set_bit(LCW_FLAG_STOP, &lcw_flags);
        wake_up(&lcw_event_waitq);

        wait_for_completion(&lcw_stop_completion);

        CDEBUG(D_INFO, "watchdog dispatcher has shut down.\n");

        EXIT;
}

struct lc_watchdog *lc_watchdog_add(int timeout_ms,
                                    void (*callback)(struct lc_watchdog *,
                                                     struct task_struct *,
                                                     void *),
                                    void *data)
{
        struct lc_watchdog *lcw = NULL;
        ENTRY;

        PORTAL_ALLOC(lcw, sizeof(*lcw));
        if (!lcw) {
                CDEBUG(D_INFO, "Could not allocate new lc_watchdog\n");
                RETURN(ERR_PTR(-ENOMEM));
        }

        lcw->lcw_task = cfs_current();
        lcw->lcw_pid = cfs_curproc_pid();
        lcw->lcw_time = (timeout_ms * HZ) / 1000;
        lcw->lcw_callback = callback ? callback : lc_watchdog_dumplog;
        lcw->lcw_data = data;
        lcw->lcw_state = LC_WATCHDOG_DISABLED;

        INIT_LIST_HEAD(&lcw->lcw_list);

        lcw->lcw_timer.function = lcw_cb;
        lcw->lcw_timer.data = (unsigned long)lcw;
        lcw->lcw_timer.expires = jiffies + lcw->lcw_time;
        init_timer(&lcw->lcw_timer);

        down(&lcw_refcount_sem);
        if (++lcw_refcount == 1)
                lcw_dispatch_start();
        up(&lcw_refcount_sem);

        /* Keep this working in case we enable them by default */
        if (lcw->lcw_state == LC_WATCHDOG_ENABLED) {
                do_gettimeofday(&lcw->lcw_last_touched);
                add_timer(&lcw->lcw_timer);
        }

        RETURN(lcw);
}
EXPORT_SYMBOL(lc_watchdog_add);

static long
timeval_sub(struct timeval *large, struct timeval *small)
{
        return (large->tv_sec - small->tv_sec) * 1000000 +
                (large->tv_usec - small->tv_usec);
}

static void lcw_update_time(struct lc_watchdog *lcw, const char *message)
{
        struct timeval newtime;
        unsigned long timediff;

        do_gettimeofday(&newtime);
        if (lcw->lcw_state == LC_WATCHDOG_EXPIRED) {
                timediff = timeval_sub(&newtime, &lcw->lcw_last_touched);
                CWARN("Expired watchdog for pid %d %s after %lu.%.4lus\n",
                      lcw->lcw_pid,
                      message,
                      timediff / 1000000,
                      (timediff % 1000000) / 100);
        }
        lcw->lcw_last_touched = newtime;
}

void lc_watchdog_touch(struct lc_watchdog *lcw)
{
        unsigned long flags;
        ENTRY;
        LASSERT(lcw != NULL);

        spin_lock_irqsave(&lcw_pending_timers_lock, flags);
        if (!list_empty(&lcw->lcw_list))
                list_del_init(&lcw->lcw_list);
        spin_unlock_irqrestore(&lcw_pending_timers_lock, flags);

        lcw_update_time(lcw, "touched");
        lcw->lcw_state = LC_WATCHDOG_ENABLED;

        mod_timer(&lcw->lcw_timer, jiffies + lcw->lcw_time);

        EXIT;
}
EXPORT_SYMBOL(lc_watchdog_touch);

void lc_watchdog_disable(struct lc_watchdog *lcw)
{
        unsigned long flags;
        ENTRY;
        LASSERT(lcw != NULL);

        spin_lock_irqsave(&lcw_pending_timers_lock, flags);
        if (!list_empty(&lcw->lcw_list))
                list_del_init(&lcw->lcw_list);
        spin_unlock_irqrestore(&lcw_pending_timers_lock, flags);

        lcw_update_time(lcw, "disabled");
        lcw->lcw_state = LC_WATCHDOG_DISABLED;

        EXIT;
}
EXPORT_SYMBOL(lc_watchdog_disable);

void lc_watchdog_delete(struct lc_watchdog *lcw)
{
        unsigned long flags;
        ENTRY;
        LASSERT(lcw != NULL);

        del_timer(&lcw->lcw_timer);

        lcw_update_time(lcw, "deleted");

        spin_lock_irqsave(&lcw_pending_timers_lock, flags);
        if (!list_empty(&lcw->lcw_list))
                list_del_init(&lcw->lcw_list);
        spin_unlock_irqrestore(&lcw_pending_timers_lock, flags);

        down(&lcw_refcount_sem);
        if (--lcw_refcount == 0)
                lcw_dispatch_stop();
        up(&lcw_refcount_sem);

        PORTAL_FREE(lcw, sizeof(*lcw));

        EXIT;
}
EXPORT_SYMBOL(lc_watchdog_delete);

/*
 * Provided watchdog handlers
 */

extern void portals_debug_dumplog_internal(void *arg);

void lc_watchdog_dumplog(struct lc_watchdog *lcw,
                         struct task_struct *tsk,
                         void               *data)
{
        tsk = tsk ? tsk : current;
        portals_debug_dumplog_internal((void *)(long)tsk->pid);
}
EXPORT_SYMBOL(lc_watchdog_dumplog);
