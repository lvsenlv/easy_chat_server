/*************************************************************************
	> File Name: message.c
	> Author: 
	> Mail: 
	> Created Time: 2018年01月31日 星期三 16时35分24秒
 ************************************************************************/

#ifdef __SERVER
#include "server.h"
#else
#include "message.h"
#endif
#include "log.h"
#include "crc.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>
#include <string.h>

#if ((MSG_DATA_OFFSET_ADD_PASSWORD+PASSWORD_MAX_LENGTH) > MSG_DATA_MAX_LENGTH)
#error "Invalid macro: MSG_DATA_OFFSET related over range"
#endif

static G_STATUS MSG_CreateMsgQueue(void);
static G_STATUS MSG_InitGlobalVariables(void);
static G_STATUS MSG_ProcessMsg(MsgPkt_t *pMsgPkt);
static G_STATUS MSG_SendToUser(MsgPkt_t *pMsgPkt);
static G_STATUS MSG_SendResponse(MsgPkt_t *pMsgPkt);

#ifdef __DEBUG
char *g_pCmdDetail[MSG_CMD_MAX];
#endif
int g_MsgQueue;
char g_MsgTaskLiveFlag;

G_STATUS MSG_CreateTask(pthread_t *pMsgTaskID)
{
    int res;
    pthread_t MsgTaskID;
    pthread_attr_t MsgTaskAttr;
    
    if(STAT_OK != MSG_BeforeCreateTask())
        return STAT_ERR;
    
    res = pthread_attr_init(&MsgTaskAttr);
    if(0 != res)
    {
        LOG_FATAL_ERROR("[MSG create task] Fail to init thread attribute\n");
        return STAT_ERR;
    }
    
    res = pthread_attr_setdetachstate(&MsgTaskAttr, PTHREAD_CREATE_DETACHED);
    if(0 != res)
    {
        LOG_FATAL_ERROR("[MSG create task] Fail to set thread attribute\n");
        return STAT_ERR;
    }

    res = pthread_create(&MsgTaskID, &MsgTaskAttr, MSG_MessageTask, NULL);
    if(0 != res)
    {
        LOG_FATAL_ERROR("[MSG create task] Fail to create task\n");
        return STAT_ERR;
    }
    
    pthread_attr_destroy(&MsgTaskAttr);
    *pMsgTaskID = MsgTaskID;

    return STAT_OK;
}

void *MSG_MessageTask(void *pArg)
{
    struct timeval TimeInterval;
    fd_set ReadFds;
    int res;
    int ReadDataLength;
    MsgPkt_t MsgPkt;

    TimeInterval.tv_usec = 0;
    
    while(1)
    {
        g_MsgTaskLiveFlag = 1;
        //LOG_INFO("[MSG msg handle task] Activate msg task\n");
        
        FD_ZERO(&ReadFds);
        FD_SET(g_MsgQueue, &ReadFds);
        TimeInterval.tv_sec = MSG_SELECT_TIME_INTERVAL;
        
        res = select(g_MsgQueue+1, &ReadFds, NULL, NULL, &TimeInterval);
        if(0 > res)
        {
            LOG_ERROR("[Msg task] Select function return negative value\n");
            continue;
        }
        else if(0 == res)
            continue;
            
        if(!(FD_ISSET(g_MsgQueue, &ReadFds)))
            continue;

        ReadDataLength = read(g_MsgQueue, &MsgPkt, sizeof(MsgPkt_t));
        if(sizeof(MsgPkt_t) != ReadDataLength)
        {
            LOG_ERROR("[Msg task] read(): %s\n", strerror(errno));
            continue;
        }

        MSG_ProcessMsg(&MsgPkt);
    }

    return NULL;
}





#define COMMON_FUNC //Only use for locating function efficiently
//Common function
//>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
G_STATUS MSG_BeforeCreateTask(void)
{
    if(STAT_OK != MSG_InitGlobalVariables())
        return STAT_ERR;
        
    if(STAT_OK != MSG_CreateMsgQueue())
        return STAT_ERR;

#ifdef __CRC8
    CRC8_InitTable();
#endif

#ifdef __CRC16
    CRC16_InitTable();
#endif

#ifdef __CRC32
    CRC32_InitTable();
#endif

    return STAT_OK;
}

void MSG_InitMsgPkt(MsgPkt_t *pMsgPkt)
{
    pMsgPkt->cmd = MSG_CMD_DO_NOTHING;
    pMsgPkt->fd = -1;
    memset(pMsgPkt->data, 0, sizeof(pMsgPkt->data));
    pMsgPkt->CCFlag = 0;
    pMsgPkt->CheckCode = 0;
}

