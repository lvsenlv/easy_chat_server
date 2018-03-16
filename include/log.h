/*************************************************************************
	> File Name: log.h
	> Author: 
	> Mail: 
	> Created Time: 2018年02月01日 星期四 09时19分48秒
 ************************************************************************/

#ifndef __LOG_H
#define __LOG_H

#include "common.h"
#include <unistd.h>

#define LOG_PATH                            "/var/log/easy_chat"
#define LOG_SYSLOG_FILE                     "/var/log/easy_chat_syslog"
#define LOG_MONITOR_TIME_INTERVAL           10 //Unit: second
#define LOG_FILE_MAX_SIZE                   (1024*10) //10Mb
#ifdef __LOG_CLEAR
#define LOG_OPEN_FORMAT                     "w+"
#else
#define LOG_OPEN_FORMAT                     "a+"
#endif

#define LOG_CheckSize(fp) \
        do \
        { \
            if(LOG_FILE_MAX_SIZE <= ftell(fp)) \
            { \
                ftruncate(fileno(fp), 0); \
            } \
        }while(0)

#define LOG_INFO(format, args...) \
        do \
        { \
            pthread_mutex_lock(&g_LogLockTbl[LOG_LEVEL_INFO]); \
            LOG_DispLogTime(g_LogFileTbl[LOG_LEVEL_INFO]); \
            fprintf(g_LogFileTbl[LOG_LEVEL_INFO], format, ##args); \
            fflush(g_LogFileTbl[LOG_LEVEL_INFO]); \
            pthread_mutex_unlock(&g_LogLockTbl[LOG_LEVEL_INFO]); \
        }while(0)

#define LOG_WARNING(format, args...) \
        do \
        { \
            pthread_mutex_lock(&g_LogLockTbl[LOG_LEVEL_WARNING]); \
            LOG_DispLogTime(g_LogFileTbl[LOG_LEVEL_WARNING]); \
            fprintf(g_LogFileTbl[LOG_LEVEL_WARNING], format, ##args); \
            fflush(g_LogFileTbl[LOG_LEVEL_WARNING]); \
            pthread_mutex_unlock(&g_LogLockTbl[LOG_LEVEL_WARNING]); \
        }while(0)
        
#define LOG_ERROR(format, args...) \
        do \
        { \
            pthread_mutex_lock(&g_LogLockTbl[LOG_LEVEL_ERROR]); \
            LOG_DispLogTime(g_LogFileTbl[LOG_LEVEL_ERROR]); \
            fprintf(g_LogFileTbl[LOG_LEVEL_ERROR], format, ##args); \
            fflush(g_LogFileTbl[LOG_LEVEL_ERROR]); \
            pthread_mutex_unlock(&g_LogLockTbl[LOG_LEVEL_ERROR]); \
        }while(0)
        
#define LOG_FATAL_ERROR(format, args...) \
        do \
        { \
            pthread_mutex_lock(&g_LogLockTbl[LOG_LEVEL_FATAL_ERROR]); \
            LOG_DispLogTime(g_LogFileTbl[LOG_LEVEL_FATAL_ERROR]); \
            fprintf(g_LogFileTbl[LOG_LEVEL_FATAL_ERROR], format, ##args); \
            fflush(g_LogFileTbl[LOG_LEVEL_FATAL_ERROR]); \
            pthread_mutex_unlock(&g_LogLockTbl[LOG_LEVEL_FATAL_ERROR]); \
        }while(0)

#ifdef __DEBUG
#define LOG_DEBUG(format, args...) \
        do \
        { \
            pthread_mutex_lock(&g_LogLockTbl[LOG_LEVEL_DEBUG]); \
            LOG_DispLogTime(g_LogFileTbl[LOG_LEVEL_DEBUG]); \
            fprintf(g_LogFileTbl[LOG_LEVEL_DEBUG], format, ##args); \
            fflush(g_LogFileTbl[LOG_LEVEL_DEBUG]); \
            pthread_mutex_unlock(&g_LogLockTbl[LOG_LEVEL_DEBUG]); \
        }while(0)
#else
#define LOG_DEBUG(format, args...)          ((void)0)
#endif
            
typedef enum {
    LOG_LEVEL_INFO = 0,
    LOG_LEVEL_WARNING,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_FATAL_ERROR,
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_MAX,
}LOG_LEVEL;

extern FILE *g_LogFileTbl[LOG_LEVEL_MAX];
extern pthread_mutex_t g_LogLockTbl[LOG_LEVEL_MAX];

G_STATUS LOG_SysLog(char *pFormat, ...) PRINTF_FORMAT;
G_STATUS LOG_InitLog(const char *LogPath);
G_STATUS LOG_CloseLog(void);
G_STATUS LOG_DispLogTime(FILE *fp);
G_STATUS LOG_CheckLogFileSize(void);

#endif
