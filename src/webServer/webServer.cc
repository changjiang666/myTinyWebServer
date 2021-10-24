#include "webServer/webServer.hpp"

// 主要完成服务器初始化：http连接、根目录、定时器
WebServer::WebServer() {
    users = new HttpConn[MAX_FD];

    // root文件夹路径
    char serverPath[200];
    getcwd(serverPath, 200);
    char root[6] = "/root";
    m_root = (char*) malloc(strlen(serverPath) + strlen(root) + 1);
    strcpy(m_root, serverPath);
    strcat(m_root, root);

    // 定时器
    usersTimer = new client_data[MAX_FD];
}

// 服务器资源释放
WebServer::~WebServer() {
    close(m_epollfd);
    close(m_listenfd);
    close(m_pipefd[0]);
    close(m_pipefd[1]);
    delete[] users;
    delete[] usersTimer;
    delete m_threadPool;
}

// 初始化用户名、数据库等信息
void WebServer::init(int port,
          std::string user,
          std::string passWord,
          std::string databaseName,
          int logWrite,
          int optLinger,
          int trigMode,
          int sqlNum,
          int threadNum,
          int closeLog,
          int actorModel) {
    m_port = port;
    m_user = user;
    m_passWord = passWord;
    m_databaseName = databaseName;
    m_logWrite = logWrite;
    m_optLinger = optLinger;
    m_trigMode = trigMode;
    m_sqlNum = sqlNum;
    m_threadNum = threadNum;
    m_closeLog = closeLog;
    m_actorModel = actorModel;
}

// 设置epoll的触发模式
void WebServer::trigMode() {
    // LT + ET
    if (0 == m_trigMode) {
        m_listenTrigMode = 0;
        m_connTrigMode = 0;
    } else if (1 == m_trigMode) {
        m_listenTrigMode = 0;
        m_connTrigMode = 1;
    } else if (2 == m_trigMode) {
        m_listenTrigMode = 1;
        m_connTrigMode = 0;
    } else if (3 == m_trigMode) {
        m_listenTrigMode = 1;
        m_connTrigMode = 1;
    }
}

// 初始化日志
void WebServer::logWrite() {
    if (0 == m_closeLog)
    {
        //初始化日志
        if (1 == m_logWrite)
            Log::get_instance()->init("./ServerLog", m_closeLog, 2000, 800000, 800);
        else
            Log::get_instance()->init("./ServerLog", m_closeLog, 2000, 800000, 0);
    }
}

// 初始化数据库连接池
void WebServer::sqlPool() {
    m_connPool = ConnectionPool::getInstance();
    m_connPool->init("localhost", m_user, m_passWord, m_databaseName, 3306, m_sqlNum, m_closeLog);

    // 初始化数据库读取表
    users->initMysqlResult(m_connPool);
}

// 创建线程池
void WebServer::threadPool() {
    m_threadPool = new ThreadPool<HttpConn>(m_actorModel, m_connPool, m_threadNum);
}

// 创建网络编程
void WebServer::eventListen() {
    // 创建监听套接字
    m_listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(m_listenfd >= 0);

    // 优雅关闭连接
    if (0 == m_optLinger) {
        struct linger tmp = {0, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp)); 
    } else if (1 == m_optLinger) {
        struct linger tmp = {1, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }

    // 设置套接字地址
    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(m_port);

    // 允许端口重用
    int flag = 1;
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));

    // 命名套接字
    ret = bind(m_listenfd, (struct sockaddr*)&address, sizeof(address));
    assert(ret >= 0);

    // 监听套接字
    ret = listen(m_listenfd, 5);
    assert(ret >= 0);
    
    // 初始化时间槽
    utils.init(TIMESLOT);

    // 创建epoll内核事件表
    m_epollfd = epoll_create(5);
    assert(m_epollfd != -1);
    epoll_event events[MAX_EVENT_NUMBER];

    // 监听m_listenfd上的读事件
    utils.addfd(m_epollfd, m_listenfd, false, m_listenTrigMode);
    HttpConn::m_epollfd = m_epollfd;

    // 创建管道，用于信号和主线程之间的通信
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd);
    assert(ret != -1);
    utils.setnonblocking(m_pipefd[1]);
    utils.addfd(m_epollfd, m_pipefd[0], false, 0);

    // 注册信号
    utils.addsig(SIGPIPE, SIG_IGN);
    utils.addsig(SIGALRM, utils.sig_handler, false);
    utils.addsig(SIGTERM, utils.sig_handler, false);

    alarm(TIMESLOT);

    // 工具类,信号和描述符基础操作
    Utils::u_epollfd = m_epollfd;
    Utils::u_pipefd = m_pipefd;
}

