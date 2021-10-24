#ifndef LOG_BLOCKQUEUE_HPP_
#define LOG_BLOCKQUEUE_HPP_

#include "lock/locker.hpp"
#include <iostream>
#include <queue>

template<typename T>
class block_queue {
public:
    block_queue(int max_size = 1000) {
        if (max_size <= 0) {
            exit(-1);
        }
        m_max_size = max_size;
        m_size = 0;
    }

    void clear() {
        m_mutex.lock();
        m_size = 0;
        m_mutex.unlock();
    }

    ~block_queue() = default;

    bool full() {
        m_mutex.lock();
        if (m_size >= m_max_size) {
            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
        return false;
    }

    bool empty() {
        m_mutex.lock();
        if (m_size == 0) {
            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
        return false;
    }

    bool front(T& value) {
        m_mutex.lock();
        if (0 == m_size) {
            m_mutex.unlock();
            return false;
        }
        value = q.front();
        m_mutex.unlock();
        return true;
    }
    
    bool back(T& value) {
        m_mutex.lock();
        if (0 == m_size) {
            m_mutex.unlock();
            return false;
        }
        value = q.back();
        m_mutex.unlock();
        return true;
    }

    int size() {
        int tmp = 0;
        m_mutex.lock();
        tmp = m_size;
        m_mutex.unlock();
        return tmp;
    }

    int max_size() {
        int tmp = 0;
        m_mutex.lock();
        tmp = m_max_size;
        m_mutex.unlock();
        return tmp;
    }

    bool push(const T& item) {
        m_mutex.lock();
        if (m_size >= m_max_size) {
            m_cond.broadcast();
            m_mutex.unlock();
            return false;
        }

        q.push(item);
        m_size += 1;
        m_cond.broadcast();
        m_mutex.unlock();
        return true;
    }

    bool pop(T& item) {
        m_mutex.lock();
        while (m_size <= 0) {
            if (!m_cond.wait(m_mutex.get())) {
                m_mutex.unlock();
                return false;
            }
        }
        item = q.front();
        q.pop();
        m_size -= 1;
        m_mutex.unlock();
        return true;
    }

    bool pop(T& item, int ms_timeout) {
        struct timespec t = {0, 0};
        struct timeval now = {0, 0};
        gettimeofday(&now, NULL);
        m_mutex.lock();
        if (m_size <= 0) {
            t.tv_sec = now.tv_sec + ms_timeout / 1000;
            t.tv_nsec = (ms_timeout % 1000) * 1000;
            if (!m_cond.timewait(m_mutex.get(), t))
            {
                m_mutex.unlock();
                return false;
            }
        }

        if (m_size <= 0)
        {
            m_mutex.unlock();
            return false;
        }
        item = q.front();
        m_size -= 1;
        m_mutex.unlock();
        return true;
    }
    
private:
    Locker m_mutex;
    Cond m_cond;

    std::queue<T> q;
    int m_size;
    int m_max_size;
};
#endif