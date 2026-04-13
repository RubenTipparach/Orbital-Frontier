#include "job_system.h"
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <pthread.h>
#endif

#define JOB_QUEUE_SIZE 1024

typedef struct {
    JobFunc func;
    void*   data;
} Job;

struct JobSystem {
    Job queue[JOB_QUEUE_SIZE];
    volatile int head;
    volatile int tail;
    volatile int pending_count;
    volatile bool shutdown;

#ifdef _WIN32
    HANDLE* threads;
    CRITICAL_SECTION mutex;
    CONDITION_VARIABLE cond_work;
    CONDITION_VARIABLE cond_done;
#else
    pthread_t* threads;
    pthread_mutex_t mutex;
    pthread_cond_t cond_work;
    pthread_cond_t cond_done;
#endif
    int num_workers;
};

static void lock(JobSystem* js) {
#ifdef _WIN32
    EnterCriticalSection(&js->mutex);
#else
    pthread_mutex_lock(&js->mutex);
#endif
}

static void unlock(JobSystem* js) {
#ifdef _WIN32
    LeaveCriticalSection(&js->mutex);
#else
    pthread_mutex_unlock(&js->mutex);
#endif
}

static void wait_work(JobSystem* js) {
#ifdef _WIN32
    SleepConditionVariableCS(&js->cond_work, &js->mutex, INFINITE);
#else
    pthread_cond_wait(&js->cond_work, &js->mutex);
#endif
}

static void signal_work(JobSystem* js) {
#ifdef _WIN32
    WakeConditionVariable(&js->cond_work);
#else
    pthread_cond_signal(&js->cond_work);
#endif
}

static void signal_done(JobSystem* js) {
#ifdef _WIN32
    WakeAllConditionVariable(&js->cond_done);
#else
    pthread_cond_broadcast(&js->cond_done);
#endif
}

static void wait_done(JobSystem* js) {
#ifdef _WIN32
    SleepConditionVariableCS(&js->cond_done, &js->mutex, INFINITE);
#else
    pthread_cond_wait(&js->cond_done, &js->mutex);
#endif
}

static bool try_pop(JobSystem* js, Job* out) {
    if (js->head == js->tail) return false;
    *out = js->queue[js->head];
    js->head = (js->head + 1) % JOB_QUEUE_SIZE;
    return true;
}

#ifdef _WIN32
static DWORD WINAPI worker_thread(LPVOID param) {
#else
static void* worker_thread(void* param) {
#endif
    JobSystem* js = (JobSystem*)param;
    for (;;) {
        lock(js);
        while (js->head == js->tail && !js->shutdown) {
            wait_work(js);
        }
        if (js->shutdown && js->head == js->tail) {
            unlock(js);
            break;
        }
        Job job;
        bool got = try_pop(js, &job);
        unlock(js);

        if (got) {
            job.func(job.data);
            lock(js);
            js->pending_count--;
            signal_done(js);
            unlock(js);
        }
    }
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

JobSystem* job_system_create(int num_workers) {
    JobSystem* js = (JobSystem*)calloc(1, sizeof(JobSystem));
    js->num_workers = num_workers;
    js->shutdown = false;

#ifdef _WIN32
    InitializeCriticalSection(&js->mutex);
    InitializeConditionVariable(&js->cond_work);
    InitializeConditionVariable(&js->cond_done);
    js->threads = (HANDLE*)calloc(num_workers, sizeof(HANDLE));
    for (int i = 0; i < num_workers; i++) {
        js->threads[i] = CreateThread(NULL, 0, worker_thread, js, 0, NULL);
    }
#else
    pthread_mutex_init(&js->mutex, NULL);
    pthread_cond_init(&js->cond_work, NULL);
    pthread_cond_init(&js->cond_done, NULL);
    js->threads = (pthread_t*)calloc(num_workers, sizeof(pthread_t));
    for (int i = 0; i < num_workers; i++) {
        pthread_create(&js->threads[i], NULL, worker_thread, js);
    }
#endif
    return js;
}

void job_system_destroy(JobSystem* js) {
    if (!js) return;
    lock(js);
    js->shutdown = true;
    unlock(js);

#ifdef _WIN32
    WakeAllConditionVariable(&js->cond_work);
    WaitForMultipleObjects(js->num_workers, js->threads, TRUE, INFINITE);
    for (int i = 0; i < js->num_workers; i++) CloseHandle(js->threads[i]);
    DeleteCriticalSection(&js->mutex);
#else
    pthread_cond_broadcast(&js->cond_work);
    for (int i = 0; i < js->num_workers; i++) pthread_join(js->threads[i], NULL);
    pthread_mutex_destroy(&js->mutex);
    pthread_cond_destroy(&js->cond_work);
    pthread_cond_destroy(&js->cond_done);
#endif
    free(js->threads);
    free(js);
}

void job_system_submit(JobSystem* js, JobFunc func, void* data) {
    lock(js);
    int next = (js->tail + 1) % JOB_QUEUE_SIZE;
    // Spin-wait if queue full (shouldn't happen with budget caps)
    while (next == js->head) {
        unlock(js);
        lock(js);
        next = (js->tail + 1) % JOB_QUEUE_SIZE;
    }
    js->queue[js->tail] = (Job){ func, data };
    js->tail = next;
    js->pending_count++;
    signal_work(js);
    unlock(js);
}

bool job_system_try_submit(JobSystem* js, JobFunc func, void* data) {
    lock(js);
    int next = (js->tail + 1) % JOB_QUEUE_SIZE;
    if (next == js->head) {
        unlock(js);
        return false;
    }
    js->queue[js->tail] = (Job){ func, data };
    js->tail = next;
    js->pending_count++;
    signal_work(js);
    unlock(js);
    return true;
}

int job_system_pending(JobSystem* js) {
    lock(js);
    int n = js->pending_count;
    unlock(js);
    return n;
}

void job_system_flush(JobSystem* js) {
    lock(js);
    while (js->pending_count > 0) {
        wait_done(js);
    }
    unlock(js);
}
