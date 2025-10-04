#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>

// Optional: use these functions to add debug or error prints to your application
#define DEBUG_LOG(msg,...)
//#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)

int msleep( int time_in_ms )
{
  struct timespec ts;

  if( time_in_ms < 0 )
  {
    DEBUG_LOG( "wrong time value: %d", time_in_ms );
    errno = EINVAL;
    return -1;
  }

  ts.tv_sec = time_in_ms / 1000;
  ts.tv_nsec = (time_in_ms % 1000) * 1000000;
  
  DEBUG_LOG( "Sleeping for: %ldsec %ldnsec", ts.tv_sec, ts.tv_nsec );
  nanosleep( &ts, &ts );
  DEBUG_LOG( "Awake");

  return 0;
}

void* threadfunc(void* thread_param)
{

    // TODO: wait, obtain mutex, wait, release mutex as described by thread_data structure
    // hint: use a cast like the one below to obtain thread arguments from your parameter
    struct thread_data* thread_func_args = (struct thread_data *) thread_param;

    DEBUG_LOG( "Going sleep before locking mutex" );
    if( 0 != msleep( thread_func_args->time_to_obtain_ms ))
    {
      ERROR_LOG( "failed on sleep before obtaining mutex" );
      thread_func_args->thread_complete_success = false;
      return thread_param;
    }

    DEBUG_LOG( "Locking mutex" );
    if( 0 != pthread_mutex_lock( thread_func_args->mutex ))
    {
      ERROR_LOG( "cannot lock mutex" );
      thread_func_args->thread_complete_success = false;
      return thread_param;
    }

    DEBUG_LOG( "Going sleep with mutex locked");
    if( 0 != msleep( thread_func_args->time_to_release_ms ))
    {
      ERROR_LOG( "failed on sleep with mutex locked" );
      thread_func_args->thread_complete_success = false;
      return thread_param;
    }

    DEBUG_LOG( "Releasing mutex" );
    if( 0 != pthread_mutex_unlock( thread_func_args->mutex ))
    {
      ERROR_LOG( "cannot unlock mutex" );
      thread_func_args->thread_complete_success = false;
      return thread_param;
    }

    DEBUG_LOG( "Exiting thread" );

    return thread_param;
}


bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex,int wait_to_obtain_ms, int wait_to_release_ms)
{
    /**
     * TODO: allocate memory for thread_data, setup mutex and wait arguments, pass thread_data to created thread
     * using threadfunc() as entry point.
     *
     * return true if successful.
     *
     * See implementation details in threading.h file comment block
     */

    struct thread_data* tdata = calloc( 1, sizeof(struct thread_data) );
    if( NULL == tdata )
    {
      ERROR_LOG( "cannot allocate memory for thread data" );
      return false;
    }

    tdata->thread_complete_success = true;
    tdata->time_to_obtain_ms = wait_to_obtain_ms;
    tdata->time_to_release_ms = wait_to_release_ms;
    tdata->mutex = mutex;

    if( 0 != pthread_create( thread, NULL, &threadfunc, tdata ))
    {
      ERROR_LOG( "failed when starting thread" );
      return false;
    }

    return true;
}

