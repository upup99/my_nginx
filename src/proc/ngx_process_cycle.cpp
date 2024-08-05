//和开启子进程相关

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>   //信号相关头文件 
#include <errno.h>    //errno
#include <unistd.h>

#include "ngx_func.h"
#include "ngx_macro.h"
#include "ngx_c_conf.h"

//函数声明
static void ngx_start_worker_processes(int threadnums);
static int ngx_spawn_process(int threadnums,const char *pprocname);
static void ngx_worker_process_cycle(int inum,const char *pprocname);
static void ngx_worker_process_init(int inum);

//变量声明
static u_char  master_process[] = "master process";

// 描述：创建worker子进程
void ngx_master_process_cycle()
{
    sigset_t set; // 信号集

    sigemptyset(&set);

    //下列这些信号在执行本函数期间不希望收到（保护不希望由信号中断的代码临界区）
    //建议fork()子进程时学习这种写法，防止信号的干扰；
    sigaddset(&set, SIGCHLD);     //子进程状态改变
    sigaddset(&set, SIGALRM);     //定时器超时
    sigaddset(&set, SIGIO);       //异步I/O
    sigaddset(&set, SIGINT);      //终端中断符
    sigaddset(&set, SIGHUP);      //连接断开
    sigaddset(&set, SIGUSR1);     //用户定义信号
    sigaddset(&set, SIGUSR2);     //用户定义信号
    sigaddset(&set, SIGWINCH);    //终端窗口大小改变
    sigaddset(&set, SIGTERM);     //终止
    sigaddset(&set, SIGQUIT);     //终端退出符
    //.........可以根据开发的实际需要往其中添加其他要屏蔽的信号......

    //设置，此时无法接受的信号；阻塞期间，你发过来的上述信号，多个会被合并为一个，暂存着，等你放开信号屏蔽后才能收到这些信号
    if (sigprocmask(SIG_BLOCK, &set, NULL) == -1)
    {
        ngx_log_error_core(NGX_LOG_ALERT,errno,"ngx_master_process_cycle()中sigprocmask()失败!");
    }
    //即便sigprocmask失败，程序流程 也继续往下走

    //首先我设置主进程标题---------begin
    size_t size;
    int i;
    size = sizeof(master_process);
    size += g_argvneedmem;
    if (size < 1000)
    {
        char title[1000] = {0};
        strcpy(title, (const char *)master_process);
        strcat(title, " ");
        for (i = 0; i < g_os_argc; i++)
        {
            strcat(title, g_os_argv[i]);
        }
        ngx_setproctitle(title); //设置标题
        ngx_log_error_core(NGX_LOG_NOTICE,0,"%s %P 【master进程】启动并开始运行......!",title,ngx_pid); //设置标题时顺便记录下来进程名，进程id等信息到日志
    }
    //首先我设置主进程标题---------end

    //从配置文件中读取要创建的worker进程数量
    CConfig *p_config = CConfig::GetInstance();
    int workprocess = p_config->GetIntDefault("WorkProcesses", 1);
    ngx_start_worker_processes(workprocess);  //这里要创建worker子进程

    //创建子进程后，父进程的执行流程会返回到这里，子进程不会走进来    
    sigemptyset(&set); //信号屏蔽字为空，表示不屏蔽任何信号

    for (;;)
    {
        sigsuspend(&set);
        sleep(1);
    }

    return ;
}

//描述：根据给定的参数创建指定数量的子进程，因为以后可能要扩展功能，增加参数，所以单独写成一个函数
//threadnums:要创建的子进程数量
static void ngx_start_worker_processes(int threadnums)
{
    int i;
    for (i = 0; i < threadnums; i++)
    {
        ngx_spawn_process(i, "worker process");
    }
    return;
}

//描述：产生一个子进程
//inum：进程编号【0开始】
//pprocname：子进程名字"worker process"
static int ngx_spawn_process(int inum, const char *pprocname)
{
    pid_t pid;

    pid = fork();
    switch(pid)
    {
    case -1: // 产生子进程失败
        ngx_log_error_core(NGX_LOG_ALERT,errno,"ngx_spawn_process()fork()产生子进程num=%d,procname=\"%s\"失败!",inum,pprocname);
        return -1;
    case 0: // 子进程
        ngx_parent = ngx_pid;
        ngx_pid = getpid();
        ngx_worker_process_cycle(inum,pprocname);    //我希望所有worker子进程，在这个函数里不断循环着不出来，也就是说，子进程流程不往下边走;
        break;
    default: // 父进程
        break;
    }

    return pid;
}

//描述：worker子进程的功能函数，每个woker子进程，就在这里循环着了（无限循环【处理网络事件和定时器事件以对外提供web服务】）
//     子进程分叉才会走到这里
//inum：进程编号【0开始】
static void ngx_worker_process_cycle(int inum,const char *pprocname) 
{
    // 设置一下变量
    ngx_process = NGX_PROCESS_WORKER;

    // 重新为子进程设置进程名，避免与父进程名重复
    ngx_worker_process_init(inum);
    ngx_setproctitle(pprocname);
    ngx_log_error_core(NGX_LOG_NOTICE,0,"%s %P 【worker进程】启动并开始运行......!",pprocname,ngx_pid); //设置标题时顺便记录下来进程名，进程id等信息到日志

    // 可以测试业务代码
    for (;;)
    {
        ngx_process_events_and_timers();
    }

    // 如果从这个循环跳出来了
    g_threadpool.stopAll();
    g_socket.Shutdown_subproc();
    return ;
}

//描述：子进程创建时调用本函数进行一些初始化工作
static void ngx_worker_process_init(int inum)
{
    sigset_t set;

    sigemptyset(&set);
    if (sigprocmask(SIG_SETMASK, &set, NULL) == -1) // 不阻塞任何信号
    {
        ngx_log_error_core(NGX_LOG_ALERT,errno,"ngx_worker_process_init()中sigprocmask()失败!");
    }

    //线程池代码，率先创建，至少要比和socket相关的内容优先
    CConfig *p_config = CConfig::GetInstance();
    int tmpthreadnums = p_config->GetIntDefault("ProcMsgRecvWorkThreadCount", 5);//处理接收到的消息的线程池中线程数量
    if (g_threadpool.create(tmpthreadnums) == false)
    {
        //内存没释放，但是简单粗暴退出；
        exit(-2);
    }
    sleep(1);

    if (g_socket.Initialize_subproc() == false)
    {
        //内存没释放，但是简单粗暴退出；
        exit(-2);
    }

    g_socket.ngx_epoll_init();

    return ;
}