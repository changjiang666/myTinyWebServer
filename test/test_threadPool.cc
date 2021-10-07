#include "threadpool/threadPool.hpp"
#include "mysql/sqlConnectionPool.hpp"
#include "lock/locker.hpp"
#include <iostream>
#include <unistd.h>

int num = 0;
Locker lock;
class T{
public:
    void process() {
        lock.lock();
        for (int i = 0; i < 10000; ++i) {
            num += 1;
        }
        lock.unlock();
    }

public:
    MYSQL *mysql;
};

int main() {
    ConnectionPool* connpool = ConnectionPool::getInstance();
    std::string user = "root";
    std::string passwd = "123456";
    std::string databasename = "webServerDB";
    connpool->init("localhost", user, passwd, databasename, 0, 8, 0);

    ThreadPool<T>* threadPool = new ThreadPool<T>(1, connpool);
    for (int i = 0; i < 8; ++i) {
        T* request = new T();
        threadPool->append(request);
    }
    sleep(5);
    threadPool->~ThreadPool();
    std::cout << num << std::endl;
    return 0;
}