//和网络  中 客户端发送来数据/服务器端收包 有关的代码

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
#include <pthread.h>   //多线程

#include "ngx_c_conf.h"
#include "ngx_macro.h"
#include "ngx_global.h"
#include "ngx_func.h"
#include "ngx_c_socket.h"
#include "ngx_c_memory.h"
#include "ngx_c_lockmutex.h"  //自动释放互斥量的一个类

//来数据时候的处理，当连接上有数据来的时候，本函数会被ngx_epoll_process_events()所调用  ,官方的类似函数为ngx_http_wait_request_handler();
void CSocket::ngx_read_request_handler(lpngx_connection_t pConn)
{
    bool isflood = false;

    //收包，注意我们用的第二个和第三个参数，我们用的始终是这两个参数，因此我们必须保证 c->precvbuf指向正确的收包位置，保证c->irecvlen指向正确的收包宽度
    ssize_t reco = recvproc(pConn, pConn->precvbuf, pConn->irecvlen);
    if (reco <= 0)
    {
        return;
    }
    // 说明收到了一些数据
    if(pConn->curStat == _PKG_HD_INIT) //连接建立起来时肯定是这个状态，因为在ngx_get_connection()中已经把curStat成员赋值成_PKG_HD_INIT了
    {
        if (reco == m_iLenMsgHeader)
        {
            ngx_wait_request_handler_proc_p1(pConn, isflood); //那就调用专门针对包头处理完整的函数去处理
        }
        else
        {
            //收到的包头不完整
            pConn->curStat = _PKG_HD_RECVING;
            pConn->precvbuf = pConn->precvbuf + reco;
            pConn->irecvlen = pConn->irecvlen - reco;
        } //end  if(reco == m_iLenPkgHeader)
    }
    else if(pConn->curStat == _PKG_HD_RECVING) //接收包头中，包头不完整，继续接收中，这个条件才会成立
    {
        if (pConn->irecvlen == reco)
        {
            //包头收完整了
            ngx_wait_request_handler_proc_p1(pConn, isflood); //那就调用专门针对包头处理完整的函数去处理
        }
        else
        {
            pConn->precvbuf = pConn->precvbuf + reco;
            pConn->irecvlen = pConn->irecvlen - reco;
        }
    }
    else if(pConn->curStat == _PKG_BD_INIT) // 收包体
    {
        if (reco == pConn->irecvlen)
        {
            if (m_floodAkEnable == 1)
            {
                isflood = TestFlood(pConn);
            }
            ngx_wait_request_handler_proc_plast(pConn,isflood);
        }
        else
        {
			//收到的宽度小于要收的宽度
			pConn->curStat = _PKG_BD_RECVING;					
			pConn->precvbuf = pConn->precvbuf + reco;
			pConn->irecvlen = pConn->irecvlen - reco;
        }
    }
    else if(pConn->curStat == _PKG_BD_RECVING) // 包体不完整
    {
        if (reco == pConn->irecvlen)
        {
            ngx_wait_request_handler_proc_plast(pConn, isflood);
        }
        else 
        {
			pConn->precvbuf = pConn->precvbuf + reco;
			pConn->irecvlen = pConn->irecvlen - reco;
        }
    }

    if (isflood == true) // 被flood攻击
    {
        zdClosesocketProc(pConn);
    }

    return ;
}