// 初始化定时器
void WebServer::timer(int connfd,
                      struct sockaddr_in clientAddr) {
    users[connfd].init(connfd, clientAddr, m_root, m_connTrigMode, 
                       m_closeLog, m_user, m_passWord, m_databaseName);
    
    //初始化client_data数据
    //创建定时器，设置回调函数和超时时间，绑定用户数据，将定时器添加到链表中
    usersTimer[connfd].sockfd = connfd;
    usersTimer[connfd].address = clientAddr;
    util_timer* timer = new util_timer;
    timer->user_data = &usersTimer[connfd];
    timer->cb_func = cb_func;
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;
    usersTimer[connfd].timer = timer;
    utils.m_timer_lst.add_timer(timer);
}

// 若有数据传输，则将定时器往后延迟3个单位
// 并对新的定时器在链表上的位置进行调整
void WebServer::adjustTimer(util_timer* timer) {
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;
    utils.m_timer_lst.adjust_timer(timer);
    // printf("%s\n", "adjust timer once");
}

// 删除定时器
void WebServer::dealTimer(util_timer* timer, int sockfd) {
    timer->cb_func(&usersTimer[sockfd]);
    if (timer) {
        utils.m_timer_lst.del_timer(timer);
    }
    printf("close fd %d\n", usersTimer[sockfd].sockfd);
}

// http处理用户数据
bool WebServer::dealClientData() {
    struct sockaddr_in clientAddr;
    socklen_t clientAddrLen = sizeof(clientAddr);

    
    if (0 == m_listenTrigMode) {
        // listenTrigMode == LT
        int connfd = accept(m_listenfd, (sockaddr*)&clientAddr, &clientAddrLen);
        printf("exe accept\n");
        if (connfd < 0) {
            printf("accept error: error is: %d", errno);
            return false;
        }

        if (HttpConn::m_userCount >= MAX_FD) {
            utils.show_error(connfd, "Internal server busy");
            printf("Internal server busy");
            return false;
        }
        timer(connfd, clientAddr);
    } else {
        // listenTrigMode == ET
        while (1) {
            int connfd = accept(m_listenfd, (sockaddr*)&clientAddr, &clientAddrLen);
            if (connfd < 0) {
                printf("accept error: error is: %d", errno);
                break;
            }

            if (HttpConn::m_userCount >= MAX_FD) {
                utils.show_error(connfd, "Internal server busy");
                printf("Internal server busy");
                break;
            }
            timer(connfd, clientAddr);
        }
        return false;
    }
    return true;
}

// 处理定时器信号,set the timeout ture
bool WebServer::dealSignal(bool& timeout, bool& stopServer) {
    int ret = 0;
    int sig;
    char signals[1024];
    ret = recv(m_pipefd[0], signals, sizeof(signals), 0);
    if (-1 == ret || 0 == ret) {
        return false;
    } else {
        for (int i = 0; i < ret; ++i) {
            switch (signals[i]) {
                case SIGALRM: {
                    timeout = true;
                    break;
                }
                case SIGTERM: {
                    stopServer = true;
                    break;
                }
                default: {
                    break;
                }
            }
        }
    }
    return true;
}

// 处理客户连接上接收到的数据
void WebServer::dealRread(int sockfd) {
    util_timer* timer = usersTimer[sockfd].timer;
    if (1 == m_actorModel) {
        // reactor
    } else {
        // proactor
        if (users[sockfd].readOnce()) {
            m_threadPool->append(users + sockfd);
            if (timer) {
                adjustTimer(timer);
            }
        } else {
            dealTimer(timer, sockfd);
        }
    }
}

// 写操作
void WebServer::dealWrite(int sockfd) {
    util_timer* timer = usersTimer[sockfd].timer;
    if (1 == m_actorModel) {
        // reactor
    } else {
        // proactor
        if (users[sockfd].write()) {
            if (timer) {
                adjustTimer(timer);
            }
        } else {
            dealTimer(timer, sockfd);
        }
    }
}

// 事件回环（即服务器主线程）
void WebServer::eventLoop() {
    bool timeout = false;
    bool stopServer = false;

    while (!stopServer) {
        int num = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
        if (num < 0 && errno != EINTR) {
            printf("epoll failure\n");
            break;
        }
        
        for (int i = 0; i < num; ++i) {
            int sockfd = events[i].data.fd;
            
            if (sockfd == m_listenfd) {
                //处理新到的客户连接
                bool flag = dealClientData();
                if (false == flag) {
                    continue;
                }
            } else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                //处理异常事件
                util_timer* timer = usersTimer[sockfd].timer;
                dealTimer(timer, sockfd);
            } else if ((sockfd == m_pipefd[0]) && (events[i].events & EPOLLIN)) {
                //接收到SIGALRM信号，timeout设置为True
                bool flag = dealSignal(timeout, stopServer);
                if (false == flag) {
                    printf("dealclientdata failure\n");
                }
            } else if (events[i].events & EPOLLIN) {
                //处理客户连接上接收到的数据
                dealRread(sockfd);
            } else if (events[i].events & EPOLLOUT) {
                //处理客户连接上send的数据
                dealWrite(sockfd);
            }
        }

        if (timeout) {
            utils.timer_handler();
            printf("time tick\n");
            timeout = false;
        }
    }
}