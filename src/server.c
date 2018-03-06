/*************************************************************************
	> File Name: server.c
	> Author: 
	> Mail: 
	> Created Time: 2018年01月30日 星期二 08时39分23秒
 ************************************************************************/

#include "server.h"
#include "log.h"
#include "completion_code.h"
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/param.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/time.h>
#include <errno.h>

static int SERVER_ConfigServer(struct sockaddr_in *pServerSocketAddr);
static G_STATUS SERVER_InitServerFile(void);
static G_STATUS SERVER_InitGlobalVaribles(void);
static G_STATUS SERVER_CreateUserID(uint64_t *pUserID);
static G_STATUS SERVER_CreateSession(int fd);

volatile char g_ServerLiveFlag;         //1 mean server task is alive
int g_ServerSocketFd;                   //It would be initialized in server task

UserList_t g_administrator;
UserList_t *g_pUserListTail;
pthread_mutex_t g_UserListLock = PTHREAD_MUTEX_INITIALIZER;

G_STATUS SERVER_CreateTask(pthread_t *pServerTaskID)
{
    int res;
    pthread_t ServerTaskID;
    pthread_attr_t ServerTaskAttr;
    
    if(STAT_OK != SERVER_BeforeCreateTask())
        return STAT_ERR;
    
    res = pthread_attr_init(&ServerTaskAttr);
    if(0 != res)
    {
        LOG_FATAL_ERROR("[SERVER create task] Fail to init thread attribute\n");
        return STAT_ERR;
    }
    
    res = pthread_attr_setdetachstate(&ServerTaskAttr, PTHREAD_CREATE_DETACHED);
    if(0 != res)
    {
        LOG_FATAL_ERROR("[SERVER create task] Fail to set thread attribute\n");
        return STAT_ERR;
    }

    res = pthread_create(&ServerTaskID, &ServerTaskAttr, SERVER_ServerTask, NULL);
    if(0 != res)
    {
        LOG_FATAL_ERROR("[SERVER create task] Fail to create task\n");
        return STAT_ERR;
    }
    
    pthread_attr_destroy(&ServerTaskAttr);
    *pServerTaskID = ServerTaskID;

    return STAT_OK;
}


void *SERVER_ServerTask(void *pArg)
{
    int ServerSocketFd;
    struct sockaddr_in ServerSocketAddr;
    struct timeval TimeInterval;
    fd_set fds;
    fd_set PrevFds;
    int MaxFd;
    int res;
    socklen_t ClientSocketAddrLen;
    int ClientSocketFd;
    struct sockaddr_in ClientSocketAddr;
    int ReadDataLength;
    MsgPkt_t MsgPkt;
    UserList_t *pPrevUser;
    UserList_t *pCurUser;
    UserList_t *pTmpUser;
    int TimeCount;
    int TmpFd;

    ServerSocketFd = SERVER_ConfigServer(&ServerSocketAddr);
    if(0 > ServerSocketFd)
        return NULL;

    MSG_InitMsgPkt(&MsgPkt);
    g_ServerSocketFd = ServerSocketFd;
    TimeInterval.tv_usec = 0;
    MaxFd = ServerSocketFd;
    FD_ZERO(&PrevFds);
    FD_SET(ServerSocketFd, &PrevFds);
    TimeCount = 0;
 
    while(1)
    {
        g_ServerLiveFlag = 1;
        fds = PrevFds;
        TimeInterval.tv_sec = SERVER_SELECT_TIME_INTERVAL;

        res = select(MaxFd+1, &fds, NULL, NULL, &TimeInterval);
        if(0 > res)
        {
            LOG_ERROR("[SERVER thread] "
                "Select function return a negative value\n");
            continue;
        }

        if(res == 0) //Timeout
        {
            TimeCount += SERVER_SELECT_TIME_INTERVAL;
            if(SERVER_CHECK_USER_STATUS_INTERVAL <= TimeCount)
            {
                TimeCount = 0;
                MsgPkt.cmd = MSG_CMD_CHECK_ALL_USER_STATUS;
                MsgPkt.fd = -1;
                MsgPkt.CCFlag = 0;
                MSG_PostMsg(&MsgPkt);
            }
            
            continue;
        }
        
        if(FD_ISSET(ServerSocketFd, &fds)) //New connection
        {
            ClientSocketAddrLen = sizeof(struct sockaddr_in);
            ClientSocketFd = accept(ServerSocketFd, (struct sockaddr*)&ClientSocketAddr, 
                &ClientSocketAddrLen);
            if(0 > ClientSocketFd)
            {
                LOG_WARNING("[Server thread] accept(): %s\n", strerror(errno));
                continue;
            }

            SERVER_CreateSession(ClientSocketFd);
            FD_SET(ClientSocketFd, &PrevFds);
            if(MaxFd < ClientSocketFd)
            {
                MaxFd = ClientSocketFd;
            }

            MsgPkt.cmd = MSG_CMD_SEND_TO_USER;
            MsgPkt.fd = ClientSocketFd;
            MsgPkt.CCFlag = 0;
            MSG_Set32BitData(&MsgPkt, MSG_DATA_OFFSET_CC, CC_NORMAL);
            MSG_PostMsg(&MsgPkt);
            
            continue;
        }

        pthread_mutex_lock(&g_UserListLock); //Pay attention to unlock
        pPrevUser = &g_administrator;
        pCurUser = g_administrator.pNext;
        while(1)
        {
            if(NULL == pCurUser)
            {
                pthread_mutex_unlock(&g_UserListLock);
                break;
            }
        
            if(!FD_ISSET(pCurUser->fd, &fds))
            {
                pPrevUser = pCurUser;
                pCurUser = pCurUser->pNext;
                continue;
            }

            ReadDataLength = read(pCurUser->fd, (char *)&MsgPkt, sizeof(MsgPkt_t));
            if(0 > ReadDataLength)
            {
                pthread_mutex_unlock(&g_UserListLock);
                break;
            }
            
            if(0 == ReadDataLength)
            {
                FD_CLR(pCurUser->fd, &PrevFds);
                if(g_pUserListTail == pCurUser)
                {
                    g_pUserListTail = pPrevUser;
                }
                
                pPrevUser->pNext = pCurUser->pNext;
                TmpFd = pCurUser->fd;
                LOG_INFO("[SERVER thread][%s] Abnormal disconnect\n", pCurUser->UserName);
                close(pCurUser->fd);
                free(pCurUser);
                pCurUser = pPrevUser->pNext;
                g_administrator.fd--;
                
                if(MaxFd == TmpFd) //If it needs to get new max fd value
                {
                    MaxFd = ServerSocketFd;
                    pTmpUser = g_administrator.pNext;
                    while(NULL != pTmpUser)
                    {
                        if(MaxFd < pTmpUser->fd)
                        {
                            MaxFd = pTmpUser->fd;
                        }
                                        
                        pTmpUser = pTmpUser->pNext;
                    }
                }
                
                continue;
            }
            
            pthread_mutex_unlock(&g_UserListLock);

            LOG_INFO("[SERVER thread] Get the message form fd=%d\n", pCurUser->fd);

            if(sizeof(MsgPkt_t) == ReadDataLength)
            {
                MSG_PostMsg(&MsgPkt);
            }
            
            break;
        }

        if(SERVER_SELECT_TIME_INTERVAL == TimeInterval.tv_sec)
        {
            TimeCount++;
        }
        else
        {
            TimeCount += SERVER_SELECT_TIME_INTERVAL - TimeInterval.tv_sec;
        }
        
        if(SERVER_CHECK_USER_STATUS_INTERVAL <= TimeCount)
        {
            TimeCount = 0;
            MsgPkt.cmd = MSG_CMD_CHECK_ALL_USER_STATUS;
            MsgPkt.fd = -1;
            MsgPkt.CCFlag = 0;
            MSG_PostMsg(&MsgPkt);
        }
    }
    
    return NULL;
}





