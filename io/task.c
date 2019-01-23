/*
 * QEMU I/O task
 *
 * Copyright (c) 2015 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "qemu/osdep.h"
#include "io/task.h"
#include "qapi/error.h"
#include "qemu/thread.h"
#include "trace.h"

struct QIOTaskThreadData {
    QIOTaskWorker worker;
    gpointer opaque;
    GDestroyNotify destroy;
    GMainContext *context;
};


struct QIOTask {
    Object *source;
    QIOTaskFunc func;
    gpointer opaque;
    GDestroyNotify destroy;
    Error *err;
    gpointer result;
    GDestroyNotify destroyResult;
    struct QIOTaskThreadData *thread;
};


QIOTask *qio_task_new(Object *source,
                      QIOTaskFunc func,
                      gpointer opaque,
                      GDestroyNotify destroy)
{
    QIOTask *task;

    task = g_new0(QIOTask, 1);

    task->source = source;
    object_ref(source);
    task->func = func;
    task->opaque = opaque;
    task->destroy = destroy;

    trace_qio_task_new(task, source, func, opaque);

    return task;
}

static void qio_task_free(QIOTask *task)
{
    if (task->destroy) {
        task->destroy(task->opaque);
    }
    if (task->destroyResult) {
        task->destroyResult(task->result);
    }
    if (task->err) {
        error_free(task->err);
    }
    object_unref(task->source);

    g_free(task);
}


static gboolean qio_task_thread_result(gpointer opaque)
{
    QIOTask *task = opaque;

    trace_qio_task_thread_result(task);
    qio_task_complete(task);

    if (task->thread->destroy) {
        task->thread->destroy(task->thread->opaque);
    }

    if (task->thread->context) {
        g_main_context_unref(task->thread->context);
    }

    g_free(task->thread);
    task->thread = NULL;

    return FALSE;
}


static gpointer qio_task_thread_worker(gpointer opaque)
{
    QIOTask *task = opaque;
    GSource *idle;

    trace_qio_task_thread_run(task);

    task->thread->worker(task, task->thread->opaque);

    /* We're running in the background thread, and must only
     * ever report the task results in the main event loop
     * thread. So we schedule an idle callback to report
     * the worker results
     */
    trace_qio_task_thread_exit(task);

    idle = g_idle_source_new();
    g_source_set_callback(idle, qio_task_thread_result, task, NULL);
    g_source_attach(idle, task->thread->context);

    return NULL;
}


void qio_task_run_in_thread(QIOTask *task,
                            QIOTaskWorker worker,
                            gpointer opaque,
                            GDestroyNotify destroy,
                            GMainContext *context)
{
    struct QIOTaskThreadData *data = g_new0(struct QIOTaskThreadData, 1);
    QemuThread thread;

    if (context) {
        g_main_context_ref(context);
    }

    data->worker = worker;
    data->opaque = opaque;
    data->destroy = destroy;
    data->context = context;

    task->thread = data;

    trace_qio_task_thread_start(task, worker, opaque);
    qemu_thread_create(&thread,
                       "io-task-worker",
                       qio_task_thread_worker,
                       task,
                       QEMU_THREAD_DETACHED);
}


void qio_task_complete(QIOTask *task)
{
    task->func(task, task->opaque);
    trace_qio_task_complete(task);
    qio_task_free(task);
}


void qio_task_set_error(QIOTask *task,
                        Error *err)
{
    error_propagate(&task->err, err);
}


bool qio_task_propagate_error(QIOTask *task,
                              Error **errp)
{
    if (task->err) {
        error_propagate(errp, task->err);
        task->err = NULL;
        return true;
    }

    return false;
}


void qio_task_set_result_pointer(QIOTask *task,
                                 gpointer result,
                                 GDestroyNotify destroy)
{
    task->result = result;
    task->destroyResult = destroy;
}


gpointer qio_task_get_result_pointer(QIOTask *task)
{
    return task->result;
}


Object *qio_task_get_source(QIOTask *task)
{
    return task->source;
}
