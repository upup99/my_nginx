//和网络 有关的函数放这里

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
#include <sys/ioctl.h> //ioctl
#include <arpa/inet.h>

#include "ngx_c_conf.h"
#include "ngx_macro.h"
#include "ngx_global.h"
#include "ngx_func.h"
#include "ngx_c_socket.h"
#include "ngx_c_memory.h"
#include "ngx_c_lockmutex.h"



// 构造函数
CSocket::CSocket()
{
    m_worker_connections = 1;
    m_ListenPortCount = 1;
    m_RecyConnectionWaitTime = 60;

    m_epollhandle = -1;

    m_iLenPkgHeader = sizeof(COMM_PKG_HEADER);
    m_iLenMsgHeader = sizeof(STRUC_MSG_HEADER);

    m_iSendMsgQueueCount = 0;
    m_totol_recyconnection_n = 0;
    m_cur_size_          = 0;
    m_timer_value_       = 0;
    m_iDiscardSendPkgCount = 0;

    m_onlineUserCount    = 0;
    m_lastprintTime = 0;
    // return ;
}

CSocket::~CSocket()
{
    std::vector<lpngx_listening_t>::iterator pos;
    for (pos = m_ListenSocketList.begin(); pos != m_ListenSocketList.end(); pos++)
    {
        delete (*pos);
    }
    m_ListenSocketList.clear();
    // return ;
}

bool CSocket::Initialize()
{
    ReadConf();
    if (ngx_open_listening_sockets() == false)
        return false;
    return true;
}

bool CSocket::Initialize_subproc()
{
    //发消息互斥量初始化
    if(pthread_mutex_init(&m_sendMessageQueueMutex, NULL)  != 0)
    {        
        ngx_log_stderr(0,"CSocekt::Initialize_subproc()中pthread_mutex_init(&m_sendMessageQueueMutex)失败.");
        return false;    
    }
    //连接相关互斥量初始化
    if(pthread_mutex_init(&m_connectionMutex, NULL)  != 0)
    {
        ngx_log_stderr(0,"CSocekt::Initialize_subproc()中pthread_mutex_init(&m_connectionMutex)失败.");
        return false;    
    }    
    //连接回收队列相关互斥量初始化
    if(pthread_mutex_init(&m_recyconnqueueMutex, NULL)  != 0)
    {
        ngx_log_stderr(0,"CSocekt::Initialize_subproc()中pthread_mutex_init(&m_recyconnqueueMutex)失败.");
        return false;    
    } 
    //和时间处理队列有关的互斥量初始化
    if(pthread_mutex_init(&m_timequeueMutex, NULL)  != 0)
    {
        ngx_log_stderr(0,"CSocekt::Initialize_subproc()中pthread_mutex_init(&m_timequeueMutex)失败.");
        return false;    
    }  

    //初始化发消息相关信号量，信号量用于进程/线程 之间的同步，
    //虽然 互斥量[pthread_mutex_lock]和 条件变量[pthread_cond_wait]都是线程之间的同步手段，但
    //这里用信号量实现 则 更容易理解，更容易简化问题，使用书写的代码短小且清晰；
    //第二个参数=0，表示信号量在线程之间共享，确实如此 ，如果非0，表示在进程之间共享
    //第三个参数=0，表示信号量的初始值，为0时，调用sem_wait()就会卡在那里卡着
    if (sem_init(&m_semEventSendQueue, 0, 0) == -1)
    {
        ngx_log_stderr(0,"CSocekt::Initialize_subproc()中sem_init(&m_semEventSendQueue,0,0)失败.");
        return false;
    }

    // 创建线程
    int err;
    ThreadItem *pSendQueue;
    m_threadVector.push_back(pSendQueue = new ThreadItem(this));
    err = pthread_create(&pSendQueue->_Handle, NULL, ServerSendQueueThread, pSendQueue);
    if (err != 0)
    {
        ngx_log_stderr(0,"CSocekt::Initialize_subproc()中pthread_create(ServerSendQueueThread)失败.");
        return false;
    }

    // 回收线程
    if (m_ifkickTimeCount == 1)
    {
        ThreadItem *pTimeMonitor;
        m_threadVector.push_back(pTimeMonitor = new ThreadItem(this));
        err = pthread_create(&pTimeMonitor->_Handle, NULL, ServerTimerQueueMonitorThread, pTimeMonitor);
        if (err != 0)
        {
            ngx_log_stderr(0,"CSocekt::Initialize_subproc()中pthread_create(ServerTimerQueueMonitorThread)失败.");
            return false;   
        }
    }
    return true;
}