#define ROOT_LEVEL_FUNC_START //Only use for locating function efficiently
//Root level function
//>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>

/*
 *  @Briefs: Verify the identity of root
 *  @Return: STAT_OK / STAT_ERR
 *  @Note:   None
 */
static G_STATUS SERVER_ROOT_VerifyIdentity(MsgPkt_t *pMsgPkt)
{
    uint64_t UserID;
    char UserName[USER_NAME_MAX_LENGTH];
    char password[PASSWORD_MAX_LENGTH];
    
    MSG_GetStringData(pMsgPkt, MSG_DATA_OFFSET_USER_NAME, UserName, USER_NAME_MAX_LENGTH);
    if(0 != strcmp("lvsenlv", UserName))
    {
        LOG_WARNING("[%s][Verify root identity] "
            "Invalid user name: it needs root user name\n", UserName);
        return STAT_ERR;
    }
    
    UserID = pMsgPkt->UserID;
    if(((0x1<<12) | (0x3<<8) | (0x1<<4) | (0x4<<0)) != UserID)
    {
        LOG_WARNING("[%s - 0x%lx][Verify root identity] "
            "Invalid user id: it needs root user id\n", UserName, UserID);
        return STAT_ERR;
    }
    
    MSG_GetStringData(pMsgPkt, MSG_DATA_OFFSET_PASSWORD, password, PASSWORD_MAX_LENGTH);
    if(0 != strcmp("linuxroot", password))
    {
        LOG_WARNING("[%s][Verify root identity] "
            "Invalid password: it needs root password\n", UserName);
        return STAT_ERR;
    }

    return STAT_OK;
}

