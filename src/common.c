/*************************************************************************
	> File Name: common.c
	> Author: lvsenlv
	> Mail: lvsen46000@163.com
	> Created Time: March 9th,2017 Thursday 14:47:06
 ************************************************************************/

#include "common.h"
#include <stdarg.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>

#ifndef __DISABLE_COMPILE_INFO
    #ifdef __LINUX
        #pragma message("Activate __LINUX")
    #else
        #error "You must assign the platform as __LINUX"
    #endif
    
    #ifdef __32BIT
        #pragma message("Activate __32BIT")
    #elif defined __64BIT
        #pragma message("Activate __64BIT")
    #else
        #error "You must assign the platform as __32BIT or __64BIT"
    #endif //__LINUX
    
    #ifdef __DEBUG
        #pragma message("Activate __DEBUG")
    #endif //__DEBUG
    
    #ifdef __REDIRECTION
        #pragma message("Activate __REDIRECTION")
    #endif //__REDIRECTION
#endif //__DISABLE_COMPILE_INFO

/******************************Modifiable*******************************/
void __attribute__((constructor)) BeforeMain(void)
{
    (void)0;
}

void __attribute__((destructor)) AfterMain(void)
{
    (void)0;
}

G_STATUS CreateFile(const char *pFileName, mode_t mode)
{
    int fd;
    fd = open(pFileName, O_CREAT, mode);
    if(0 > fd)
        return STAT_ERR;

    close(fd);
    return STAT_OK;
}

int GetFileSize(const char *pFileName)
{
    stat_t FileInfo;

    if(0 != GetFileInfo(pFileName, &FileInfo))
        return 0;

    return FileInfo.st_size;
}

void ExitDaemon(void)
{
    exit(0);
}