void CSocket::Shutdown_subproc()
{
    if (sem_post(&m_semEventSendQueue) == -1)
    {
        ngx_log_stderr(0,"CSocekt::Shutdown_subproc()中sem_post(&m_semEventSendQueue)失败.");
    }

    std::vector<ThreadItem*>::iterator iter;
    for (iter = m_threadVector.begin(); iter != m_threadVector.end(); iter++)
    {
        pthread_join((*iter)->_Handle, NULL);
    }

    for (iter = m_threadVector.begin(); iter != m_threadVector.end(); iter++)
    {
        if (*iter)
            delete *iter;
    }
    m_threadVector.clear();

    clearMsgSendQueue();
    clearconnection();
    clearAllFromTimerQueue();

    //(4)多线程相关    
    pthread_mutex_destroy(&m_connectionMutex);          //连接相关互斥量释放
    pthread_mutex_destroy(&m_sendMessageQueueMutex);    //发消息互斥量释放    
    pthread_mutex_destroy(&m_recyconnqueueMutex);       //连接回收队列相关的互斥量释放
    pthread_mutex_destroy(&m_timequeueMutex);           //时间处理队列相关的互斥量释放
    sem_destroy(&m_semEventSendQueue);                  //发消息相关线程信号量释放
}

// 清理TCP发送消息队列
void CSocket::clearMsgSendQueue()
{
    char *sTmpMempoint;
    CMemory *p_memory = CMemory::GetInstance();

    while (!m_MsgSendQueue.empty())
    {
        sTmpMempoint = m_MsgSendQueue.front();
        m_MsgSendQueue.pop_front();
        p_memory->FreeMemory(sTmpMempoint);
    }
}

// 用于读各种配置项
void CSocket::ReadConf()
{
    CConfig *p_config = CConfig::GetInstance();
    m_worker_connections      = p_config->GetIntDefault("worker_connections",m_worker_connections);              //epoll连接的最大项数
    m_ListenPortCount         = p_config->GetIntDefault("ListenPortCount",m_ListenPortCount);                    //取得要监听的端口数量
    m_RecyConnectionWaitTime  = p_config->GetIntDefault("Sock_RecyConnectionWaitTime",m_RecyConnectionWaitTime); //等待这么些秒后才回收连接

    m_ifkickTimeCount         = p_config->GetIntDefault("Sock_WaitTimeEnable",0);                                //是否开启踢人时钟，1：开启   0：不开启
	m_iWaitTime               = p_config->GetIntDefault("Sock_MaxWaitTime",m_iWaitTime);                         //多少秒检测一次是否 心跳超时，只有当Sock_WaitTimeEnable = 1时，本项才有用	
	m_iWaitTime               = (m_iWaitTime > 5)?m_iWaitTime:5;                                                 //不建议低于5秒钟，因为无需太频繁
    m_ifTimeOutKick           = p_config->GetIntDefault("Sock_TimeOutKick",0);                                   //当时间到达Sock_MaxWaitTime指定的时间时，直接把客户端踢出去，只有当Sock_WaitTimeEnable = 1时，本项才有用 

    m_floodAkEnable          = p_config->GetIntDefault("Sock_FloodAttackKickEnable",0);                          //Flood攻击检测是否开启,1：开启   0：不开启
	m_floodTimeInterval      = p_config->GetIntDefault("Sock_FloodTimeInterval",100);                            //表示每次收到数据包的时间间隔是100(毫秒)
	m_floodKickCount         = p_config->GetIntDefault("Sock_FloodKickCounter",10);                              //累积多少次踢出此人

    return;
}

