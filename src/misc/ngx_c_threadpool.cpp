//和 线程池 有关的函数放这里

#include <stdarg.h>
#include <unistd.h>  //usleep

#include "ngx_global.h"
#include "ngx_func.h"
#include "ngx_c_threadpool.h"
#include "ngx_c_memory.h"
#include "ngx_macro.h"

//静态成员初始化
pthread_mutex_t CThreadPool::m_pthreadMutex = PTHREAD_MUTEX_INITIALIZER;  //#define PTHREAD_MUTEX_INITIALIZER ((pthread_mutex_t) -1)
pthread_cond_t CThreadPool::m_pthreadCond = PTHREAD_COND_INITIALIZER;     //#define PTHREAD_COND_INITIALIZER ((pthread_cond_t) -1)
bool CThreadPool::m_shutdown = false;    //刚开始标记整个线程池的线程是不退出的    

// 构造函数
CThreadPool::CThreadPool()
{
    m_iRunningThreadNum = 0;
    m_iLastEmgTime      = 0;
    m_iRecvMsgQueueCount = 0;
}

// 析构函数
CThreadPool::~CThreadPool()
{
    // 接收消息队列内容释放
    clearMsgRecvQueue();
}

//各种清理函数-------------------------
//清理接收消息队列，注意这个函数的写法。
void CThreadPool::clearMsgRecvQueue()
{
    char *sTmpMempoint;
    CMemory *p_memory = CMemory::GetInstance();

    while (!m_msgRecvQueue.empty())
    {
        sTmpMempoint = m_msgRecvQueue.front();
        m_msgRecvQueue.pop_front();
        p_memory->FreeMemory(sTmpMempoint);
    }
}

//创建线程池中的线程，要手工调用，不在构造函数里调用了
//返回值：所有线程都创建成功则返回true，出现错误则返回false
bool CThreadPool::create(int threadNums)
{
    ThreadItem *pNew;
    int err;

    m_iThreadNum = threadNums;

    for (int i = 0; i < m_iThreadNum; i++)
    {
        m_threadVector.push_back(pNew = new ThreadItem(this));
        err = pthread_create(&pNew->_Handle, NULL, threadFunc, pNew);
        if (err != 0)
        {
            //创建线程有错
            ngx_log_stderr(err,"CThreadPool::Create()创建线程%d失败，返回的错误码为%d!",i,err);
            return false;
        }
        else
        {
            // 创建success
        }
    }

    // 必须保证每个线程都启动并运行到pthread_cond_wait()，本函数才返回，只有这样，这几个线程才能进行后续的正常工作
    std::vector<ThreadItem*>::iterator pos;
lblfor:
    for (pos = m_threadVector.begin(); pos != m_threadVector.end(); pos++)
    {
        if ((*pos)->ifRunning == false) // 该条件保证所有线程启动了
        {
            usleep(1000);
            goto lblfor;
        }
    }

    return true;
}

//线程入口函数，当用pthread_create()创建线程后，这个ThreadFunc()函数都会被立即执行；
void * CThreadPool::threadFunc(void *threadData)
{
    //这个是静态成员函数，是不存在this指针的；
    ThreadItem *pThread = static_cast<ThreadItem*>(threadData);
    CThreadPool *pThreadPoolObj = pThread->_pThis;

    CMemory *p_memory = CMemory::GetInstance();
    int err;

    pthread_t tid = pthread_self();
    while (true)
    {
        err = pthread_mutex_lock(&m_pthreadMutex);
        if(err != 0) ngx_log_stderr(err,"CThreadPool::ThreadFunc()中pthread_mutex_lock()失败，返回的错误码为%d!",err);//有问题，要及时报告

        
        //以下这行程序写法技巧十分重要，必须要用while这种写法，
        //因为：pthread_cond_wait()是个值得注意的函数，调用一次pthread_cond_signal()可能会唤醒多个【惊群】【官方描述是 至少一个/pthread_cond_signal 在多处理器上可能同时唤醒多个线程】
        while ( (pThreadPoolObj->m_msgRecvQueue.size() == 0) && m_shutdown == false)
        {
            if (pThread->ifRunning == false)
            {
                pThread->ifRunning = true;
            }

            pthread_cond_wait(&m_pthreadCond, &m_pthreadMutex);
        }

        if (m_shutdown)
        {
            pthread_mutex_unlock(&m_pthreadMutex);
            break;
        }

        //走到这里，可以取得消息进行处理了【消息队列中必然有消息】,注意，目前还是互斥着
        char *jobbuf = pThreadPoolObj->m_msgRecvQueue.front();
        pThreadPoolObj->m_msgRecvQueue.pop_front();
        pThreadPoolObj->m_iRecvMsgQueueCount--;

        // 解锁互斥量
        err = pthread_mutex_unlock(&m_pthreadMutex);
        if(err != 0)  ngx_log_stderr(err,"CThreadPool::ThreadFunc()中pthread_mutex_unlock()失败，返回的错误码为%d!",err);//有问题，要及时报告
        
        //能走到这里的，就是有消息可以处理，开始处理
        pThreadPoolObj->m_iRunningThreadNum++;
        g_socket.threadRecvProcFunc(jobbuf);

        p_memory->FreeMemory(jobbuf);
        pThreadPoolObj->m_iRunningThreadNum--;
    } // end while (true)

    return (void*)0;
}

