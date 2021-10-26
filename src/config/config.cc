#include "config/config.hpp"

Config::Config() {
    // 端口号默认9006
    port = 9006;

    // 日志写入方式默认同步
    logWrite = 0;

    // 触发组合方式，默认listenfd LT + connfd LT
    trigMode = 0;

    // listenfd触发模式，默认LT
    listenTrigMode = 0;

    // connfd触发模式，默认LT
    connTrigMode = 0;

    // 优雅的关闭连接，默认不使用
    optLinger = 0;

    // 数据库连接池数量，默认8
    sqlNum = 8;

    // 线程池内线程数量，默认8
    threadNum = 8;

    // 关闭日志，默认不关闭
    closeLog = 0;

    // 并发模型，默认选择proactor
    actorModel = 1;
}

void Config::parseArg(int argc, char* argv[]) {
    int opt;
    const char* str = "p:l:m:o:s:t:c:a:";
    while ((opt = getopt(argc, argv, str)) != -1) {
        switch (opt) {
            case 'p': {
                port = atoi(optarg);
                break;
            }
            case 'l': {
                logWrite = atoi(optarg);
                break;
            }
            case 'm': {
                trigMode = atoi(optarg);
                break;
            }
            case 'o': {
                optLinger = atoi(optarg);
                break;
            }
            case 's': {
                sqlNum = atoi(optarg);
                break;
            }
            case 't': {
                threadNum = atoi(optarg);
                break;
            }
            case 'c': {
                closeLog = atoi(optarg);
                break;
            }
            case 'a': {
                actorModel = atoi(optarg);
                break;
            }
            default: {
                break;
            }
        }
    }
}