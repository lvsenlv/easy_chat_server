/*************************************************************************
	> File Name: log.c
	> Author: 
	> Mail: 
	> Created Time: 2018年02月01日 星期四 09时19分41秒
 ************************************************************************/

#include "log.h"
#include <time.h>
#include <pthread.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>

static G_STATUS LOG_InitLogLock(void);
static G_STATUS LOG_InitLogFile(const char *LogPath);

FILE *g_LogFileTbl[LOG_LEVEL_MAX];
pthread_mutex_t g_LogLockTbl[LOG_LEVEL_MAX];

/*
 *  @Briefs: Display info to LOG_SYSLOG_FILE
 *  @Return: STAT_OK / STAT_ERR
 *  @Note:   
 *         1. It could only invoked in when single thread is running
 *         2. If LOG_InitLog was successful to invoke, avoid using this function
 */
G_STATUS LOG_SysLog(char *pFormat, ...)
{
    va_list varg;
    int length;
    char buf[BUF_SIZE];
    FILE *fp;
    
    va_start(varg, pFormat);
    length = vsnprintf(buf, sizeof(buf), pFormat, varg);
    va_end(varg);

    if(0 >= length)
        return STAT_ERR;

    fp = fopen(LOG_SYSLOG_FILE, LOG_OPEN_FORMAT);
    if(NULL == fp)
        return STAT_ERR;

    LOG_DispLogTime(fp);

    fwrite(buf, 1, length, fp);

    fclose(fp);
    
    return STAT_OK;
}

G_STATUS LOG_InitLog(const char *LogPath)
{
    if(STAT_OK != LOG_InitLogLock())
        return STAT_ERR;
        
    if(STAT_OK != LOG_InitLogFile(LogPath))
        return STAT_ERR;

#ifndef __LOG_CLEAR
    if(STAT_OK != LOG_CheckLogFileSize())
        return STAT_ERR;
#endif
    
    return STAT_OK;
}

G_STATUS LOG_CloseLog(void)
{
    int i;

    for(i = 0; i < LOG_LEVEL_MAX; i++)
    {
        if(NULL != g_LogFileTbl[i])
        {
            fclose(g_LogFileTbl[i]);
            g_LogFileTbl[i] = NULL;
        }
    }

    return STAT_OK;
}

G_STATUS LOG_DispLogTime(FILE *fp)
{
    struct tm *TimeInfo;
    time_t ti;
    
    ti = time(NULL);
    TimeInfo = localtime(&ti);
    fprintf(fp, "[%4d-%02d-%02d %02d:%02d:%02d] ", TimeInfo->tm_year+1900, TimeInfo->tm_mon, 
        TimeInfo->tm_mday, TimeInfo->tm_hour, TimeInfo->tm_min, TimeInfo->tm_sec);
        
    return STAT_OK;
}

G_STATUS LOG_CheckLogFileSize(void)
{
    int i;
    int FileSize;
    int retry;
    int fd;
    
    for(i = 0; i < LOG_LEVEL_MAX; i++)
    {
        pthread_mutex_lock(&g_LogLockTbl[i]);
        
        FileSize = ftell(g_LogFileTbl[i]);
        if(LOG_FILE_MAX_SIZE > FileSize)
        {
            pthread_mutex_unlock(&g_LogLockTbl[i]);
            continue;
        }
        
        fd = fileno(g_LogFileTbl[i]);
        for(retry = 1; retry <= 3; retry++)
        {
            if(0 == ftruncate(fd, 0))
                break;
        }

        if(3 == retry)
        {
            pthread_mutex_unlock(&g_LogLockTbl[i]);
            return STAT_FATAL_ERR;
        }

        lseek(fd, 0, SEEK_SET);
        
        pthread_mutex_unlock(&g_LogLockTbl[i]);
    }
        
    return STAT_OK;
}

static G_STATUS LOG_InitLogLock(void)
{
    int i;

    for(i = 0; i < LOG_LEVEL_MAX; i++)
    {
        if(0 != pthread_mutex_init(&g_LogLockTbl[i], NULL))
        {
            LOG_SysLog("[LOG init lock] pthread_mutex_init(): %s\n", strerror(errno));
            return STAT_ERR;
        }
    }
    
    return STAT_OK;
}

static G_STATUS LOG_InitLogFile(const char *LogPath)
{
    if(0 != access(LogPath, F_OK))
    {
        if(0 != mkdir(LogPath, S_IRUSR | S_IWUSR))
        {
            LOG_SysLog("Fail to create log directory: %s\n", LogPath);
            return STAT_ERR;
        }
    }

    char buf[BUF_SIZE];
    FILE *fp;

    snprintf(buf, sizeof(buf), "%s/info.log", LogPath);
    if(STAT_OK != CreateFile(buf, 0600))
    {
        LOG_SysLog("Fail to create or open log file: %s\n", buf);
        return STAT_ERR;
    }
    
    fp = fopen(buf, LOG_OPEN_FORMAT);
    if(NULL == fp)
    {
        LOG_SysLog("Fail to create or open log file: %s\n", buf);
        return STAT_ERR;
    }
    //setvbuf(fp, NULL, _IONBF, 0);
    g_LogFileTbl[LOG_LEVEL_INFO] = fp;

    snprintf(buf, sizeof(buf), "%s/warning.log", LogPath);
    if(STAT_OK != CreateFile(buf, 0600))
    {
        LOG_SysLog("Fail to create or open log file: %s\n", buf);
        return STAT_ERR;
    }
        
    fp = fopen(buf, LOG_OPEN_FORMAT);
    if(NULL == fp)
    {
        LOG_SysLog("Fail to create or open log file: %s\n", buf);
        return STAT_ERR;
    }
    //setvbuf(fp, NULL, _IONBF, 0);
    g_LogFileTbl[LOG_LEVEL_WARNING] = fp;
    
    snprintf(buf, sizeof(buf), "%s/error.log", LogPath);
    if(STAT_OK != CreateFile(buf, 0600))
        return STAT_ERR;
    
    fp = fopen(buf, LOG_OPEN_FORMAT);
    if(NULL == fp)
    {
        LOG_SysLog("Fail to create or open log file: %s\n", buf);
        return STAT_ERR;
    }
    //setvbuf(fp, NULL, _IONBF, 0);
    g_LogFileTbl[LOG_LEVEL_ERROR] = fp;
    
    snprintf(buf, sizeof(buf), "%s/fatal_error.log", LogPath);
    if(STAT_OK != CreateFile(buf, 0600))
    {
        LOG_SysLog("Fail to create or open log file: %s\n", buf);
        return STAT_ERR;
    }
    
    fp = fopen(buf, LOG_OPEN_FORMAT);
    if(NULL == fp)
    {
        LOG_SysLog("Fail to create or open log file: %s\n", buf);
        return STAT_ERR;
    }
    //setvbuf(fp, NULL, _IONBF, 0);
    g_LogFileTbl[LOG_LEVEL_FATAL_ERROR] = fp;

#ifdef __DEBUG
    snprintf(buf, sizeof(buf), "%s/debug.log", LogPath);
    if(STAT_OK != CreateFile(buf, 0600))
    {
        LOG_SysLog("Fail to create or open log file: %s\n", buf);
        return STAT_ERR;
    }
    
    fp = fopen(buf, LOG_OPEN_FORMAT);
    if(NULL == fp)
    {
        LOG_SysLog("Fail to create or open log file: %s\n", buf);
        return STAT_ERR;
    }
    //setvbuf(fp, NULL, _IONBF, 0);
    g_LogFileTbl[LOG_LEVEL_DEBUG] = fp;
#endif

    return STAT_OK;
}