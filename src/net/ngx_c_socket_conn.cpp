//和网络 中 连接/连接池 有关的函数放这里

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
#include "ngx_c_memory.h"
#include "ngx_c_lockmutex.h"

//---------------------------------------------------------------
//连接池成员函数
ngx_connection_s::ngx_connection_s()
{
    iCurrsequence = 0;
    pthread_mutex_init(&logicPorcMutex, NULL);
}

ngx_connection_s::~ngx_connection_s()
{
    pthread_mutex_destroy(&logicPorcMutex);
}

//分配出去一个连接的时候初始化一些内容,原来内容放在 ngx_get_connection()里，现在放在这里
void ngx_connection_s::GetOneToUse()
{
    iCurrsequence++;

    fd = -1;
    curStat = _PKG_HD_INIT;
    precvbuf = dataHeadInfo;
    irecvlen = sizeof(COMM_PKG_HEADER);

    precvMemPointer = NULL;
    iThrowsendCount = 0;
    psendMemPointer = NULL;
    events          = 0;
    lastPingTime    = time(NULL);

    FloodkickLastTime = 0;
    FloodAttackCount  = 0;
    iSendCount        = 0;
}

// 回收回来一个连接的时候做一些事
void ngx_connection_s::PutOneToFree()
{
    iCurrsequence++;
    if (precvMemPointer != NULL)
    {
        CMemory::GetInstance()->FreeMemory(precvMemPointer);
        precvMemPointer = NULL;
    }
    if (psendMemPointer != NULL)
    {
        CMemory::GetInstance()->FreeMemory(psendMemPointer);
        psendMemPointer = NULL;
    }

    iThrowsendCount = 0;
}

void CSocket::initconnection()
{
    lpngx_connection_t p_Conn;
    CMemory *p_memory = CMemory::GetInstance();

    int ilenconnpool = sizeof(ngx_connection_t);
    for (int i = 0; i < m_worker_connections; i++)
    {
        p_Conn = (lpngx_connection_t)p_memory->AllocMemory(ilenconnpool, true);
        p_Conn = new(p_Conn) ngx_connection_t();
        p_Conn->GetOneToUse();
        m_connectionList.push_back(p_Conn);
        m_freeconnectionList.push_back(p_Conn);
    }
    m_free_connection_n = m_total_connection_n = m_connectionList.size();
    return ;
}

// 最终回收连接池，释放内存
void CSocket::clearconnection()
{
    lpngx_connection_t p_Conn;
    CMemory *p_memory = CMemory::GetInstance();

    while (!m_connectionList.empty())
    {
        p_Conn = m_connectionList.front();
        m_connectionList.pop_front();
        p_Conn->~ngx_connection_t();
        p_memory->FreeMemory(p_Conn);
    }
}

//从连接池中获取一个空闲连接【当一个客户端连接TCP进入，我希望把这个连接和我的 连接池中的 一个连接【对象】绑到一起，后续 我可以通过这个连接，把这个对象拿到，因为对象里边可以记录各种信息】
lpngx_connection_t CSocket::ngx_get_connection(int isock)
{
    //因为可能有其他线程要访问m_freeconnectionList
    CLock lock(&m_connectionMutex);

    if (!m_freeconnectionList.empty())
    {
        lpngx_connection_t p_Conn = m_freeconnectionList.front();
        m_freeconnectionList.pop_front();
        p_Conn->GetOneToUse();
        m_free_connection_n--;
        p_Conn->fd = isock;
        return p_Conn;
    }

    // 如果没有空闲连接
    CMemory *p_memory = CMemory::GetInstance();
    lpngx_connection_t p_Conn = (lpngx_connection_t)p_memory->AllocMemory(sizeof(ngx_connection_t), true);
    p_Conn = new(p_Conn)ngx_connection_t();
    p_Conn->GetOneToUse();
    m_connectionList.push_back(p_Conn);
    m_total_connection_n++;
    p_Conn->fd = isock;
    return p_Conn;
}

//归还参数pConn所代表的连接到到连接池中，注意参数类型是lpngx_connection_t
void CSocket::ngx_free_connection(lpngx_connection_t pConn)
{
    //因为有线程可能要动连接池中连接，所以在合理互斥也是必要的
    CLock lock(&m_connectionMutex);
    pConn->PutOneToFree();
    m_freeconnectionList.push_back(pConn);
    m_free_connection_n++;
    return ;
}

