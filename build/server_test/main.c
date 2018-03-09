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

G_STATUS FillMsgPkt(MsgPkt_t *pMsgPkt, char ch);
G_STATUS GetResponse(MsgPkt_t *pMsgPkt, int fd, int timeout);
 
int g_ResFd;
 
int main(int argc, char **argv)
{
    if(2 != argc)
    {
        printf("please input ip address\n");
        return -1;
    }

    InitErrorCodeTable();

    int ClientSocketFd = 0;
    struct sockaddr_in ClientSocketAddr;
    int res;
    int MaxFd = 0;
    char buf[BUF_SIZE] = {0};
    int SendDataLength;
    MsgPkt_t MsgPkt;
    COMPLETION_CODE code;
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
    MsgPkt.CCFlag = 1;
    
    res = connect(ClientSocketFd, (struct sockaddr *)&ClientSocketAddr, sizeof(struct sockaddr_in));
    if(0 > res)
    {
        perror("connect");
        return -1;
    }
    
    for(retry = 1; retry <= 3; retry++)
    {
        if(STAT_OK != GetResponse(&MsgPkt, ClientSocketFd, 1))
            continue;
        
        g_ResFd = MsgPkt.fd;
        printf("Server response fd: %d\n", g_ResFd);
        
//        MsgPkt.cmd = MSG_CMD_ROOT_LOGIN;
//        MSG_Set64BitData(&MsgPkt, MSG_DATA_OFFSET_USER_ID, 0x1314);
//        MSG_SetStringData(&MsgPkt, MSG_DATA_OFFSET_USER_NAME, "lvsenlv", 7);
//        MSG_SetStringData(&MsgPkt, MSG_DATA_OFFSET_PASSWORD, "linuxroot", 9);
        MsgPkt.cmd = MSG_CMD_USER_LOGIN;
        MSG_SetStringData(&MsgPkt, MSG_DATA_OFFSET_USER_NAME, "admin01", 7);
        MSG_SetStringData(&MsgPkt, MSG_DATA_OFFSET_PASSWORD, "admin01", 7);
        
        SendDataLength = write(ClientSocketFd, &MsgPkt, sizeof(MsgPkt_t));
        if(sizeof(MsgPkt_t) != SendDataLength)
        {
            printf("write(): %s\n", strerror(errno));
            return -1;
        }
        
        if(STAT_OK != GetResponse(&MsgPkt, ClientSocketFd, 1))
            continue;
        
        code = MSG_Get32BitData(&MsgPkt, MSG_DATA_OFFSET_CC);
        if(CC_NORMAL != code)
        {
            printf("%s\n", GetErrorDetails(code));
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

            SendDataLength = write(ClientSocketFd, (char *)&MsgPkt, sizeof(MsgPkt_t));
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

        if(STAT_OK != GetResponse(&MsgPkt, ClientSocketFd, 1))
        {
            printf("Server no response\n");
            continue;
        }
            
        code = MSG_Get32BitData(&MsgPkt, MSG_DATA_OFFSET_CC);
        if(CC_NORMAL != code)
            printf("%s\n", GetErrorDetails(code));
        else
            printf("Success\n");
    }

    return 0;
}

G_STATUS FillMsgPkt(MsgPkt_t *pMsgPkt, char choice)
{
    memset(pMsgPkt, 0, sizeof(MsgPkt_t));
    pMsgPkt->CCFlag = 1;
    pMsgPkt->fd = g_ResFd;

    switch(choice)
    {
        case '1':
            pMsgPkt->cmd = MSG_CMD_ROOT_ADD_ADMIN;
            MSG_Set64BitData(pMsgPkt, MSG_DATA_OFFSET_USER_ID, 0x1314);
            MSG_SetStringData(pMsgPkt, MSG_DATA_OFFSET_USER_NAME, "lvsenlv", 7);
            MSG_SetStringData(pMsgPkt, MSG_DATA_OFFSET_PASSWORD, "linuxroot", 9);
            MSG_SetStringData(pMsgPkt, MSG_DATA_OFFSET_ADD_USER_NAME, "admin01", 7);
            MSG_SetStringData(pMsgPkt, MSG_DATA_OFFSET_ADD_PASSWORD, "admin01", 7);
            break;
        case '2':
            pMsgPkt->cmd = MSG_CMD_ROOT_DEL_ADMIN;
            MSG_Set64BitData(pMsgPkt, MSG_DATA_OFFSET_USER_ID, 0x1314);
            MSG_SetStringData(pMsgPkt, MSG_DATA_OFFSET_USER_NAME, "lvsenlv", 7);
            MSG_SetStringData(pMsgPkt, MSG_DATA_OFFSET_PASSWORD, "linuxroot", 9);
            MSG_SetStringData(pMsgPkt, MSG_DATA_OFFSET_DEL_USER_NAME, "admin01", 7);
            break;
        case '3':
            pMsgPkt->cmd = MSG_CMD_ADMIN_ADD_USER;
            MSG_SetStringData(pMsgPkt, MSG_DATA_OFFSET_USER_NAME, "admin01", 7);
            MSG_SetStringData(pMsgPkt, MSG_DATA_OFFSET_PASSWORD, "admin01", 7);
            MSG_SetStringData(pMsgPkt, MSG_DATA_OFFSET_ADD_USER_NAME, "user01", 6);
            MSG_SetStringData(pMsgPkt, MSG_DATA_OFFSET_ADD_PASSWORD, "user01", 6);
            break;
        case 'q':
            pMsgPkt->cmd = MSG_CMD_USER_LOGOUT;
            MSG_Set64BitData(pMsgPkt, MSG_DATA_OFFSET_USER_ID, 0x1314);
            MSG_SetStringData(pMsgPkt, MSG_DATA_OFFSET_USER_NAME, "lvsenlv", 7);
            MSG_SetStringData(pMsgPkt, MSG_DATA_OFFSET_PASSWORD, "linuxroot", 9);
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

