/*************************************************************************
	> File Name: main.c
	> Author: 
	> Mail: 
	> Created Time: 2018年01月29日 星期一 08时54分56秒
 ************************************************************************/

#include "message.h"
#include "completion_code.h"
#include <errno.h>
#include <sys/socket.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>

#define __ROOT_LOGIN
#ifdef __ROOT_LOGIN
#define LOGIN_USER_NAME                     "lvsenlv"
#define LOGIN_PASSWORD                      "linuxroot"
#define LOGIN_USER_ID                       4884
#else
#define LOGIN_USER_NAME                     "admin01"
#define LOGIN_PASSWORD                      "admin123"
#endif

G_STATUS FillMsgPkt(MsgPkt_t *pMsgPkt, char ch);
G_STATUS GetResponse(MsgPkt_t *pMsgPkt, int fd, int timeout);
void DispHelpInfo(void);
 
int g_ResFd;
 
int main(int argc, char **argv)
{
    if(2 != argc)
    {
        printf("please input ip address\n");
        return -1;
    }

    if(0 == strcmp("-h", argv[1]))
    {
        DispHelpInfo();
        return 0;
    }

    InitErrorCodeTable();

    int ClientSocketFd = 0;
    struct sockaddr_in ClientSocketAddr;
    int res;
    int MaxFd = 0;
    char buf[BUF_SIZE] = {0};
    int SendDataLength;
    MsgPkt_t MsgPkt;
    MsgPkt_t ResMsgPkt;
    MsgDataVerifyIdentity_t *pVerifyData;
    MsgDataRes_t *pResMsgData;
    int retry;
    
    ClientSocketFd = socket(AF_INET, SOCK_STREAM, 0);
    if(0 > ClientSocketFd)
    {
        perror("create socket failed");
        return -1;
    }
    
    if(ClientSocketFd > MaxFd)
    {
        MaxFd = ClientSocketFd;
    }
    
    memset(&ClientSocketAddr, 0, sizeof(struct sockaddr_in));
    ClientSocketAddr.sin_family = AF_INET;
    ClientSocketAddr.sin_port = htons(8080);
    
    res = inet_pton(AF_INET, argv[1], &ClientSocketAddr.sin_addr);
    if(0 > res)
    {
        return -1;
    }

    memset(&MsgPkt, 0, sizeof(MsgPkt_t));
    memset(&ResMsgPkt, 0, sizeof(MsgPkt_t));
    pVerifyData = (MsgDataVerifyIdentity_t *)&MsgPkt.data;
    pResMsgData = (MsgDataRes_t *)&ResMsgPkt.data;

#ifdef __ROOT_LOGIN
    MsgPkt.cmd = MSG_CMD_ROOT_LOGIN;
    pVerifyData->UserID = LOGIN_USER_ID;
#else
    MsgPkt.cmd = MSG_CMD_USER_LOGIN;
#endif
    MsgPkt.CCFlag = 1;
    memcpy(pVerifyData->UserName, LOGIN_USER_NAME, sizeof(LOGIN_USER_NAME)-1);
    memcpy(pVerifyData->password, LOGIN_PASSWORD, sizeof(LOGIN_PASSWORD)-1);
    
    res = connect(ClientSocketFd, (struct sockaddr *)&ClientSocketAddr, sizeof(struct sockaddr_in));
    if(0 > res)
    {
        perror("connect");
        return -1;
    }
    
    for(retry = 1; retry <= 3; retry++)
    {
        if(STAT_OK != GetResponse(&ResMsgPkt, ClientSocketFd, 1))
            continue;
        
        g_ResFd = ResMsgPkt.fd;
        MsgPkt.fd = ResMsgPkt.fd;
        printf("Server response fd: %d\n", g_ResFd);
        
        SendDataLength = write(ClientSocketFd, &MsgPkt, sizeof(MsgPkt_t));
        if(sizeof(MsgPkt_t) != SendDataLength)
        {
            printf("write(): %s\n", strerror(errno));
            return -1;
        }
        
        if(STAT_OK != GetResponse(&ResMsgPkt, ClientSocketFd, 1))
            continue;
        
        if(CC_NORMAL != pResMsgData->CC)
        {
            printf("%s\n", GetErrorDetails(pResMsgData->CC));
            return -1;
        }
            
        break;
    }

    if(3 < retry)
    {
        printf("Fail to connect server: server no response\n");
        return -1;
    }
    
    printf("Success connect to server\n");
    
    while(1)
    {
        while(1)
        {
            fgets(buf, 2, stdin);
            
            if(STAT_OK != FillMsgPkt(&MsgPkt, buf[0]))
                continue;

            SendDataLength = write(ClientSocketFd, &MsgPkt, sizeof(MsgPkt_t));
            if(0 < SendDataLength)
            {
                printf("Success sending message\n");
            }
            else
            {
                printf("Fail to send message\n");
                continue;
            }

            break;
        }

        if(STAT_OK != GetResponse(&ResMsgPkt, ClientSocketFd, 1))
        {
            printf("Server no response\n");
            continue;
        }
            
        if(CC_NORMAL != pResMsgData->CC)
            printf("%s\n", GetErrorDetails(pResMsgData->CC));
        else
            printf("Success\n");
    }

    return 0;
}