G_STATUS SERVER_ROOT_AddAdmin(MsgPkt_t *pMsgPkt)
{
    MsgPkt_t MsgPkt;
    int fd;
    char UserName[USER_NAME_MAX_LENGTH];
    char password[PASSWORD_MAX_LENGTH];
    char SmallBuf[SMALL_BUF_SIZE];
    uint64_t UserID;
    int WriteDataLength;
    int length;

    MSG_GetStringData(pMsgPkt, MSG_DATA_OFFSET_ADD_USER_NAME, UserName, sizeof(UserName));

    if(0 != pMsgPkt->CCFlag)
    {
        fd = pMsgPkt->fd;
        if(0 > fd)
        {
            LOG_WARNING("[root][Add admin][%s] "
                "Invalid fd value, actual value: %d\n", UserName, fd);
            return STAT_ERR;
        }
        
        MsgPkt.cmd = MSG_CMD_SEND_TO_USER;
        MsgPkt.fd = fd;
        MsgPkt.CCFlag = 0;
        MSG_Set32BitData(&MsgPkt, MSG_DATA_OFFSET_CC, CC_NORMAL);
    }
    
    if(STAT_OK != SERVER_ROOT_VerifyIdentity(pMsgPkt))
    {
        if(0 != pMsgPkt->CCFlag)
        {
            MSG_Set32BitData(&MsgPkt, MSG_DATA_OFFSET_CC, CC_PERMISSION_DENIED);
            MSG_PostMsg(&MsgPkt);
        }
        
        return STAT_ERR;
    }

    snprintf(SmallBuf, sizeof(SmallBuf), "%s/%s", SERVER_USER_LIST_DIR, UserName);
    if(0 == access(SmallBuf, F_OK))
    {
        LOG_WARNING("[root][Add admin][%s] "
            "User has been exist\n", UserName);
        
        if(0 != pMsgPkt->CCFlag)
        {
            MSG_Set32BitData(&MsgPkt, MSG_DATA_OFFSET_CC, CC_USER_HAS_BEEN_EXIST);
            MSG_PostMsg(&MsgPkt);
        }
        
        return STAT_ERR;
    }
    
    if(STAT_OK != SERVER_CreateUserID(&UserID))
    {
        LOG_ERROR("[root][Add admin][%s] "
            "Fail to create user id\n", UserName);
        
        if(0 != pMsgPkt->CCFlag)
        {
            MSG_Set32BitData(&MsgPkt, MSG_DATA_OFFSET_CC, CC_FAIL_TO_OPEN);
            MSG_PostMsg(&MsgPkt);
        }
        
        return STAT_ERR;
    }
    UserID |= (uint64_t)0x1 << 63; //Set as admin permission
    
    fd = open(SmallBuf, O_CREAT | O_WRONLY, 0600);
    if(0 > fd)
    {
        LOG_ERROR("[root][Add admin][%s] "
            "Fail to open user list file: %s\n", UserName, SmallBuf);
        
        if(0 != pMsgPkt->CCFlag)
        {
            MSG_Set32BitData(&MsgPkt, MSG_DATA_OFFSET_CC, CC_FAIL_TO_OPEN);
            MSG_PostMsg(&MsgPkt);
        }
        
        return STAT_ERR;
    }

    WriteDataLength = write(fd, &UserID, sizeof(uint64_t));
    if(sizeof(uint64_t) != WriteDataLength)
    {
        close(fd);
        LOG_ERROR("[root][Add admin][%s] "
            "Fail to write to user list file: %s\n", UserName, SmallBuf);
        
        if(0 != pMsgPkt->CCFlag)
        {
            MSG_Set32BitData(&MsgPkt, MSG_DATA_OFFSET_CC, CC_FAIL_TO_WRITE);
            MSG_PostMsg(&MsgPkt);
        }
        
        return STAT_ERR;
    }
    
    MSG_GetStringData(pMsgPkt, MSG_DATA_OFFSET_ADD_PASSWORD, password, PASSWORD_MAX_LENGTH);
    length = strlen(password);
    if(PASSWORD_MIN_LENGTH > length)
    {
        close(fd);
        LOG_WARNING("[root][Add user][%s] "
            "Password is too short\n", UserName);
        if(0 != pMsgPkt->CCFlag)
        {
            MSG_Set32BitData(&MsgPkt, MSG_DATA_OFFSET_CC, CC_PASSWORD_IS_TOO_SHORT);
            MSG_PostMsg(&MsgPkt);
        }
    }
    
    WriteDataLength = write(fd, password, length);
    if(length != WriteDataLength)
    {
        close(fd);
        LOG_ERROR("[root][Add admin][%s] "
            "Fail to write to user list file: %s\n", UserName, SmallBuf);
        
        if(0 != pMsgPkt->CCFlag)
        {
            MSG_Set32BitData(&MsgPkt, MSG_DATA_OFFSET_CC, CC_FAIL_TO_WRITE);
            MSG_PostMsg(&MsgPkt);
        }
        
        return STAT_ERR;
    }

    close(fd);

    if(0 != pMsgPkt->CCFlag)
    {
        MSG_PostMsg(&MsgPkt);
    }

    LOG_INFO("[root][Add admin][%s] "
        "Success adding admin, user id: 0x%lx\n", UserName, UserID);

    return STAT_OK;
}

