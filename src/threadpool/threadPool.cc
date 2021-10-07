#include "threadpool/threadPool.hpp"

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
        if (pthread_detach(m_threads[i]) != 0) {
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