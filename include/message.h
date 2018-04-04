/*************************************************************************
	> File Name: message.h
	> Author: 
	> Mail: 
	> Created Time: 2018年01月31日 星期三 16时35分36秒
 ************************************************************************/

#ifndef __MESSAGE_H
#define __MESSAGE_H

#include "common.h"
#include "completion_code.h"

#define MSG_QUEUE_NAME                      "/var/easy_char_msg_queue"
#define HANDSHAKE_SYMBOL                    '#'

/*
    If the time of twice restarting less than this value,
    it means it quite fast to restart the msg task,
    and it may cause unknown error.
    It is recommended that if this situation appeares over 3 times,
    make the process exit.
*/
#define MSG_TASK_RESTART_TIME_INTERVAL      10

#define MSG_SELECT_TIME_INTERVAL            5 //Unit: second
#define MSG_MAX_NUM                         1024
#define MSG_DATA_MAX_LENGTH                 1024
#define MSG_RESERVE_DATA_MAX_LENGTH         64
#define MSG_TRANSFER_DATA_BASE_SIZE         (1024*4) //4Kb
#define MSG_TRANSFER_DATA_MAX_SIZE          ((uint64_t)1024*1024*1024*4) //4Gb

typedef enum {
    //To server
    MSG_CMD_SEND_TO_USER = 1,
    MSG_CMD_SEND_RES,
    MSG_CMD_ROOT_LOGIN,
    MSG_CMD_ROOT_ADD_ADMIN,
    MSG_CMD_ROOT_DEL_ADMIN,
    MSG_CMD_ROOT_RENAME_ADMIN,
    MSG_CMD_ROOT_CLEAR_LOG,
    MSG_CMD_ROOT_DOWNLOAD_LOG,
    MSG_CMD_ADMIN_ADD_USER,
    MSG_CMD_ADMIN_DEL_USER,
    MSG_CMD_USER_LOGIN,
    MSG_CMD_USER_LOGOUT,
    MSG_CMD_CHECK_ALL_USER_STATUS,
    MSG_CMD_DO_NOTHING,
    MSG_CMD_MAX,
}MSG_CMD;

typedef enum {
    MSG_TRANSFER_START = MSG_CMD_MAX + 1,
    MSG_TRANSFER_DATA,
    MSG_TRANSFER_END,
}MSG_TRANSFER_CMD;

typedef struct MsgPktStruct {
    MSG_CMD cmd;
    int fd;
    char data[MSG_DATA_MAX_LENGTH];
    char CCFlag; //1 means it need to send a response message
    uint32_t CheckCode;
}ALIGN_4K MsgPkt_t;

typedef struct MsgTransferPktStruct {
    MSG_TRANSFER_CMD cmd;
    uint64_t size;
    uint32_t CheckCode;
}MsgTransferPkt_t;

typedef struct MsgDataVerifyIdentityStruct {
    uint64_t UserID;
    char UserName[USER_NAME_MAX_LENGTH];
    char password[PASSWORD_MAX_LENGTH];
}MsgDataVerifyIdentity_t;

typedef struct MsgDataResStruct {
    COMPLETION_CODE CC;
    uint64_t UserID;
}MsgDataRes_t;

typedef struct MsgDataAddUserStruct {
    MsgDataVerifyIdentity_t VerifyData;
    char AddUserName[USER_NAME_MAX_LENGTH];
    char AddPassword[PASSWORD_MAX_LENGTH];
}MsgDataAddUser_t;

typedef struct MsgDataDelUserStruct {
    MsgDataVerifyIdentity_t VerifyData;
    char DelUserName[USER_NAME_MAX_LENGTH];
}MsgDataDelUser_t;

typedef struct MsgDataRenameUserStruct {
    MsgDataVerifyIdentity_t VerifyData;
    char OldUserName[USER_NAME_MAX_LENGTH];
    char NewUserName[USER_NAME_MAX_LENGTH];
}MsgDataRenameUser_t;

typedef struct MsgDataTransferFileStruct {
    MsgDataVerifyIdentity_t VerifyData;
    char FileName[FILE_NAME_MAX_LENGTH];
}MsgDataTransferFile_t;

extern char g_MsgTaskLiveFlag;
extern char *g_pCmdDetail[MSG_CMD_MAX];

G_STATUS MSG_CreateTask(pthread_t *pMsgTaskID);
void *MSG_MessageTask(void *pArg);
void MSG_InitMsgPkt(MsgPkt_t *pMsgPkt);
G_STATUS MSG_BeforeCreateTask(void);
G_STATUS MSG_LockMsgQueue(int fd);
G_STATUS MSG_UnlockMsgQueue(int fd);
G_STATUS MSG_PostMsg(MsgPkt_t *pMsgPkt);
G_STATUS MSG_PostMsgNoLock(MsgPkt_t *pMsgPkt);
G_STATUS MSG_GetResponse(MsgPkt_t *pMsgPkt, int timeout);

#endif
