#include "timer/lst_timer.hpp"
#include "log/log.hpp"
#include <iostream>
using namespace std;

#define FD_LIMIT 65535
#define MAX_EVENT_NUMBER 1024
#define TIMESLOT 5
#define BUFFER_SIZE 1000


// 局部代替http类的功能
struct UserData {
    char buf[BUFFER_SIZE];
};
UserData usersData[FD_LIMIT];


int main(int argc, char* argv[]) {
    if( argc <= 2 ) {
        printf( "usage: %s ip_address port_number\n", basename( argv[0] ) );
        return 1;
    }

    const char* ip = argv[1];
    int port = atoi( argv[2] );
    int ret = 0;

    // 设置服务器端的socket地址
    struct sockaddr_in address;
    bzero( &address, sizeof( address ) );
    address.sin_family = AF_INET;
    inet_pton( AF_INET, ip, &address.sin_addr );
    address.sin_port = htons( port );

    // 创建监听socket
    int listenfd = socket( PF_INET, SOCK_STREAM, 0 );
    assert( listenfd >= 0 );

    // 命名socket
    ret = bind( listenfd, ( struct sockaddr* )&address, sizeof( address ) );
    assert( ret != -1 );

    // 监听socket
    ret = listen( listenfd, 5 );
    assert( ret != -1 );

    // 创建内核事件表
    epoll_event events[ MAX_EVENT_NUMBER ];
    int epollfd = epoll_create( 5 );
    assert( epollfd != -1 );

    // 内核事件表上 add 监听套接字的读事件
    Utils utils;
    utils.init(TIMESLOT);
    utils.addfd( epollfd, listenfd, false, 0 );
    utils.u_epollfd = epollfd;

    // 创建管道
    int pipefd[2];
    ret = socketpair( PF_UNIX, SOCK_STREAM, 0, pipefd );
    assert( ret != -1 );
    utils.setnonblocking( pipefd[1] );
    utils.addfd( epollfd, pipefd[0], false, 0 );
    utils.u_pipefd = pipefd;

    // add all the interesting signals here
    utils.addsig(SIGPIPE, SIG_IGN);
    utils.addsig(SIGALRM, utils.sig_handler, false);
    utils.addsig(SIGTERM, utils.sig_handler, false);


    bool stop_server = false;
    bool timeout = false;
    client_data* users = new client_data[FD_LIMIT]; 
    alarm( TIMESLOT );


    while( !stop_server ) {
        int number = epoll_wait( epollfd, events, MAX_EVENT_NUMBER, -1 );
        if ( ( number < 0 ) && ( errno != EINTR ) ) {
            printf( "epoll failure\n" );
            break;
        }
    
        for ( int i = 0; i < number; i++ ) {
            int sockfd = events[i].data.fd;
            if( sockfd == listenfd ) {
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof( client_address );
                int connfd = accept( listenfd, ( struct sockaddr* )&client_address, &client_addrlength );
                utils.addfd( epollfd, connfd, true, 0);

                users[connfd].address = client_address;
                users[connfd].sockfd = connfd;
                util_timer* timer = new util_timer;
                timer->user_data = &users[connfd];
                timer->cb_func = cb_func;
                time_t cur = time( NULL );
                timer->expire = cur + 3 * TIMESLOT;
                users[connfd].timer = timer;
                utils.m_timer_lst.add_timer( timer );
            }
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                //服务器端关闭连接，移除对应的定时器
                util_timer *timer = users[sockfd].timer;
                timer->cb_func(&users[sockfd]);
                if (timer)
                {
                    utils.m_timer_lst.del_timer(timer);
                }
            }
            else if( ( sockfd == pipefd[0] ) && ( events[i].events & EPOLLIN ) ) {
                int sig;
                char signals[1024];
                ret = recv( pipefd[0], signals, sizeof( signals ), 0 );
                if( ret == -1 ) {
                    // handle the error
                    continue;
                }
                else if( ret == 0 ) {
                    continue;
                }
                else
                {
                    for( int i = 0; i < ret; ++i )
                    {
                        switch( signals[i] )
                        {
                            case SIGALRM:
                            {
                                cout << "time out, check list again" << endl;
                                timeout = true;
                                break;
                            }
                            case SIGTERM:
                            {
                                stop_server = true;
                            }
                        }
                    }
                }
            }
            else if(  events[i].events & EPOLLIN )
            {
                memset( usersData[sockfd].buf, '\0', BUFFER_SIZE );
                ret = recv( sockfd, usersData[sockfd].buf, BUFFER_SIZE-1, 0 );
                printf( "get %d bytes of client data %s from %d\n", ret, usersData[sockfd].buf, sockfd );
                util_timer* timer = users[sockfd].timer;
                if( ret < 0 ) {
                    if( errno != EAGAIN )
                    {
                        cb_func( &users[sockfd] );
                        if( timer )
                        {
                            utils.m_timer_lst.del_timer( timer );
                        }
                    }
                } else if( ret == 0 )
                {
                    cb_func( &users[sockfd] );
                    if( timer )
                    {
                        utils.m_timer_lst.del_timer( timer );
                    }
                }
                else {
                    send( sockfd, usersData[sockfd].buf, BUFFER_SIZE-1, 0 );
                    if( timer ) {
                        time_t cur = time( NULL );
                        timer->expire = cur + 3 * TIMESLOT;
                        printf( "adjust timer once\n" );
                        utils.m_timer_lst.adjust_timer( timer );
                    }
                }
            }
            else
            {
                // others
            }
        }

        if( timeout ) {
            cout << utils.m_timer_lst.getHead() << endl;
            utils.timer_handler();
            cout << utils.m_timer_lst.getHead() << endl;       
            timeout = false;
        }
    }
    return 0;
}