G_STATUS FillMsgPkt(MsgPkt_t *pMsgPkt, char choice)
{
    MsgDataAddUser_t *pMsgDataAddUser;
    MsgDataDelUser_t *pMsgDataDelUser;
    MsgDataRenameUser_t *pMsgDataRenameUser;

    memset(pMsgPkt, 0, sizeof(MsgPkt_t));
    pMsgPkt->CCFlag = 1;
    pMsgPkt->fd = g_ResFd;
    
    switch(choice)
    {
        case '1':
#ifdef __ROOT_LOGIN
            pMsgPkt->cmd = MSG_CMD_ROOT_ADD_ADMIN;
            pMsgDataAddUser = (MsgDataAddUser_t *)pMsgPkt->data;
            pMsgDataAddUser->VerifyData.UserID = LOGIN_USER_ID;
            memcpy(pMsgDataAddUser->VerifyData.UserName, LOGIN_USER_NAME, sizeof(LOGIN_USER_NAME)-1);
            memcpy(pMsgDataAddUser->VerifyData.password, LOGIN_PASSWORD, sizeof(LOGIN_PASSWORD)-1);
            memcpy(pMsgDataAddUser->AddUserName, "admin01", 7);
            memcpy(pMsgDataAddUser->AddPassword, "admin123", 8);
#else
            pMsgPkt->cmd = MSG_CMD_DO_NOTHING;
            printf("No root login\n");
#endif
            break;
        case '2':
#ifdef __ROOT_LOGIN
            pMsgPkt->cmd = MSG_CMD_ROOT_DEL_ADMIN;
            pMsgDataDelUser = (MsgDataDelUser_t *)pMsgPkt->data;
            pMsgDataDelUser->VerifyData.UserID = LOGIN_USER_ID;
            memcpy(pMsgDataDelUser->VerifyData.UserName, LOGIN_USER_NAME, sizeof(LOGIN_USER_NAME)-1);
            memcpy(pMsgDataDelUser->VerifyData.password, LOGIN_PASSWORD, sizeof(LOGIN_PASSWORD)-1);
            memcpy(pMsgDataDelUser->DelUserName, "admin01", 7);
#else
            pMsgPkt->cmd = MSG_CMD_DO_NOTHING;
            printf("No root login\n");
#endif
            break;
        case '3':
            pMsgPkt->cmd = MSG_CMD_ADMIN_ADD_USER;
            pMsgDataAddUser = (MsgDataAddUser_t *)pMsgPkt->data;
            memcpy(pMsgDataAddUser->VerifyData.UserName, LOGIN_USER_NAME, sizeof(LOGIN_USER_NAME)-1);
            memcpy(pMsgDataAddUser->VerifyData.password, LOGIN_PASSWORD, sizeof(LOGIN_PASSWORD)-1);
            memcpy(pMsgDataAddUser->AddUserName, "user01", 6);
            memcpy(pMsgDataAddUser->AddPassword, "user123", 7);
            break;
        case '4':
            pMsgPkt->cmd = MSG_CMD_ADMIN_DEL_USER;
            pMsgDataDelUser = (MsgDataDelUser_t *)pMsgPkt->data;
            memcpy(pMsgDataDelUser->VerifyData.UserName, LOGIN_USER_NAME, sizeof(LOGIN_USER_NAME)-1);
            memcpy(pMsgDataDelUser->VerifyData.password, LOGIN_PASSWORD, sizeof(LOGIN_PASSWORD)-1);
            memcpy(pMsgDataDelUser->DelUserName, "user01", 7);
            break;
        case '5':
#ifdef __ROOT_LOGIN
            pMsgPkt->cmd = MSG_CMD_ROOT_RENAME_ADMIN;
            pMsgDataRenameUser = (MsgDataRenameUser_t *)pMsgPkt->data;
            pMsgDataRenameUser->VerifyData.UserID = LOGIN_USER_ID;
            memcpy(pMsgDataRenameUser->VerifyData.UserName, LOGIN_USER_NAME, sizeof(LOGIN_USER_NAME)-1);
            memcpy(pMsgDataRenameUser->VerifyData.password, LOGIN_PASSWORD, sizeof(LOGIN_PASSWORD)-1);
            memcpy(pMsgDataRenameUser->OldUserName, "admin01", 7);
            memcpy(pMsgDataRenameUser->NewUserName, "admin02", 7);
#else
            pMsgPkt->cmd = MSG_CMD_DO_NOTHING;
            printf("No root login\n");
#endif
            break;
        case 'q':
            break;
        case 'h':
            DispHelpInfo();
            break;
        default:
            return STAT_ERR;
            break;
    }

    return STAT_OK;
}

