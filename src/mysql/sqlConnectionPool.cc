#include "mysql/sqlConnectionPool.hpp"
#include <iostream>

//私有的构造析构函数
ConnectionPool::ConnectionPool() : m_curConn(0), m_freeConn(0) {}
ConnectionPool::~ConnectionPool() {
    destoryPool();
}

// 获得全局唯一的单例
ConnectionPool* ConnectionPool::getInstance() {
    static ConnectionPool connPool;
    return &connPool;
}

// 类的初始化
void ConnectionPool::init(std::string url, 
              std::string user,
              std::string passWord, 
              std::string dbName,
              int port,
              int maxConn,
              int close_log) {
    m_url = url;
    m_user = user;
    m_passWord = passWord;
    m_dbName = dbName;
    m_port = port;
    // m_maxConn = maxConn;
    m_close_log = close_log;

    for (int i = 0; i < maxConn; ++i) {
        MYSQL* conn = nullptr;
        conn = mysql_init(conn);
        if (conn == nullptr) {
            std::cout << "conn(mysql_init) is nullptr" << std::endl;
            exit(1);
        }

        conn = mysql_real_connect(conn, url.c_str(), user.c_str(), passWord.c_str(), dbName.c_str(), port, nullptr, 0);
        if (conn == nullptr) {
            std::cout << "conn(mysql_real_connect) is nullptr" << std::endl;
            exit(1);
        }

        connList.push_back(conn);
        m_freeConn += 1;
    }

    m_sem = Sem(m_freeConn);
    m_maxConn = m_freeConn;
}

MYSQL* ConnectionPool::getConnection() {
    if (0 == m_freeConn) {
        return nullptr;
    }

    MYSQL* ret = nullptr;
    m_sem.wait();
    m_lock.lock();
    ret = connList.front();
    m_freeConn -= 1;
    m_curConn += 1;
    connList.pop_front();
    m_lock.unlock();

    return ret;
}

bool ConnectionPool::releaseConnection(MYSQL* conn) {
    if (nullptr == conn) {
        return false;
    }

    m_lock.lock();
    connList.push_back(conn);
    m_freeConn += 1;
    m_curConn -= 1;
    m_lock.unlock();

    m_sem.post();
    return true;
}

int ConnectionPool::getFreeConn() {
    return m_freeConn;
}

void ConnectionPool::destoryPool() {
    m_lock.lock();
    if (m_maxConn > 0) {
        for (auto it = connList.begin(); it != connList.end(); ++it) {
            mysql_close(*it);
        }
        m_curConn = 0;
        m_freeConn = 0;
        connList.clear();
    }
    m_lock.unlock();
}




ConnectionPoolRAII::ConnectionPoolRAII(MYSQL** conn, ConnectionPool *connectionPool) : connectionPool(connectionPool) {
    *conn = connectionPool->getConnection();
    connRAII = *conn;
}

ConnectionPoolRAII::~ConnectionPoolRAII() {
    connectionPool->releaseConnection(connRAII);
}
