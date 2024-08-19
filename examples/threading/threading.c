#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

// Optional: use these functions to add debug or error prints to your
// application #define DEBUG_LOG(msg,...)
#define DEBUG_LOG(msg, ...) printf("threading: " msg "\n", ##__VA_ARGS__)
#define ERROR_LOG(msg, ...) printf("threading ERROR: " msg "\n", ##__VA_ARGS__)

void *threadfunc(void *thread_param) {

    struct thread_data *args = (struct thread_data *)thread_param;
    unsigned long int id = (unsigned long int) pthread_self();
    DEBUG_LOG("Thread %lu: Sleeping for %d ms before obtaining the lock", id,
              args->wait_to_obtain_ms);
    if (usleep(args->wait_to_obtain_ms * 1000))
        goto threadfunc_fail;
    DEBUG_LOG("Thread %lu: Acquiring the mutex now", id);
    if (pthread_mutex_lock(args->mutex))
        goto threadfunc_fail;
    if (usleep(args->wait_to_release_ms * 1000))
        goto threadfunc_fail;
    DEBUG_LOG("Thread %lu: Releasing the mutex after %d ms", id,
              args->wait_to_release_ms);
    if (pthread_mutex_unlock(args->mutex))
        goto threadfunc_fail;

    args->thread_complete_success = true;
    return thread_param;
threadfunc_fail:
    args->thread_complete_success = false;
    return thread_param;
}

bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex,
                                  int wait_to_obtain_ms,
                                  int wait_to_release_ms) {
    // mutex seems to be already init from the unit tests

    // 1- allocate memory for the arguments to pass to the thread and initialize
    // them
    struct thread_data *args = malloc(sizeof(struct thread_data));
    args->mutex = mutex;
    args->thread_complete_success = false;
    args->wait_to_obtain_ms = wait_to_obtain_ms;
    args->wait_to_release_ms = wait_to_release_ms;
    // 2- create the thread
    if (pthread_create(thread, NULL, threadfunc, args))
        return false;
    return true;
}