G_STATUS GetResponse(MsgPkt_t *pMsgPkt, int fd, int timeout)
{
    if(0 > timeout)
        return STAT_ERR;

    if(0 > fd)
        return STAT_ERR;
    
    struct timeval TimeInterval;
    fd_set fds;
    int res;
    int ReadDataLength;

    TimeInterval.tv_usec = 0;

    do
    {
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        TimeInterval.tv_sec = timeout;

        res = select(fd+1, &fds, NULL, NULL, &TimeInterval);
        if(0 > res)
        {
            printf("[Get response] "
                "Select function return a negative value\n");
            break;
        }

        if(0 == res)
            return STAT_ERR;
        
        if(!(FD_ISSET(fd, &fds)))
            return STAT_ERR;

        ReadDataLength = read(fd, pMsgPkt, sizeof(MsgPkt_t));
        if(sizeof(MsgPkt_t) != ReadDataLength)
        {
            if(0 < ReadDataLength)
            {
                if(HANDSHAKE_SYMBOL == ((char *)pMsgPkt)[0])
                {
                    printf("Detect handshake symbol and ignore it\n");
                    continue;
                }
            }

            printf("[Get response] "
                "Invalid data length in msg queque, actual length: %d\n", ReadDataLength);
            return STAT_ERR;
        }

        if(MSG_CMD_DO_NOTHING == pMsgPkt->cmd)
            continue;

        break;
    }while(1);
    
    return STAT_OK;
}

void DispHelpInfo(void)
{
    printf("1: Add admin\n");
    printf("2: Del admin\n");
    printf("3: Add user\n");
    printf("4: Del user\n");
    printf("5: Rename user\n");
}
