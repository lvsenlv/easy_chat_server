/*************************************************************************
	> File Name: server.h
	> Author: 
	> Mail: 
	> Created Time: 2018年01月29日 星期一 08时55分11秒
 ************************************************************************/

#ifndef __SERVER_H
#define __SERVER_H

#include "common.h"
#include "message.h"

#define SERVER_ROOT_DIR                     "/root/.easy_chat"
#define SERVER_IDENTITY_FILE_NAME           "identity.info"

/*
    If the time of twice restarting less than this value,
    it means it quite fast to restart the server task,
    and it may cause unknown error.
    It is recommended that if this situation appeares over 3 times,
    make the process exit.
*/
#define SERVER_TASK_RESTART_TIME_INTERVAL   10

#define USER_MAX_NUM                        512
#define SERVER_SELECT_TIME_INTERVAL         5 //Unit: second
#define SERVER_CHECK_USER_STATUS_INTERVAL   10

#define SERVER_GET_ACTUAL_USER_ID(id)       (id & (~((uint64_t)0x1 << 63)))

typedef enum {
    USER_STATUS_ON_LINE = 0,
    USER_STATUS_OFF_LINE,
}USER_STATUS;

typedef struct UserInfoStruct {
    uint64_t UserID;
    char UserName[USER_NAME_MAX_LENGTH];
}UserInfo_t;

typedef struct SessionStruct {
    int fd;
    char ip[IP_ADDR_MAX_LENGTH];
    UserInfo_t UserInfo;
    struct SessionStruct *pNext;
}session_t;

extern volatile char g_ServerLiveFlag;
extern int g_ServerSocketFd;

G_STATUS SERVER_CreateTask(pthread_t *pServerTaskID);
void *SERVER_ServerTask(void *pArg);
G_STATUS SERVER_ROOT_AddAdmin(MsgPkt_t *pMsgPkt);
G_STATUS SERVER_ROOT_UserLogin(MsgPkt_t *pMsgPkt);
G_STATUS SERVER_ROOT_DelAdmin(MsgPkt_t *pMsgPkt);
G_STATUS SERVER_ADMIN_AddUser(MsgPkt_t *pMsgPkt);
G_STATUS SERVER_ADMIN_DelUser(MsgPkt_t *pMsgPkt);
G_STATUS SERVER_daemonize(void);
G_STATUS SERVER_BeforeCreateTask(void);
G_STATUS SERVER_UserLogin(MsgPkt_t *pMsgPkt);
G_STATUS SERVER_UserLogout(MsgPkt_t *pMsgPkt);
G_STATUS SERVER_CheckAllUserStatus(MsgPkt_t *pMsgPkt);



//Static inline functions
//>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>

static inline G_STATUS SERVER_CreateFile(char *pFileName)
{
    FILE *fp = fopen(pFileName, "w+");
    if(NULL == fp)
        return STAT_ERR;

    fclose(fp);
    return STAT_OK;
}

#endif
