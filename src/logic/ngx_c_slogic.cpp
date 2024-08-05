//和网络以及逻辑处理 有关的函数放这里

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
#include <sys/socket.h>
#include <sys/ioctl.h> //ioctl
#include <arpa/inet.h>
#include <pthread.h>   //多线程

#include "ngx_c_conf.h"
#include "ngx_macro.h"
#include "ngx_global.h"
#include "ngx_func.h"
#include "ngx_c_socket.h"
#include "ngx_c_memory.h"
#include "ngx_c_crc32.h"
#include "ngx_c_slogic.h"  
#include "ngx_logiccomm.h"  
#include "ngx_c_lockmutex.h"  

//定义成员函数指针
typedef bool (CLogicSocket::*handler)(  lpngx_connection_t pConn,      //连接池中连接的指针
                                        LPSTRUC_MSG_HEADER pMsgHeader,  //消息头指针
                                        char *pPkgBody,                 //包体指针
                                        unsigned short iBodyLength);    //包体长度

// 用来保存 成员函数指针 的一个数组
static const handler statusHandler[] =
{
    //数组前5个元素，保留，以备将来增加一些基本服务器功能
    &CLogicSocket::_HandlePing,                             //【0】：心跳包的实现
    NULL,                                                   //【1】：下标从0开始
    NULL,                                                   //【2】：下标从0开始
    NULL,                                                   //【3】：下标从0开始
    NULL,                                                   //【4】：下标从0开始

    &CLogicSocket::_HandleRegister,                         //【5】：实现具体的注册功能
    &CLogicSocket::_HandleLogIn,                            //【6】：实现具体的登录功能
    //......其他待扩展，比如实现攻击功能，实现加血功能等等；
};

#define AUTH_TOTAL_COMMANDS sizeof(statusHandler)/sizeof(handler) //整个命令有多少个，编译时即可知道

// 构造函数
CLogicSocket::CLogicSocket()
{

}

// 析构函数
CLogicSocket::~CLogicSocket()
{

}

//初始化函数【fork()子进程之前干这个事】
//成功返回true，失败返回false
bool CLogicSocket::Initialize()
{
    bool bParentInit = CSocket::Initialize();
    return bParentInit;
}

//处理收到的数据包，由线程池来调用本函数，本函数是一个单独的线程；
//pMsgBuf：消息头 + 包头 + 包体 ：自解释；
void CLogicSocket::threadRecvProcFunc(char *pMsgBuf)
{
    LPSTRUC_MSG_HEADER pMsgHeader = (LPSTRUC_MSG_HEADER)pMsgBuf;                  //消息头
    LPCOMM_PKG_HEADER  pPkgHeader = (LPCOMM_PKG_HEADER)(pMsgBuf+m_iLenMsgHeader); //包头
    void  *pPkgBody;                                                              //指向包体的指针
    unsigned short pkglen = ntohs(pPkgHeader->pkgLen);                            //客户端指明的包宽度【包头+包体】

    if (m_iLenMsgHeader == pkglen)
    {
        // 没有包体，只有消息头
        if (pPkgHeader->crc32 != 0)
        {
            return ;
        }
        pPkgBody = NULL;
    }
    else 
    {
        // 有包体
        pPkgHeader->crc32 = ntohl(pPkgHeader->crc32);
        pPkgBody = (void *)(pMsgBuf + m_iLenMsgHeader + m_iLenPkgHeader); // 指向包体

        // 计算crc值判断包的完整性
        int calccrc = CCRC32::GetInstance()->Get_CRC((unsigned char *)pPkgBody, pkglen - m_iLenPkgHeader);
        if (calccrc != pPkgHeader->crc32)
        {
            ngx_log_stderr(0,"CLogicSocket::threadRecvProcFunc()中CRC错误[服务器:%d/客户端:%d]，丢弃数据!",calccrc,pPkgHeader->crc32);    //正式代码中可以干掉这个信息
            return ;
        }
        else 
        {
            //ngx_log_stderr(0,"CLogicSocket::threadRecvProcFunc()中CRC正确[服务器:%d/客户端:%d]，不错!",calccrc,pPkgHeader->crc32);
        }
    } 

    // 包校验OK
    unsigned short imsgCode = ntohs(pPkgHeader->msgCode); // 消息代码
    lpngx_connection_t p_Conn = pMsgHeader->pConn;

    // 我们要做一些判断
    //(1)如果从收到客户端发送来的包，到服务器释放一个线程池中的线程处理该包的过程中，客户端断开了，那显然，这种收到的包我们就不必处理了；    
    if(p_Conn->iCurrsequence != pMsgHeader->iCurrsequence)   //该连接池中连接以被其他tcp连接【其他socket】占用，这说明原来的 客户端和本服务器的连接断了，这种包直接丢弃不理
    {
        return; //丢弃不理这种包了【客户端断开了】
    }

    //(2)判断消息码是正确的，防止客户端恶意侵害我们服务器，发送一个不在我们服务器处理范围内的消息码
    if (imsgCode >= AUTH_TOTAL_COMMANDS)
    {
        ngx_log_stderr(0,"CLogicSocket::threadRecvProcFunc()中imsgCode=%d消息码不对!",imsgCode); //这种有恶意倾向或者错误倾向的包，希望打印出来看看是谁干的
        return; //丢弃不理这种包【恶意包或者错误包】
    }

    //能走到这里的，包没过期，不恶意，那好继续判断是否有相应的处理函数
    //(3)有对应的消息处理函数吗
    if (statusHandler[imsgCode] == NULL)
    {
        ngx_log_stderr(0,"CLogicSocket::threadRecvProcFunc()中imsgCode=%d消息码找不到对应的处理函数!",imsgCode); //这种有恶意倾向或者错误倾向的包，希望打印出来看看是谁干的
        return;  //没有相关的处理函数
    }

    //一切正确，可以放心大胆的处理了
    //(4)调用消息码对应的成员函数来处理
    (this->*statusHandler[imsgCode])(p_Conn, pMsgHeader, (char *)pPkgBody, pkglen - m_iLenPkgHeader);
    return ;
}

