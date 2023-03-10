#include "locker.h"

// locker类的构造函数，初始化锁资源
locker::locker(){
    // 构造函数, 初始化lock
    int ret = pthread_mutex_init(&m_lock, NULL);
    if(ret != 0){
        throw std::exception();
    }
}
// 申请这个类所指向的锁
bool locker::apply_lock(){
    // 申请加锁不是阻塞的吗？那这个返回值有什么意义
    return pthread_mutex_lock(&m_lock) == 0;
}
// 解锁这个类所指向的锁
bool locker::unlock(){
    return pthread_mutex_unlock(&m_lock) == 0;
}
// 析构函数，负责回收锁资源
locker::~locker(){
    pthread_mutex_destroy(&m_lock);
}
// 返回这个类中锁的地址
pthread_mutex_t * locker::get_mutex(){
    return &m_lock;
}

// 初始化m_cond条件量
cond::cond(){
    pthread_cond_init(&m_cond, nullptr);
}
// 阻塞等待条件信号量，当条件量没空的时候，将锁释放，等待条件量
bool cond::cond_wait(pthread_mutex_t * lk){
    return pthread_cond_wait(&m_cond, lk) == 0;
}
// 阻塞等待条件量，直到超时
bool cond::cond_timedwait(pthread_mutex_t * lk, const timespec * tm = NULL){
    return pthread_cond_timedwait(&m_cond, lk, tm) == 0;
}
// 发出一个条件满足信号，唤醒一个进程来处理
bool cond::cond_signal(){
    return pthread_cond_signal(&m_cond) == 0;
}
// 广播条件满足信号
bool cond::cond_broadcast(){
    return pthread_cond_broadcast(&m_cond) == 0;
}
// 回收m_cond条件量资源
cond::~cond(){
    pthread_cond_destroy(&m_cond);
}

// 初始化信号量 
// 线程间共享 信号量初始时为0
sem::sem(){
    sem_init(&m_sem, 0, 0);
}
// 初始化信号量 
// shrd为0--线程间共享 非0--进程间共享
// n 表示信号量初始时的值
sem::sem(int shrd = 0, int n = 0){
    sem_init(&m_sem, shrd, n);
}
// 阻塞等待信号量
bool sem::sem_waitBlock(){
    return sem_wait(&m_sem) == 0;
}
// 阻塞一定的时间，等待信号量
bool sem::sem_waitTime(const timespec * tm){
    return sem_timedwait(&m_sem, tm) == 0;
}
// 使信号量的值+1
bool sem::post(){
    return sem_post(&m_sem) == 0;
}
// 回收信号量资源
sem::~sem(){
    sem_destroy(&m_sem);
}
