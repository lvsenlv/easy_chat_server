/*************************************************************************
	> File Name: server.c
	> Author: 
	> Mail: 
	> Created Time: 2018年01月30日 星期二 08时39分23秒
 ************************************************************************/

#include "server.h"
#include "log.h"
#include "completion_code.h"
#include "crc.h"
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
static uint64_t SERVER_CreateUserID(void);
static G_STATUS SERVER_CreateSession(int fd, struct sockaddr_in *pClientSocketAddr);
static G_STATUS SERVER_CloseSession(int fd, uint64_t UserID);
static COMPLETION_CODE SERVER_AddUser(const char *pUserName, const char *pPassword, _BOOL_ flag);
static COMPLETION_CODE SERVER_DelUser(const char *pUserName, _BOOL_ flag);
static COMPLETION_CODE SERVER_RenameUser(const char *pOldUserName, 
    const char *pNewUserName, _BOOL_ flag);
static void SERVER_GetSession(session_t **ppPrevSession, session_t **ppCurSession, int fd, uint64_t UserID);
static void SERVER_FreeSession(session_t *pPrevSession , session_t *pCurSession, _BOOL_ flag);
static void SERVER_UpdateMaxFd(__IO int *pMaxFd);
static G_STATUS SERVER_TransferFile(const char *pFileName, int UserFd);

volatile char g_ServerLiveFlag;         //1 mean server task is alive
int g_ServerSocketFd;                   //It would be initialized in server task
int g_ServerTransferFd;