//监听端口【支持多个端口】
//在创建worker进程之前就要执行这个函数
bool CSocket::ngx_open_listening_sockets()
{
    int                 isock;
    struct sockaddr_in  serv_addr;
    int                 iport;
    char                strinfo[100];

    // 初始化相关
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    CConfig *p_config = CConfig::GetInstance();
    for (int i = 0; i < m_ListenPortCount; i++)
    {
        isock = socket(AF_INET, SOCK_STREAM, 0);
        if (isock == -1)
        {
            ngx_log_stderr(errno,"CSocekt::Initialize()中socket()失败,i=%d.",i);
            return false;
        }


        // 设置端口复用
        int reuseaddr = 1;
        if (setsockopt(isock, SOL_SOCKET, SO_REUSEADDR, (const void*)&reuseaddr, sizeof(reuseaddr)) == -1)
        {
            ngx_log_stderr(errno,"CSocekt::Initialize()中setsockopt(SO_REUSEADDR)失败,i=%d.",i);
            close(isock); //无需理会是否正常执行了                                                  
            return false;
        }

        // 处理惊群问题
        int reuseport = 1;
        if (setsockopt(isock, SOL_SOCKET, SO_REUSEPORT, (const void*)&reuseport, sizeof(reuseport)) == -1)
        {
            ngx_log_stderr(errno,"CSocekt::Initialize()中setsockopt(SO_REUSEPORT)失败",i);
        }

        // 设置套接字非阻塞
        if (setnonblocking(isock) == false)
        {
            ngx_log_stderr(errno,"CSocekt::Initialize()中setnonblocking()失败,i=%d.",i);
            close(isock);
            return false;
        }

        strinfo[0] = 0;
        sprintf(strinfo, "ListenPort%d", i);
        iport = p_config->GetIntDefault(strinfo, 10000);
        serv_addr.sin_port = htons((in_port_t)iport);

        if (bind(isock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1)
        {
            ngx_log_stderr(errno,"CSocekt::Initialize()中bind()失败,i=%d.",i);
            close(isock);
            return false;
        }

        if (listen(isock, NGX_LISTEN_BACKLOG) == -1)
        {
            ngx_log_stderr(errno,"CSocekt::Initialize()中listen()失败,i=%d.",i);
            close(isock);
            return false;
        }

        lpngx_listening_t p_listenSocketItem = new ngx_listening_t;
        memset(p_listenSocketItem, 0, sizeof(ngx_listening_t));
        p_listenSocketItem->port = iport;
        p_listenSocketItem->fd   = isock;
        ngx_log_error_core(NGX_LOG_INFO,0,"监听%d端口成功!",iport); //显示一些信息到日志中
        m_ListenSocketList.push_back(p_listenSocketItem);
    } // end for (int i = 0; i < m_ListenPortCount; i++)

    if (m_ListenSocketList.size() <= 0)
    {
        return false;
    }

    return true;
}

//设置socket连接为非阻塞模式
bool CSocket::setnonblocking(int sockfd)
{
    int nb = 1;
    if (ioctl(sockfd, FIONBIO, &nb) == -1)
    {
        return false;
    }
    return true;
}

void CSocket::ngx_close_listening_sockets()
{
    for (int i = 0; i < m_ListenPortCount; i++)
    {
        close(m_ListenSocketList[i]->fd);
        ngx_log_error_core(NGX_LOG_INFO,0,"关闭监听端口%d!",m_ListenSocketList[i]->port); //显示一些信息到日志中
    }
    return ;
}

void CSocket::msgSend(char *psendbuf)
{
    CMemory *p_memory = CMemory::GetInstance();

    CLock lock(&m_sendMessageQueueMutex);

    // 消息队列过大
    if (m_iSendMsgQueueCount > 50000)
    {
        m_iDiscardSendPkgCount ++;
        p_memory->FreeMemory(psendbuf);
        return ;
    }

    // 个体数据是否过大
    LPSTRUC_MSG_HEADER pMsgHeader = (LPSTRUC_MSG_HEADER)psendbuf;
    lpngx_connection_t p_Conn = pMsgHeader->pConn;
    if (p_Conn->iSendCount > 400)
    {
        ngx_log_stderr(0,"CSocekt::msgSend()中发现某用户%d积压了大量待发送数据包，切断与他的连接！",p_Conn->fd);      
        m_iDiscardSendPkgCount++;
        p_memory->FreeMemory(psendbuf);
        zdClosesocketProc(p_Conn);
        return ;
    }

    p_Conn->iSendCount++;
    m_MsgSendQueue.push_back(psendbuf);
    m_iSendMsgQueueCount++;

    if (sem_post(&m_semEventSendQueue) == -1)
    {
        ngx_log_stderr(0,"CSocekt::msgSend()中sem_post(&m_semEventSendQueue)失败.");      
    }
    return ;
}

void CSocket::zdClosesocketProc(lpngx_connection_t p_Conn)
{
    if (m_ifkickTimeCount == 1)
    {
        DeleteFromTimerQueue(p_Conn);
    }

    if (p_Conn->fd != -1)
    {
        close(p_Conn->fd);
        p_Conn->fd = -1;
    }

    if (p_Conn->iThrowsendCount > 0)
        p_Conn->iThrowsendCount--;

    inRecyConnectQueue(p_Conn);
    return ;
}

//测试是否flood攻击成立，成立则返回true，否则返回false
bool CSocket::TestFlood(lpngx_connection_t pConn)
{
    struct  timeval sCurrTime;   //当前时间结构
	uint64_t        iCurrTime;   //当前时间（单位：毫秒）
	bool  reco      = false;
	
	gettimeofday(&sCurrTime, NULL); //取得当前时间
    iCurrTime =  (sCurrTime.tv_sec * 1000 + sCurrTime.tv_usec / 1000);  //毫秒
	if((iCurrTime - pConn->FloodkickLastTime) < m_floodTimeInterval)   //两次收到包的时间 < 100毫秒
	{
        //发包太频繁记录
		pConn->FloodAttackCount++;
		pConn->FloodkickLastTime = iCurrTime;
	}
	else
	{
        //既然发布不这么频繁，则恢复计数值
		pConn->FloodAttackCount = 0;
		pConn->FloodkickLastTime = iCurrTime;
	}

    //ngx_log_stderr(0,"pConn->FloodAttackCount=%d,m_floodKickCount=%d.",pConn->FloodAttackCount,m_floodKickCount);

	if(pConn->FloodAttackCount >= m_floodKickCount)
	{
		//可以踢此人的标志
		reco = true;
	}
	return reco;
}

//打印统计信息
void CSocket::printTDInfo()
{
    //return;
    time_t currtime = time(NULL);
    if( (currtime - m_lastprintTime) > 10)
    {
        //超过10秒我们打印一次
        int tmprmqc = g_threadpool.getRecvMsgQueueCount(); //收消息队列

        m_lastprintTime = currtime;
        int tmpoLUC = m_onlineUserCount;    //atomic做个中转，直接打印atomic类型报错；
        int tmpsmqc = m_iSendMsgQueueCount; //atomic做个中转，直接打印atomic类型报错；
        ngx_log_stderr(0,"------------------------------------begin--------------------------------------");
        ngx_log_stderr(0,"当前在线人数/总人数(%d/%d)。",tmpoLUC,m_worker_connections);        
        ngx_log_stderr(0,"连接池中空闲连接/总连接/要释放的连接(%d/%d/%d)。",m_freeconnectionList.size(),m_connectionList.size(),m_recyconnectionList.size());
        ngx_log_stderr(0,"当前时间队列大小(%d)。",m_timerQueuemap.size());        
        ngx_log_stderr(0,"当前收消息队列/发消息队列大小分别为(%d/%d)，丢弃的待发送数据包数量为%d。",tmprmqc,tmpsmqc,m_iDiscardSendPkgCount);        
        if( tmprmqc > 100000)
        {
            //接收队列过大，报一下，这个属于应该 引起警觉的，考虑限速等等手段
            ngx_log_stderr(0,"接收队列条目数量过大(%d)，要考虑限速或者增加处理线程数量了！！！！！！",tmprmqc);
        }
        ngx_log_stderr(0,"-------------------------------------end---------------------------------------");
    }
    return;
}

//(1)epoll功能初始化，子进程中进行 ，本函数被ngx_worker_process_init()所调用
int CSocket::ngx_epoll_init()
{
    m_epollhandle = epoll_create(m_worker_connections);
    if (m_epollhandle == -1)
    {
        ngx_log_stderr(errno,"CSocekt::ngx_epoll_init()中epoll_create()失败.");
        exit(2); //这是致命问题了，直接退，资源由系统释放吧，这里不刻意释放了，比较麻烦
    }

    initconnection();

    std::vector<lpngx_listening_t>::iterator pos;
    for (pos = m_ListenSocketList.begin(); pos != m_ListenSocketList.end(); pos++)
    {
        lpngx_connection_t p_Conn = ngx_get_connection((*pos)->fd);
        if (p_Conn == NULL)
        {
            ngx_log_stderr(errno,"CSocekt::ngx_epoll_init()中ngx_get_connection()失败.");
            exit(2);
        }
        p_Conn->listening = (*pos);
        (*pos)->connection = p_Conn;

        p_Conn->rhandler = &CSocket::ngx_event_accept;


        // 往监听socket上增加监听事件
        if (ngx_epoll_oper_event((*pos)->fd, 
                                EPOLL_CTL_ADD,
                                EPOLLIN|EPOLLRDHUP,
                                0,
                                p_Conn) == -1)
        {
            exit(2);
        }
    } // end for

    return 1;
}

//对epoll事件的具体操作
//返回值：成功返回1，失败返回-1；
int CSocket::ngx_epoll_oper_event(
                        int                fd,               //句柄，一个socket
                        uint32_t           eventtype,        //事件类型，一般是EPOLL_CTL_ADD，EPOLL_CTL_MOD，EPOLL_CTL_DEL ，说白了就是操作epoll红黑树的节点(增加，修改，删除)
                        uint32_t           flag,             //标志，具体含义取决于eventtype
                        int                bcaction,         //补充动作，用于补充flag标记的不足  :  0：增加   1：去掉 2：完全覆盖 ,eventtype是EPOLL_CTL_MOD时这个参数就有用
                        lpngx_connection_t pConn             //pConn：一个指针【其实是一个连接】，EPOLL_CTL_ADD时增加到红黑树中去，将来epoll_wait时能取出来用
                        )
{
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));

    if (eventtype == EPOLL_CTL_ADD) 
    {
        ev.events = flag;
        pConn->events = flag;
    }
    else if (eventtype == EPOLL_CTL_MOD)
    {
        ev.events = pConn->events;
        if (bcaction == 0)
        {
            // 增加某个标记
            ev.events |= flag;
        }
        else if (bcaction == 1)
        {
            // 去掉某个标记
            ev.events &= ~flag;
        }
        else
        {
            // 完全覆盖某个标记
            ev.events = flag;
        }
        pConn->events = ev.events;
    }
    else 
    {
        return 1;
    }

    ev.data.ptr = (void*)pConn;

    if (epoll_ctl(m_epollhandle, eventtype, fd, &ev) == -1)
    {
        ngx_log_stderr(errno,"CSocekt::ngx_epoll_oper_event()中epoll_ctl(%d,%ud,%ud,%d)失败.",fd,eventtype,flag,bcaction);    
        return -1;
    }
    return 1;
}