//停止所有线程【等待结束线程池中所有线程，该函数返回后，应该是所有线程池中线程都结束了】
void CThreadPool::stopAll()
{
    //(1)已经调用过，就不要重复调用了
    if (m_shutdown)
    {
        return ;
    }
    m_shutdown = true;

    //(2)唤醒等待该条件【卡在pthread_cond_wait()的】的所有线程，一定要在改变条件状态以后再给线程发信号
    int err = pthread_cond_broadcast(&m_pthreadCond);
    if (err != 0)
    {
        //这肯定是有问题，要打印紧急日志
        ngx_log_stderr(err,"CThreadPool::StopAll()中pthread_cond_broadcast()失败，返回的错误码为%d!",err);
        return;
    }

    //(3)等等线程，让线程真返回    
    std::vector<ThreadItem*>::iterator pos;
    for (pos = m_threadVector.begin(); pos != m_threadVector.end(); pos++)
    {
        pthread_join((*pos)->_Handle, NULL); // 等待一个线程终止
    }

    //流程走到这里，那么所有的线程池中的线程肯定都返回了；
    pthread_mutex_destroy(&m_pthreadMutex);
    pthread_cond_destroy(&m_pthreadCond);

    //(4)释放一下new出来的ThreadItem【线程池中的线程】   
    for (pos = m_threadVector.begin(); pos != m_threadVector.end(); pos++)
    {
        if ((*pos))
        {
            delete *pos;
        }
    }
    m_threadVector.clear();

    ngx_log_stderr(0,"CThreadPool::StopAll()成功返回，线程池中线程全部正常结束!");
    return;   
}

//--------------------------------------------------------------------------------------
//收到一个完整消息后，入消息队列，并触发线程池中线程来处理该消息
void CThreadPool::inMsgRecvQueueAndSignal(char *buf)
{
    int err = pthread_mutex_lock(&m_pthreadMutex);
    if (err != 0) ngx_log_stderr(err,"CThreadPool::inMsgRecvQueueAndSignal()pthread_mutex_lock()失败，返回的错误码为%d!",err);

    m_msgRecvQueue.push_back(buf);
    m_iRecvMsgQueueCount++;

    err = pthread_mutex_unlock(&m_pthreadMutex);
    if (err != 0) ngx_log_stderr(err,"CThreadPool::inMsgRecvQueueAndSignal()pthread_mutex_lock()失败，返回的错误码为%d!",err);

    call();
    return;
}

// 调一个线程池中的线程下来干活
void CThreadPool::call()
{
    int err = pthread_cond_signal(&m_pthreadCond); //唤醒一个等待该条件的线程，也就是可以唤醒卡在pthread_cond_wait()的线程
    if(err != 0 )
    {
        //这是有问题啊，要打印日志啊
        ngx_log_stderr(err,"CThreadPool::Call()中pthread_cond_signal()失败，返回的错误码为%d!",err);
    }

    // 线程不够了
    if (m_iThreadNum == m_iRunningThreadNum)
    {
        time_t currtime = time(NULL);
        if(currtime - m_iLastEmgTime > 10) //最少间隔10秒钟才报一次线程池中线程不够用的问题；
        {
            //两次报告之间的间隔必须超过10秒，不然如果一直出现当前工作线程全忙，但频繁报告日志也够烦的
            m_iLastEmgTime = currtime;  //更新时间
            //写日志，通知这种紧急情况给用户，用户要考虑增加线程池中线程数量了
            ngx_log_stderr(0,"CThreadPool::Call()中发现线程池中当前空闲线程数量为0，要考虑扩容线程池了!");
        }
    }

    return;
}