G_STATUS MSG_LockMsgQueue(int fd)
{
    struct flock lock;

    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start = 0;
    lock.l_len = 0;
    lock.l_pid = (pid_t)0;
    
    if(-1 == fcntl(fd, F_SETLKW, &lock))
        return STAT_ERR;

    return STAT_OK;
}

G_STATUS MSG_UnlockMsgQueue(int fd)
{
    struct flock lock;

    lock.l_type = F_UNLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start = 0;
    lock.l_len = 0;
    lock.l_pid = (pid_t)0;
    
    if(-1 == fcntl(fd, F_SETLK, &lock))
        return STAT_ERR;

    return STAT_OK;
}

G_STATUS MSG_PostMsg(MsgPkt_t *pMsgPkt)
{
    if(STAT_OK != MSG_LockMsgQueue(g_MsgQueue))
    {
        LOG_ERROR("[Post msg] Fail to lock msg queue\n");
        return STAT_ERR;
    }

    int WriteDataLength;
    WriteDataLength = write(g_MsgQueue, pMsgPkt, sizeof(MsgPkt_t));
    if(sizeof(MsgPkt_t) != WriteDataLength)
    {
        LOG_ERROR("[Post msg] write(): %s\n", strerror(errno));
    
        MSG_UnlockMsgQueue(g_MsgQueue);
        return STAT_ERR;
    }

    if(STAT_OK != MSG_UnlockMsgQueue(g_MsgQueue))
    {
        LOG_ERROR("[Post msg] Fail to unlock msg queue\n");
        return STAT_ERR;
    }

    LOG_DEBUG("[Post msg] Success, cmd: %s\n", g_pCmdDetail[pMsgPkt->cmd]);
    
    return STAT_OK;
}

G_STATUS MSG_PostMsgNoLock(MsgPkt_t *pMsgPkt)
{
    int WriteDataLength;
    
    WriteDataLength = write(g_MsgQueue, pMsgPkt, sizeof(MsgPkt_t));
    if(sizeof(MsgPkt_t) != WriteDataLength)
    {
        LOG_ERROR("[Post msg] write(): %s\n", strerror(errno));
        return STAT_ERR;
    }

    LOG_DEBUG("[Post msg no lock] Success, cmd: %s\n", g_pCmdDetail[pMsgPkt->cmd]);
    
    return STAT_OK;
}

G_STATUS MSG_GetResponse(MsgPkt_t *pMsgPkt, int timeout)
{
    if(0 > timeout)
        return STAT_ERR;

    if(0 > pMsgPkt->fd)
        return STAT_ERR;
    
    struct timeval TimeInterval;
    int fd;
    fd_set fds;
    int res;
    int ReadDataLength;

    TimeInterval.tv_usec = 0;
    fd = pMsgPkt->fd;

    do
    {
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        TimeInterval.tv_sec = timeout;

        res = select(fd+1, &fds, NULL, NULL, &TimeInterval);
        if(0 > res)
        {
            LOG_ERROR("[MSG get response] Select function return a negative value\n");
            break;
        }

        if(0 == res)
            return STAT_ERR;
            
        if(!(FD_ISSET(fd, &fds)))
            return STAT_ERR;

        ReadDataLength = read(fd, pMsgPkt, sizeof(MsgPkt_t));
        if(sizeof(MsgPkt_t) != ReadDataLength)
        {
            LOG_ERROR("[MSG get response] read(): %s\n", strerror(errno));
            return STAT_ERR;
        }

        break;
    }while(0);
    
    return STAT_OK;
}
//<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
//Common function





#define STATIC_FUNC //Only use for locating function efficiently
//Static function
//>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>

static G_STATUS MSG_CreateMsgQueue(void)
{
    if(0 == access(MSG_QUEUE_NAME, F_OK))
    {
        if(0 != unlink(MSG_QUEUE_NAME))
        {
            LOG_FATAL_ERROR("[MSG create queue] unlink(): %s", strerror(errno));
            return STAT_FATAL_ERR;
        }
    }
    
    if(0 != mkfifo(MSG_QUEUE_NAME, 0600))
    {
        LOG_FATAL_ERROR("[MSG create queue] mkfifo(): %s", strerror(errno));
        return STAT_FATAL_ERR;
    }
    
    g_MsgQueue = open(MSG_QUEUE_NAME, O_RDWR); //It would be block if not use O_RDWR
    if(0 > g_MsgQueue)
    {
        LOG_FATAL_ERROR("[MSG create queue] open(): %s", strerror(errno));
        return STAT_FATAL_ERR;
    }
    
    return STAT_OK;
}

