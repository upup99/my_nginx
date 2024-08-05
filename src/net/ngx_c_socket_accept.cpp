// 和网络 中 接受连接【accept】 有关的函数放这里
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>    //uintptr_t
#include <stdarg.h>    //va_start....
#include <unistd.h>    //STDERR_FILENO等
#include <sys/time.h>  //gettimeofday
#include <time.h>      //localtime_r
#include <fcntl.h>     //open
#include <errno.h>     //errno
//#include <sys/socket.h>
#include <sys/ioctl.h> //ioctl
#include <arpa/inet.h>

#include "ngx_c_conf.h"
#include "ngx_macro.h"
#include "ngx_global.h"
#include "ngx_func.h"
#include "ngx_c_socket.h"

void CSocket::ngx_event_accept(lpngx_connection_t oldc)
{
    // LT【水平触发】，需要尽快返回
    struct sockaddr mysockaddr;
    socklen_t       socklen;
    int             err;
    int             level;
    int             s;
    static int      use_accept4 = 1;
    lpngx_connection_t newc;

    socklen = sizeof(mysockaddr);
    do
    {
        if (use_accept4)
        {
            s = accept4(oldc->fd, &mysockaddr, &socklen, SOCK_NONBLOCK);
        }
        else
        {
            s = accept(oldc->fd, &mysockaddr, &socklen);
        }

        if (s == -1)
        {
            err = errno;
            //对accept、send和recv而言，事件未发生时errno通常被设置成EAGAIN（意为“再来一次”）或者EWOULDBLOCK（意为“期待阻塞”）
            if (err == EAGAIN)
            {
                return ;
            }
            level = NGX_LOG_ALERT;
            // ECONNRESET错误则发生在对方意外关闭套接字后,
            // 【您的主机中的软件放弃了一个已建立的连接--由于超时或者其它失败而中止接连(用户插拔网线就可能有这个错误出现)】
            if (err == ECONNABORTED)
            {
                level = NGX_LOG_ERR;
            }
            // EMFILE:进程的fd已用尽,ENFILE这个errno的存在，表明一定存在system-wide的resource limits，而不仅仅有process-specific的resource limits。
            else if (err == EMFILE || err == ENFILE)
            {
                level = NGX_LOG_CRIT;
            }
            // accept4没实现
            if (use_accept4 && err == ENOSYS)
            {
                use_accept4 = 0;
                continue;
            }
            return ;
        }

        // accept4()/accept()成功了        
        if (m_onlineUserCount >= m_worker_connections) // 连接数过多
        {
            close(s);
            return ;
        }
        //如果某些恶意用户连上来发了1条数据就断，不断连接，会导致频繁调用ngx_get_connection()使用我们短时间内产生大量连接，危及本服务器安全
        if(m_connectionList.size() > (m_worker_connections * 5))
        {
            //比如你允许同时最大2048个连接，但连接池却有了 2048*5这么大的容量，这肯定是表示短时间内 产生大量连接/断开，因为我们的延迟回收机制，这里连接还在垃圾池里没有被回收
            if(m_freeconnectionList.size() < m_worker_connections)
            {
                //整个连接池这么大了，而空闲连接却这么少了，所以我认为是  短时间内 产生大量连接，发一个包后就断开，我们不可能让这种情况持续发生，所以必须断开新入用户的连接
                //一直到m_freeconnectionList变得足够大【连接池中连接被回收的足够多】
                close(s);
                return ;   
            }
        }
        // 针对新连入用户的连接，和监听套接字所对应的连接是两个不同的东西
        newc = ngx_get_connection(s);
        if (newc == NULL)
        {
            if (close(s) == -1)
            {
                ngx_log_error_core(NGX_LOG_ALERT,errno,"CSocekt::ngx_event_accept()中close(%d)失败!",s);                
            }
            return ;
        }

        //...........将来这里会判断是否连接超过最大允许连接数，现在，这里可以不处理

        // 成功拿到了连接池的一个连接
        memcpy(&newc->s_sockaddr, &mysockaddr, socklen);

        if (!use_accept4)
        {
            // 不是accept4，设置非阻塞
            if (setnonblocking(s) == false)
            {
                ngx_close_connection(newc);
                return ;
            }
        }

        newc->listening = oldc->listening;

        newc->rhandler = &CSocket::ngx_read_request_handler;
        newc->whandler = &CSocket::ngx_write_request_handler;
        //客户端应该主动发送第一次的数据，这里将读事件加入epoll监控，这样当客户端发送数据来时，会触发ngx_wait_request_handler()被ngx_epoll_process_events()调用        
        if(ngx_epoll_oper_event(
                                s,                  //socekt句柄
                                EPOLL_CTL_ADD,      //事件类型，这里是增加
                                EPOLLIN|EPOLLRDHUP, //标志，这里代表要增加的标志,EPOLLIN：可读，EPOLLRDHUP：TCP连接的远端关闭或者半关闭 ，如果边缘触发模式可以增加 EPOLLET
                                0,                  //对于事件类型为增加的，不需要这个参数
                                newc                //连接池中的连接
                                ) == -1)  
        {
            ngx_close_connection(newc);
            return ;
        }

        if (m_ifkickTimeCount == 1)
        {
            AddToTimerQueue(newc);
        }
        m_onlineUserCount++;
        break;

    } while (1);
    
    return;
}