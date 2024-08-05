//和信号有关的函数放这里
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>    //信号相关头文件 
#include <errno.h>     //errno
#include <sys/wait.h>  //waitpid

#include "ngx_global.h"
#include "ngx_macro.h"
#include "ngx_func.h" 

//一个信号有关的结构 ngx_signal_t
typedef struct
{
    int         sigNo;  //信号对应的数字编号 ，每个信号都有对应的#define
    const char  *sigName; //信号对应的中文名字 ，比如SIGHUP
    //信号处理函数,这个函数由我们自己来提供，但是它的参数和返回值是固定的
    void (*handler)(int sigNo, siginfo_t *siginfo, void *ucontext);
} ngx_signal_t;

// 声明一个信号处理函数
static void ngx_signal_handler(int sigNo, siginfo_t *siginfo, void *ucontext);
static void ngx_process_get_status(void);

ngx_signal_t  signals[] = {
    // signo      signame             handler
    { SIGHUP,    "SIGHUP",           ngx_signal_handler },        //终端断开信号，对于守护进程常用于reload重载配置文件通知--标识1
    { SIGINT,    "SIGINT",           ngx_signal_handler },        //标识2   
	{ SIGTERM,   "SIGTERM",          ngx_signal_handler },        //标识15
    { SIGCHLD,   "SIGCHLD",          ngx_signal_handler },        //子进程退出时，父进程会收到这个信号--标识17
    { SIGQUIT,   "SIGQUIT",          ngx_signal_handler },        //标识3
    { SIGIO,     "SIGIO",            ngx_signal_handler },        //指示一个异步I/O事件【通用异步I/O信号】
    { SIGSYS,    "SIGSYS, SIG_IGN",  NULL               },        //我们想忽略这个信号，SIGSYS表示收到了一个无效系统调用，如果我们不忽略，进程会被操作系统杀死，--标识31
                                                                  //所以我们把handler设置为NULL，代表 我要求忽略这个信号，请求操作系统不要执行缺省的该信号处理动作（杀掉我）
    //...日后根据需要再继续增加
    { 0,         NULL,               NULL               }         //信号对应的数字至少是1，所以可以用0作为一个特殊标记
};

//初始化信号的函数，用于注册信号处理程序
//返回值：0成功  ，-1失败
int ngx_init_signals()
{
    ngx_signal_t        *sig;
    struct sigaction     sa;

    for (sig = signals; sig->sigNo != 0; sig++)
    {
        memset(&sa, 0, sizeof(struct sigaction));

        if (sig->handler)
        {
            sa.sa_sigaction = sig->handler;
            sa.sa_flags = SA_SIGINFO;
        }
        else
        {
            sa.sa_handler = SIG_IGN;
        } //end if

        sigemptyset(&sa.sa_mask);

        if (sigaction(sig->sigNo, &sa, NULL) == -1)
        {
            ngx_log_error_core(NGX_LOG_EMERG, errno, "sigaction(%s) failed", sig->sigName);
            return -1;
        }
        else
        {
            //ngx_log_stderr(0,"sigaction(%s) succed!",sig->signame); //直接往屏幕上打印看看 ，不需要时可以去掉
        }
    }
    return 0;
}

// 信号处理函数
static void ngx_signal_handler(int sigNo, siginfo_t *siginfo, void *ucontext)
{
    ngx_signal_t    *sig;
    char            *action;

    for (sig = signals; sig->sigNo != 0; sig++)
    {
        // 找到相应信号
        if (sig->sigNo == sigNo) break;
    }
    
    action = (char*)"";

    if (ngx_process == NGX_PROCESS_MASTER) //master进程，管理进程，处理的信号一般会比较多
    {
        // master进程
        switch (sigNo)
        {
        case SIGCHLD: // 子进程退出
            ngx_reap = 1; // 标记子进程状态变化
            break;
        
        default:
            break;
        }
    } 
    else if (ngx_process == NGX_PROCESS_WORKER) // worker进程，具体干活的进程，处理的信号相对比较少
    {
        // ...
    }
    else
    {
        // ...
    }
 
    if (siginfo && siginfo->si_pid)  //si_pid = sending process ID【发送该信号的进程id】
    {
        ngx_log_error_core(NGX_LOG_NOTICE,0,"signal %d (%s) received from %P%s", sigNo, sig->sigName, siginfo->si_pid, action); 
    }
    else
    {
        ngx_log_error_core(NGX_LOG_NOTICE,0,"signal %d (%s) received %s", sigNo, sig->sigName, action);//没有发送该信号的进程id，所以不显示发送该信号的进程id
    }

    //子进程状态有变化，通常是意外退出
    if (sigNo == SIGCHLD)
    {
        ngx_process_get_status();
    }
    return ;
}

//获取子进程的结束状态，防止单独kill子进程时子进程变成僵尸进程
static void ngx_process_get_status(void)
{
    pid_t       pid;
    int         status;
    int         err;
    int         one = 0; // 标记信号正常处理过一次

    //当你杀死一个子进程时，父进程会收到这个SIGCHLD信号。
    for (;;)
    {
        pid = waitpid(-1, &status, WNOHANG);

        if (pid == 0) // 子进程没有结束
        {
            return ;
        }

        if (pid == -1)
        {
            err = errno;
            if (err == EINTR)  //调用被某个信号中断
            {
                continue;
            }

            if (err == ECHILD && one) // 没有子进程
            {
                return ;
            }

            if (err == ECHILD)
            {
                ngx_log_error_core(NGX_LOG_INFO,err,"waitpid() failed!");
                return;
            }
        }

        one = 1;
        if (WTERMSIG(status)) //获取使子进程终止的信号编号
        {
            ngx_log_error_core(NGX_LOG_ALERT,0,"pid = %P exited on signal %d!",pid,WTERMSIG(status)); //获取使子进程终止的信号编号
        }
        else
        {
            ngx_log_error_core(NGX_LOG_NOTICE,0,"pid = %P exited with code %d!",pid,WEXITSTATUS(status)); //WEXITSTATUS()获取子进程传递给exit或者_exit参数的低八位
        }
    }
    return ;
}