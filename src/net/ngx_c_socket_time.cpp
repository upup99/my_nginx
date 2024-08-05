//和网络 中 时间 有关的函数放这里

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

//设置踢出时钟(向multimap表中增加内容)，用户三次握手成功连入，然后我们开启了踢人开关【Sock_WaitTimeEnable = 1】，那么本函数被调用；
void CSocket::AddToTimerQueue(lpngx_connection_t pConn)
{
    CMemory *p_memory = CMemory::GetInstance();

    time_t  fultime = time(NULL);
    fultime += m_iWaitTime;

    CLock lock(&m_timequeueMutex);
    LPSTRUC_MSG_HEADER tmpMsgHeader = (LPSTRUC_MSG_HEADER)p_memory->AllocMemory(m_iLenMsgHeader, false);
    tmpMsgHeader->pConn = pConn;
    tmpMsgHeader->iCurrsequence = pConn->iCurrsequence;
    m_timerQueuemap.insert(std::make_pair(fultime, tmpMsgHeader)); // 从小到大排序
    m_cur_size_++;
    m_timer_value_ = GetEarliestTime();
    return ;
}

//从multimap中取得最早的时间返回去，调用者负责互斥，所以本函数不用互斥，调用者确保m_timeQueuemap中一定不为空
time_t CSocket::GetEarliestTime()
{
    std::multimap<time_t, LPSTRUC_MSG_HEADER>::iterator pos;
    pos = m_timerQueuemap.begin();
    return pos->first;
}

//从m_timeQueuemap移除最早的时间，并把最早这个时间所在的项的值所对应的指针 返回，调用者负责互斥，所以本函数不用互斥，
LPSTRUC_MSG_HEADER CSocket::RemoveFirstTimer()
{
    std::multimap<time_t, LPSTRUC_MSG_HEADER>::iterator pos;
    LPSTRUC_MSG_HEADER p_tmp;
    if (m_cur_size_ <= 0)
    {
        return NULL;
    }
    pos = m_timerQueuemap.begin();
    p_tmp = pos->second;
    m_timerQueuemap.erase(pos);
    m_cur_size_--;
    return p_tmp;
}

//根据给的当前时间，从m_timeQueuemap找到比这个时间更老（更早）的节点【1个】返回去，这些节点都是时间超过了，要处理的节点
//调用者负责互斥，所以本函数不用互斥
LPSTRUC_MSG_HEADER CSocket::GetOverTimeTimer(time_t cur_time)
{
    // std::multimap<time_t, LPSTRUC_MSG_HEADER>::iterator pos;
    // for (pos = m_timerQueuemap.begin(); pos != m_timerQueuemap.end(); pos++)
    // {
    //     if (pos->first )
    // }
    CMemory *p_memory = CMemory::GetInstance();
    LPSTRUC_MSG_HEADER p_tmp;

    if (m_cur_size_ == 0 || m_timerQueuemap.empty())
    {
        return NULL;
    }

    time_t earliesttime = GetEarliestTime();
    if (earliesttime <= cur_time)
    {
        p_tmp = RemoveFirstTimer();

        if (m_ifTimeOutKick != 1) // 如果不是超时就退出
        {
            time_t newtime = cur_time + m_iWaitTime;
            LPSTRUC_MSG_HEADER tmpMsgHeader = (LPSTRUC_MSG_HEADER)p_memory->AllocMemory(m_iLenMsgHeader, false);
            tmpMsgHeader->pConn = p_tmp->pConn;
            tmpMsgHeader->iCurrsequence = p_tmp->iCurrsequence;
            m_timerQueuemap.insert(std::make_pair(newtime, tmpMsgHeader));
            m_cur_size_++;
        }

        if (m_cur_size_ > 0)
        {
            m_timer_value_ = GetEarliestTime();
        }
    } // end if (earliesttime <= cur_time)
    return p_tmp;
}

