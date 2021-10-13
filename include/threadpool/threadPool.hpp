#ifndef THREADPOOL_THREADPOOL_HPP_
#define THREADPOOL_THREADPOOL_HPP_

#include <list>
#include <exception>
#include <pthread.h>
#include "lock/locker.hpp"
#include "mysql/sqlConnectionPool.hpp"

template<typename T>
class ThreadPool {
public:
    ThreadPool(int actorModel, ConnectionPool* connPool, int threadNum = 8, int maxReq = 10000);
    ~ThreadPool();

    // reactor, 工作线程负责读写和逻辑
    bool append(T* request, int state);
    // proactor, 工作线程只负责逻辑
    bool append(T* request);

private:
    // 工作线程运行的函数，从工作队列中取出任务并且执行
    static void* worker(void* arg);
    void run();

private:
    // 线程池中的线程数
    int m_threadNum;
    // 描述线程池的数组，大小是m_threadNum
    pthread_t* m_threads;

    // 请求队列中允许的最大请求数
    int m_maxReq;
    // 请求队列
    std::list<T*> m_workQueue;

    // 保护请求队列的互斥锁
    Locker m_queueLocker;
    // 是否有任务需要处理
    Sem m_queueState;

    // 数据库
    ConnectionPool* m_connPool;
    // 模型切换
    int m_actorModel;
};


template<typename T>
ThreadPool<T>::ThreadPool(int actorModel,
                          ConnectionPool* connPool, 
                          int threadNum, 
                          int maxReq) : 
                          m_actorModel(actorModel), 
                          m_connPool(connPool), 
                          m_threadNum(threadNum), 
                          m_maxReq(maxReq) {
    if (threadNum <= 0 || maxReq <= 0) {
        throw std::exception();
    }

    m_threads = new pthread_t[m_threadNum];
    if (m_threads == nullptr) {
        throw std::exception();
    }

    for (int i = 0; i < m_threadNum; ++i) {
        if (pthread_create(m_threads + i, nullptr, worker, this) != 0) {
            delete[] m_threads;
            throw std::exception();
        }
        if (pthread_detach(m_threads[i])) {
            delete[] m_threads;
            throw std::exception();
        } 
    }
}

template<typename T>
ThreadPool<T>::~ThreadPool() {
    delete[] m_threads;
}

template<typename T>
bool ThreadPool<T>::append(T *request) {
    m_queueLocker.lock();
    if (m_workQueue.size() >= m_maxReq) {
        m_queueLocker.unlock();
        return false;
    }
    m_workQueue.push_back(request);
    m_queueLocker.unlock();
    m_queueState.post();
    return true;
}

template<typename T>
bool ThreadPool<T>::append(T* request, int state) {
    return true;
}

template<typename T>
void* ThreadPool<T>::worker(void* arg) {
    ThreadPool *pool = (ThreadPool*) arg;
    pool->run();
    return pool;
}


// T这个任务类，必须有MYSQL* mysql 和 process的处理函数
template<typename T> 
void ThreadPool<T>::run() {
    while (true) {
        m_queueState.wait();
        m_queueLocker.lock();
        if (m_workQueue.empty()) {
            m_queueLocker.unlock();
            continue;
        }
        T* request = m_workQueue.front();
        m_workQueue.pop_front();
        m_queueLocker.unlock();

        if (request == nullptr) {
            continue;
        }
        ConnectionPoolRAII mysqlConn(&request->mysql, m_connPool);
        request->process();
    }
}
#endif