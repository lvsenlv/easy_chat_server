/*************************************************************************
	> File Name: main.c
	> Author: 
	> Mail: 
	> Created Time: 2018年02月11日 星期日 15时00分24秒
 ************************************************************************/

#include "common.h"
#include <fcntl.h>
#include <unistd.h>

int main(void)
{
    char *FileName = "/root/.easy_chat/user_list/admin01";
    int fd;
    uint64_t UserID;
    char password[PASSWORD_MAX_LENGTH];
    int ReadDataLength;

    fd = open(FileName, O_RDONLY);
    if(0 > fd)
    {
        printf("Fail to open file: %s\n", FileName);
        return -1;
    }

    ReadDataLength = read(fd, &UserID, sizeof(uint64_t));
    if(sizeof(uint64_t) != ReadDataLength)
    {
        printf("Fail to read, actual reading data length: %d\n", ReadDataLength);
        close(fd);
        return -1;
    }

    ReadDataLength = read(fd, password, PASSWORD_MAX_LENGTH);
    if(PASSWORD_MIN_LENGTH > ReadDataLength)
    {
        printf("Invalid format in file: %s\n", FileName);
        close(fd);
        return -1;
    }

    printf("UserID = 0x%lx\n", UserID);
    printf("Password = %s\n", password);

    close(fd);

    return 0;
}
