#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h> 
#include <errno.h>
#include <arpa/inet.h>
#include <sys/time.h>          //gettimeofday
#include <iostream>

#include "ngx_c_conf.h"
#include "ngx_func.h"
#include "ngx_c_memory.h"
#include "ngx_c_crc32.h"
#include "ngx_c_socket.h"
#include "ngx_c_slogic.h"
#include "ngx_c_threadpool.h"
#include "ngx_macro.h"

//本文件用的函数声明
static void freeresource();

//和设置标题有关的全局量
size_t  g_argvneedmem = 0;        //保存下这些argv参数所需要的内存大小
size_t  g_envneedmem = 0;         //环境变量所占内存大小
int     g_os_argc;              //参数个数 
char** g_os_argv;            //原始命令行参数数组,在main中会被赋值
char* gp_envmem = NULL;        //指向自己分配的env环境变量的内存，在ngx_init_setproctitle()函数中会被分配内存
int     g_daemonized = 0;         //守护进程标记，标记是否启用了守护进程模式，0：未启用，1：启用了

//socket/线程池相关
//CSocket      g_socket;          //socket全局对象
CLogicSocket   g_socket;        //socket全局对象  
CThreadPool    g_threadpool;    //线程池全局对象

//和进程本身有关的全局量
pid_t   ngx_pid;                //当前进程的pid
pid_t   ngx_parent;             //父进程的pid
int     ngx_process;            //进程类型，比如master,worker进程等
int     g_stopEvent;            //标志程序退出,0不退出1，退出

sig_atomic_t  ngx_reap;         //标记子进程状态变化[一般是子进程发来SIGCHLD信号表示退出],sig_atomic_t:系统定义的类型：访问或改变这些变量需要在计算机的一条指令内完成
//一般等价于int【通常情况下，int类型的变量通常是原子访问的，也可以认为 sig_atomic_t就是int类型的数据】                                   

int main(int argc, char *const *argv)
{
	std::cout << "Hello nginx!" << std::endl;
	int exitcode = 0; // 退出代码，0表示正常

	g_stopEvent = 0; // 标记程序是否退出，0不退出

	ngx_pid = getpid(); // 取得进程pid
	ngx_parent = getppid();

	// 统计argv所占的内存
	for (int i = 0; i < argc; i++)
	{
		g_argvneedmem += strlen(argv[i]) + 1; // +1给\0留空间
	}

	// 统计环境变量所占的内存，注意判断方法是environ[i]是否为空 作为环境变量结束标记
	for (int i = 0; environ[i]; i++)
	{
		g_envneedmem += strlen(environ[i]) + 1;
	}

	g_os_argc = argc; // 保存参数个数
	g_os_argv = (char**)argv; // 保存参数指针

	// 全局量有必要初始化的
	ngx_log.fd = -1;                  //-1：表示日志文件尚未打开；因为后边ngx_log_stderr要用所以这里先给-1
	ngx_process = NGX_PROCESS_MASTER; //先标记本进程是master进程
	ngx_reap = 0;                     //标记子进程没有发生变化

	// 配置文件读取
	CConfig* p_config = CConfig::GetInstance(); // 单例类
	const char *pconfname = "/root/code/nginx/src/nginx.conf";
	if (p_config->Load(pconfname) == false)
	{
		ngx_log_init(); // 初始化日志
		ngx_log_stderr(0, "配置文件[%s]载入失败，退出!", "nginx.conf");

		exitcode = 2; //标记找不到文件
		goto lblexit;
	}

	// 内存单例类初始化
	CMemory::GetInstance();

	// crc32校验算法单例类初始化
	CCRC32::GetInstance();

	// 初始化日志，需要配置项，故在配置文件载入的后面
	ngx_log_init();

	// 一些初始化函数
	if (ngx_init_signals() != 0)
	{
		exitcode = 1;
		goto lblexit;
	}
	if (g_socket.Initialize() == false)
	{
		exitcode = 1;
		goto lblexit;
	}

	// 其他初始化
	ngx_init_setproctitle(); // 环境变量搬家

	//------------------------------------
	// 创建守护进程
	if (p_config->GetIntDefault("Daemon", 0) == 1) //读配置文件，拿到配置文件中是否按守护进程方式启动的选项
	{
		// 按守护进程方式运行
		int cdaemonresult = ngx_daemon();
		if (cdaemonresult == -1) // fork失败
		{
			exitcode = 1;
			goto lblexit;
		}
		if (cdaemonresult == 1)
		{
			// 这时原始的父进程
			freeresource();
			exitcode = 0;
			return exitcode;
		}
		g_daemonized = 1; //守护进程标记，标记是否启用了守护进程模式，0：未启用，1：启用
	}

	ngx_master_process_cycle();
lblexit:
	ngx_log_stderr(0, "程序退出！");
	freeresource();
	return exitcode;
}

void freeresource()
{
	if (gp_envmem)
	{
		delete[]gp_envmem;
		gp_envmem = nullptr;
	}

	if (ngx_log.fd != STDERR_FILENO && ngx_log.fd != -1)
	{
		close(ngx_log.fd);
		ngx_log.fd = -1;
	}
}