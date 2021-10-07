#include "lock/locker.hpp"

Sem::Sem() {
    if (sem_init(&m_sem, 0, 0) != 0) {
        throw std::exception();
    }
}

Sem::Sem(int value) {
    if (sem_init(&m_sem, 0, value) != 0) {
        throw std::exception();
    }
}

Sem::~Sem() {
    sem_destroy(&m_sem);
}

bool Sem::wait() {
    return sem_wait(&m_sem) == 0;
}

bool Sem::post() {
    return sem_post(&m_sem) == 0;
}


Locker::Locker() {
    if (pthread_mutex_init(&m_mutex, nullptr) != 0) {
        throw std::exception();
    }
}

Locker::~Locker() {
    pthread_mutex_destroy(&m_mutex);
}

bool Locker::lock() {
    return pthread_mutex_lock(&m_mutex) == 0;
}

bool Locker::unlock() {
    return pthread_mutex_unlock(&m_mutex) == 0;
}


Cond::Cond() {
    if (pthread_mutex_init(&m_mutex, nullptr) != 0) {
        throw std::exception();
    }
    if (pthread_cond_init(&m_cond, nullptr) != 0) {
        pthread_mutex_destroy(&m_mutex);
        throw std::exception();
    }
}

Cond::~Cond() {
    pthread_mutex_destroy(&m_mutex);
    pthread_cond_destroy(&m_cond);
}

bool Cond::wait() {
    pthread_mutex_lock(&m_mutex);
    if (pthread_cond_wait(&m_cond, &m_mutex) != 0) {
        pthread_mutex_unlock(&m_mutex);
        return false;
    } 
    pthread_mutex_unlock(&m_mutex);
    return true;
}

bool Cond::signal() {
    pthread_cond_signal(&m_cond) == 0;
}