G_STATUS SERVER_ROOT_DelAdmin(MsgPkt_t *pMsgPkt)
{
    MsgPkt_t MsgPkt;
    int fd;
    char UserName[USER_NAME_MAX_LENGTH];
    char SmallBuf[SMALL_BUF_SIZE];
//    uint64_t UserID;
//    int WriteDataLength;
//    int length;

    MSG_GetStringData(pMsgPkt, MSG_DATA_OFFSET_DEL_USER_NAME, UserName, sizeof(UserName));

    if(0 != pMsgPkt->CCFlag)
    {
        fd = pMsgPkt->fd;
        if(0 > fd)
        {
            LOG_WARNING("[root][Del admin][%s] "
                "Invalid fd value, actual value: %d\n", UserName, fd);
            return STAT_ERR;
        }
        
        MsgPkt.cmd = MSG_CMD_SEND_TO_USER;
        MsgPkt.fd = fd;
        MsgPkt.CCFlag = 0;
        MSG_Set32BitData(&MsgPkt, MSG_DATA_OFFSET_CC, CC_NORMAL);
    }
    
    if(STAT_OK != SERVER_ROOT_VerifyIdentity(pMsgPkt))
    {
        if(0 != pMsgPkt->CCFlag)
        {
            MSG_Set32BitData(&MsgPkt, MSG_DATA_OFFSET_CC, CC_PERMISSION_DENIED);
            MSG_PostMsg(&MsgPkt);
        }
        
        return STAT_ERR;
    }
    
    snprintf(SmallBuf, sizeof(SmallBuf), "%s/%s", SERVER_USER_LIST_DIR, UserName);
    if(0 != access(SmallBuf, F_OK))
    {
        LOG_WARNING("[root][Del admin][%s] "
            "User does not exist\n", UserName);
        
        if(0 != pMsgPkt->CCFlag)
        {
            MSG_Set32BitData(&MsgPkt, MSG_DATA_OFFSET_CC, CC_USER_DOES_NOT_EXIST);
            MSG_PostMsg(&MsgPkt);
        }
        
        return STAT_ERR;
    }
    
    if(0 != unlink(SmallBuf))
    {
        LOG_WARNING("[root][Del admin][%s] "
            "Fail to delete user list file\n", UserName);
        
        if(0 != pMsgPkt->CCFlag)
        {
            MSG_Set32BitData(&MsgPkt, MSG_DATA_OFFSET_CC, CC_FAIL_TO_UNLINK);
            MSG_PostMsg(&MsgPkt);
        }
        
        return STAT_ERR;
    }
    
    if(0 != pMsgPkt->CCFlag)
    {
        MSG_PostMsg(&MsgPkt);
    }

    LOG_INFO("[root][Del admin][%s] "
        "Success deleting admin\n", UserName);

    return STAT_OK;
}
//<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
//Root level function





#define ADMIN_LEVEL_FUNC_START //Only use for locating function efficiently
//Admin level function
//>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>

/*
 *  @Briefs: Verify the identity of administrator
 *  @Return: STAT_OK / STAT_ERR
 *  @Note:   None
 */
static G_STATUS SERVER_ADMIN_VerifyIdentity(MsgPkt_t *pMsgPkt)
{
    char SmallBuf[SMALL_BUF_SIZE];
    int fd;
    int ReadDataLength;
    uint64_t CorrectUserID;
    uint64_t UserID;
    char UserName[USER_NAME_MAX_LENGTH];
    char CorrectPassword[PASSWORD_MAX_LENGTH];
    char password[PASSWORD_MAX_LENGTH];
    
    MSG_GetStringData(pMsgPkt, MSG_DATA_OFFSET_USER_NAME, UserName, sizeof(UserName));
    snprintf(SmallBuf, sizeof(SmallBuf), "%s/%s", SERVER_USER_LIST_DIR, UserName);
    if(0 != access(SmallBuf, F_OK))
    {
        LOG_WARNING("[%s][Add user] "
            "Pemission denied: no such admin: %s\n", UserName, UserName);
        return STAT_ERR;
    }

    fd= open(SmallBuf, O_RDONLY);
    if(0 > fd)
    {
        LOG_ERROR("[%s][Add user] "
            "Fail to open user list file: %s\n", UserName, SmallBuf);
        return STAT_ERR;
    }

    ReadDataLength = read(fd, &CorrectUserID, sizeof(uint64_t));
    if(sizeof(uint64_t) != ReadDataLength)
    {
        close(fd);
        LOG_ERROR("[%s][Add user] "
            "Fail to read from user list file: %s\n", UserName, SmallBuf);
        return STAT_ERR;
    }

    if(!(CorrectUserID>>63))
    {
        close(fd);
        LOG_WARNING("[%s][Add user] "
            "Pemission denied: no such admin: %s\n", UserName, UserName);
        return STAT_ERR;
    }

    UserID = pMsgPkt->UserID;
    if(CorrectUserID != UserID)
    {
        close(fd);
        LOG_WARNING("[%s][Add user] "
            "Invalid user id: %ld\n", UserName, UserID);
        return STAT_ERR;
    }
    
    ReadDataLength = read(fd, CorrectPassword, PASSWORD_MAX_LENGTH);
    if(PASSWORD_MIN_LENGTH > ReadDataLength)
    {
        close(fd);
        LOG_ERROR("[%s][Add user] "
            "Invalid password format in file: %s\n", UserName, SmallBuf);
        return STAT_ERR;
    }

    if(PASSWORD_MAX_LENGTH != ReadDataLength)
    {
        CorrectPassword[ReadDataLength] = '\0';
    }
    else
    {
        CorrectPassword[PASSWORD_MAX_LENGTH-1] = '\0';
    }

    MSG_GetStringData(pMsgPkt, MSG_DATA_OFFSET_PASSWORD, password, PASSWORD_MAX_LENGTH);
    if(0 != strcmp(CorrectPassword, password))
    {
        close(fd);
        LOG_WARNING("[%s][Add user] "
            "Permission denied: password error, %s %s\n", UserName, CorrectPassword, password);
        return STAT_ERR;
    }

    close(fd);

    return STAT_OK;
}