//接收数据专用函数--引入这个函数是为了方便，如果断线，错误之类的，这里直接 释放连接池中连接，然后直接关闭socket，以免在其他函数中还要重复的干这些事
//参数c：连接池中相关连接
//参数buff：接收数据的缓冲区
//参数buflen：要接收的数据大小
//返回值：返回-1，则是有问题发生并且在这里把问题处理完毕了，调用本函数的调用者一般是可以直接return
//        返回>0，则是表示实际收到的字节数
ssize_t CSocket::recvproc(lpngx_connection_t pConn, char *buff, ssize_t buflen)  //ssize_t是有符号整型，在32位机器上等同与int，在64位机器上等同与long int，size_t就是无符号型的ssize_t
{
     ssize_t n;

     n = recv(pConn->fd, buff, buflen, 0);

     if (n == 0) // 客户端关闭
     {
        zdClosesocketProc(pConn);
        return -1;
     }

     if (n < 0) // 有错误
     {
        if(errno == EAGAIN || errno == EWOULDBLOCK)
        {
            ngx_log_stderr(errno,"CSocekt::recvproc()中errno == EAGAIN || errno == EWOULDBLOCK成立，出乎我意料！");//epoll为LT模式不应该出现这个返回值
            return -1;
        }
        //EINTR错误的产生：当阻塞于某个慢系统调用的一个进程捕获某个信号且相应信号处理函数返回时，该系统调用可能返回一个EINTR错误
        //例如：在socket服务器端，设置了信号捕获机制，有子进程，当在父进程阻塞于慢系统调用时由父进程捕获到了一个有效信号时，内核会致使accept返回一个EINTR错误(被中断的系统调用)
        if (errno == EINTR)
        {
            ngx_log_stderr(errno,"CSocekt::recvproc()中errno == EINTR成立，出乎我意料！");
            return -1;
        }
        //所有从这里走下来的错误，都认为异常：意味着我们要关闭客户端套接字要回收连接池中连接；

        //errno参考：http://dhfapiran1.360drm.com
        if (errno == ECONNRESET)
        {

        }
        else
        {
            if (errno == EBADF)
            {
                //因为多线程，偶尔会干掉socket
            }
            else
            {
                ngx_log_stderr(errno,"CSocekt::recvproc()中发生错误，我打印出来看看是啥错误！");  //正式运营时可以考虑这些日志打印去掉
            }
        }
        zdClosesocketProc(pConn);
        return -1;
     }
     
     return n; // 返回字节数
}

//包头收完整后的处理，我们称为包处理阶段1【p1】：写成函数，方便复用
//注意参数isflood是个引用
void CSocket::ngx_wait_request_handler_proc_p1(lpngx_connection_t pConn,bool &isflood)
{
    CMemory *p_memory = CMemory::GetInstance();
    LPCOMM_PKG_HEADER pPkgHeader = (LPCOMM_PKG_HEADER)pConn->dataHeadInfo;

    unsigned short e_pkgLen = ntohs(pPkgHeader->pkgLen); // 网络序转本机序
    if (e_pkgLen < m_iLenPkgHeader)//恶意包或者错误包的判断
    {
        pConn->curStat == _PKG_HD_INIT;
        pConn->precvbuf = pConn->dataHeadInfo;
        pConn->irecvlen = m_iLenPkgHeader;
    }
    else if (e_pkgLen > _PKG_MAX_LENGTH - 1000)
    {
        pConn->curStat == _PKG_HD_INIT;
        pConn->precvbuf = pConn->dataHeadInfo;
        pConn->irecvlen = m_iLenPkgHeader;
    }
    else
    {
        // 合法包头
        char *pTmpBuffer = (char *)p_memory->AllocMemory(m_iLenMsgHeader + e_pkgLen, false);
        pConn->precvMemPointer = pTmpBuffer;

        //a)先填写消息头内容
        LPSTRUC_MSG_HEADER pTmpMsgHeader = (LPSTRUC_MSG_HEADER)pTmpBuffer;
        pTmpMsgHeader->pConn = pConn;
        pTmpMsgHeader->iCurrsequence = pConn->iCurrsequence;

        //b)再填写包头内容
        pTmpBuffer += m_iLenMsgHeader;
        memcpy(pTmpBuffer, pPkgHeader, m_iLenPkgHeader); //直接把收到的包头拷贝进来
        if(e_pkgLen == m_iLenPkgHeader)
        {
            //该报文只有包头无包体【我们允许一个包只有包头，没有包体】
            //这相当于收完整了，则直接入消息队列待后续业务逻辑线程去处理吧
            if(m_floodAkEnable == 1) 
            {
                //Flood攻击检测是否开启
                isflood = TestFlood(pConn);
            }
            ngx_wait_request_handler_proc_plast(pConn,isflood);
        } 
        else
        {
            //开始收包体，注意我的写法
            pConn->curStat = _PKG_BD_INIT;                   //当前状态发生改变，包头刚好收完，准备接收包体	    
            pConn->precvbuf = pTmpBuffer + m_iLenPkgHeader;  //pTmpBuffer指向包头，这里 + m_iLenPkgHeader后指向包体 weizhi
            pConn->irecvlen = e_pkgLen - m_iLenPkgHeader;    //e_pkgLen是整个包【包头+包体】大小，-m_iLenPkgHeader【包头】  = 包体
        }    
    }
    return ;
}

