#include "http/httpConn.hpp"
#include <mysql/mysql.h>
#include <fstream>

// 定义http响应的状态信息
const char *ok_200_title = "OK";

const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";

const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";

const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";

const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";

// 定义全局变量
Locker m_lock;
std::unordered_map<std::string, std::string> users;

// 初始化HttpConn静态变量
int HttpConn::m_userCount = 0;
int HttpConn::m_epollfd = -1;



// 下面定义一些普通函数，被类的成员函数调用
// 将文件描述符设置为非阻塞
int setNonBlocking(int fd) {
    int oldOption = fcntl(fd, F_GETFL);
    int newOption = oldOption | O_NONBLOCK;
    fcntl(fd, F_SETFL, newOption);
    return oldOption;
}

// 向内核事件表注册读事件，根据trigMode决定是否开启ET模式
void addFd(int epollfd, int fd, bool oneShot, int trigMode) {
    epoll_event event;
    event.data.fd = fd;
    if (1 == trigMode) {
        event.events = EPOLLIN | EPOLLET | EPOLLHUP;
    } else {
        event.events = EPOLLIN | EPOLLHUP;
    }

    if (oneShot) {
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setNonBlocking(fd);
}

// 从内核事件表删除描述符
void removeFd(int epollfd, int fd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

// 将事件重置为EPOLLONESHOT
void modFd(int epollfd, int fd, int ev, int trigMode) {
    epoll_event event;
    event.data.fd = fd;
    if (1 == trigMode) {
        event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    } else {
        event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
    }
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}



// 下面实现HttpConn类中的成员函数
// 初始化连接，外部调用初始化socket地址
void HttpConn::init(int sockfd, 
          const sockaddr_in& addr, 
          char* root, 
          int trigMode, 
          int closeLog, 
          std::string user, 
          std::string passwd, 
          std::string sqlName) {
    m_sockfd = sockfd;
    m_addr = addr;
    
    addFd(m_epollfd, sockfd, true, m_trigMode);
    m_userCount += 1;

    docRoot = root;
    m_trigMode = trigMode;
    m_closeLog = closeLog;

    strcpy(sqlUser, user.c_str());
    strcpy(sqlPasswd, passwd.c_str());
    strcpy(sqlDBName, sqlName.c_str());

    init();
}

// 初始化连接, 被上面的函数调用
void HttpConn::init() {
    mysql = nullptr;
    byteHaveSend = 0;
    byteToSend = 0;
    m_checkState = CHECK_STATE_REQUESTLINE;
    m_linger = false;
    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_host = 0;
    m_startLine = 0;
    m_readIdx = 0;
    m_writeIdx = 0;
    cgi = 0;
    m_state = 0;
    timerFlag = 0;
    improv = 0;

    memset(m_readBuf, '\0', READ_BUFFER_SZ);
    memset(m_writeBuf, '\0', WRITE_BUFFER_SZ);
    memset(m_realFile, '\0', FILENAME_LEN);
}

// 关闭一个连接，用户数减1
void HttpConn::closeConn(bool realClose) {
    if (realClose && m_sockfd != -1) {
        printf("close %d\n", m_sockfd);
        removeFd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_userCount -= 1;
    }
}

// 初始化mysql连接，将用户数据表读入unordered_map中
void HttpConn::initMysqlResult(ConnectionPool* connPool) {
    MYSQL* mysql = nullptr;
    ConnectionPoolRAII mysqlConn(&mysql, connPool);

    if (mysql_query(mysql, "SELECT username,passwd FROM user")) {
        printf("SELECT error:%s\n", mysql_error(mysql));
    }

    MYSQL_RES* result = mysql_store_result(mysql);
    while (MYSQL_ROW row = mysql_fetch_row(result)) {
        std::string name(row[0]), passwd(row[1]);
        users[name] = passwd;
    }
}

// 从状态机
HttpConn::LINE_STATUS HttpConn::parseLine() {
    char tmp;
    for (; m_checkIdx < m_readIdx; ++m_checkIdx) {
        tmp = m_readBuf[m_checkIdx];
        if ('\r' == tmp) {
            if (m_checkIdx + 1 == m_readIdx) {
                return LINE_OPEN;
            } else if (m_readBuf[m_checkIdx + 1] == '\n') {
                m_readBuf[m_checkIdx++] = '\0';
                m_readBuf[m_checkIdx++] = '\0';
                return LINE_OK;
            } else {
                return LINE_BAD;
            }
        } else if ('\n' == tmp) {
            if (m_checkIdx >= 1 && m_readBuf[m_checkIdx - 1] == '\r') {
                m_readBuf[m_checkIdx - 1] = '\0';
                m_readBuf[m_checkIdx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

// 循环读取客户端数据，直到无数据可读或者对方关闭连接
// 非阻塞ET模式下，需要一次性将数据读完
bool HttpConn::readOnce() {
    if (m_readIdx >= READ_BUFFER_SZ) {
        return false;
    }
    int bytesRead = 0;

    // LT模式下读取数据
    if (0 == m_trigMode) {
        bytesRead = recv(m_sockfd, m_readBuf + m_readIdx, READ_BUFFER_SZ - m_readIdx, 0);
        m_readIdx += bytesRead;
        if (bytesRead <= 0) {
            return false;
        }
        return true;
    } else {    //ET模式下读取数据
        while (1) {
            bytesRead = recv(m_sockfd, m_readBuf + m_readIdx, READ_BUFFER_SZ - m_readIdx, 0);
            if (bytesRead == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;
                }
                return false;
            } else if (0 == bytesRead) {
                return false;
            } else {
                m_readIdx += bytesRead;
            }
        }
        return true;
    }
}

HttpConn::HTTP_CODE HttpConn::parseRequestLine(char* text) {
    //strpbrk在源字符串（s1）中找出最先含有搜索字符串（s2）
    //中任一字符的位置并返回，若找不到则返回空指针
    m_url = strpbrk(text, " \t");
    if (m_url == nullptr) {
        return BAD_REQUEST;
    }
    *m_url++ = '\0';

    // 保存请求方法
    char* method = text;
    if (strcasecmp(method, "GET") == 0) {
        m_method = GET;
    } else if (strcasecmp(method, "POST") == 0) {
        m_method = POST;
    } else {
        return BAD_REQUEST;
    }

    //m_url此时跳过了第一个空格或\t字符，但不知道之后是否还有
    //将m_url向后偏移，通过查找
    //继续跳过空格和\t字符，指向请求资源的第一个字符
    //strspn:检索字符串 str1 中第一个不在字符串 str2 中出现的字符下标
    //即跳过匹配的字符串片段
    m_url += strspn(m_url, " \t");

    // 采用相同的逻辑来判断HTTP版本号
    m_version = strpbrk(m_url, " \t");
    if (m_version == nullptr) {
        return BAD_REQUEST;
    } 
    *m_version = '\0';
    m_version += strspn(m_version, " \t");
    // 仅支持HTTP/1.1
    if (strcasecmp(m_version, "HTTP/1.1") != 0) {
        return BAD_REQUEST;
    }

    //对请求资源前7个字符进行判断
    //这里主要是有些报文的请求资源中会带有http://
    //这里需要对这种情况进行单独处理
    if (strncasecmp(m_url, "http://", 7) == 0) {
        m_url += 7;
        m_url = strchr(m_url, '/');
    } else if (strncasecmp(m_url, "https://", 8) == 0) {
        m_url += 8;
        m_url = strchr(m_url, '/');
    }
    //一般的不会带有上述两种符号，直接是单独的/或/后面带访问资源
    if (m_url == nullptr || m_url[0] != '/') {
        return BAD_REQUEST;
    }
    //当url为/时，显示欢迎界面
    if (strlen(m_url) == 1) {
        strcat(m_url, "judge.html");
    }

    //请求行处理完毕，将主状态机转移处理请求头部
    m_checkState = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

HttpConn::HTTP_CODE HttpConn::parseHeaders(char* text) {
    //判断是空行还是请求头
    if (text[0] == '\0') {
        //判断是GET还是POST请求
        //!0 is POST
        if (m_contentLen != 0) {
            m_checkState = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        //==0 is GET
        return GET_REQUEST;
    } else if (strncasecmp(text, "Connection:", 11) == 0) {
        text += 11;
        //跳过空格和\t字符
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0) {
            //如果是长连接，则将linger标志设置为true
            m_linger = true;
        }
    } else if (strncasecmp(text, "Content-length:", 15) == 0) {
        text += 15;
        text += strspn(text, " \t");
        m_contentLen = atol(text);
    } else if (strncasecmp(text, "Host:", 5) == 0) {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    } else {
        printf("oop!unkown header: %s", text);
    }
    return NO_REQUEST;
}
   
// 判断http请求是否被完整读入
HttpConn::HTTP_CODE HttpConn::parseContent(char* text) {
    if (m_readIdx >= (m_contentLen + m_checkIdx)) {
        text[m_contentLen] = '\0';
        m_string = text;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

HttpConn::HTTP_CODE HttpConn::processRead() {
    LINE_STATUS lineStatus = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char* text = nullptr;

    while ( (m_checkState == CHECK_STATE_CONTENT && lineStatus == LINE_OK) || ((lineStatus = parseLine()) == LINE_OK) ) {
        text = getLine();
        m_startLine = m_checkIdx;
        switch (m_checkState) {
            case CHECK_STATE_REQUESTLINE: {
                ret = parseRequestLine(text);
                if (BAD_REQUEST == ret) {
                    return BAD_REQUEST;
                }
                break;
            }
            case CHECK_STATE_HEADER: {
                ret = parseHeaders(text);
                if (BAD_REQUEST == ret) {
                    return BAD_REQUEST;
                } else if (ret == GET_REQUEST) {
                    return doRequest();
                }
                break;
            }
            case CHECK_STATE_CONTENT: {
                ret = parseContent(text);
                if (ret == GET_REQUEST) {
                    return doRequest();
                }
                lineStatus = LINE_OPEN;
                break;
            }
            default: 
                return INTERNAL_ERROR;
        }
    }
    return NO_REQUEST;
}

/*
http://ihindy.xyz:9006/0
http://ihindy.xyz:9006/1
http://ihindy.xyz:9006/2CGISQL.cgi 登录进入的页面
http://ihindy.xyz:9006/3CGISQL.cgi 注册进入的页面
http://ihindy.xyz:9006/5
*/
//功能逻辑单元
HttpConn::HTTP_CODE HttpConn::doRequest() {
    strcpy(m_realFile, docRoot);
    int len = strlen(docRoot);
    const char* p = strrchr(m_url, '/');

    // 处理cgi
    if (cgi == 1 && (*(p + 1) == '2' || *(p + 1) == '3')) {
        //根据标志判断是登录检测还是注册检测
        char flag = m_url[1];

        char* m_urlReal = (char*)malloc(sizeof(char) * 200);
        strcpy(m_urlReal, "/");
        strcat(m_urlReal, m_url + 2);
        strncpy(m_realFile + len, m_urlReal, FILENAME_LEN - len - 1);
        free(m_urlReal);

        //将用户名和密码提取出来
        //eg:user=123&passwd=123
        char name[100], passwd[100];
        int i;
        for (i = 5; m_string[i] != '&'; ++i) {
            name[i - 5] = m_string[i];
        }
        name[i - 5] = '\0';

        int j = 0;
        for (i = i + 10; m_string[i] != '\0'; ++i, ++j) {
            passwd[j] = m_string[i];
        }
        passwd[j] = '\0';


        //如果是注册，先检测数据库中是否有重名的
        //没有重名的，进行增加数据
        if (*(p + 1) == '3') {
            // 构建一个mysql的插入语句
            char *sql_insert = (char *)malloc(sizeof(char) * 200);
            strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, name);
            strcat(sql_insert, "', '");
            strcat(sql_insert, passwd);
            strcat(sql_insert, "')");

            if (users.find(name) == users.end()) {
                m_lock.lock();
                int res = mysql_query(mysql, sql_insert);
                users.insert({name, passwd});
                m_lock.unlock();

                if (!res) {
                    strcpy(m_url, "/log.html");
                } else {
                    strcpy(m_url, "/registerError.html");
                }
            } else {
                strcpy(m_url, "/registerError.html");
            }
        } else if (*(p + 1) == '2') {
            //如果是登录，直接判断
            //若浏览器端输入的用户名和密码在表中可以查找到，返回1，否则返回0
            if (users.find(name) != users.end() && passwd == users[name]) {
                strcpy(m_url, "/welcome.html");
            } else {
                strcpy(m_url, "/logError.html");
            }
        }
    } 

    //如果请求资源为/0，表示跳转注册界面
    if (*(p + 1) == '0') {
        char* m_urlReal = (char*)malloc(sizeof(char) * 200);
        strcpy(m_urlReal, "/register.html");
        strncpy(m_realFile + len, m_urlReal, strlen(m_urlReal));
        free(m_urlReal);
    } else if (*(p + 1) == '1') { //如果请求资源为/1，表示跳转登录界面
        char *m_urlReal = (char *)malloc(sizeof(char) * 200);
        strcpy(m_urlReal, "/log.html");
        strncpy(m_realFile + len, m_urlReal, strlen(m_urlReal));
        free(m_urlReal);
    } else if (*(p + 1) == '5') { //如果请求资源为/5，表示跳转pic
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/picture.html");
        strncpy(m_realFile + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    } else if (*(p + 1) == '6') { //如果请求资源为/6，表示跳转video
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/video.html");
        strncpy(m_realFile + len, m_url_real, strlen(m_url_real)); 
        free(m_url_real);
    } else if (*(p + 1) == '7') { //如果请求资源为/7，表示跳转weixin
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/fans.html");
        strncpy(m_realFile + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    } else {
        //如果以上均不符合，即不是登录和注册，直接将url与网站目录拼接
        strncpy(m_realFile + len, m_url, FILENAME_LEN - len - 1);
    }

    //通过stat获取请求资源文件信息，成功则将信息更新到m_file_stat结构体
    //失败返回NO_RESOURCE状态，表示资源不存在
    if (stat(m_realFile, &m_fileStat) < 0)
        return NO_RESOURCE;
    //判断文件类型，如果是目录，则返回BAD_REQUEST，表示请求报文有误
    if (S_ISDIR(m_fileStat.st_mode))
        return BAD_REQUEST;
    
    //以只读方式获取文件描述符，通过mmap将该文件映射到内存中
    int fd = open(m_realFile, O_RDONLY);
    m_fileAddr = (char *)mmap(0, m_fileStat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    //避免文件描述符的浪费和占用
    close(fd);

    //表示请求文件存在，且可以访问
    return FILE_REQUEST;
}

// 取消内存映射
void HttpConn::unmap() {
    if (m_fileAddr) {
        munmap(m_fileAddr, m_fileStat.st_size);
        m_fileAddr = 0;
    }
}

bool HttpConn::write() {
    int tmp = 0;

    //表示响应报文为空，一般不会出现这种情况
    if (byteToSend == 0) {
        modFd(m_epollfd, m_sockfd, EPOLLIN, m_trigMode);
        init();
        return true;
    }

    while (1) {
        tmp = writev(m_sockfd, m_iv, m_iv_cnt);
        if (tmp < 0) {
            //判断缓冲区是否满了
            if (errno == EAGAIN) {
                modFd(m_epollfd, m_sockfd, EPOLLOUT, m_trigMode);
                return true;
            }
            unmap();
            return false;
        }

        // 更新已发送和未发送的字节数
        byteHaveSend += tmp;
        byteToSend -= tmp;

        if (byteHaveSend >= m_iv[0].iov_len) {
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_fileAddr + byteHaveSend - m_writeIdx;
            m_iv[1].iov_len = byteToSend;
        } else {
            m_iv[0].iov_base = m_writeBuf + byteHaveSend;
            m_iv[0].iov_len = m_iv[0].iov_len - byteHaveSend;
        }

        //判断条件，数据已全部发送完 
        if (byteToSend <= 0) {
            unmap();
            modFd(m_epollfd, m_sockfd, EPOLLIN, m_trigMode);
            if (m_linger) {
                init();
                return true;
            } else {
                return false;
            }
        }
    }
}

// 添加响应报文的公共函数
bool HttpConn::addResponse(const char* format, ...) {
    //如果写入内容超出m_write_buf大小则报错
    if (m_writeIdx >= WRITE_BUFFER_SZ) {
        return false;
    }

    //定义可变参数列表
    va_list arg_list;
    //将变量arg_list初始化为传入参数
    va_start(arg_list, format);
    //将数据format从可变参数列表写入缓冲区写，返回写入数据的长度
    int len = vsnprintf(m_writeBuf + m_writeIdx, WRITE_BUFFER_SZ - 1 - m_writeIdx, format, arg_list);
    //如果写入的数据长度超过缓冲区剩余空间，则报错
    if (len >= (WRITE_BUFFER_SZ - 1 - m_writeIdx)) {
        va_end(arg_list);
        return false;
    }
    m_writeIdx += len;
    va_end(arg_list);
    printf("request:%s", m_writeBuf);
    return true;
}

// 添加状态行
bool HttpConn::addStatusLine(int status, const char* title) {
    return addResponse("%s %d %s\r\n", "HTTP/1.1", status, title);
}

//添加响应报文的消息报头，具体的添加文本长度、连接状态和空行
bool HttpConn::addHeaders(int contentLen) {
    return addContentLength(contentLen) && addLinger() && addBlankLine();
}

//添加文本content
bool HttpConn::addContent(const char* content) {
    return addResponse("%s", content);
}

//添加文本类型，这里是html
bool HttpConn::addContentType() {
    return addResponse("Content-Type:%s\r\n", "text/html");
}

// 添加Content-Length，表示响应报文的长度
bool HttpConn::addContentLength(int contentLen) {
    return addResponse("Content-Length:%d\r\n", contentLen);
}

// 添加连接状态，通知浏览器端是保持连接还是关闭
bool HttpConn::addLinger() {
    return addResponse("Connection:%s\r\n", (m_linger == true) ? "keep-alive" : "close");
}

// 添加空行
bool HttpConn::addBlankLine() {
    return addResponse("%s", "\r\n");
}

// 生成响应报文
bool HttpConn::processWrite(HTTP_CODE ret) {
    switch (ret) {
        case INTERNAL_ERROR: {
            addStatusLine(500, error_500_title);
            addHeaders(strlen(error_500_form));
            if (addContent(error_500_form) == false) {
                return false;
            }
            break;
        }
        case BAD_REQUEST: {
            addStatusLine(404, error_404_title);
            addHeaders(strlen(error_404_form));
            if (addContent(error_404_form) == false) {
                return false;
            }
            break;
        }
        case FORBIDDEN_REQUEST: {
            addStatusLine(403, error_404_title);
            addHeaders(strlen(error_400_form));
            if (addContent(error_403_form) == false) {
                return false;
            }
            break;
        }
        case FILE_REQUEST: {
            addStatusLine(200, ok_200_title);
            if (m_fileStat.st_size != 0) {
                addHeaders(m_fileStat.st_size);
                m_iv[0].iov_base = m_writeBuf;
                m_iv[0].iov_len = m_writeIdx;
                m_iv[1].iov_base = m_fileAddr;
                m_iv[1].iov_len = m_fileStat.st_size;
                m_iv_cnt = 2;
                byteToSend = m_writeIdx + m_fileStat.st_size;
                return true;
            } else {
                const char *ok_string = "<html><body></body></html>";
                addHeaders(strlen(ok_string));
                if (addContent(ok_string) == false) {
                    return false;
                }
            }
        }
        default: {
            return false;
        }
    }

    m_iv[0].iov_base = m_writeBuf;
    m_iv[0].iov_len = m_writeIdx;
    m_iv_cnt = 1;
    byteToSend = m_writeIdx;
    return true;
}

// 处理http报文请求与报文响应
void HttpConn::process() {
    HTTP_CODE ret = processRead();

    //NO_REQUEST，表示请求不完整，需要继续接收请求数据
    if(NO_REQUEST == ret) {
        modFd(m_epollfd, m_sockfd, EPOLLIN, m_trigMode);
        return;
    }

    // 调用process_write完成报文响应
    bool write_ret = processWrite(ret);
    if (!write_ret) {
        closeConn();
    }

    // 注册并监听写事件
    modFd(m_epollfd, m_sockfd, EPOLLOUT, m_trigMode);
}