/*
 *  @Briefs: Add a user to user list directory
 *  @Return: STAT_OK / STAT_ERR
 *  @Note:   Only administrator can invoke this function
 */
G_STATUS SERVER_ADMIN_AddUser(MsgPkt_t *pMsgPkt)
{
    MsgPkt_t MsgPkt;
    char UserName[USER_NAME_MAX_LENGTH];
    char AdminUserName[USER_NAME_MAX_LENGTH];
    char password[PASSWORD_MAX_LENGTH];
    char SmallBuf[SMALL_BUF_SIZE];
    int fd;
    int WriteDataLength;
    int length;
    uint64_t UserID;
    
    MSG_GetStringData(pMsgPkt, MSG_DATA_OFFSET_USER_NAME, AdminUserName, USER_NAME_MAX_LENGTH);
    MSG_GetStringData(pMsgPkt, MSG_DATA_OFFSET_ADD_USER_NAME, UserName, USER_NAME_MAX_LENGTH);

    if(0 != pMsgPkt->CCFlag)
    {
        if(0 > pMsgPkt->fd)
        {
            LOG_WARNING("[%s][Add user][%s] "
                "Invalid fd value, actual value: %d\n", AdminUserName, UserName, pMsgPkt->fd);
            return STAT_ERR;
        }
        
        MsgPkt.cmd = MSG_CMD_SEND_TO_USER;
        MsgPkt.CCFlag = 0;
        MsgPkt.fd = pMsgPkt->fd;
        MSG_Set32BitData(&MsgPkt, MSG_DATA_OFFSET_CC, CC_NORMAL);
    }

    if(STAT_OK != SERVER_ADMIN_VerifyIdentity(pMsgPkt))
    {
        if(0 != pMsgPkt->CCFlag)
        {
            MSG_Set32BitData(&MsgPkt, MSG_DATA_OFFSET_CC, CC_PERMISSION_DENIED);
            MSG_PostMsg(&MsgPkt);
        }
        return STAT_ERR;
    }

    snprintf(SmallBuf, sizeof(SmallBuf), "%s/%s", SERVER_USER_LIST_DIR, UserName);
    
    if(0 == access(SmallBuf, F_OK))
    {
        LOG_WARNING("[%s][Add user][%s] "
            "User has been exist\n", AdminUserName, UserName);
        if(0 != pMsgPkt->CCFlag)
        {
            MSG_Set32BitData(&MsgPkt, MSG_DATA_OFFSET_CC, CC_USER_HAS_BEEN_EXIST);
            MSG_PostMsg(&MsgPkt);
        }
        return STAT_ERR;
    }
    
    if(STAT_OK != SERVER_CreateUserID(&UserID))
    {
        LOG_ERROR("[%s][Add user][%s] "
            "Fail to create user id\n", AdminUserName, UserName);
        
        if(0 != pMsgPkt->CCFlag)
        {
            MSG_Set32BitData(&MsgPkt, MSG_DATA_OFFSET_CC, CC_FAIL_TO_CREATE_USER_ID);
            MSG_PostMsg(&MsgPkt);
        }
        
        return STAT_ERR;
    }

    fd = open(SmallBuf, O_CREAT | O_WRONLY, 0600);
    if(0 > fd)
    {
        LOG_ERROR("[%s][Add user][%s] "
            "Fail to create user file in %s\n", AdminUserName, UserName, SERVER_USER_LIST_DIR);
        if(0 != pMsgPkt->CCFlag)
        {
            MSG_Set32BitData(&MsgPkt, MSG_DATA_OFFSET_CC, CC_FAIL_TO_OPEN);
            MSG_PostMsg(&MsgPkt);
        }
        return STAT_ERR;
    }
    
    WriteDataLength = write(fd, &UserID, sizeof(uint64_t));
    if(sizeof(uint64_t) != WriteDataLength)
    {
        close(fd);
        LOG_ERROR("[%s][Add user][%s] "
            "Fail to write to user list file: %s\n", AdminUserName, UserName, SmallBuf);
        
        if(0 != pMsgPkt->CCFlag)
        {
            MSG_Set32BitData(&MsgPkt, MSG_DATA_OFFSET_CC, CC_FAIL_TO_WRITE);
            MSG_PostMsg(&MsgPkt);
        }
        
        return STAT_ERR;
    }

    MSG_GetStringData(pMsgPkt, MSG_DATA_OFFSET_ADD_PASSWORD, password, PASSWORD_MAX_LENGTH);
    length = strlen(password);
    if(PASSWORD_MIN_LENGTH > length)
    {
        close(fd);
        LOG_WARNING("[%s][Add user][%s] "
            "Password is too short\n", AdminUserName, UserName);
        if(0 != pMsgPkt->CCFlag)
        {
            MSG_Set32BitData(&MsgPkt, MSG_DATA_OFFSET_CC, CC_PASSWORD_IS_TOO_SHORT);
            MSG_PostMsg(&MsgPkt);
        }
    }
    
    WriteDataLength = write(fd, password, length);
    if(length != WriteDataLength)
    {
        close(fd);
        LOG_ERROR("[%s][Add user][%s] "
            "Fail to write to user list file: %s\n", AdminUserName, UserName, SmallBuf);
        if(0 != pMsgPkt->CCFlag)
        {
            MSG_Set32BitData(&MsgPkt, MSG_DATA_OFFSET_CC, CC_FAIL_TO_WRITE);
            MSG_PostMsg(&MsgPkt);
        }
    }

    close(fd);
    
    if(0 != pMsgPkt->CCFlag)
    {
        MSG_PostMsg(&MsgPkt);
    }
    
    LOG_INFO("[%s][Add user][%s] "
        "Success adding user, user id: 0x%lx\n", AdminUserName, UserName, UserID);

    return STAT_OK;
}
//<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
//Admin level function





