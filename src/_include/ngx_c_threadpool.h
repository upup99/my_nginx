#pragma once
#ifndef __NGX_THREADPOOL_H__
#define __NGX_THREADPOOL_H__

#include <vector>
#include <list>
#include <pthread.h>
#include <atomic>

// 线程池相关类
class CThreadPool
{
public:
	CThreadPool();
	~CThreadPool();

public:
	bool create(int threadNum = 8);
	void stopAll();

	void inMsgRecvQueueAndSignal(char* buf);
	void call();
	int getRecvMsgQueueCount() { return m_iRecvMsgQueueCount; }

private:
	static void* threadFunc(void* threadData);
	void clearMsgRecvQueue();

private:
	//定义一个 线程池中的 线程 的结构，以后可能做一些统计之类的 功能扩展，所以引入这么个结构来 代表线程
	struct ThreadItem
	{
		pthread_t		_Handle;
		CThreadPool		* _pThis;
		bool			ifRunning;

		ThreadItem(CThreadPool *pthis) : _pThis(pthis), ifRunning(false){}
		~ThreadItem(){}
	};

private:
	static pthread_mutex_t		m_pthreadMutex;
	static pthread_cond_t		m_pthreadCond;
	static bool					m_shutdown;

	int							m_iThreadNum;

	std::atomic<int>			m_iRunningThreadNum;
	time_t						m_iLastEmgTime;

	std::vector<ThreadItem*>	m_threadVector;

	// 接收队列相关
	std::list<char*>			m_msgRecvQueue;
	int							m_iRecvMsgQueueCount;
};

#endif