//收到一个完整包后的处理【plast表示最后阶段】，放到一个函数中，方便调用
//注意参数isflood是个引用
void CSocket::ngx_wait_request_handler_proc_plast(lpngx_connection_t pConn,bool &isflood)
{
    if (isflood == false)
    {
        g_threadpool.inMsgRecvQueueAndSignal(pConn->precvMemPointer); //入消息队列并触发线程处理消息
    }
    else
    {
        CMemory *p_memory = CMemory::GetInstance();
        p_memory->FreeMemory(pConn);
    }

    pConn->precvMemPointer = NULL;
    pConn->curStat         = _PKG_HD_INIT;     //收包状态机的状态恢复为原始态，为收下一个包做准备                    
    pConn->precvbuf        = pConn->dataHeadInfo;  //设置好收包的位置
    pConn->irecvlen        = m_iLenPkgHeader;  //设置好要接收数据的大小
    return;
}

//发送数据专用函数，返回本次发送的字节数
//返回 > 0，成功发送了一些字节
//=0，估计对方断了
//-1，errno == EAGAIN ，本方发送缓冲区满了
//-2，errno != EAGAIN != EWOULDBLOCK != EINTR ，一般我认为都是对端断开的错误
ssize_t CSocket::sendproc(lpngx_connection_t c,char *buff,ssize_t size)  //ssize_t是有符号整型，在32位机器上等同与int，在64位机器上等同与long int，size_t就是无符号型的ssize_t
{
    ssize_t n;

    for (;;)
    {
        n = send(c->fd, buff, size, false);
        if (n > 0)
        {
            return 0;
        }

        if (n == 0)
        {
            return 0;
        }

        if (errno == EINTR)
        {
            ngx_log_stderr(errno,"CSocekt::sendproc()中send()失败.");
        }
        else
        {
            return -2;
        }
    }
}

//设置数据发送时的写处理函数,当数据可写时epoll通知我们，我们在 int CSocekt::ngx_epoll_process_events(int timer)  中调用此函数
//能走到这里，数据就是没法送完毕，要继续发送
void CSocket::ngx_write_request_handler(lpngx_connection_t pConn)
{
    CMemory *p_memory = CMemory::GetInstance();

    ssize_t sendsize = sendproc(pConn, pConn->psendbuf, pConn->isendlen);

    if(sendsize > 0 && sendsize != pConn->isendlen)
    {
        pConn->psendbuf += sendsize;
        pConn->isendlen -= sendsize;
        return ;
    }
    else if (sendsize == -1)
    {
        //这不太可能，可以发送数据时通知我发送数据，我发送时你却通知我发送缓冲区满？
        ngx_log_stderr(errno,"CSocekt::ngx_write_request_handler()时if(sendsize == -1)成立，这很怪异。"); //打印个日志，别的先不干啥
        return;
    }
    if(sendsize > 0 && sendsize == pConn->isendlen) //成功发送完毕，做个通知是可以的；
    {
        //如果是成功的发送完毕数据，则把写事件通知从epoll中干掉吧；其他情况，那就是断线了，等着系统内核把连接从红黑树中干掉即可；
        if(ngx_epoll_oper_event(
                pConn->fd,          //socket句柄
                EPOLL_CTL_MOD,      //事件类型，这里是修改【因为我们准备减去写通知】
                EPOLLOUT,           //标志，这里代表要减去的标志,EPOLLOUT：可写【可写的时候通知我】
                1,                  //对于事件类型为增加的，EPOLL_CTL_MOD需要这个参数, 0：增加   1：去掉 2：完全覆盖
                pConn               //连接池中的连接
                ) == -1)
        {
            //有这情况发生？这可比较麻烦，不过先do nothing
            ngx_log_stderr(errno,"CSocekt::ngx_write_request_handler()中ngx_epoll_oper_event()失败。");
        }  
    }

    // 发送完了，或者对端断开
    p_memory->FreeMemory(pConn->psendMemPointer);
    pConn->psendMemPointer = NULL;
    pConn->iThrowsendCount--;
    if (sem_post(&m_semEventSendQueue) == -1)
    {
        ngx_log_stderr(0,"CSocekt::ngx_write_request_handler()中sem_post(&m_semEventSendQueue)失败.");
    }

    return ;
}

//消息处理线程主函数，专门处理各种接收到的TCP消息
//pMsgBuf：发送过来的消息缓冲区，消息本身是自解释的，通过包头可以计算整个包长
//         消息本身格式【消息头+包头+包体】 
void CSocket::threadRecvProcFunc(char *pMsgBuf)
{   
    return;
}