#define COMMON_FUNC_START //Only use for locating function efficiently
//Common function
//>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>

G_STATUS SERVER_daemonize(void)
{
    //Ignore terminal I/O signal and stop signal
    signal(SIGTTOU, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGHUP,  SIG_IGN);
    //Ignore SIGCHLD sighal
    signal(SIGCHLD, SIG_IGN);

    int pid;
    int i;
    
    pid = fork();
    if(0 < pid)
    {
        exit(0); //Father process exit and make children process become background process
    }  
    else if(0 > pid)
    {
        DISP_ERR("Fail to create daemon\n");
        return STAT_ERR;  
    }
    
    /*
        Set up a new process group
        In this group, children process is the first process
        To make the process detach with all terminals
    */
    setsid();

    /*
        Create a children process again
        To make the children process is not the first process in the process group
        To make the children process unable to open a new terminal
    */    
//    pid=fork();
//    if(0 < pid)
//    {
//        exit(0);
//    }
//    else if(0 > pid)
//    {
//        DISP_ERR("Fail to create daemon\n");
//        return STAT_ERR;
//    }
    
    //Switch working directory to make process is not related with any file system
    chdir("/");
    
    umask(0);
    
    DISP("Success start server, pid: %d\n", getpid());
    DISP("Log files locate in %s\n", LOG_PATH);
    
    //Close all file descriptors inheriting from father process
    for(i = 0; i < NOFILE; i++);
    {
        close(i);
    }
    
#if 0
    /*
        Two ways to redirect stdin stdout stderr to /dev/null
    */
    //Way 1:
    fd = open("/dev/null", O_RDWR);
    dup2(fd, STDIN_FILENO);
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
    
    //Way 2:
    fd = open("/dev/null", O_RDWR); //Redirect stdin
    dup(fd);                        //Redirect stdout
    dup(fd);                        //Redirect stderr
#endif
    
    return STAT_OK;
}

G_STATUS SERVER_BeforeCreateTask(void)
{
    if(STAT_OK != SERVER_InitServerFile())
        return STAT_ERR;

    if(STAT_OK != SERVER_InitGlobalVaribles())
        return STAT_ERR;

    return STAT_OK;
}

/*
 *  @Briefs: Add a user to user list struct
 *  @Return: STAT_OK / STAT_ERR
 *  @Note:   None
 */
