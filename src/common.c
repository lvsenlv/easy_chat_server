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
#include <dirent.h>
#include <string.h>
#include <errno.h>

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

/*
 *  @Briefs: Search files with all format in pFolderName and delete it
 *  @Return: STAT_OK / STAT_ERR
 *  @Note:   None
 */
G_STATUS RemoveDirectory(const char *pFolderName)
{
    DIR *pDir;
    struct dirent *pEntry;
    char FileName[1024];
    stat_t FileInfo;
    
    pDir = opendir(pFolderName);
    if(NULL == pDir)
        return STAT_ERR;
    
    while(1)
    {
        pEntry = readdir(pDir);
        if(NULL == pEntry)
            break;

        //Ignore ".", ".."
        if('.' == pEntry->d_name[0])
        {
            if('\0' == pEntry->d_name[1])
                continue;
            else if(('.' == pEntry->d_name[1]) && ('\0' == pEntry->d_name[2]))
                continue;
        }
        
        snprintf(FileName, sizeof(FileName), "%s%c%s", pFolderName, DIR_DELIMITER, pEntry->d_name);
        FileName[sizeof(FileName)-1] = '\0';

        if(0 != GetFileInfo(FileName, &FileInfo))
            continue;
        
#ifdef __LINUX
        if(DT_DIR == pEntry->d_type)
#elif defined __WINDOWS
        if(FileInfo.st_mode & S_IFDIR)
#endif
        {
            RemoveDirectory(FileName);
            continue;
        }
#ifdef __LINUX
        else if(DT_REG == pEntry->d_type)
#elif defined __WINDOWS
        else if((FileInfo.st_mode & S_IFREG) || (FileInfo.st_mode & S_IFLNK))
#endif
        {
            unlink(FileName);
            continue;
        }
        else
        {
            unlink(FileName);
            continue;
        }
    }
    
    closedir(pDir);
    rmdir(pFolderName);
    
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
