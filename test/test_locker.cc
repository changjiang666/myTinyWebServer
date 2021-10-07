#include "lock/locker.hpp"

#include <unistd.h>
#include <iostream>
#include <pthread.h>

int num = 0;
Sem sem(1);
Locker locker;
Cond cond;

void* add(void*) {
    locker.lock();
    // sem.wait();
    // cond.wait();
    for (int i = 0; i < 100000000; ++i) {
        num += 1;
    }
    std::cout << num << std::endl;
    locker.unlock();
    // sem.post();
    // cond.signal();
    pthread_exit(nullptr);
}

int main() {
    pthread_t id1, id2;
    pthread_create(&id1, nullptr, add, nullptr);
    pthread_create(&id2, nullptr, add, nullptr);
    

    pthread_join(id1, nullptr);
    pthread_join(id2, nullptr);
    std::cout << num << std::endl;
    return 0;
}