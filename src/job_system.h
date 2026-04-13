#ifndef JOB_SYSTEM_H
#define JOB_SYSTEM_H

#include <stdbool.h>

typedef void (*JobFunc)(void* data);

typedef struct JobSystem JobSystem;

JobSystem* job_system_create(int num_workers);
void       job_system_destroy(JobSystem* js);
void       job_system_submit(JobSystem* js, JobFunc func, void* data);
bool       job_system_try_submit(JobSystem* js, JobFunc func, void* data);
int        job_system_pending(JobSystem* js);
void       job_system_flush(JobSystem* js);

#endif