//把指定用户tcp连接从timer表中抠出去
void CSocket::DeleteFromTimerQueue(lpngx_connection_t pConn)
{
    std::multimap<time_t, LPSTRUC_MSG_HEADER>::iterator pos, posend;
    CMemory *p_memory = CMemory::GetInstance();

    CLock lock(&m_timequeueMutex);

    //因为实际情况可能比较复杂，将来可能还扩充代码等等，所以如下我们遍历整个队列找 一圈，而不是找到一次就拉倒，以免出现什么遗漏
lblMTQM:
    pos = m_timerQueuemap.begin();
    posend = m_timerQueuemap.end();
    for (; pos != posend; pos++)
    {
        if (pos->second->pConn == pConn)
        {
            p_memory->FreeMemory(pos->second);
            m_timerQueuemap.erase(pos);
            m_cur_size_--;
            goto lblMTQM;
        }
    }

    if (m_cur_size_ > 0)
    {
        m_timer_value_ = GetEarliestTime();
    }
    return ;
}

//清理时间队列中所有内容
void CSocket::clearAllFromTimerQueue()
{
	std::multimap<time_t, LPSTRUC_MSG_HEADER>::iterator pos,posend;

	CMemory *p_memory = CMemory::GetInstance();	
	pos    = m_timerQueuemap.begin();
	posend = m_timerQueuemap.end();    
	for(; pos != posend; ++pos)	
	{
		p_memory->FreeMemory(pos->second);		
		--m_cur_size_; 		
	}
	m_timerQueuemap.clear();
}

//时间队列监视和处理线程，处理到期不发心跳包的用户踢出的线程
void* CSocket::ServerTimerQueueMonitorThread(void* threadData)
{
    ThreadItem *pThread = static_cast<ThreadItem*>(threadData);
    CSocket *pSocketObj = pThread->_pThis;

    time_t absolute_time, cur_time;
    int err;

    while (g_stopEvent == 0)
    {
        //这里没互斥判断，所以只是个初级判断，目的至少是队列为空时避免系统损耗
        if (pSocketObj->m_cur_size_ > 0)
        {
            absolute_time = pSocketObj->m_timer_value_;
            cur_time = time(NULL);

            if (absolute_time < cur_time)
            {
                //时间到了，可以处理了
                std::list<LPSTRUC_MSG_HEADER> m_lsIdleList;
                LPSTRUC_MSG_HEADER result;

                err = pthread_mutex_lock(&pSocketObj->m_timequeueMutex);
                if (err != 0) ngx_log_stderr(err,"CSocekt::ServerTimerQueueMonitorThread()中pthread_mutex_lock()失败，返回的错误码为%d!",err);//有问题，要及时报告
                while ((result = pSocketObj->GetOverTimeTimer(cur_time)) != NULL) // 一次性拿出所有的超时节点
                {
                    m_lsIdleList.push_back(result);
                }
                err = pthread_mutex_unlock(&pSocketObj->m_timequeueMutex); 
                if(err != 0)  ngx_log_stderr(err,"CSocekt::ServerTimerQueueMonitorThread()pthread_mutex_unlock()失败，返回的错误码为%d!",err);//有问题，要及时报告

                LPSTRUC_MSG_HEADER p_tmp;
                while (!m_lsIdleList.empty())
                {
                    p_tmp = m_lsIdleList.front();
                    m_lsIdleList.pop_front();
                    pSocketObj->procPingTimeOutChecking(p_tmp, cur_time);
                }

            }
        }//end if(pSocketObj->m_cur_size_ > 0)
        usleep(500 * 1000);
    }
    return (void*)0;
}

//心跳包检测时间到，该去检测心跳包是否超时的事宜，本函数只是把内存释放，子类应该重新事先该函数以实现具体的判断动作
void CSocket::procPingTimeOutChecking(LPSTRUC_MSG_HEADER tmpmsg, time_t cur_time)
{
    CMemory *p_memory = CMemory::GetInstance();
    p_memory->FreeMemory(tmpmsg);
}