//心跳包检测时间到，该去检测心跳包是否超时的事宜，本函数是子类函数，实现具体的判断动作
void CLogicSocket::procPingTimeOutChecking(LPSTRUC_MSG_HEADER tmpmsg, time_t cur_time)
{
    CMemory *p_memory = CMemory::GetInstance();

    if (tmpmsg->iCurrsequence == tmpmsg->pConn->iCurrsequence) // 连接没断
    {
        lpngx_connection_t p_Conn = tmpmsg->pConn;

        if (m_ifTimeOutKick == 1)
        {
            // 到时间直接踢出去
            zdClosesocketProc(p_Conn);
        }
         //超时踢的判断标准就是 每次检查的时间间隔*3，超过这个时间没发送心跳包，就踢
        else if ( (cur_time - p_Conn->lastPingTime) > (m_iWaitTime * 3 + 10) )
        {
            //踢出去【如果此时此刻该用户正好断线，则这个socket可能立即被后续上来的连接复用  如果真有人这么倒霉，赶上这个点了，那么可能错踢，错踢就错踢】
            zdClosesocketProc(p_Conn);
        }

        p_memory->FreeMemory(tmpmsg);
    }
    else 
    {
        p_memory->FreeMemory(tmpmsg);
    }
    return ;
}

// 发送没有包体的数据包给客户端
void CLogicSocket::SendNoBodyPkgToClient(LPSTRUC_MSG_HEADER pMsgHeader, unsigned int iMsgCode)
{
    CMemory *p_memory = CMemory::GetInstance();

    char *p_sendbuf = (char *)p_memory->AllocMemory(m_iLenMsgHeader + m_iLenPkgHeader, false);
    char *p_tmpbuf = p_sendbuf;

    memcpy(p_tmpbuf, pMsgHeader, m_iLenMsgHeader);
    p_tmpbuf += m_iLenMsgHeader;

    LPCOMM_PKG_HEADER pPkgHeader = (LPCOMM_PKG_HEADER)p_tmpbuf; // 指向的是我要发送出去的包的包头	
    pPkgHeader->msgCode = htons(iMsgCode);
    pPkgHeader->pkgLen  = htons(m_iLenPkgHeader);
    pPkgHeader->crc32   = 0;
    msgSend(p_sendbuf);
    return ;
}