G_STATUS SERVER_UserLogin(MsgPkt_t *pMsgPkt)
{
    MsgPkt_t MsgPkt;
    char UserName[USER_NAME_MAX_LENGTH];
    char SmallBuf[SMALL_BUF_SIZE];
    int fd;
    int ReadDataLength;
    UserList_t *pCurUser;
    uint64_t UserID;
    
    MSG_GetStringData(pMsgPkt, MSG_DATA_OFFSET_USER_NAME, UserName, USER_NAME_MAX_LENGTH);
    snprintf(SmallBuf, sizeof(SmallBuf), "%s/%s", SERVER_USER_LIST_DIR, UserName);
    
    MsgPkt.cmd = MSG_CMD_SEND_TO_USER;
    MsgPkt.CCFlag = 0;
    MsgPkt.fd = pMsgPkt->fd;
    MsgPkt.UserID = 0;
    MSG_Set32BitData(&MsgPkt, MSG_DATA_OFFSET_CC, CC_NORMAL);

    if(0 > pMsgPkt->fd)
    {
        MSG_Set32BitData(&MsgPkt, MSG_DATA_OFFSET_CC, CC_INVALID_FD);
        MSG_PostMsg(&MsgPkt);
        
        LOG_WARNING("[SERVER user login][%s] "
            "Invalid fd value, actual value: %d\n", UserName, pMsgPkt->fd);
        
        return STAT_ERR;
    }
        
    if(0 != access(SmallBuf, F_OK))
    {
        MSG_Set32BitData(&MsgPkt, MSG_DATA_OFFSET_CC, CC_USER_DOES_NOT_EXIST);
        MSG_PostMsg(&MsgPkt);
        
        LOG_WARNING("[SERVER user login][%s] User does not exist\n", UserName);
        
        return STAT_ERR;
    }
    
    fd = open(SmallBuf, O_RDONLY);
    if(0 > fd)
    {
        MSG_Set32BitData(&MsgPkt, MSG_DATA_OFFSET_CC, CC_FAIL_TO_OPEN);
        MSG_PostMsg(&MsgPkt);
        
        LOG_ERROR("[SERVER user login][%s] open(): %s\n", UserName, strerror(errno));
        
        return STAT_ERR;
    }

    ReadDataLength = read(fd, &UserID, sizeof(UserID));
    if(sizeof(UserID) != ReadDataLength)
    {
        close(fd);
        MSG_Set32BitData(&MsgPkt, MSG_DATA_OFFSET_CC, CC_FAIL_TO_READ);
        MSG_PostMsg(&MsgPkt);
        
        LOG_ERROR("[SERVER user login][%s] read(): %s\n", UserName, strerror(errno));
        
        return STAT_ERR;
    }
    
    close(fd);
    
    fd = pMsgPkt->fd;
    pthread_mutex_lock(&g_UserListLock);
    
    pCurUser = &g_administrator;
    while(1)
    {
        pCurUser = pCurUser->pNext;
        if(NULL == pCurUser)
            break;

        if(fd == pCurUser->fd)
            break;
    }

    if(NULL == pCurUser)
    {
        pthread_mutex_unlock(&g_UserListLock);
        MSG_Set32BitData(&MsgPkt, MSG_DATA_OFFSET_CC, CC_FAIL_TO_READ);
        MSG_PostMsg(&MsgPkt);
        
        LOG_WARNING("[SERVER user login][%s] Session is not found\n", UserName);
        return STAT_ERR;
    }
    
    pCurUser->UserID = UserID;
    memcpy(pCurUser->UserName, UserName, USER_NAME_MAX_LENGTH);
    pthread_mutex_unlock(&g_UserListLock);
    
    MsgPkt.UserID = UserID;
    MSG_PostMsg(&MsgPkt);
    
    LOG_INFO("[SERVER user login][%s] Success login, user id: 0x%lx\n", UserName, UserID);
    
    return STAT_OK;
}

/*
 *  @Briefs: Delete a user in user list struct
 *  @Return: STAT_OK / STAT_ERR
 *  @Note:   None
 */
G_STATUS SERVER_UserLogout(MsgPkt_t *pMsgPkt)
{
    if(0 > pMsgPkt->fd)
        return STAT_ERR;

    char UserName[USER_NAME_MAX_LENGTH];
    UserList_t *pPrevUser;
    UserList_t *pCurUser;
    int fd;

    fd = pMsgPkt->fd;
    MSG_GetStringData(pMsgPkt, MSG_DATA_OFFSET_USER_NAME, UserName, USER_NAME_MAX_LENGTH);
    pthread_mutex_lock(&g_UserListLock);
    
    pPrevUser = &g_administrator;
    pCurUser = g_administrator.pNext;
    
    while(1)
    {
        if(NULL == pCurUser)
            break;

        if(fd == pCurUser->fd)
            break;
        
        pPrevUser = pCurUser;
        pCurUser = pCurUser->pNext;
    }
    
    if(NULL == pCurUser)
    {
        pthread_mutex_unlock(&g_UserListLock);
        LOG_INFO("[SERVER user logout][%s] Session is not found \n", UserName);
        return STAT_ERR;
    }
    
    if(g_pUserListTail == pCurUser)
    {
        g_pUserListTail = pPrevUser;
    }
    
    pPrevUser->pNext = pCurUser->pNext;
    close(pCurUser->fd);
    free(pCurUser);
    //pCurUser = pPrevUser->pNext;
    g_administrator.fd--;

    pthread_mutex_unlock(&g_UserListLock);
    
    LOG_INFO("[SERVER user logout][%s] Success logout \n", UserName);
    
    return STAT_OK;
}

G_STATUS SERVER_CheckAllUserStatus(MsgPkt_t *pMsgPkt)
{
    UserList_t *pPrevUser;
    UserList_t *pCurUser;
    int WriteDataLength;
    char SmallBuf[2] = {HANDSHAKE_SYMBOL, '\0'};

    pthread_mutex_lock(&g_UserListLock);
    pPrevUser = &g_administrator;
    pCurUser = g_administrator.pNext;
    
    while(NULL != pCurUser)
    {
        if(0 > pCurUser->fd)
        {
            if(g_pUserListTail == pCurUser)
            {
                g_pUserListTail = pPrevUser;
            }
            
            pPrevUser->pNext = pCurUser->pNext;
            LOG_INFO("[Check user status][%s]: User is offline\n", pCurUser->UserName);
            free(pCurUser);
            pCurUser = pPrevUser->pNext;
            g_administrator.fd--;
            continue;
        }
        
        WriteDataLength = write(pCurUser->fd, SmallBuf, sizeof(SmallBuf));
        if(sizeof(SmallBuf) != WriteDataLength)
        {
            if(g_pUserListTail == pCurUser)
            {
                g_pUserListTail = pPrevUser;
            }
            
            pPrevUser->pNext = pCurUser->pNext;
            close(pCurUser->fd);
            LOG_INFO("[Check user status][%s]: User is offline\n", pCurUser->UserName);
            free(pCurUser);
            pCurUser = pPrevUser->pNext;
            g_administrator.fd--;
        }
        else
        {
            LOG_INFO("[Check user status][%s]: User is online\n", pCurUser->UserName);
            pPrevUser = pCurUser;
            pCurUser = pCurUser->pNext;
        }
    }
    
    pthread_mutex_unlock(&g_UserListLock);
    
    return STAT_OK;
}
//<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
//Common function





