/*
    封装各种线程同步需要的类，包括 互斥锁类、条件变量类、信号量类
*/
#ifndef LOCKER_H
#define LOCKER_H
#include <pthread.h>
#include <exception>
#include <semaphore.h>

// 互斥锁类
class locker{
public:
    locker();
    bool apply_lock();
    bool unlock();
    pthread_mutex_t * get_mutex();
    ~locker();
private:
    pthread_mutex_t m_lock; // 一个私有成员lock
};

// 信号量类
class sem{
public:
    sem();
    sem(int shrd, int n);
    bool sem_waitBlock();
    bool sem_waitTime(const timespec * tm);
    bool post();
    ~sem();
private:
    sem_t m_sem; // 一个私有成员lock
};

// 条件变量类
class cond{
public:
    cond();
    bool cond_wait(pthread_mutex_t * lk);
    bool cond_timedwait(pthread_mutex_t * lk, const timespec * tm);
    bool cond_signal();
    bool cond_broadcast();
    ~cond();
private:
    pthread_cond_t m_cond; // 一个私有成员lock
};

#endif