//将要回收的连接放到一个队列中来，后续有专门的线程会处理这个队列中的连接的回收
//有些连接，我们不希望马上释放，
//要隔一段时间后再释放以确保服务器的稳定，所以，我们把这种隔一段时间才释放的连接先放到一个队列中来
void CSocket::inRecyConnectQueue(lpngx_connection_t pConn)
{
    std::list<lpngx_connection_t>::iterator pos;
    bool iffind = false;

    // 针对连接回收列表的互斥量，因为线程ServerRecyConnectionThread()也有要用到这个回收列表；
    CLock lock(&m_recyconnqueueMutex);

    for (pos = m_recyconnectionList.begin(); pos != m_recyconnectionList.end(); pos++)
    {
        if ((*pos) == pConn)
        {
            iffind = true;
            break;
        }
        if (iffind == true)
        {
            return ;
        }
    }

    pConn->inRecyTime = time(NULL); // 记录回收时间
    pConn->iCurrsequence++;
    m_recyconnectionList.push_back(pConn); //等待ServerRecyConnectionThread线程自会处理
    m_totol_recyconnection_n++;
    m_onlineUserCount--;
    return ;
}

// 处理回收的线程
void* CSocket::ServerRecyConnectionThread(void* threadData)
{
    ThreadItem *pThread = static_cast<ThreadItem*>(threadData);
    CSocket *pSocketObj = pThread->_pThis;

    time_t currtime;
    int err;
    std::list<lpngx_connection_t>::iterator pos, posend;
    lpngx_connection_t p_Conn;

    while (1)
    {
        //为简化问题，我们直接每次休息200毫秒
        usleep(200 * 100);

        if (pSocketObj->m_totol_recyconnection_n > 0)
        {
            currtime = time(NULL);
            err = pthread_mutex_lock(&pSocketObj->m_recyconnqueueMutex);
            if (err != 0) ngx_log_stderr(err,"CSocekt::ServerRecyConnectionThread()中pthread_mutex_lock()失败，返回的错误码为%d!",err);

lblRRTD:
            pos = pSocketObj->m_recyconnectionList.begin();
            posend = pSocketObj->m_recyconnectionList.end();
            for (; pos != posend; pos++)
            {
                p_Conn = (*pos);
                if (
                    ((p_Conn->inRecyTime + pSocketObj->m_RecyConnectionWaitTime) > currtime) &&
                    (g_stopEvent == 0)
                )
                {
                    continue; // 还没到释放的时间
                }

                // 当需要释放连接时，那么发送缓存不满
                if (p_Conn->iThrowsendCount > 0)
                {
                    ngx_log_stderr(0,"CSocekt::ServerRecyConnectionThread()中到释放时间却发现p_Conn.iThrowsendCount!=0，这个不该发生");
                }

                //流程走到这里，表示可以释放，那我们就开始释放
                pSocketObj->m_totol_recyconnection_n--;
                pSocketObj->m_recyconnectionList.erase(pos);

                pSocketObj->ngx_free_connection(p_Conn);
                goto lblRRTD;
            } // end for

            err = pthread_mutex_unlock(&pSocketObj->m_recyconnqueueMutex);
            if (err != 0) ngx_log_stderr(err,"CSocekt::ServerRecyConnectionThread()pthread_mutex_unlock()失败，返回的错误码为%d!",err);
        }    // end if

        if (g_stopEvent == 1)
        {
            if (pSocketObj->m_totol_recyconnection_n > 0)
            {
                // 因为要退出，那么就需要硬释放
                err = pthread_mutex_lock(&pSocketObj->m_recyconnqueueMutex);
                if (err != 0) ngx_log_stderr(err,"CSocekt::ServerRecyConnectionThread()中pthread_mutex_lock2()失败，返回的错误码为%d!",err);
        lblRRTD2:
                pos = pSocketObj->m_recyconnectionList.begin();
                posend = pSocketObj->m_recyconnectionList.end();
                for (; pos != posend; pos ++)
                {
                    p_Conn = (*pos);
                    pSocketObj->m_totol_recyconnection_n--;
                    pSocketObj->m_connectionList.erase(pos);
                    pSocketObj->ngx_free_connection(p_Conn);
                    goto lblRRTD2;
                } // end for

                err = pthread_mutex_unlock(&pSocketObj->m_recyconnqueueMutex);
                if(err != 0)  ngx_log_stderr(err,"CSocekt::ServerRecyConnectionThread()pthread_mutex_unlock2()失败，返回的错误码为%d!",err);
            } // end if (pSocketObj->m_totol_recyconnection_n > 0)
            break;
        } // end if (g_stopEvent == 1)
    } // end while

    return (void*)0;
}

//用户连入，我们accept4()时，得到的socket在处理中产生失败，则资源用这个函数释放【因为这里涉及到好几个要释放的资源，所以写成函数】
//我们把ngx_close_accepted_connection()函数改名为让名字更通用，并从文件ngx_socket_accept.cxx迁移到本文件中，并改造其中代码，注意顺序
void CSocket::ngx_close_connection(lpngx_connection_t pConn)
{
    ngx_free_connection(pConn);
    if (pConn->fd != -1)
    {
        close(pConn->fd);
        pConn->fd == -1;
    }
    return ;
}