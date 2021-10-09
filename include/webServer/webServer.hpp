#ifndef WEBSERVER_WEBSERVER_HPP_
#define WEBSERVER_WEBSERVER_HPP_

#include "threadpool/threadPool.hpp"
#include "http/httpConn.hpp"
#include "timer/lst_timer.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/errno.h>


// 最大文件描述符数
const int MAX_FD = 65536;
// 最大事件数
const int MAX_EVENT_NUMBER = 10000;
// 最小超时单位
const int TIMESLOT = 5;


class WebServer {
public:
    WebServer();
    ~WebServer();

    void init(int port,
              std::string user,
              std::string passWord,
              std::string databaseName,
              int logWrite,
              int optLinger,
              int trigMode,
              int sqlNum,
              int threadNum,
              int closeLog,
              int actorModel);

    void threadPool();
    void sqlPool();
    void logWrite();
    void trigMode();
    void eventListen();
    void eventLoop();
    void timer(int connfd,
               struct sockaddr_in clientAddr);


    void adjustTimer(util_timer* timer);
    void dealTimer(util_timer* timer, int sockfd);
    bool dealClientData();
    bool dealSignal(bool& timeout, bool& stopServer);
    void dealRread(int sockfd);
    void dealWrite(int sockfd);

public:
    int m_port;
    char* m_root;
    int m_logWrite;
    int m_closeLog;
    int m_actorModel;
    int m_optLinger;
    int m_trigMode;
    int m_listenTrigMode;
    int m_connTrigMode;

    int m_listenfd;
    int m_pipefd[2];
    int m_epollfd;
    HttpConn* users;


    // 数据库相关
    ConnectionPool* m_connPool;
    std::string m_user;
    std::string m_passWord;
    std::string m_databaseName;
    int m_sqlNum;


    // 线程池相关
    ThreadPool<HttpConn>* m_threadPool;
    int m_threadNum;


    // epoll_event相关
    epoll_event events[MAX_EVENT_NUMBER];


    // 定时器相关
    client_data* usersTimer;
    Utils utils;
};
#endif