#ifndef CONFIG_CONFIG_HPP_
#define CONFIG_CONFIG_HPP_

#include "webServer/webServer.hpp"

class Config {
public:
    Config();
    ~Config() {};

    void parseArg(int argc, char* argv[]);

public:
    // 端口号
    int port;
    // 日志写入方式
    int logWrite;
    // 触发组合方式
    int trigMode;
    // listenfd触发方式
    int listenTrigMode;
    // connfd触发方式
    int connTrigMode;
    // 优雅的关闭连接
    int optLinger;
    // 数据库连接池的数量
    int sqlNum;
    // 线程池中的线程数量
    int threadNum;
    // 是否关闭日志
    int closeLog;
    // 并发模型选择
    int actorModel;
};
#endif