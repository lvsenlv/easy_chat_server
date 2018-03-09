/*************************************************************************
	> File Name: message.h
	> Author: 
	> Mail: 
	> Created Time: 2018年01月31日 星期三 16时35分36秒
 ************************************************************************/

#ifndef __MESSAGE_H
#define __MESSAGE_H

#include "common.h"

#define MSG_QUEUE_NAME                      "/var/MsgHandleQueue"
#define HANDSHAKE_SYMBOL                    '#'
#define SUCCESS_SYMBOL                      'Y'
#define FAIL_SYMBOL                         'N'

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

//Pay attention to the interval of following offset
#define MSG_DATA_OFFSET_CC                  0
#define MSG_DATA_OFFSET_USER_ID             (MSG_DATA_OFFSET_CC + 32)
#define MSG_DATA_OFFSET_USER_NAME           (MSG_DATA_OFFSET_USER_ID + 64)
#define MSG_DATA_OFFSET_PASSWORD            (MSG_DATA_OFFSET_USER_NAME + USER_NAME_MAX_LENGTH)
#define MSG_DATA_OFFSET_ADD_USER_NAME       (MSG_DATA_OFFSET_PASSWORD + PASSWORD_MAX_LENGTH)
#define MSG_DATA_OFFSET_DEL_USER_NAME       MSG_DATA_OFFSET_ADD_USER_NAME
#define MSG_DATA_OFFSET_ADD_PASSWORD        (MSG_DATA_OFFSET_ADD_USER_NAME + USER_NAME_MAX_LENGTH)
#define MSG_DATA_OFFSET_DEL_PASSWORD        MSG_DATA_OFFSET_ADD_PASSWORD

typedef enum {
    MSG_CMD_SEND_TO_USER = 1,
    MSG_CMD_ROOT_LOGIN,
    MSG_CMD_ROOT_ADD_ADMIN,
    MSG_CMD_ROOT_DEL_ADMIN,
    MSG_CMD_ADMIN_ADD_USER,
    MSG_CMD_USER_LOGIN,
    MSG_CMD_USER_LOGOUT,
    MSG_CMD_CHECK_ALL_USER_STATUS,
    MSG_CMD_DO_NOTHING,
    MSG_CMD_MAX,
}MSG_CMD;

typedef struct MsgPktStruct {
    MSG_CMD cmd;
    int fd;
    char data[MSG_DATA_MAX_LENGTH];
    char CCFlag; //1 means it need to send a response message
    int CheckSum;
}MsgPkt_t;

extern char g_MsgTaskLiveFlag;
extern char *g_pCmdDetail[MSG_CMD_MAX];

G_STATUS MSG_CreateTask(pthread_t *pMsgTaskID);
void *MSG_MessageTask(void *pArg);
void MSG_InitMsgPkt(MsgPkt_t *pMsgPkt);
G_STATUS MSG_BeforeCreateTask(void);
G_STATUS MSG_LockMsgQueue(int fd);
G_STATUS MSG_UnlockMsgQueue(int fd);
G_STATUS MSG_PostMsg(MsgPkt_t *pMsgPkt);
G_STATUS MSG_GetResponse(MsgPkt_t *pMsgPkt, int timeout);



//Static inline functions
//>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>

static inline void MSG_Set32BitData(MsgPkt_t *pMsgPkt, int offset, int data)
{
    int *ptr = (int *)&pMsgPkt->data[offset];
    *ptr = data;
}

static inline int MSG_Get32BitData(MsgPkt_t *pMsgPkt, int offset)
{
    int *ptr = (int *)&pMsgPkt->data[offset];
    return *ptr;
}

static inline void MSG_Set64BitData(MsgPkt_t *pMsgPkt, int offset, uint64_t data)
{
    uint64_t *ptr = (uint64_t *)&pMsgPkt->data[offset];
    *ptr = data;
}

static inline uint64_t MSG_Get64BitData(MsgPkt_t *pMsgPkt, int offset)
{
    uint64_t *ptr = (uint64_t *)&pMsgPkt->data[offset];
    return *ptr;
}

static inline void MSG_GetStringData(MsgPkt_t *pMsgPkt, int offset, 
    char *pBuf, int length)
{
    char *ptr = (char *)&pMsgPkt->data[offset];

    while(1 < length)
    {
        if('\0' == *ptr)
            break;
            
        *pBuf++ = *ptr++;
        
        length--;
    }

    *pBuf = '\0';
}

static inline void MSG_SetStringData(MsgPkt_t *pMsgPkt, int offset, 
    char *pBuf, int length)
{
    char *ptr = (char *)&pMsgPkt->data[offset];
    int i;

    for(i = 0; i < length; i++)
    {
        if('\0' == *pBuf)
            break;

        *ptr++ = *pBuf++;
    }

    *ptr = '\0';
}

#endif