#define STATIC_FUNC_START //Only use for locating function efficiently
//Static function
//>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>

static int SERVER_ConfigServer(struct sockaddr_in *pServerSocketAddr)
{
    int SocketFd;
    int res;
    
    SocketFd = socket(AF_INET, SOCK_STREAM, 0);
    if(0 > SocketFd)
    {
        LOG_FATAL_ERROR("[SERVER config] "
            "Fail to create socket: %s(error code: %d)\n", 
            strerror(errno), errno);
        
        return -1;
    }
    
    memset(pServerSocketAddr, 0, sizeof(struct sockaddr_in));
    pServerSocketAddr->sin_family = AF_INET;
    pServerSocketAddr->sin_addr.s_addr = htonl(INADDR_ANY);
    pServerSocketAddr->sin_port = htons(8080);
    
    res = bind(SocketFd, (struct sockaddr*)pServerSocketAddr, sizeof(struct sockaddr_in));
    if(0 != res)
    {
        LOG_FATAL_ERROR("[SERVER bind] "
            "Fail to bind socket: %s(error code: %d)\n", 
            strerror(errno), errno);
        
        return -1;
    }
    
    res = listen(SocketFd, 10);
    if(0 != res)
    {
        close(SocketFd);
        LOG_FATAL_ERROR("[SERVER listen] "
            "Fail to listen socket: %s(error code: %d)\n", 
            strerror(errno), errno);
        
        return -1;
    }

    return SocketFd;
}

static G_STATUS SERVER_InitServerFile(void)
{
    if(0 != access(SERVER_ROOT_DIR, F_OK))
    {
        if(0 != mkdir(SERVER_ROOT_DIR, S_IRUSR | S_IWUSR))
        {
            LOG_FATAL_ERROR("[SERVER init file] "
                "Fail to create server root directory: %s\n", SERVER_ROOT_DIR);
            return STAT_FATAL_ERR;
        }
    }
    
    if(0 != access(SERVER_USER_LIST_DIR, F_OK))
    {
        if(0 != mkdir(SERVER_USER_LIST_DIR, S_IRUSR | S_IWUSR))
        {
            LOG_FATAL_ERROR("[SERVER init file] "
                "Fail to create server user list directory: %s\n", SERVER_USER_LIST_DIR);
            return STAT_FATAL_ERR;
        }
    }
    
    return STAT_OK;
}

static G_STATUS SERVER_InitGlobalVaribles(void)
{
    g_ServerLiveFlag = 0;
    g_pUserListTail = &g_administrator;
    
    memset(&g_administrator, 0, sizeof(UserList_t));
    
    return STAT_OK;
}

static G_STATUS SERVER_CreateUserID(uint64_t *pUserID)
{
    uint64_t UserID;
    struct timeval TimeInterval;
    struct tm *TimeInfo;
    
    gettimeofday(&TimeInterval, NULL);
    TimeInfo = localtime(&TimeInterval.tv_sec);

    //99 12 31 23 59 59 999999
    UserID = 0;
    UserID += (TimeInfo->tm_year % 100) * 1E16;
    UserID += (TimeInfo->tm_mon+1) * 1E14;
    UserID += TimeInfo->tm_mday * 1E12;
    UserID += TimeInfo->tm_hour * 1E10;
    UserID += TimeInfo->tm_min * 1E8;
    UserID += TimeInfo->tm_sec * 1E6;
    UserID += TimeInterval.tv_usec;
    UserID &= ~((uint64_t)0x1 << 63);

    *pUserID = UserID;

    return STAT_OK;
}

static G_STATUS SERVER_CreateSession(int fd)
{
    if(0 > fd)
        return STAT_ERR;
    
    UserList_t *pNewUser;

    pNewUser = (UserList_t *)calloc(1, sizeof(UserList_t));
    if(NULL == pNewUser)
    {
        LOG_ERROR("[SERVER create session] "
            "malloc(): %s\n", strerror(errno));
        return STAT_ERR;
    }

    pNewUser->fd = fd;
    pNewUser->pNext = NULL;

    pthread_mutex_lock(&g_UserListLock);
    g_pUserListTail->pNext = pNewUser;
    g_pUserListTail = pNewUser;
    g_administrator.fd++;
    pthread_mutex_unlock(&g_UserListLock);
    
    LOG_INFO("[SERVER create session] New session, fd=%d\n", fd);
    
    return STAT_OK;
}
//<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
//Static function
