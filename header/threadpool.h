#ifndef THREAD_POOL_H
#define THREAD_POOL_H
/*
    封装线程池需要的类
*/
#include <iostream>
#include <pthread.h>
#include <locker.h>
#include <exception>
#include <list>
#include <http_connection.h>

// 将线程池定义成模板，提高代码的复用性
template<typename T>
class threadpool{
public:
    threadpool(int num = 6, int mxqueue = 10000);
    bool addqueue(T * request);
    ~threadpool();
private:
    static void * workerthread(void * arg);
    void run();
private:
    int m_threads_num;// 线程池中 线程的数量
    pthread_t * m_threads; // 线程的容器
    int m_max_queue;// 请求队列中 最大的请求数量
    std::list<T*> m_wkqueue;// 请求队列
    locker m_qlock;// 请求队列互斥锁
    sem m_qsem;// 请求队列信号量, 来判断是否有请求需要处理
    bool m_stop;// 是否结束线程
};


#endif