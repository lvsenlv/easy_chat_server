/*************************************************************************
	> File Name: main.c
	> Author: 
	> Mail: 
	> Created Time: 2018年01月29日 星期一 08时54分56秒
 ************************************************************************/

#include "message.h"
#include "completion_code.h"
#include "crc.h"
#include <errno.h>
#include <sys/socket.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <fcntl.h>

#define __ROOT_LOGIN
#ifdef __ROOT_LOGIN
#define LOGIN_USER_NAME                     "lvsenlv"
#define LOGIN_PASSWORD                      "linuxroot"
#define LOGIN_USER_ID                       4884
#else
#define LOGIN_USER_NAME                     "admin01"
#define LOGIN_PASSWORD                      "admin123"
#endif

G_STATUS FillMsgPkt(MsgPkt_t *pMsgPkt, char ch, _BOOL_ *pFlag);
G_STATUS GetResponse(MsgPkt_t *pMsgPkt, int fd, int timeout);
void DispHelpInfo(void);
G_STATUS GetFileFromServer(MsgPkt_t *pMsgPkt, int fd, int timeout);
 
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
    CRC32_InitTable();
    CRC16_InitTable();

    int ClientSocketFd = 0;
    struct sockaddr_in ClientSocketAddr;
    int res;
    fd_set ReadFds;
    int MaxFd = 0;
    char buf[BUF_SIZE] = {0};
    int SendDataLength;
    MsgPkt_t MsgPkt;
    MsgPkt_t ResMsgPkt;
    MsgDataVerifyIdentity_t *pVerifyData;
    MsgDataRes_t *pResMsgData;
    int retry;
    struct timeval tv;
    _BOOL_ flag;
    
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
    flag = FALSE;
    tv.tv_usec = 0;
    
    while(1)
    {
        while(1)
        {
            //fgets(buf, 2, stdin);
            FD_ZERO(&ReadFds);
            FD_SET(0, &ReadFds);
            tv.tv_sec = 9;
            
            res = select(1, &ReadFds, NULL, NULL,&tv);
            if(-1 == res)
            {
                perror("select:");
                return 0;
            }

            if(0 == res)
            {
                buf[0] = '0';
            }
            else
            {
                buf[0] = '\0';
                if(FD_ISSET(0, &ReadFds))
                {
                    read(STDIN_FILENO, buf, sizeof(buf));
                }
                
                if('\0' == buf[0])
                    continue;
            }
//            buf[0] = '7';
//            res = 1;

            if(STAT_OK != FillMsgPkt(&MsgPkt, buf[0], &flag))
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
        
        if(MSG_CMD_ROOT_DOWNLOAD_LOG == MsgPkt.cmd)
        {
            GetFileFromServer(NULL, ClientSocketFd, 3);
            continue;
        }

        if(0 == res)
            continue;

        if(TRUE != flag)
            continue;

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

G_STATUS FillMsgPkt(MsgPkt_t *pMsgPkt, char choice, _BOOL_ *pFlag)
{
    MsgDataAddUser_t *pMsgDataAddUser;
    MsgDataDelUser_t *pMsgDataDelUser;
    MsgDataRenameUser_t *pMsgDataRenameUser;
    MsgDataTransferFile_t *pMsgDataTransferFile;

    memset(pMsgPkt, 0, sizeof(MsgPkt_t));
    pMsgPkt->CCFlag = 1;
    pMsgPkt->fd = g_ResFd;
    *pFlag = FALSE;
    
    switch(choice)
    {
        case '0':
            pMsgPkt->cmd = MSG_CMD_DO_NOTHING;
            break;
        case '1':
#ifdef __ROOT_LOGIN
            *pFlag = TRUE;
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
            *pFlag = TRUE;
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
            *pFlag = TRUE;
            pMsgPkt->cmd = MSG_CMD_ADMIN_ADD_USER;
            pMsgDataAddUser = (MsgDataAddUser_t *)pMsgPkt->data;
            memcpy(pMsgDataAddUser->VerifyData.UserName, LOGIN_USER_NAME, sizeof(LOGIN_USER_NAME)-1);
            memcpy(pMsgDataAddUser->VerifyData.password, LOGIN_PASSWORD, sizeof(LOGIN_PASSWORD)-1);
            memcpy(pMsgDataAddUser->AddUserName, "user01", 6);
            memcpy(pMsgDataAddUser->AddPassword, "user123", 7);
            break;
        case '4':
            *pFlag = TRUE;
            pMsgPkt->cmd = MSG_CMD_ADMIN_DEL_USER;
            pMsgDataDelUser = (MsgDataDelUser_t *)pMsgPkt->data;
            memcpy(pMsgDataDelUser->VerifyData.UserName, LOGIN_USER_NAME, sizeof(LOGIN_USER_NAME)-1);
            memcpy(pMsgDataDelUser->VerifyData.password, LOGIN_PASSWORD, sizeof(LOGIN_PASSWORD)-1);
            memcpy(pMsgDataDelUser->DelUserName, "user01", 7);
            
            break;
        case '5':
#ifdef __ROOT_LOGIN
            *pFlag = TRUE;
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
        case '6':
#ifdef __ROOT_LOGIN
            pMsgPkt->cmd = MSG_CMD_ROOT_CLEAR_LOG;
            pMsgDataAddUser = (MsgDataAddUser_t *)pMsgPkt->data;
            pMsgDataAddUser->VerifyData.UserID = LOGIN_USER_ID;
            memcpy(pMsgDataAddUser->VerifyData.UserName, LOGIN_USER_NAME, sizeof(LOGIN_USER_NAME)-1);
            memcpy(pMsgDataAddUser->VerifyData.password, LOGIN_PASSWORD, sizeof(LOGIN_PASSWORD)-1);
#else
            pMsgPkt->cmd = MSG_CMD_DO_NOTHING;
            printf("No root login\n");
#endif
            break;
        case '7':
#ifdef __ROOT_LOGIN
            pMsgPkt->cmd = MSG_CMD_ROOT_DOWNLOAD_LOG;
            pMsgDataTransferFile = (MsgDataTransferFile_t *)pMsgPkt->data;
            pMsgDataTransferFile->VerifyData.UserID = LOGIN_USER_ID;
            memcpy(pMsgDataTransferFile->VerifyData.UserName, LOGIN_USER_NAME, sizeof(LOGIN_USER_NAME)-1);
            memcpy(pMsgDataTransferFile->VerifyData.password, LOGIN_PASSWORD, sizeof(LOGIN_PASSWORD)-1);
            memcpy(pMsgDataTransferFile->FileName, "info.log", 10);
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

void DispHelpInfo(void)
{
    printf("1: Add admin\n");
    printf("2: Del admin\n");
    printf("3: Add user\n");
    printf("4: Del user\n");
    printf("5: Rename user\n");
    printf("6: Clear log\n");
    printf("7: Download log\n");
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

G_STATUS GetFileFromServer(MsgPkt_t *pMsgPkt, int fd, int timeout)
{
    if(0 > timeout)
        return STAT_ERR;

    if(0 > fd)
        return STAT_ERR;
    
    struct timeval TimeInterval;
    fd_set fds;
    int res;
    int ReadDataLength;
    char buf[1024*1024+sizeof(MsgTransferPkt_t)];
    MsgTransferPkt_t *pMsgTransferPkt;
    char NewFileFd;
    uint32_t crc;

    TimeInterval.tv_usec = 0;
    pMsgTransferPkt = (MsgTransferPkt_t *)buf;
    
    NewFileFd = open("file.tmp", O_CREAT | O_RDWR, 0666);
    if(0 > NewFileFd)
        return STAT_ERR;

    while(1)
    {
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        TimeInterval.tv_sec = timeout;

        res = select(fd+1, &fds, NULL, NULL, &TimeInterval);
        if(0 > res)
        {
            printf("[Get file] Select function return a negative value\n");
            return STAT_ERR;
        }

        if(0 == res)
            break;
        
        if(!(FD_ISSET(fd, &fds)))
            return STAT_ERR;

        ReadDataLength = read(fd, buf, sizeof(MsgTransferPkt_t));
        if(0 >= ReadDataLength)
        {
            printf("[Get file] Invalid data length in msg queque, actual length: %d\n", ReadDataLength);
            return STAT_ERR;
        }

        printf("ReadDataLength=%d\n, cmd=%d\n", ReadDataLength, pMsgTransferPkt->cmd);

        if(MSG_TRANSFER_END == pMsgTransferPkt->cmd)
            break;

        if(MSG_TRANSFER_START == pMsgTransferPkt->cmd)
        {
            crc = CRC16_calculate(&pMsgTransferPkt->size, sizeof(pMsgTransferPkt->size));
            if(crc != pMsgTransferPkt->CheckCode)
            {
                res = -1;
                printf("CRC verify error, crc=0x%x, crc=0x%x\n", crc, pMsgTransferPkt->CheckCode);
                printf("size=%ld\n", pMsgTransferPkt->size);
                break;
            }

            printf("CRC verify success, start to recieve file ......\n");
            continue;
        }

        if(MSG_TRANSFER_DATA != pMsgTransferPkt->cmd)
        {
            res = -1;
            printf("Unknown error");
            break;
        }

        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        TimeInterval.tv_sec = timeout;

        res = select(fd+1, &fds, NULL, NULL, &TimeInterval);
        if(0 > res)
        {
            printf("[Get file] Select function return a negative value\n");
            return STAT_ERR;
        }
        
        if(0 > res)
        {
            printf("[Get file] Select function return a negative value\n");
            return STAT_ERR;
        }

        if(0 == res)
            break;
        
        if(!(FD_ISSET(fd, &fds)))
            return STAT_ERR;

        ReadDataLength = read(fd, buf+sizeof(MsgTransferPkt_t), pMsgTransferPkt->size);
        if(0 >= ReadDataLength)
        {
            printf("[Get file] Invalid data length in msg queque, actual length: %d\n", ReadDataLength);
            return STAT_ERR;
        }
        
        printf("ReadDataLength=%d\n", ReadDataLength);

        crc = CRC32_calculate(buf+sizeof(MsgTransferPkt_t), pMsgTransferPkt->size);
        if(crc != pMsgTransferPkt->CheckCode)
        {
            res = -1;
            printf("CRC verify error, crc=0x%x, crc=0x%x\n", crc, pMsgTransferPkt->CheckCode);
            break;
        }

        write(NewFileFd, buf+sizeof(MsgTransferPkt_t), pMsgTransferPkt->size);
    }

    close(NewFileFd);
    
    return STAT_OK;
}