/*
    Mutex lock sequence:
        g_SessionLock -> g_LogLockTbl[*]
    Warning:
        strictly ban using g_LogLockTbl[*] before using g_SessionLock
*/
pthread_mutex_t g_SessionLock = PTHREAD_MUTEX_INITIALIZER;
__IO int g_MaxFd;                       //It would be initialized in server task
__IO fd_set g_PrevFds;                  //It would be initialized in server task
session_t g_HeadSession;
session_t *g_pTailSession;

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
    int res;
    socklen_t ClientSocketAddrLen;
    int ClientSocketFd;
    struct sockaddr_in ClientSocketAddr;
    int ReadDataLength;
    MsgPkt_t MsgPkt;
    MsgDataRes_t *pResMsgPkt;
    session_t *pPrevSession;
    session_t *pCurSession;
    int TimeCount;
    int CurFd;

    ServerSocketFd = SERVER_ConfigServer(&ServerSocketAddr);
    if(0 > ServerSocketFd)
        return NULL;

    g_ServerSocketFd = ServerSocketFd;
    
    MSG_InitMsgPkt(&MsgPkt);
    pResMsgPkt = (MsgDataRes_t *)&MsgPkt.data;
    TimeInterval.tv_usec = 0;
    g_MaxFd = ServerSocketFd;
    FD_ZERO(&g_PrevFds);
    FD_SET(ServerSocketFd, &g_PrevFds);
    TimeCount = 0;
 
    while(1)
    {
        g_ServerLiveFlag = 1;
        fds = g_PrevFds;
        TimeInterval.tv_sec = SERVER_SELECT_TIME_INTERVAL;

        res = select(g_MaxFd+1, &fds, NULL, NULL, &TimeInterval);
        if(0 > res)
        {
            LOG_DEBUG("[SERVER task] select(): force to close fd\n");
            continue;
        }

        if(res == 0) //Timeout
        {
            TimeCount += SERVER_SELECT_TIME_INTERVAL;
#ifdef __CHECK_USER_STATUS
            if(SERVER_CHECK_USER_STATUS_INTERVAL <= TimeCount)
            {
                TimeCount = 0;
                MsgPkt.cmd = MSG_CMD_CHECK_ALL_USER_STATUS;
                MsgPkt.fd = -1;
                MsgPkt.CCFlag = 0;
                MSG_PostMsg(&MsgPkt);
            }
#endif
            continue;
        }
        
        if(FD_ISSET(ServerSocketFd, &fds)) //New connection
        {
            ClientSocketAddrLen = sizeof(struct sockaddr_in);
            ClientSocketFd = accept(ServerSocketFd, (struct sockaddr*)&ClientSocketAddr, 
                &ClientSocketAddrLen);
            if(0 > ClientSocketFd)
            {
                LOG_WARNING("[Server task] accept(): %s\n", strerror(errno));
                continue;
            }

            SERVER_CreateSession(ClientSocketFd, &ClientSocketAddr);

            MsgPkt.cmd = MSG_CMD_SEND_RES;
            MsgPkt.fd = ClientSocketFd;
            pResMsgPkt->CC = CC_NORMAL;
            MSG_PostMsg(&MsgPkt);
            
            continue;
        }

        pthread_mutex_lock(&g_SessionLock); //Pay attention to unlock
        pPrevSession = &g_HeadSession;
        pCurSession = g_HeadSession.pNext;
        while(1)
        {
            if(NULL == pCurSession)
            {
                pthread_mutex_unlock(&g_SessionLock);
                break;
            }
            
            if(!FD_ISSET(pCurSession->fd, &fds))
            {
                pPrevSession = pCurSession;
                pCurSession = pCurSession->pNext;
                continue;
            }
            
            ReadDataLength = read(pCurSession->fd, (char *)&MsgPkt, sizeof(MsgPkt_t));
            if(0 > ReadDataLength)
            {
                pthread_mutex_unlock(&g_SessionLock);
                LOG_DEBUG("[SERVER task][%s] Fail to read: may be abnormal disconnection\n", pCurSession->ip);
                SERVER_FreeSession(pPrevSession, pCurSession, TRUE);
                pCurSession = pPrevSession->pNext;
                break;
            }
            
            if(0 == ReadDataLength)
            {
                LOG_DEBUG("[SERVER task][%s] Abnormal disconnection\n", pCurSession->ip);
                SERVER_FreeSession(pPrevSession, pCurSession, TRUE);
                pCurSession = pPrevSession->pNext;
                continue;
            }

            if(MSG_CMD_DO_NOTHING == MsgPkt.cmd)
            {
                pCurSession->status = SESSION_STATUS_ON_LINE;
            }

            CurFd = pCurSession->fd;
            LOG_DEBUG("[SERVER task][%s] New message\n", pCurSession->ip);
            pthread_mutex_unlock(&g_SessionLock);

            if((sizeof(MsgPkt_t) == ReadDataLength)  && (MSG_CMD_DO_NOTHING != MsgPkt.cmd))
            {
                if(CurFd == MsgPkt.fd)
                {
                    MSG_PostMsg(&MsgPkt);
                }
                else
                {
                    //Make sure close session operation could only close the session of msg source own
                    LOG_WARNING("[SERVER task] Msg fd does not match session fd\n");
                }
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

#ifdef __CHECK_USER_STATUS        
        if(SERVER_CHECK_USER_STATUS_INTERVAL <= TimeCount)
        {
            TimeCount = 0;
            MsgPkt.cmd = MSG_CMD_CHECK_ALL_USER_STATUS;
            MSG_PostMsg(&MsgPkt);
        }
#endif
    }
    
    return NULL;
}

void *SERVER_TransferTask(void *pArg)
{
    fd_set fds;
    struct timeval TimeInterval;
    int res;
    int ReadDataLength;
    char buf[FILE_NAME_MAX_LENGTH + 32];
    int UserFd;

    TimeInterval.tv_usec = 0;

    while(1)
    {
        FD_ZERO(&fds);
        FD_SET(g_ServerTransferFd, &fds);
        TimeInterval.tv_sec = 10;
        
        res = select(g_ServerTransferFd+1, &fds, NULL, NULL, &TimeInterval);
        if(0 > res)
        {
            LOG_ERROR("[SERVER transfer task] Select function return negative value\n");
            continue;
        }

        if(0 == res)
            continue;

        if(!(FD_ISSET(g_ServerTransferFd, &fds)))
            continue;

        ReadDataLength = read(g_ServerTransferFd, buf, sizeof(buf));
        if(0 >= ReadDataLength)
        {
            LOG_ERROR("[SERVER transfer task] read(): %s\n", strerror(errno));
            continue;
        }

        buf[sizeof(buf)-1] = '\0';

        UserFd = *((int *)buf);
        if(0 > UserFd)
        {
            LOG_WARNING("[SERVER transfer task] Invalid fd: %d\n", UserFd);
            continue;
        }

        SERVER_TransferFile(buf+32, UserFd);
    }

    return NULL;
}





#define ROOT_LEVEL //Only use for locating function efficiently
//Root level function
//>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>

/*
 *  @Briefs: Verify the identity of root
 *  @Return: STAT_OK / STAT_ERR
 *  @Note:   None
 */
static G_STATUS SERVER_ROOT_VerifyIdentity(MsgDataVerifyIdentity_t *pVerifyData)
{
    pVerifyData->UserName[USER_NAME_MAX_LENGTH-1] = '\0';
    pVerifyData->password[PASSWORD_MAX_LENGTH-1] = '\0';
    
    if(0 != strcmp("lvsenlv", pVerifyData->UserName))
    {
        LOG_WARNING("[Root verify] Invalid root name: %s\n", pVerifyData->UserName);
        return STAT_ERR;
    }
    
    if(4884 != pVerifyData->UserID)
    {
        LOG_WARNING("[Root verify] Invalid user id: 0x%lx\n", pVerifyData->UserID);
        return STAT_ERR;
    }
    
    if(0 != strcmp("linuxroot", pVerifyData->password))
    {
        LOG_WARNING("[Root verify] Password error\n");
        return STAT_ERR;
    }

    return STAT_OK;
}

/*
 *  @Briefs: Root login
 *  @Return: STAT_OK / STAT_ERR
 *  @Note:   None
 */
G_STATUS SERVER_ROOT_UserLogin(MsgPkt_t *pMsgPkt)
{
    MsgPkt_t ResMsgPkt;
    MsgDataVerifyIdentity_t *pVerifyData;
    MsgDataRes_t *pResMsgData;
    session_t *pCurSession;

    pVerifyData = (MsgDataVerifyIdentity_t *)pMsgPkt->data;
    pResMsgData = (MsgDataRes_t *)&ResMsgPkt.data;
    
    ResMsgPkt.cmd = MSG_CMD_SEND_RES;
    ResMsgPkt.fd = pMsgPkt->fd;
    pResMsgData->CC = CC_NORMAL;
    
    if(STAT_OK != SERVER_ROOT_VerifyIdentity(pVerifyData))
    {
        pResMsgData->CC = CC_PERMISSION_DENIED;
        MSG_PostMsg(&ResMsgPkt);
        
        return STAT_ERR;
    }

    pthread_mutex_lock(&g_SessionLock);
    SERVER_GetSession(NULL, &pCurSession, pMsgPkt->fd, -1);
    if(NULL == pCurSession)
    {
        pthread_mutex_unlock(&g_SessionLock);
        pResMsgData->CC = CC_SESSION_IS_NOT_FOUND;
        MSG_PostMsg(&ResMsgPkt);
        LOG_WARNING("[root] Session is not found\n");
        
        return STAT_ERR;
    }
    
    pCurSession->UserInfo.UserID = 4884;
    memcpy(pCurSession->UserInfo.UserName, "root", 4);
    pthread_mutex_unlock(&g_SessionLock);
    
    MSG_PostMsg(&ResMsgPkt);
    LOG_INFO("[root] Login\n");
    
    return STAT_OK;    
}

/*
 *  @Briefs: Add administrator
 *  @Return: STAT_OK / STAT_ERR
 *  @Note:   None
 */
G_STATUS SERVER_ROOT_AddAdmin(MsgPkt_t *pMsgPkt)
{
    MsgPkt_t ResMsgPkt;
    MsgDataAddUser_t *pReqMsgData;
    MsgDataRes_t *pResMsgData;
    COMPLETION_CODE code;

    pReqMsgData = (MsgDataAddUser_t *)pMsgPkt->data;
    pResMsgData = (MsgDataRes_t *)&ResMsgPkt.data;

    if(0 != pMsgPkt->CCFlag)
    {
        ResMsgPkt.cmd = MSG_CMD_SEND_RES;
        ResMsgPkt.fd = pMsgPkt->fd;
        pResMsgData->CC = CC_NORMAL;
    }
    
    if(STAT_OK != SERVER_ROOT_VerifyIdentity(&pReqMsgData->VerifyData))
    {
        if(0 != pMsgPkt->CCFlag)
        {
            pResMsgData->CC = CC_PERMISSION_DENIED;
            MSG_PostMsg(&ResMsgPkt);
        }
        
        return STAT_ERR;
    }

    pReqMsgData->AddUserName[USER_NAME_MAX_LENGTH-1] = '\0';
    pReqMsgData->AddPassword[PASSWORD_MAX_LENGTH-1] = '\0';
    code = SERVER_AddUser(pReqMsgData->AddUserName, pReqMsgData->AddPassword, TRUE);
    if(0 != pMsgPkt->CCFlag)
    {
        pResMsgData->CC = code;
        MSG_PostMsg(&ResMsgPkt);
    }

    if(CC_NORMAL == code)
    {
        LOG_INFO("[Add admin][%s] Success\n", pReqMsgData->AddUserName);
    }

    return STAT_OK;
}

/*
 *  @Briefs: Del administrator
 *  @Return: STAT_OK / STAT_ERR
 *  @Note:   None
 */
G_STATUS SERVER_ROOT_DelAdmin(MsgPkt_t *pMsgPkt)
{
    MsgPkt_t ResMsgPkt;
    MsgDataDelUser_t *pReqMsgData;
    MsgDataRes_t *pResMsgData;
    COMPLETION_CODE code;

    pReqMsgData = (MsgDataDelUser_t *)pMsgPkt->data;
    pResMsgData = (MsgDataRes_t *)&ResMsgPkt.data;

    if(0 != pMsgPkt->CCFlag)
    {
        ResMsgPkt.cmd = MSG_CMD_SEND_RES;
        ResMsgPkt.fd = pMsgPkt->fd;
        pResMsgData->CC = CC_NORMAL;
    }
    
    if(STAT_OK != SERVER_ROOT_VerifyIdentity(&pReqMsgData->VerifyData))
    {
        if(0 != pMsgPkt->CCFlag)
        {
            pResMsgData->CC = CC_PERMISSION_DENIED;
            MSG_PostMsg(&ResMsgPkt);
        }
        
        return STAT_ERR;
    }
    
    pReqMsgData->DelUserName[USER_NAME_MAX_LENGTH-1] = '\0';
    code = SERVER_DelUser(pReqMsgData->DelUserName, TRUE);
    
    if(0 != pMsgPkt->CCFlag)
    {
        pResMsgData->CC = code;
        MSG_PostMsg(&ResMsgPkt);
    }

    if(CC_NORMAL == code)
    {
        LOG_INFO("[Del admin][%s] Success\n", pReqMsgData->DelUserName);
    }

    return STAT_OK;
}

/*
 *  @Briefs: Rename administrator
 *  @Return: STAT_OK / STAT_ERR
 *  @Note:   None
 */
G_STATUS SERVER_ROOT_RenameAdmin(MsgPkt_t *pMsgPkt)
{
    MsgPkt_t ResMsgPkt;
    MsgDataRenameUser_t *pReqMsgData;
    MsgDataRes_t *pResMsgData;
    COMPLETION_CODE code;

    pReqMsgData = (MsgDataRenameUser_t *)pMsgPkt->data;
    pResMsgData = (MsgDataRes_t *)&ResMsgPkt.data;

    if(0 != pMsgPkt->CCFlag)
    {
        ResMsgPkt.cmd = MSG_CMD_SEND_RES;
        ResMsgPkt.fd = pMsgPkt->fd;
        pResMsgData->CC = CC_NORMAL;
    }
    
    if(STAT_OK != SERVER_ROOT_VerifyIdentity(&pReqMsgData->VerifyData))
    {
        if(0 != pMsgPkt->CCFlag)
        {
            pResMsgData->CC = CC_PERMISSION_DENIED;
            MSG_PostMsg(&ResMsgPkt);
        }
        
        return STAT_ERR;
    }
    
    pReqMsgData->OldUserName[USER_NAME_MAX_LENGTH-1] = '\0';
    pReqMsgData->NewUserName[USER_NAME_MAX_LENGTH-1] = '\0';
    code = SERVER_RenameUser(pReqMsgData->OldUserName, pReqMsgData->NewUserName, TRUE);
    
    if(0 != pMsgPkt->CCFlag)
    {
        pResMsgData->CC = code;
        MSG_PostMsg(&ResMsgPkt);
    }

    if(CC_NORMAL == code)
    {
        LOG_INFO("[Rename admin] Success %s -> %s\n", pReqMsgData->OldUserName, pReqMsgData->NewUserName);
    }

    return STAT_OK;
}

/*
 *  @Briefs: Clear log
 *  @Return: STAT_OK / STAT_ERR
 *  @Note:   None
 */
G_STATUS SERVER_ROOT_ClearLog(MsgPkt_t *pMsgPkt)
{
    MsgDataVerifyIdentity_t *pVerifyData;
    int i;
    int fd;
    int retry;

    pVerifyData = (MsgDataVerifyIdentity_t *)pMsgPkt->data;
    
    if(STAT_OK != SERVER_ROOT_VerifyIdentity(pVerifyData))
        return STAT_ERR;
    
    for(i = 0; i < LOG_LEVEL_MAX; i++)
    {
        pthread_mutex_lock(&g_LogLockTbl[i]);
        
        fd = fileno(g_LogFileTbl[i]);
        for(retry = 1; retry <= 3; retry++)
        {
            if(0 == ftruncate(fd, 0))
                break;
        }
        
        if(3 == retry)
        {
            pthread_mutex_unlock(&g_LogLockTbl[i]);
            return STAT_ERR;
        }

        lseek(fd, 0, SEEK_SET);
        
        pthread_mutex_unlock(&g_LogLockTbl[i]);
    }

    LOG_INFO("[SERVER clear log] success\n");
    return STAT_OK;
}

/*
 *  @Briefs: Download log file
 *  @Return: STAT_OK / STAT_ERR
 *  @Note:   None
 */
G_STATUS SERVER_ROOT_DownloadLog(MsgPkt_t *pMsgPkt)
{
    MsgDataTransferFile_t *pReqMsgData;
    char FileName[FILE_NAME_MAX_LENGTH+128];

    pReqMsgData = (MsgDataTransferFile_t *)pMsgPkt->data;
    
    if(STAT_OK != SERVER_ROOT_VerifyIdentity(&pReqMsgData->VerifyData))
        return STAT_ERR;

    //snprintf(FileName, sizeof(FileName), "%s/%s", LOG_PATH, pReqMsgData->FileName);
    snprintf(FileName, sizeof(FileName), "/root/%s", pReqMsgData->FileName);

    if(0 != access(FileName, F_OK))
    {
        LOG_DEBUG("[SERVER download log] No such file: %s\n", FileName);
        return STAT_ERR;
    }

    if(STAT_OK != SERVER_TransferFile(FileName, pMsgPkt->fd))
        return STAT_ERR;

    LOG_DEBUG("[SERVER download log] Success\n");
    
    return STAT_OK;
}
//<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
//Root level function





#define ADMIN_LEVEL //Only use for locating function efficiently
//Admin level function
//>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>

/*
 *  @Briefs: Verify the identity of administrator
 *  @Return: STAT_OK / STAT_ERR
 *  @Note:   None
 */
static G_STATUS SERVER_ADMIN_VerifyIdentity(MsgDataVerifyIdentity_t *pVerifyData)
{
    char SmallBuf[SMALL_BUF_SIZE];
    int fd;
    int ReadDataLength;
    uint64_t CorrectUserID;
    char CorrectPassword[PASSWORD_MAX_LENGTH];

    pVerifyData->UserName[USER_NAME_MAX_LENGTH-1] = '\0';
    pVerifyData->password[PASSWORD_MAX_LENGTH-1] = '\0';
    
    snprintf(SmallBuf, sizeof(SmallBuf), "%s/%s/%s", 
        SERVER_ROOT_DIR, pVerifyData->UserName, SERVER_IDENTITY_FILE_NAME);
    if(0 != access(SmallBuf, F_OK))
    {
        LOG_WARNING("[Admin verify][%s] Admin does not exist\n", pVerifyData->UserName);
        return STAT_ERR;
    }
    
    fd= open(SmallBuf, O_RDONLY);
    if(0 > fd)
    {
        LOG_ERROR("[Admin verify][%s] open(): %s\n", pVerifyData->UserName, strerror(errno));
        return STAT_ERR;
    }
    
    ReadDataLength = read(fd, &CorrectUserID, sizeof(uint64_t));
    if(sizeof(uint64_t) != ReadDataLength)
    {
        close(fd);
        LOG_ERROR("[Admin verify][%s] read(): %s\n", pVerifyData->UserName, strerror(errno));
        return STAT_ERR;
    }
    
#ifdef __VERIFY_USER_ID
    if(!(CorrectUserID>>63))
    {
        close(fd);
        LOG_WARNING("[Admin verify][%s] Not an admin\n", pVerifyData->UserName);
        return STAT_ERR;
    }

    if(CorrectUserID != pVerifyData->UserID)
    {
        close(fd);
        LOG_WARNING("[Admin verify][%s] Invalid user id: 0x%lx\n", pVerifyData->UserName, pVerifyData->UserID);
        return STAT_ERR;
    }
#endif
    
    ReadDataLength = read(fd, CorrectPassword, PASSWORD_MAX_LENGTH);
    if(PASSWORD_MIN_LENGTH > ReadDataLength)
    {
        close(fd);
        LOG_ERROR("[Admin verify][%s] Invalid format in %s\n", pVerifyData->UserName, SmallBuf);
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

    if(0 != strcmp(CorrectPassword, pVerifyData->password))
    {
        close(fd);
        LOG_WARNING("[Admin verify][%s] Password error\n", pVerifyData->UserName);
        return STAT_ERR;
    }

    close(fd);

    return STAT_OK;
}

/*
 *  @Briefs: Add user
 *  @Return: STAT_OK / STAT_ERR
 *  @Note:   None
 */
G_STATUS SERVER_ADMIN_AddUser(MsgPkt_t *pMsgPkt)
{
    MsgPkt_t ResMsgPkt;
    MsgDataAddUser_t *pReqMsgData;
    MsgDataRes_t *pResMsgData;
    COMPLETION_CODE code;

    pReqMsgData = (MsgDataAddUser_t *)pMsgPkt->data;
    pResMsgData = (MsgDataRes_t *)&ResMsgPkt.data;
    
    if(0 != pMsgPkt->CCFlag)
    {
        ResMsgPkt.cmd = MSG_CMD_SEND_RES;
        ResMsgPkt.fd = pMsgPkt->fd;
        pResMsgData->CC = CC_NORMAL;
    }

    if(STAT_OK != SERVER_ADMIN_VerifyIdentity(&pReqMsgData->VerifyData))
    {
        if(0 != pMsgPkt->CCFlag)
        {
            pResMsgData->CC = CC_PERMISSION_DENIED;
            MSG_PostMsg(&ResMsgPkt);
        }
        
        return STAT_ERR;
    }
    
    pReqMsgData->AddUserName[USER_NAME_MAX_LENGTH-1] = '\0';
    pReqMsgData->AddPassword[PASSWORD_MAX_LENGTH-1] = '\0';
    code = SERVER_AddUser(pReqMsgData->AddUserName, pReqMsgData->AddPassword, FALSE);
    
    if(0 != pMsgPkt->CCFlag)
    {
        pResMsgData->CC = code;
        MSG_PostMsg(&ResMsgPkt);
    }

    if(CC_NORMAL == code)
    {
        LOG_INFO("[Add user][%s][%s] Success\n", 
            pReqMsgData->VerifyData.UserName, pReqMsgData->AddUserName);
    }
    
    return STAT_OK;
}

/*
 *  @Briefs: Del user
 *  @Return: STAT_OK / STAT_ERR
 *  @Note:   None
 */
G_STATUS SERVER_ADMIN_DelUser(MsgPkt_t *pMsgPkt)
{
    MsgPkt_t ResMsgPkt;
    MsgDataDelUser_t *pReqMsgData;
    MsgDataRes_t *pResMsgData;
    COMPLETION_CODE code;

    pReqMsgData = (MsgDataDelUser_t *)pMsgPkt->data;
    pResMsgData = (MsgDataRes_t *)&ResMsgPkt.data;
    
    if(0 != pMsgPkt->CCFlag)
    {
        ResMsgPkt.cmd = MSG_CMD_SEND_RES;
        ResMsgPkt.fd = pMsgPkt->fd;
        pResMsgData->CC = CC_NORMAL;
    }

    if(STAT_OK != SERVER_ADMIN_VerifyIdentity(&pReqMsgData->VerifyData))
    {
        if(0 != pMsgPkt->CCFlag)
        {
            pResMsgData->CC = CC_PERMISSION_DENIED;
            MSG_PostMsg(&ResMsgPkt);
        }
        
        return STAT_ERR;
    }
    
    pReqMsgData->DelUserName[USER_NAME_MAX_LENGTH-1] = '\0';
    code = SERVER_DelUser(pReqMsgData->DelUserName, FALSE);
    
    if(0 != pMsgPkt->CCFlag)
    {
        pResMsgData->CC = code;
        MSG_PostMsg(&ResMsgPkt);
    }

    if(CC_NORMAL == code)
    {
        LOG_INFO("[Add user][%s][%s] Success\n", 
            pReqMsgData->VerifyData.UserName, pReqMsgData->DelUserName);
    }
    
    return STAT_OK;
}
//<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
//Admin level function





#define COMMON_FUNC //Only use for locating function efficiently
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

/*
 *  @Briefs: Do some initializing operation
 *  @Return: STAT_OK / STAT_ERR
 *  @Note:   None
 */
G_STATUS SERVER_BeforeCreateTask(void)
{
    if(STAT_OK != SERVER_InitServerFile())
        return STAT_ERR;

    if(STAT_OK != SERVER_InitGlobalVaribles())
        return STAT_ERR;

    if(0 == access(SERVER_TRANSFER_QUEUE, F_OK))
    {
        if(0 != unlink(SERVER_TRANSFER_QUEUE))
        {
            LOG_FATAL_ERROR("[SERVER create queue] unlink(): %s", strerror(errno));
            return STAT_FATAL_ERR;
        }
    }
    
    if(0 != mkfifo(SERVER_TRANSFER_QUEUE, 0600))
    {
        LOG_FATAL_ERROR("[SERVER create queue] mkfifo(): %s", strerror(errno));
        return STAT_FATAL_ERR;
    }
    
    g_ServerTransferFd = open(SERVER_TRANSFER_QUEUE, O_RDWR);
    if(0 > g_ServerTransferFd)
    {
        LOG_FATAL_ERROR("[SERVER create queue] open(): %s", strerror(errno));
        return STAT_FATAL_ERR;
    }

    return STAT_OK;
}

/*
 *  @Briefs: Make user login and initialize the user info
 *  @Return: STAT_OK / STAT_ERR
 *  @Note:   None
 */
G_STATUS SERVER_UserLogin(MsgPkt_t *pMsgPkt)
{
    MsgPkt_t ResMsgPkt;
    MsgDataVerifyIdentity_t *pVerifyData;
    MsgDataRes_t *pResMsgData;
    char SmallBuf[SMALL_BUF_SIZE];
    int fd;
    int ReadDataLength;
    session_t *pCurSession;
    uint64_t UserID;

    pVerifyData = (MsgDataVerifyIdentity_t *)pMsgPkt->data;
    pResMsgData = (MsgDataRes_t *)&ResMsgPkt.data;
    
    ResMsgPkt.cmd = MSG_CMD_SEND_RES;
    ResMsgPkt.fd = pMsgPkt->fd;
    pResMsgData->CC = CC_NORMAL;
    
    pVerifyData->UserName[USER_NAME_MAX_LENGTH-1] = '\0';
    pVerifyData->password[PASSWORD_MAX_LENGTH-1] = '\0';
    snprintf(SmallBuf, sizeof(SmallBuf), "%s/%s/%s", 
        SERVER_ROOT_DIR, pVerifyData->UserName, SERVER_IDENTITY_FILE_NAME);
    
    if(0 != access(SmallBuf, F_OK))
    {
        pResMsgData->CC = CC_USER_DOES_NOT_EXIST;
        MSG_PostMsg(&ResMsgPkt);
        LOG_WARNING("[%s][%s] User does not exist\n", __func__, pVerifyData->UserName);
        
        return STAT_ERR;
    }
    
    fd = open(SmallBuf, O_RDONLY);
    if(0 > fd)
    {
        pResMsgData->CC = CC_FAIL_TO_OPEN;
        MSG_PostMsg(&ResMsgPkt);
        LOG_ERROR("[%s][%s] open(): %s\n", __func__, pVerifyData->UserName, strerror(errno));
        
        return STAT_ERR;
    }

    ReadDataLength = read(fd, &UserID, sizeof(UserID));
    if(sizeof(UserID) != ReadDataLength)
    {
        close(fd);
        pResMsgData->CC = CC_FAIL_TO_READ;
        MSG_PostMsg(&ResMsgPkt);
        LOG_ERROR("[%s][%s] read(): %s\n", __func__, pVerifyData->UserName, strerror(errno));
        
        return STAT_ERR;
    }
    
    close(fd);
    
    fd = pMsgPkt->fd;
    pthread_mutex_lock(&g_SessionLock);
    SERVER_GetSession(NULL, &pCurSession, fd, -1);
    if(NULL == pCurSession)
    {
        pthread_mutex_unlock(&g_SessionLock);
        pResMsgData->CC = CC_SESSION_IS_NOT_FOUND;
        MSG_PostMsg(&ResMsgPkt);
        LOG_WARNING("[%s][%s] Session is not found\n", __func__, pVerifyData->UserName);
        
        return STAT_ERR;
    }
    
    pCurSession->UserInfo.UserID = UserID;
    memcpy(pCurSession->UserInfo.UserName, pVerifyData->UserName, USER_NAME_MAX_LENGTH);
    LOG_INFO("[%s][%s] Login\n", pVerifyData->UserName, pCurSession->ip);
    pthread_mutex_unlock(&g_SessionLock);
    
    pResMsgData->UserID = UserID;
    MSG_PostMsg(&ResMsgPkt);
    
    return STAT_OK;
}

/*
 *  @Briefs: Make user logout and close session
 *  @Return: STAT_OK / STAT_ERR
 *  @Note:   None
 */
G_STATUS SERVER_UserLogout(MsgPkt_t *pMsgPkt)
{
    MsgDataVerifyIdentity_t *pVerifyData;
    
    if(0 > pMsgPkt->fd)
        return STAT_ERR;

    pVerifyData = (MsgDataVerifyIdentity_t *)pMsgPkt->data;
    
    if(STAT_OK != SERVER_CloseSession(pMsgPkt->fd, -1))
    {
        LOG_WARNING("[%s] Fail to close session\n", pVerifyData->UserName);
        return STAT_ERR;
    }
    
    LOG_INFO("[%s] Logout\n", pVerifyData->UserName);
    
    return STAT_OK;
}

/*
 *  @Briefs: Check all users are online or not
 *  @Return: STAT_OK / STAT_ERR
 *  @Note:   If user is offline, the session would be closed
 */
G_STATUS SERVER_CheckAllUserStatus(MsgPkt_t *pMsgPkt)
{
    session_t *pPrevSession;
    session_t *pCurSession;
    _BOOL_ flag;

    flag = FALSE;

    pthread_mutex_lock(&g_SessionLock);
    pPrevSession = &g_HeadSession;
    pCurSession = g_HeadSession.pNext;
    
    while(NULL != pCurSession)
    {
        if(SESSION_STATUS_OFF_LINE == pCurSession->status)
        {
            flag = TRUE;
            SERVER_FreeSession(pPrevSession, pCurSession, FALSE);
            LOG_DEBUG("[SERVER check status] %s is off line\n", pCurSession->ip);
            pCurSession = pPrevSession->pNext;
            continue;
        }
    
        LOG_DEBUG("[SERVER check status] %s is on line\n", pCurSession->ip);
        pCurSession->status = SESSION_STATUS_OFF_LINE;
        pPrevSession = pCurSession;
        pCurSession = pCurSession->pNext;
    }

    if(TRUE == flag)
    {
        SERVER_UpdateMaxFd(&g_MaxFd);
    }
    
    pthread_mutex_unlock(&g_SessionLock);
    
    return STAT_OK;
}
//<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
//Common function





#define STATIC_FUNC //Only use for locating function efficiently
//Static function
//>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>

/*
 *  @Briefs: Create related directory
 *  @Return: STAT_FATAL_ERR / STAT_OK
 *  @Note: None
 */
static G_STATUS SERVER_InitServerFile(void)
{
    if(0 != access(SERVER_ROOT_DIR, F_OK))
    {
        if(0 != mkdir(SERVER_ROOT_DIR, S_IRUSR | S_IWUSR))
        {
            LOG_FATAL_ERROR("[%s] Fail to create server root directory: %s\n", 
                __func__, SERVER_ROOT_DIR);
            return STAT_FATAL_ERR;
        }
    }
    
    return STAT_OK;
}

/*
 *  @Briefs: Initialize global varibles
 *  @Return: Always STAT_OK
 *  @Note: None
 */
static G_STATUS SERVER_InitGlobalVaribles(void)
{
    g_ServerLiveFlag = 0;
    g_ServerSocketFd = 0;
    g_ServerTransferFd = 0;
    g_pTailSession = &g_HeadSession;
    memset(&g_HeadSession, 0, sizeof(session_t));
    
    return STAT_OK;
}

/*
 *  @Briefs: Configure server network function
 *  @Return: Return socket fd value if success, otherwise return -1
 *  @Note: None
 */
static int SERVER_ConfigServer(struct sockaddr_in *pServerSocketAddr)
{
    int SocketFd;
    int res;
    
    SocketFd = socket(AF_INET, SOCK_STREAM, 0);
    if(0 > SocketFd)
    {
        LOG_FATAL_ERROR("[SERVER config] socket(): %s\n", strerror(errno));
        return -1;
    }
    
    memset(pServerSocketAddr, 0, sizeof(struct sockaddr_in));
    pServerSocketAddr->sin_family = AF_INET;
    pServerSocketAddr->sin_addr.s_addr = htonl(INADDR_ANY);
    pServerSocketAddr->sin_port = htons(8080);
    
    res = bind(SocketFd, (struct sockaddr*)pServerSocketAddr, sizeof(struct sockaddr_in));
    if(0 != res)
    {
        LOG_FATAL_ERROR("[SERVER config] bind(): %s\n", strerror(errno));
        return -1;
    }
    
    res = listen(SocketFd, 10);
    if(0 != res)
    {
        close(SocketFd);
        LOG_FATAL_ERROR("[SERVER config] listen(): %s\n", strerror(errno));
        return -1;
    }

    return SocketFd;
}

/*
 *  @Briefs: Create user id based on system time
 *  @Return: User id
 *  @Note: None
 */
static uint64_t SERVER_CreateUserID(void)
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

    return UserID;
}

/*
 *  @Briefs: Add user
 *  @Return: Competion code
 *  @Note: If flag is equare to TRUE, it means the user is an administrator
 */
static COMPLETION_CODE SERVER_AddUser(const char *pUserName, const char *pPassword, _BOOL_ flag)
{
    char SmallBuf[SMALL_BUF_SIZE];
    int fd;
    uint64_t UserID;
    int WriteDataLength;
    int length;

    snprintf(SmallBuf, sizeof(SmallBuf), "%s/%s", SERVER_ROOT_DIR, pUserName);
    
    if(0 == access(SmallBuf, F_OK))
    {
        LOG_WARNING("[Add user][%s] User has been existed\n", pUserName);
        return CC_USER_HAS_BEEN_EXISTED;
    }
    
    if(0 != mkdir(SmallBuf, 0600))
    {
        LOG_ERROR("[Add user][%s] mkdir(): %s\n", pUserName, strerror(errno));
        return CC_FAIL_TO_MK_DIR;
    }
    
    snprintf(SmallBuf, sizeof(SmallBuf), "%s/%s/%s", 
        SERVER_ROOT_DIR, pUserName, SERVER_IDENTITY_FILE_NAME);
    
    UserID = SERVER_CreateUserID();
    if(TRUE == flag)
    {
        UserID |= (uint64_t)0x1 << 63; //Set as admin permission
    }
    
    fd = open(SmallBuf, O_CREAT | O_WRONLY, 0600);
    if(0 > fd)
    {
        LOG_ERROR("[Add user][%s] open(): %s\n", pUserName, strerror(errno));
        return CC_FAIL_TO_OPEN;
    }

    WriteDataLength = write(fd, &UserID, sizeof(uint64_t));
    if(sizeof(uint64_t) != WriteDataLength)
    {
        close(fd);
        LOG_ERROR("[Add user][%s] write(): %s\n", pUserName, strerror(errno));
        return CC_FAIL_TO_WRITE;
    }
    
    length = strlen(pPassword);
    if(PASSWORD_MIN_LENGTH > length)
    {
        close(fd);
        LOG_WARNING("[Add user][%s] Password is too short\n", pUserName);
        return CC_PASSWORD_IS_TOO_SHORT;
    }
    
    WriteDataLength = write(fd, pPassword, length);
    if(length != WriteDataLength)
    {
        close(fd);
        LOG_ERROR("[Add user][%s] write(): %s\n", pUserName, strerror(errno));
        return CC_FAIL_TO_WRITE;
    }

    LOG_DEBUG("[Add user][%s] UserID=0x%lx\n", pUserName, UserID);

    close(fd);
    
    return CC_NORMAL;
}

/*
 *  @Briefs: Delete user
 *  @Return: Competion code
 *  @Note: If flag is TRUE, it means the user is an administrator
 */
static COMPLETION_CODE SERVER_DelUser(const char *pUserName, _BOOL_ flag)
{
    char SmallBuf[SMALL_BUF_SIZE];
    int fd;
    uint64_t UserID;
    int ReadDataLength;

    snprintf(SmallBuf, sizeof(SmallBuf), "%s/%s", SERVER_ROOT_DIR, pUserName);
    
    if(0 != access(SmallBuf, F_OK))
    {
        LOG_WARNING("[Del user][%s] User does not exist\n", pUserName);
        return CC_USER_DOES_NOT_EXIST;
    }
    
    snprintf(SmallBuf, sizeof(SmallBuf), "%s/%s/%s", 
        SERVER_ROOT_DIR, pUserName, SERVER_IDENTITY_FILE_NAME);
        
    if(0 != access(SmallBuf, F_OK))
    {
        snprintf(SmallBuf, sizeof(SmallBuf), "%s/%s", SERVER_ROOT_DIR, pUserName);
        RemoveDirectory(SmallBuf);
        LOG_WARNING("[Del user][%s] User does not exist\n", pUserName);
        return CC_USER_DOES_NOT_EXIST;
    }

    fd = open(SmallBuf, O_RDONLY);
    if(0 > fd)
    {
        LOG_ERROR("[Del user][%s] open(): %s\n", pUserName, strerror(errno));
        return CC_FAIL_TO_OPEN;
    }

    ReadDataLength = read(fd, &UserID, sizeof(UserID));
    if(sizeof(UserID) != ReadDataLength)
    {
        close(fd);
        LOG_ERROR("[Del user][%s] read(): %s\n", pUserName, strerror(errno));
        return CC_FAIL_TO_READ;
    }
    
    close(fd);

    if(UserID >> 63) //User is an admin
    {
        if(TRUE != flag) //If not the root to execute this operation
        {
            LOG_ERROR("[Del user][%s] Permission denied\n", pUserName);
            return CC_PERMISSION_DENIED;
        }
    }
    
    SERVER_CloseSession(-1, UserID);
    
    snprintf(SmallBuf, sizeof(SmallBuf), "%s/%s", SERVER_ROOT_DIR, pUserName);
    
    if(STAT_OK != RemoveDirectory(SmallBuf))
    {
        LOG_WARNING("[Del user][%s] Fail to delete dir: %s\n", pUserName, SmallBuf);
        return CC_FAIL_TO_RM_DIR;
    }

    return CC_NORMAL;
}

/*
 *  @Briefs: Rename user
 *  @Return: Competion code
 *  @Note: If flag is TRUE, it means the user is an administrator
 */
static COMPLETION_CODE SERVER_RenameUser(const char *pOldUserName, 
    const char *pNewUserName, _BOOL_ flag)
{
    char SmallBuf[SMALL_BUF_SIZE];
    char NewSmallBuf[SMALL_BUF_SIZE];
    int fd;
    uint64_t UserID;
    int ReadDataLength;
    
    snprintf(SmallBuf, sizeof(SmallBuf), "%s/%s", SERVER_ROOT_DIR, pOldUserName);
    snprintf(NewSmallBuf, sizeof(NewSmallBuf), "%s/%s", SERVER_ROOT_DIR, pNewUserName);
    
    if(0 != access(SmallBuf, F_OK))
    {
        LOG_WARNING("[Rename user][%s] User does not exist\n", pOldUserName);
        return CC_USER_DOES_NOT_EXIST;
    }

    if(0 == access(NewSmallBuf, F_OK))
    {
        LOG_WARNING("[Rename user][%s] User has been existed\n", pNewUserName);
        return CC_USER_HAS_BEEN_EXISTED;
    }
    
    snprintf(SmallBuf, sizeof(SmallBuf), "%s/%s/%s", 
        SERVER_ROOT_DIR, pOldUserName, SERVER_IDENTITY_FILE_NAME);
    
    if(0 != access(SmallBuf, F_OK))
    {
        snprintf(SmallBuf, sizeof(SmallBuf), "%s/%s", SERVER_ROOT_DIR, pOldUserName);
        RemoveDirectory(SmallBuf);
        LOG_WARNING("[Rename user][%s] User does not exist\n", pOldUserName);
        return CC_USER_DOES_NOT_EXIST;
    }

    fd = open(SmallBuf, O_RDONLY);
    if(0 > fd)
    {
        LOG_ERROR("[Rename user][%s] open(): %s\n", pOldUserName, strerror(errno));
        return CC_FAIL_TO_OPEN;
    }

    ReadDataLength = read(fd, &UserID, sizeof(UserID));
    if(sizeof(UserID) != ReadDataLength)
    {
        close(fd);
        LOG_ERROR("[Rename user][%s] read(): %s\n", pOldUserName, strerror(errno));
        return CC_FAIL_TO_READ;
    }
    
    close(fd);

    if(UserID >> 63) //User is an admin
    {
        if(TRUE != flag) //If not the root to execute this operation
        {
            LOG_ERROR("[Rename user][%s] Permission denied\n", pOldUserName);
            return CC_PERMISSION_DENIED;
        }
    }
    
    SERVER_CloseSession(-1, UserID);
    
    snprintf(SmallBuf, sizeof(SmallBuf), "%s/%s", SERVER_ROOT_DIR, pOldUserName);
    
    if(0 != rename(SmallBuf, NewSmallBuf))
    {
        LOG_ERROR("[Rename user] rename(%s -> %s): %s\n", pOldUserName, pNewUserName, strerror(errno));
        return CC_FAIL_TO_RN_DIR;
    }

    return CC_NORMAL;
}

/*
 *  @Briefs: Create session
 *  @Return: STAT_OK / STAT_ERR
 *  @Note: None
 */
static G_STATUS SERVER_CreateSession(int fd, struct sockaddr_in *pClientSocketAddr)
{
    if(0 > fd)
        return STAT_ERR;
    
    session_t *pNewSession;

    pNewSession = (session_t *)calloc(1, sizeof(session_t));
    if(NULL == pNewSession)
    {
        LOG_ERROR("[Create session] calloc(): %s\n", strerror(errno));
        return STAT_ERR;
    }

    pNewSession->fd = fd;
    pNewSession->status = SESSION_STATUS_ON_LINE;
    memcpy(pNewSession->ip, inet_ntoa(pClientSocketAddr->sin_addr), IP_ADDR_MAX_LENGTH);
    pNewSession->pNext = NULL;

    pthread_mutex_lock(&g_SessionLock);
    g_pTailSession->pNext = pNewSession;
    g_pTailSession = pNewSession;
    g_HeadSession.fd++;
    
    FD_SET(fd, &g_PrevFds);
    if(g_MaxFd < fd)
    {
        g_MaxFd = fd;
    }
    
    LOG_DEBUG("[Create session] New session %s, fd=%d, session_addr=0x%lx\n", 
        pNewSession->ip, fd, (int64_t)pNewSession);
    
    pthread_mutex_unlock(&g_SessionLock);
    
    return STAT_OK;
}

/*
 *  @Briefs: Close the session and free memory
 *  @Return: STAT_OK / STAT_ERR
 *  @Note: If fd is negative, it will find the session based on UserID
 */
static G_STATUS SERVER_CloseSession(int fd, uint64_t UserID)
{
    session_t *pPrevSession;
    session_t *pCurSession;

    pPrevSession = NULL;
    pCurSession = NULL;
    pthread_mutex_lock(&g_SessionLock);

    SERVER_GetSession(&pPrevSession, &pCurSession, fd, UserID);
    if((NULL == pPrevSession) || (NULL == pCurSession))
    {
        pthread_mutex_unlock(&g_SessionLock);
        return STAT_ERR;
    }

    SERVER_FreeSession(pPrevSession, pCurSession, TRUE);
    
    pthread_mutex_unlock(&g_SessionLock);

    return STAT_OK;
}

/*
 *  @Briefs: Find out the session
 *  @Return: None
 *  @Note:   1. Must lock session before invoke and unlock after invoke
 *           2. Set value only if ppCurSession or ppPrevSession is not NULL
 *           3. Find out session according fd when fd is not negative, otherwise according to
 *              UserID to locate the session
 */
static void SERVER_GetSession(session_t **ppPrevSession, session_t **ppCurSession, 
    int fd, uint64_t UserID)
{
    session_t *pCurSession;
    session_t *pPrevSession;

    pCurSession = &g_HeadSession;

    if(0 < fd)
    {
        while(1)
        {
            pPrevSession = pCurSession;
            pCurSession = pCurSession->pNext;
            if(NULL == pCurSession)
                break;

            if(fd == pCurSession->fd)
                break;
        }
    }
    else
    {
        while(1)
        {
            pPrevSession = pCurSession;
            pCurSession = pCurSession->pNext;
            if(NULL == pCurSession)
                break;

            if(UserID == pCurSession->UserInfo.UserID)
                break;
        }
    }

    if(NULL != ppPrevSession)
    {
        *ppPrevSession = pPrevSession;
    }

    if(NULL != ppCurSession)
    {
        *ppCurSession = pCurSession;
    }
}

/*
 *  @Briefs: Free the memory of session and connect the previous session with the next session
 *  @Return: None
 *  @Note:   1. Must lock session before invoke and unlock after invoke
 *           2. If flag is TRUE, it means it needs to update MaxFd
 */
static void SERVER_FreeSession(session_t *pPrevSession, session_t *pCurSession, _BOOL_ flag)
{
    int fd;

    if((NULL == pPrevSession) || (NULL == pCurSession))
        return;

    if(g_pTailSession == pCurSession)
    {
        g_pTailSession = pPrevSession;
    }
    
    pPrevSession->pNext = pCurSession->pNext;
    fd = pCurSession->fd;
    if(0 <= fd)
    {
        close(fd);
    }

    free(pCurSession);
    g_HeadSession.fd--;
    
    FD_CLR(fd, &g_PrevFds);
    if((TRUE == flag) && (fd == g_MaxFd))
    {
        SERVER_UpdateMaxFd(&g_MaxFd);
    }
    
    LOG_DEBUG("[SERVER free session][0x%lx] success\n", (int64_t)pCurSession);
}

/*
 *  @Briefs: Calculate the max fd value according to all sessions
 *  @Return: None
 *  @Note:   Must lock session before invoke and unlock after invoke
 */
static void SERVER_UpdateMaxFd(__IO int *pMaxFd)
{
    int MaxFd;
    session_t *pCurSession;

    MaxFd = g_ServerSocketFd;
    pCurSession = g_HeadSession.pNext;
    
    while(NULL != pCurSession)
    {
        if(MaxFd < pCurSession->fd)
        {
            MaxFd = pCurSession->fd;
        }
                                        
        pCurSession = pCurSession->pNext;
    }

    *pMaxFd = MaxFd;
    LOG_DEBUG("[Update MaxFd] MaxFd=%d\n", MaxFd);
}

/*
 *  @Briefs: Transfer file to client
 *  @Return: None
 *  @Note:   
 *           Protocol:
 *           1. Send start cmd
 *           2. Send data. The data size is saved in fd parameter in MsgPkt. - CRC32
 *           3. Send finish cmd
 */
static G_STATUS SERVER_TransferFile(const char *pFileName, int UserFd)
{
    stat_t FileInfo;
    int fd;
    char buf[MSG_TRANSFER_DATA_BASE_SIZE+sizeof(MsgTransferPkt_t)];
    MsgTransferPkt_t *pMsgTransferPkt;
    char *pData;
    int DataLength;
    int CycleCount;
    int i;
    int RemainderDataLength;

    pMsgTransferPkt = (MsgTransferPkt_t *)buf;
    pData = buf + sizeof(MsgTransferPkt_t);

    if(0 != GetFileInfo(pFileName, &FileInfo))
    {
        LOG_DEBUG("[SERVER transfer file] %s\n", strerror(errno));
        return STAT_ERR;
    }
    
    if(MSG_TRANSFER_DATA_MAX_SIZE < FileInfo.st_size)
    {
        LOG_DEBUG("[SERVER transfer file] File too big, actual size: %ld byte\n", FileInfo.st_size);
        return STAT_ERR;
    }
    
    if(0 == FileInfo.st_size)
    {
        LOG_DEBUG("[SERVER transfer file] Empty file\n");
        return STAT_ERR;
    }
    
    fd = open(pFileName, O_RDONLY);
    if(0 > fd)
    {
        LOG_DEBUG("[SERVER transfer file] open(): %s\n", strerror(errno));
        return STAT_ERR;
    }
    
    pMsgTransferPkt->cmd = MSG_TRANSFER_START;
    pMsgTransferPkt->size = FileInfo.st_size;
    pMsgTransferPkt->CheckCode = CRC16_calculate(&pMsgTransferPkt->size, sizeof(pMsgTransferPkt->size));

    DataLength = write(UserFd, pMsgTransferPkt, sizeof(MsgTransferPkt_t));
    if(sizeof(MsgTransferPkt_t) != DataLength)
    {
        close(fd);
        LOG_DEBUG("[SERVER transfer file] write(): %s\n", strerror(errno));
        return STAT_ERR;
    }

    CycleCount = FileInfo.st_size / MSG_TRANSFER_DATA_BASE_SIZE;
    pMsgTransferPkt->cmd = MSG_TRANSFER_DATA;
    pMsgTransferPkt->size = MSG_TRANSFER_DATA_BASE_SIZE;

    for(i = 0; i < CycleCount; i++)
    {        
        DataLength = read(fd, pData, MSG_TRANSFER_DATA_BASE_SIZE);
        if(MSG_TRANSFER_DATA_BASE_SIZE != DataLength)
        {
            close(fd);
            LOG_DEBUG("[SERVER transfer file] read(): %s\n", strerror(errno));
            return STAT_ERR;
        }

        pMsgTransferPkt->CheckCode = CRC32_calculate(pData, MSG_TRANSFER_DATA_BASE_SIZE);
        
        DataLength = write(UserFd, buf, sizeof(buf));
        if(sizeof(buf) != DataLength)
        {
            close(fd);
            LOG_DEBUG("[SERVER transfer file] write(): %s\n", strerror(errno));
            return STAT_ERR;
        }

        //LOG_DEBUG("[SERVER transfer file] Tranfering .....\n");
    }

    RemainderDataLength = FileInfo.st_size % MSG_TRANSFER_DATA_BASE_SIZE;
    if(0 != RemainderDataLength)
    {
        DataLength = read(fd, pData, RemainderDataLength);
        if(RemainderDataLength != DataLength)
        {
            close(fd);
            LOG_DEBUG("[SERVER transfer file] read(): %s\n", strerror(errno));
            return STAT_ERR;
        }

        pMsgTransferPkt->size = RemainderDataLength;
        pMsgTransferPkt->CheckCode = CRC32_calculate(pData, RemainderDataLength);
    
        DataLength = write(UserFd, buf, sizeof(MsgTransferPkt_t)+RemainderDataLength);
        if((sizeof(MsgTransferPkt_t)+RemainderDataLength) != DataLength)
        {
            close(fd);
            LOG_DEBUG("[SERVER transfer file] write(): %s\n", strerror(errno));
            return STAT_ERR;
        }
    }
    
    close(fd);
    
    pMsgTransferPkt->cmd = MSG_TRANSFER_END;
    DataLength = write(UserFd, pMsgTransferPkt, sizeof(MsgTransferPkt_t));
    if(sizeof(MsgTransferPkt_t) != DataLength)
    {
        LOG_DEBUG("[SERVER transfer file] write(): %s\n", strerror(errno));
        return STAT_ERR;
    }
    
    LOG_DEBUG("[SERVER transfer file] success\n");
    
    return STAT_OK;
}
//<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
//Static function
