#ifndef HTTP_HTTPCONN_H
#define HTTP_HTTPcONN_H

#include "lock/locker.hpp"
#include "mysql/sqlConnectionPool.hpp"
#include "threadpool/threadPool.hpp"
#include "log/log.hpp"
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string>
#include <sys/mman.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <sys/stat.h>
#include <unordered_map>


#include <signal.h>
#include <assert.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>

class HttpConn {
public:
    // 读取文件的文件名长度
    static const int FILENAME_LEN = 200;
    // 读缓存大小
    static const int READ_BUFFER_SZ = 2048;
    // 写缓存大小
    static const int WRITE_BUFFER_SZ = 1024;

    // http请求方法
    enum METHOD {
        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT,
        PATH
    };

    // 主状态机状态
    enum CHECK_STATE {
        CHECK_STATE_REQUESTLINE = 0,
        CHECK_STATE_HEADER,
        CHECK_STATE_CONTENT
    };

    // http状态码
    enum HTTP_CODE {
        NO_REQUEST,
        GET_REQUEST,
        BAD_REQUEST,
        NO_RESOURCE,
        FORBIDDEN_REQUEST,
        FILE_REQUEST,
        INTERNAL_ERROR,
        CLOSED_CONNECTION
    };

    // 从状态机的状态
    enum LINE_STATUS {
        LINE_OK = 0,
        LINE_BAD,
        LINE_OPEN
    };

public:
    /* 使用默认的构造函数和析构函数
       真正的初始化在init函数
       真正的关闭连接在closeConn函数*/
    HttpConn() {}
    ~HttpConn() {}

public:
    // 初始化套接字，一些私有变量
    void init(int sockfd, const sockaddr_in& addr, char*, int, int, std::string user, std::string passwd, std::string sqlName);
    // 关闭http连接
    void closeConn(bool realClose = true);


    // http处理函数，被线程池中的线程调用
    void process();
    // 读取浏览器发送的数据
    bool readOnce();
    // 给浏览器端发送数据
    bool write();


    // 获取客户端socket地址
    sockaddr_in* getAddr() {
        return &m_addr;
    }


    // 读取数据库中的数据
    void initMysqlResult(ConnectionPool* connPool);


    // 时间时间类型
    int timerFlag;
    int improv;

private:
    // public的init调用
    void init();
    // 从m_readBuf读取请求报文，并处理
    HTTP_CODE processRead();
    // 向m_writeBuf写入响应报文
    bool processWrite(HTTP_CODE ret);
    // 主状态机解析请求报文的请求行
    HTTP_CODE parseRequestLine(char* text);
    // 主状态机解析请求报文的请求头
    HTTP_CODE parseHeaders(char* text);
    // 主状态机解析请求报文的请求内容
    HTTP_CODE parseContent(char* text);
    // 生成响应报文
    HTTP_CODE doRequest();

    // 获取请求报文中的一行
    char* getLine() {
        return m_readBuf + m_startLine;
    }
    // 从状态机将请求报文中的一行\r\n设置为\0\0
    LINE_STATUS parseLine();
    
    // 释放文件的内存映射区
    void unmap();

    // 根据响应报文的格式，生成对应的部分，以下函数均由doRequest调用
    bool addResponse(const char* format, ...);
    bool addContent(const char* content);
    bool addStatusLine(int status, const char* title);
    bool addHeaders(int contentLen);
    bool addContentType();
    bool addContentLength(int contentLen);
    bool addLinger();
    bool addBlankLine();


public:
    // 全局唯一的epollfd, 主线程和线程池中所有的线程共享
    static int m_epollfd;
    // 当前处理的用户数
    static int m_userCount;
    // 从数据库连接池中获取一个mysql连接
    MYSQL* mysql;
    // IO事件类型，0读 1写
    int m_state;


private:
    // 与客户端的连接socket
    int m_sockfd;
    // 客户端socket地址
    sockaddr_in m_addr;

    // 存储读取的请求报文段
    char m_readBuf[READ_BUFFER_SZ];
    // 读缓冲区m_readBuf中数据的最后一个字节的下一个位置
    int m_readIdx;
    // 读缓冲区m_readBuf目前已经读取的字符的下一个位置
    int m_checkIdx;
    // 读缓冲区m_readBuf目前已经处理的行的下一个位置
    int m_startLine;

    // 存储发送的响应报文段
    char m_writeBuf[WRITE_BUFFER_SZ];
    // 指示buffer中的数据长度 
    int m_writeIdx;
    
    // 主状态机的状态
    CHECK_STATE m_checkState;
    // 请求方法
    METHOD m_method;

    // 解析请求报文，将相应信息写入变量
    // 存储请求的文件名
    char m_realFile[FILENAME_LEN];
    char* m_url;
    char* m_version;
    char* m_host;
    int m_contentLen;
    bool m_linger;

    // 服务器上的文件地址
    char* m_fileAddr;
    struct stat m_fileStat;

    // 聚集写
    struct iovec m_iv[2];
    int m_iv_cnt;
    // 是否启用post
    int cgi;
    // 存储请求头数据
    char* m_string;

    // 剩余发送字节数
    int byteToSend;
    // 已经发送的字节数
    int byteHaveSend;
    char* docRoot;

    // 存储数据库中的用户信息
    std::unordered_map<std::string, std::string> m_user;
    int m_trigMode;
    int m_closeLog;

    // 与mysql连接相关的信息
    char sqlUser[100];
    char sqlPasswd[100];
    char sqlDBName[100];

};



#endif