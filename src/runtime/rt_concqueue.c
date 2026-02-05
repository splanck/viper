//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "rt_concqueue.h"

#include "rt_internal.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// --- Node for linked list queue ---

typedef struct cq_node
{
    void *value;
    struct cq_node *next;
} cq_node;

typedef struct
{
    void *vptr;
    cq_node *head;
    cq_node *tail;
    int64_t count;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} rt_concqueue_impl;

// --- Internal helpers ---

static void cq_finalizer(void *obj)
{
    rt_concqueue_impl *cq = (rt_concqueue_impl *)obj;

    pthread_mutex_lock(&cq->mutex);
    cq_node *n = cq->head;
    while (n)
    {
        cq_node *next = n->next;
        rt_obj_release_check0(n->value);
        free(n);
        n = next;
    }
    cq->head = NULL;
    cq->tail = NULL;
    cq->count = 0;
    pthread_mutex_unlock(&cq->mutex);

    pthread_mutex_destroy(&cq->mutex);
    pthread_cond_destroy(&cq->cond);
}

// --- Public API ---

void *rt_concqueue_new(void)
{
    rt_concqueue_impl *cq =
        (rt_concqueue_impl *)rt_obj_new_i64(0, sizeof(rt_concqueue_impl));
    cq->head = NULL;
    cq->tail = NULL;
    cq->count = 0;
    pthread_mutex_init(&cq->mutex, NULL);
    pthread_cond_init(&cq->cond, NULL);
    rt_obj_set_finalizer(cq, cq_finalizer);
    return (void *)cq;
}

int64_t rt_concqueue_len(void *obj)
{
    if (!obj)
        return 0;
    rt_concqueue_impl *cq = (rt_concqueue_impl *)obj;
    pthread_mutex_lock(&cq->mutex);
    int64_t len = cq->count;
    pthread_mutex_unlock(&cq->mutex);
    return len;
}

int8_t rt_concqueue_is_empty(void *obj)
{
    return rt_concqueue_len(obj) == 0 ? 1 : 0;
}

void rt_concqueue_enqueue(void *obj, void *item)
{
    if (!obj)
        return;
    rt_concqueue_impl *cq = (rt_concqueue_impl *)obj;

    cq_node *node = (cq_node *)malloc(sizeof(cq_node));
    node->value = item;
    rt_obj_retain_maybe(item);
    node->next = NULL;

    pthread_mutex_lock(&cq->mutex);
    if (cq->tail)
        cq->tail->next = node;
    else
        cq->head = node;
    cq->tail = node;
    cq->count++;
    pthread_cond_signal(&cq->cond);
    pthread_mutex_unlock(&cq->mutex);
}

void *rt_concqueue_try_dequeue(void *obj)
{
    if (!obj)
        return NULL;
    rt_concqueue_impl *cq = (rt_concqueue_impl *)obj;

    pthread_mutex_lock(&cq->mutex);
    if (!cq->head)
    {
        pthread_mutex_unlock(&cq->mutex);
        return NULL;
    }

    cq_node *node = cq->head;
    cq->head = node->next;
    if (!cq->head)
        cq->tail = NULL;
    cq->count--;
    pthread_mutex_unlock(&cq->mutex);

    void *value = node->value;
    free(node);
    return value;
}

void *rt_concqueue_dequeue(void *obj)
{
    if (!obj)
        return NULL;
    rt_concqueue_impl *cq = (rt_concqueue_impl *)obj;

    pthread_mutex_lock(&cq->mutex);
    while (!cq->head)
        pthread_cond_wait(&cq->cond, &cq->mutex);

    cq_node *node = cq->head;
    cq->head = node->next;
    if (!cq->head)
        cq->tail = NULL;
    cq->count--;
    pthread_mutex_unlock(&cq->mutex);

    void *value = node->value;
    free(node);
    return value;
}

void *rt_concqueue_dequeue_timeout(void *obj, int64_t timeout_ms)
{
    if (!obj)
        return NULL;
    rt_concqueue_impl *cq = (rt_concqueue_impl *)obj;

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout_ms / 1000;
    ts.tv_nsec += (timeout_ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L)
    {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000L;
    }

    pthread_mutex_lock(&cq->mutex);
    while (!cq->head)
    {
        int rc = pthread_cond_timedwait(&cq->cond, &cq->mutex, &ts);
        if (rc != 0)
        {
            // Timeout or error
            pthread_mutex_unlock(&cq->mutex);
            return NULL;
        }
    }

    cq_node *node = cq->head;
    cq->head = node->next;
    if (!cq->head)
        cq->tail = NULL;
    cq->count--;
    pthread_mutex_unlock(&cq->mutex);

    void *value = node->value;
    free(node);
    return value;
}

void *rt_concqueue_peek(void *obj)
{
    if (!obj)
        return NULL;
    rt_concqueue_impl *cq = (rt_concqueue_impl *)obj;

    pthread_mutex_lock(&cq->mutex);
    void *value = cq->head ? cq->head->value : NULL;
    pthread_mutex_unlock(&cq->mutex);
    return value;
}

void rt_concqueue_clear(void *obj)
{
    if (!obj)
        return;
    rt_concqueue_impl *cq = (rt_concqueue_impl *)obj;

    pthread_mutex_lock(&cq->mutex);
    cq_node *n = cq->head;
    while (n)
    {
        cq_node *next = n->next;
        rt_obj_release_check0(n->value);
        free(n);
        n = next;
    }
    cq->head = NULL;
    cq->tail = NULL;
    cq->count = 0;
    pthread_mutex_unlock(&cq->mutex);
}
