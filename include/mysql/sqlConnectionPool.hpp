#ifndef MYSQL_SQLCONNECTIONPOOL_HPP_
#define MYSQL_SQLCONNECTIONPOOL_HPP_

#include <mysql/mysql.h>
#include <string>
#include <list>
#include "lock/locker.hpp"

class ConnectionPool {
public:
    // 从池中获取一个连接
    MYSQL* getConnection();
    // 释放连接到池中
    bool releaseConnection(MYSQL* conn);

    // 获取当前空闲连接的数目
    int getFreeConn();

    // 销毁所有的连接
    void destoryPool();

    //单例模式
    static ConnectionPool* getInstance();
    void init(std::string url, 
              std::string user,
              std::string passWord, 
              std::string dbName,
              int port,
              int maxConn,
              int close_log);

private:
    ConnectionPool();
    ~ConnectionPool();

    // 最大连接数
    int m_maxConn;
    // 当前已使用连接数
    int m_curConn;
    // 当前空闲连接数
    int m_freeConn;

    Locker m_lock;
    Sem m_sem;
    // sql 连接池
    std::list<MYSQL*> connList;

public:
    // 主机地址
    std::string m_url;
    // 数据库端口号
    std::string m_port;
    // 登录数据库用户名
    std::string m_user;
    // 登录数据库密码
    std::string m_passWord;
    // 使用的数据库名
    std::string m_dbName;
    // 日子开关
    int m_close_log;
};


class ConnectionPoolRAII {
public:
    ConnectionPoolRAII(MYSQL** conn, ConnectionPool *connectionPool);
    ~ConnectionPoolRAII();

private:
    MYSQL* connRAII;
    ConnectionPool* connectionPool;
};
#endif