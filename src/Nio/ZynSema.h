/*
  ZynAddSubFX - a software synthesizer

  ZynSema.h - Semaphore Wrapper
  Copyright (C) 2016 Mark McCurry

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.
*/
#ifndef ZYNSEMA_H
#define ZYNSEMA_H

#if defined __APPLE__ || defined WIN32

// Counting semaphore built on the standard library (no pthreads needed).
// std::counting_semaphore is not enough: the queues read the count.
#include <mutex>
#include <condition_variable>

#ifndef PTHREAD_PROCESS_PRIVATE
#define PTHREAD_PROCESS_PRIVATE 0
#endif

namespace zyn {

class ZynSema
{
public:
    ZynSema (void) : _count (0)
    {
    }

    int init (int, int v)
    {
        std::lock_guard<std::mutex> lock (_mutex);
        _count = v;
        return 0;
    }

    int post (void)
    {
        {
            std::lock_guard<std::mutex> lock (_mutex);
            ++_count;
        }
        _cond.notify_one ();
        return 0;
    }

    int wait (void)
    {
        std::unique_lock<std::mutex> lock (_mutex);
        _cond.wait (lock, [this] { return _count > 0; });
        --_count;
        return 0;
    }

    int trywait (void)
    {
        std::lock_guard<std::mutex> lock (_mutex);
        if (_count < 1)
            return -1;
        --_count;
        return 0;
    }

    int getvalue (void) const
    {
        std::lock_guard<std::mutex> lock (_mutex);
        return _count;
    }

private:
    int                     _count;
    mutable std::mutex      _mutex;
    std::condition_variable _cond;
};

}

#else // POSIX semaphore

#include <pthread.h>  // for the "s" parameter to "init"
#include <semaphore.h>

namespace zyn {

class ZynSema
{
public:
    ZynSema (void)
    {
    }
    ~ZynSema (void)
    {
        sem_destroy (&_sema);
    }
    int init (int s, int v)
    {
        return sem_init (&_sema, s, v);
    }
    int post (void)
    {
        return sem_post (&_sema);
    }
    int wait (void)
    {
        return sem_wait (&_sema);
    }
    int trywait (void)
    {
        return sem_trywait (&_sema);
    }
    int getvalue(void)
    {
        int v = 0;
        sem_getvalue(&_sema, &v);
        return v;
    }

private:
    sem_t _sema;
};

}

#endif // POSIX semapore

#endif // ZYNSEMA_H