static G_STATUS MSG_InitGlobalVariables(void)
{
    g_MsgTaskLiveFlag = 0;

#ifdef __DEBUG
    int i;

    for(i = 0; i < MSG_CMD_MAX; i++)
    {
        g_pCmdDetail[i] = "Unspecified";
    }
    
    g_pCmdDetail[MSG_CMD_SEND_TO_USER]              = "send to user";
    g_pCmdDetail[MSG_CMD_SEND_RES]                  = "Send response";
    g_pCmdDetail[MSG_CMD_ROOT_LOGIN]                = "root login";
    g_pCmdDetail[MSG_CMD_ROOT_ADD_ADMIN]            = "add admin";
    g_pCmdDetail[MSG_CMD_ROOT_DEL_ADMIN]            = "del admin";
    g_pCmdDetail[MSG_CMD_ROOT_RENAME_ADMIN]         = "rename admin";
    g_pCmdDetail[MSG_CMD_ROOT_CLEAR_LOG]            = "clear log";
    g_pCmdDetail[MSG_CMD_ROOT_DOWNLOAD_LOG]         = "download log";
    g_pCmdDetail[MSG_CMD_ADMIN_ADD_USER]            = "add user";
    g_pCmdDetail[MSG_CMD_ADMIN_DEL_USER]            = "del user";
    g_pCmdDetail[MSG_CMD_USER_LOGIN]                = "user login";
    g_pCmdDetail[MSG_CMD_USER_LOGOUT]               = "user logout";
    g_pCmdDetail[MSG_CMD_CHECK_ALL_USER_STATUS]     = "check status";
    g_pCmdDetail[MSG_CMD_DO_NOTHING]                = "do nothing";
#endif
    
    return STAT_OK;
}

static G_STATUS MSG_ProcessMsg(MsgPkt_t *pMsgPkt)
{
    switch(pMsgPkt->cmd)
    {
        case MSG_CMD_SEND_TO_USER:
            MSG_SendToUser(pMsgPkt);
            break;
        case MSG_CMD_SEND_RES:
            MSG_SendResponse(pMsgPkt);
            break;
#ifdef __SERVER
        case MSG_CMD_USER_LOGIN:
            SERVER_UserLogin(pMsgPkt);
            break;
        case MSG_CMD_USER_LOGOUT:
            SERVER_UserLogout(pMsgPkt);
            break;
        case MSG_CMD_CHECK_ALL_USER_STATUS:
            SERVER_CheckAllUserStatus(pMsgPkt);
            break;
        case MSG_CMD_ADMIN_ADD_USER:
            SERVER_ADMIN_AddUser(pMsgPkt);
            break;
        case MSG_CMD_ADMIN_DEL_USER:
            SERVER_ADMIN_DelUser(pMsgPkt);
            break;
        case MSG_CMD_ROOT_LOGIN:
            SERVER_ROOT_UserLogin(pMsgPkt);
            break;
        case MSG_CMD_ROOT_ADD_ADMIN:
            SERVER_ROOT_AddAdmin(pMsgPkt);
            break;
        case MSG_CMD_ROOT_DEL_ADMIN:
            SERVER_ROOT_DelAdmin(pMsgPkt);
            break;
        case MSG_CMD_ROOT_RENAME_ADMIN:
            SERVER_ROOT_RenameAdmin(pMsgPkt);
            break;
        case MSG_CMD_ROOT_CLEAR_LOG:
            SERVER_ROOT_ClearLog(pMsgPkt);
            break;
        case MSG_CMD_ROOT_DOWNLOAD_LOG:
            SERVER_ROOT_DownloadLog(pMsgPkt);
            break;
#endif
        default:
            LOG_DEBUG("[MSG process msg] Unspecified command\n");
            break;
    }
    
    return STAT_OK;
}

static G_STATUS MSG_SendToUser(MsgPkt_t *pMsgPkt)
{
    if(0 > pMsgPkt->fd)
        return STAT_ERR;

    int WriteDataLength;

    WriteDataLength = write(pMsgPkt->fd, (char *)pMsgPkt, sizeof(MsgPkt_t));
    if(sizeof(MsgPkt_t) != WriteDataLength)
        return STAT_ERR;
    
    return STAT_OK;
}

static G_STATUS MSG_SendResponse(MsgPkt_t *pMsgPkt)
{
    if(0 > pMsgPkt->fd)
        return STAT_ERR;

    int WriteDataLength;

    WriteDataLength = write(pMsgPkt->fd, (char *)pMsgPkt, sizeof(MsgPkt_t));
    if(sizeof(MsgPkt_t) != WriteDataLength)
        return STAT_ERR;
    
    return STAT_OK;
}
//<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
//Static function