//----------------------------------------------------------------------------------------------------------
//处理各种业务逻辑
bool CLogicSocket::_HandleRegister(lpngx_connection_t pConn, LPSTRUC_MSG_HEADER pMsgHeader, char *pPkgBody, unsigned short iBodyLength)
{
    //(1)首先判断包体的合法性
    if (pPkgBody == NULL)
    {
        return false;
    }

    int iRecvLen = sizeof(STRUCT_REGISTER);
    if (iRecvLen != iBodyLength) // 发送来的结构大小不对
    {
        return false;
    }
    //(2)对于同一个用户，可能同时发送来多个请求过来，造成多个线程同时为该 用户服务，比如以网游为例，用户要在商店中买A物品，又买B物品，而用户的钱 只够买A或者B，不够同时买A和B呢？
    //那如果用户发送购买命令过来买了一次A，又买了一次B，如果是两个线程来执行同一个用户的这两次不同的购买命令，很可能造成这个用户购买成功了 A，又购买成功了 B
    //所以，为了稳妥起见，针对某个用户的命令，我们一般都要互斥,我们需要增加临界的变量于ngx_connection_s结构中
    CLock lock(&pConn->logicPorcMutex);

    //(3)取得了整个发送过来的数据
    LPSTRUCT_REGISTER p_RecvInfo = (LPSTRUCT_REGISTER)pPkgBody;
    p_RecvInfo->iType = ntohl(p_RecvInfo->iType);
    p_RecvInfo->username[sizeof(p_RecvInfo->username) - 1] = 0;//这非常关键，防止客户端发送过来畸形包，导致服务器直接使用这个数据出现错误。
    p_RecvInfo->password[sizeof(p_RecvInfo->password) - 1] = 0;//这非常关键，防止客户端发送过来畸形包，导致服务器直接使用这个数据出现错误。

    //(4)这里可能要考虑 根据业务逻辑，进一步判断收到的数据的合法性

    //(5)给客户端返回数据时，一般也是返回一个结构，这个结构内容具体由客户端/服务器协商
    LPCOMM_PKG_HEADER pPkgHeader;
    CMemory *p_memory = CMemory::GetInstance();
    CCRC32  *p_crc32 = CCRC32::GetInstance();
    int iSendLen = sizeof(STRUCT_REGISTER);

    // a)分配要发送出去的包的内存
    char *p_sendbuf = (char *)p_memory->AllocMemory(m_iLenMsgHeader + m_iLenPkgHeader + iSendLen, false);
    // b)填充消息头
    memcpy(p_sendbuf, pMsgHeader, m_iLenMsgHeader);
    // c)填充包头
    pPkgHeader = (LPCOMM_PKG_HEADER)(p_sendbuf + m_iLenMsgHeader);
    pPkgHeader->msgCode = _CMD_REGISTER;
    pPkgHeader->msgCode = htons(pPkgHeader->msgCode);
    pPkgHeader->pkgLen  = htons(m_iLenPkgHeader + iSendLen);
    // d)填充包体
    LPSTRUCT_REGISTER p_sendInfo = (LPSTRUCT_REGISTER)(p_sendbuf + m_iLenMsgHeader + m_iLenPkgHeader);
    //。。。。。这里根据需要，填充要发回给客户端的内容,int类型要使用htonl()转，short类型要使用htons()转；

    // e)包体内容全部确定好后，计算包体的crc32值
    pPkgHeader->crc32 = p_crc32->Get_CRC((unsigned char *)p_sendInfo, iSendLen);
    pPkgHeader->crc32 = htonl(pPkgHeader->crc32);

    // f)发送数据包
    msgSend(p_sendbuf);

    return true;
}

bool CLogicSocket::_HandleLogIn(lpngx_connection_t pConn,LPSTRUC_MSG_HEADER pMsgHeader,char *pPkgBody,unsigned short iBodyLength)
{
    if (pPkgBody == NULL)
    {
        return false;
    }
    int iRecvLen = sizeof(STRUCT_LOGIN);
    if (iRecvLen != iBodyLength)
    {
        return false;
    }
    CLock lock(&pConn->logicPorcMutex);

    LPSTRUCT_LOGIN p_RecvInfo = (LPSTRUCT_LOGIN)pPkgBody;
    p_RecvInfo->username[sizeof(p_RecvInfo->username) - 1] = 0;
    p_RecvInfo->password[sizeof(p_RecvInfo->password) - 1] = 0;

    LPCOMM_PKG_HEADER pPkgHeader;
    CMemory *p_memory = CMemory::GetInstance();
    CCRC32  *p_crc32  = CCRC32::GetInstance();

    int iSendLen = sizeof(STRUCT_LOGIN);
    char *p_sendbuf = (char *)p_memory->AllocMemory(m_iLenMsgHeader + m_iLenPkgHeader + iSendLen, false);
    memcpy(p_sendbuf, pMsgHeader, m_iLenMsgHeader);
    pPkgHeader = (LPCOMM_PKG_HEADER)(p_sendbuf + m_iLenMsgHeader);
    pPkgHeader->msgCode = _CMD_LOGIN;
    pPkgHeader->msgCode = htons(pPkgHeader->msgCode);
    pPkgHeader->pkgLen  = htons(m_iLenPkgHeader + iSendLen);
    LPSTRUCT_LOGIN p_sendInfo = (LPSTRUCT_LOGIN)(p_sendbuf + m_iLenPkgHeader + m_iLenMsgHeader);

    pPkgHeader->crc32   = p_crc32->Get_CRC((unsigned char *)p_sendInfo, iSendLen);
    pPkgHeader->crc32   = htonl(pPkgHeader->crc32);

    msgSend(p_sendbuf);
    return true;
}

//接收并处理客户端发送过来的ping包
bool CLogicSocket::_HandlePing(lpngx_connection_t pConn,LPSTRUC_MSG_HEADER pMsgHeader,char *pPkgBody,unsigned short iBodyLength)
{
    // 心跳包要求没有包体
    if (iBodyLength != 0)
    {
        return false;
    }

    CLock lock(&pConn->logicPorcMutex);
    pConn->lastPingTime = time(NULL);

    SendNoBodyPkgToClient(pMsgHeader, _CMD_PING);

    return true;
}