//开始获取发生的事件消息
//参数unsigned int timer：epoll_wait()阻塞的时长，单位是毫秒；
//返回值，1：正常返回  ,0：有问题返回，一般不管是正常还是问题返回，都应该保持进程继续运行
//本函数被ngx_process_events_and_timers()调用，而ngx_process_events_and_timers()是在子进程的死循环中被反复调用
int CSocket::ngx_epoll_process_events(int timer)
{
    int events = epoll_wait(m_epollhandle, m_events, NGX_MAX_EVENTS, timer);

    if (events == -1)
    {
        if(errno == EINTR) 
        {
            //信号所致，直接返回，一般认为这不是毛病，但还是打印下日志记录一下，因为一般也不会人为给worker进程发送消息
            ngx_log_error_core(NGX_LOG_INFO,errno,"CSocekt::ngx_epoll_process_events()中epoll_wait()失败!"); 
            return 1;  //正常返回
        }
        else
        {
            //这被认为应该是有问题，记录日志
            ngx_log_error_core(NGX_LOG_ALERT,errno,"CSocekt::ngx_epoll_process_events()中epoll_wait()失败!"); 
            return 0;  //非正常返回 
        } 
    }

    if (events == 0)
    {
        if (timer != -1)
        {
            return 1;
        }

        ngx_log_error_core(NGX_LOG_ALERT,0,"CSocekt::ngx_epoll_process_events()中epoll_wait()没超时却没返回任何事件!"); 
        return 0;
    }

    lpngx_connection_t p_Conn;
    uint32_t           revents;
    for (int i = 0; i < events; i++)
    {
        p_Conn = (lpngx_connection_t)(m_events[i].data.ptr);
        revents = m_events[i].events;

        if (revents & EPOLLIN) // 读事件
        {
            (this->*(p_Conn->rhandler))(p_Conn);
        }

        if (revents & EPOLLOUT) // 写事件
        {
            //客户端关闭，如果服务器端挂着一个写通知事件，则这里个条件是可能成立的
            if (revents & (EPOLLERR | EPOLLHUP | EPOLLRDHUP))
            {
                p_Conn->iThrowsendCount--;
            }
            else 
            {
                //如果有数据没有发送完毕，由系统驱动来发送，则这里执行的应该是 CSocket::ngx_write_request_handler()
                (this->*(p_Conn->whandler))(p_Conn);
            }
        }
    } // end for

    return 1;
}

