#include "threadpool.h"
#include <iostream>
using namespace std;
// threadpool的构造函数
template<typename T>
threadpool<T>::threadpool(int num, int mxqueue) : 
        m_threads_num(num), m_max_queue(mxqueue), 
        m_stop(false), m_threads(NULL){
    if( m_max_queue < 0 || m_threads_num < 0 ) throw std::exception();
    m_threads = new pthread_t[m_threads_num];
    if(!m_threads){
        // 创建失败
        throw std::exception();
    }
    // 创建num个线程，把线程pid放在threads数组中
    for(int i = 0 ; i < m_threads_num ; ++ i){
        cout << "正在创建第" << i+1 << "个线程..." << endl;
        if( pthread_create(m_threads + i, nullptr, workerthread, this) != 0 ){
            delete[] m_threads;
            throw std::exception();
        }
        // 设置线程分离
        if( pthread_detach(m_threads[i]) != 0 ){
            delete[] m_threads;
            throw std::exception();
        }
    }
}

// threadpool的析构函数
template<typename T>
threadpool<T>::~threadpool(){
    delete[] m_threads;
    m_stop = true;
}

// 向请求队列添加一个请求任务
template< typename T >
bool threadpool< T >::addqueue(T * request){
    // 先申请锁
    m_qlock.apply_lock();
    // 判断队列是否已满
    if(m_wkqueue.size() >= m_max_queue){
        m_qlock.unlock();
        return false;
    }
    // 加入队列
    m_wkqueue.push_back(request);
    // cout << "加入请求队列..." << endl;
    m_qlock.unlock(); // 解锁
    m_qsem.post(); // 信号量+1
    return true;
}

// 子线程的函数
template<typename T>
void * threadpool<T>::workerthread(void * arg){
    threadpool * thdpool = (threadpool *)arg;
    thdpool->run();
    
    return thdpool;
}

template <typename T>
void threadpool<T>::run(){
    while(m_stop != true){
        // 先查看信号量
        // cout << "等待信号量..." << endl;
        m_qsem.sem_waitBlock();
        // 申请锁
        m_qlock.apply_lock();
        // 判断队列是否已满
        if(m_wkqueue.size() >= m_max_queue){
            m_qlock.unlock();
            continue;
        }
        T * req = m_wkqueue.front();
        // cout << "获取到一个请求..." << endl;
        m_wkqueue.pop_front();
        m_qlock.unlock();
        if(!req){
            continue;
        }
        // cout << "开始处理请求..." << endl;
        req->process();
    }
}

template class threadpool<http_connection>;