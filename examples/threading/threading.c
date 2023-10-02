#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

// Optional: use these functions to add debug or error prints to your application
//#define DEBUG_LOG(msg,...)
#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)

void* threadfunc(void* thread_param)
{
    int retval;

    // TEST: wait, obtain mutex, wait, release mutex as described by thread_data structure
    DEBUG_LOG("New thread!");

    if (!thread_param)
    {
        ERROR_LOG("Invalid thread_param");
        return NULL;
    }

    struct thread_data* data = (struct thread_data *) thread_param;
    data->thread_complete_success = false; // Default to error

    DEBUG_LOG("Wait before lock: %d ms", data->wait_to_obtain_ms);
    usleep(data->wait_to_obtain_ms * 1000);

    if (!data->mutex)
    {
        ERROR_LOG("Invalid mutex");
        return thread_param;
    }

    retval = pthread_mutex_lock(data->mutex);
    if (retval != 0)
    {
        ERROR_LOG("pthread_mutex_lock() failed with error code %d", retval);
        return thread_param;
    }

    DEBUG_LOG("Wait with lock: %d ms", data->wait_to_release_ms);

    usleep(data->wait_to_release_ms * 1000);

    retval = pthread_mutex_unlock(data->mutex);
    if (retval != 0)
    {
        ERROR_LOG("pthread_mutex_unlock() failed with error code %d", retval);
        return thread_param;
    }

    DEBUG_LOG("Thread done!");

    data->thread_complete_success = true;
    return thread_param; // data and thread_param point to the same place
}


bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex, int wait_to_obtain_ms, int wait_to_release_ms)
{
    /**
     * test: allocate memory for thread_data, setup mutex and wait arguments, pass thread_data to created thread
     * using threadfunc() as entry point.
     *
     * return true if successful.
     *
     * See implementation details in threading.h file comment block
     */
    int retval;
    struct thread_data* data;

    data = (struct thread_data*) malloc(sizeof(struct thread_data));
    if (!data)
    {
        ERROR_LOG("Allocation error");
        return false;
    }

    data->wait_to_obtain_ms = wait_to_obtain_ms;
    data->wait_to_release_ms = wait_to_release_ms;
    data->mutex = mutex;

    DEBUG_LOG("Starting new thread...");
    retval = pthread_create(thread, NULL, threadfunc, (void*)data);
    if (retval != 0)
    {
        ERROR_LOG("pthread_create() failed with error code %d", retval);
        free(data);
        return false;
    }

    return true;
}

