#include "config/config.hpp"
#include <iostream>

int main(int argc, char* argv[]) {

    //需要修改的数据库信息,登录名,密码,库名
    std::string user = "root";
    std::string passwd = "123456";
    std::string databasename = "webServerDB";

    // 命令行解析
    Config config;
    config.parseArg(argc, argv);

    WebServer server;

    // 初始化
    server.init(config.port,
                user, passwd, databasename, config.logWrite,
                config.optLinger, config.trigMode, config.sqlNum,
                config.threadNum, config.closeLog, config.actorModel);
    
    // 日志
    server.logWrite();

    // 数据库
    server.sqlPool();

    // 线程池
    server.threadPool();

    // 触发模式
    server.trigMode();

    // 监听
    server.eventListen();

    // 运行
    server.eventLoop();

    return 0;

}