//--------------------------------------------------------------------
//处理发送消息队列的线程
void* CSocket::ServerSendQueueThread(void* threadData)
{
    ThreadItem *pThread = static_cast<ThreadItem*>(threadData);
    CSocket *pSocketObj = pThread->_pThis;
    int err;
    std::list<char*>::iterator pos, pos2, posend;
    char *pMsgBuf;
    LPSTRUC_MSG_HEADER pMsgHeader;
    LPCOMM_PKG_HEADER  pPkgHeader;
    lpngx_connection_t p_Conn;
    unsigned short     itmp;
    ssize_t            sendsize;

    CMemory *p_memory = CMemory::GetInstance();

    while (g_stopEvent == 0) // 不退出
    {
        if (sem_wait(&pSocketObj->m_semEventSendQueue) == -1)
        {
            if (errno != EINTR)
                ngx_log_stderr(errno,"CSocekt::ServerSendQueueThread()中sem_wait(&pSocketObj->m_semEventSendQueue)失败.");            
        }
        if (g_stopEvent != 0) break;

        if (pSocketObj->m_iSendMsgQueueCount > 0)
        {
            err = pthread_mutex_lock(&pSocketObj->m_sendMessageQueueMutex);
            if(err != 0) ngx_log_stderr(err,"CSocekt::ServerSendQueueThread()中pthread_mutex_lock()失败，返回的错误码为%d!",err);

            pos     = pSocketObj->m_MsgSendQueue.begin();
            posend  = pSocketObj->m_MsgSendQueue.end();
            while (pos != posend)
            {
                pMsgBuf = (*pos);
                pMsgHeader = (LPSTRUC_MSG_HEADER)pMsgBuf;
                pPkgHeader = (LPCOMM_PKG_HEADER)(pMsgBuf + pSocketObj->m_iLenMsgHeader);
                p_Conn = pMsgHeader->pConn;

                // 包过期
                if (p_Conn->iCurrsequence != pMsgHeader->iCurrsequence)
                {
                    //本包中保存的序列号与p_Conn【连接池中连接】中实际的序列号已经不同，丢弃此消息，小心处理该消息的删除
                    pos2 = pos;
                    pos++;
                    pSocketObj->m_MsgSendQueue.erase(pos2);
                    pSocketObj->m_iSendMsgQueueCount--;
                    p_memory->FreeMemory(pMsgBuf);
                    continue;
                }

                if (p_Conn->iThrowsendCount > 0)
                {
                    // 发送缓存满，靠系统驱动发送
                    pos++;
                    continue;
                }

                p_Conn->iSendCount--;

                // 可以发送了
                p_Conn->psendMemPointer = pMsgBuf;
                pos2 = pos;
                pos++;
                pSocketObj->m_MsgSendQueue.erase(pos2);
                pSocketObj->m_iSendMsgQueueCount--;
                // 打包时用了htons【本机序转网络序】，所以这里为了得到该数值，用了个ntohs【网络序转本机序】；
                itmp = ntohs(pPkgHeader->pkgLen);
                p_Conn->isendlen = itmp;

                //这里是重点，我们采用 epoll水平触发的策略，能走到这里的，都应该是还没有投递 写事件 到epoll中
                    //epoll水平触发发送数据的改进方案：
	                //开始不把socket写事件通知加入到epoll,当我需要写数据的时候，直接调用write/send发送数据；
	                //如果返回了EAGIN【发送缓冲区满了，需要等待可写事件才能继续往缓冲区里写数据】，此时，我再把写事件通知加入到epoll，
	                //此时，就变成了在epoll驱动下写数据，全部数据发送完毕后，再把写事件通知从epoll中干掉；
	                //优点：数据不多的时候，可以避免epoll的写事件的增加/删除，提高了程序的执行效率；                         
                //(1)直接调用write或者send发送数据
                //ngx_log_stderr(errno,"即将发送数据%ud。",p_Conn->isendlen);

                sendsize = pSocketObj->sendproc(p_Conn, p_Conn->psendbuf, p_Conn->isendlen);
                if (sendsize > 0)
                {
                    if (sendsize == p_Conn->isendlen) // 成功一次就发送出去了
                    {
                        p_memory->FreeMemory(p_Conn->psendMemPointer);
                        p_Conn->psendMemPointer = NULL;
                        p_Conn->iThrowsendCount = 0;
                    }
                    else // EAGAIN，发送缓冲区满
                    {
                        // 找到发到哪里了
                        p_Conn->psendbuf = p_Conn->psendbuf + sendsize;
                        p_Conn->isendlen = p_Conn->isendlen - sendsize;
                        p_Conn->iThrowsendCount++; 

                        if(pSocketObj->ngx_epoll_oper_event(
                                p_Conn->fd,         //socket句柄
                                EPOLL_CTL_MOD,      //事件类型，这里是增加【因为我们准备增加个写通知】
                                EPOLLOUT,           //标志，这里代表要增加的标志,EPOLLOUT：可写【可写的时候通知我】
                                0,                  //对于事件类型为增加的，EPOLL_CTL_MOD需要这个参数, 0：增加   1：去掉 2：完全覆盖
                                p_Conn              //连接池中的连接
                                ) == -1)
                        {
                            //有这情况发生？这可比较麻烦，不过先do nothing
                            ngx_log_stderr(errno,"CSocekt::ServerSendQueueThread()ngx_epoll_oper_event()失败.");
                        }
                    }
                    continue; // 继续处理其他消息
                } // end if(sendsize > 0)
                else if (sendsize == 0)
                {
                    p_memory->FreeMemory(p_Conn->psendMemPointer);
                    p_Conn->psendMemPointer = NULL;
                    p_Conn->iThrowsendCount = 0;
                    continue;
                }
                else if (sendsize == -1) //能走到这里，继续处理问题
                {
                    //发送缓冲区已经满了【一个字节都没发出去，说明发送 缓冲区当前正好是满的】
                    ++p_Conn->iThrowsendCount; //标记发送缓冲区满了，需要通过epoll事件来驱动消息的继续发送
                    //投递此事件后，我们将依靠epoll驱动调用ngx_write_request_handler()函数发送数据
                    if(pSocketObj->ngx_epoll_oper_event(
                                p_Conn->fd,         //socket句柄
                                EPOLL_CTL_MOD,      //事件类型，这里是增加【因为我们准备增加个写通知】
                                EPOLLOUT,           //标志，这里代表要增加的标志,EPOLLOUT：可写【可写的时候通知我】
                                0,                  //对于事件类型为增加的，EPOLL_CTL_MOD需要这个参数, 0：增加   1：去掉 2：完全覆盖
                                p_Conn              //连接池中的连接
                                ) == -1)
                    {
                        //有这情况发生？这可比较麻烦，不过先do nothing
                        ngx_log_stderr(errno,"CSocekt::ServerSendQueueThread()中ngx_epoll_add_event()_2失败.");
                    }
                    continue;
                }
                else
                {
                    //能走到这里的，应该就是返回值-2了，一般就认为对端断开了，等待recv()来做断开socket以及回收资源
                    p_memory->FreeMemory(p_Conn->psendMemPointer);  //释放内存
                    p_Conn->psendMemPointer = NULL;
                    p_Conn->iThrowsendCount = 0;  //这行其实可以没有，因此此时此刻这东西就是=0的  
                    continue;
                }
            } // end while(pos != posend)

            err = pthread_mutex_unlock(&pSocketObj->m_sendMessageQueueMutex);
            if(err != 0)  ngx_log_stderr(err,"CSocekt::ServerSendQueueThread()pthread_mutex_unlock()失败，返回的错误码为%d!",err);

        } //if(pSocketObj->m_iSendMsgQueueCount > 0)
    } //end while

    return (void*)0;
}