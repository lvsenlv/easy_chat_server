/*************************************************************************
	> File Name: common.h
	> Author: lvsenlv
	> Mail: lvsen46000@163.com
	> Created Time: March 6th,2017 Monday 13:42:52
 ************************************************************************/

#ifndef __COMMON_H
#define __COMMON_H

#ifdef __LINUX
#define _FILE_OFFSET_BITS 64 //Make sure st_size is 64bits instead of 32bits
#endif

#include <stdio.h>
#include <stdlib.h>

/******************************Modifiable*******************************/
#define     BUF_SIZE                            1024
#define     SMALL_BUF_SIZE                      256
#define     IP_ADDR_MAX_LENGTH                  16      //Count in '\0'
#define     USER_NAME_MAX_LENGTH                16      //Count in '\0'
#define     PASSWORD_MAX_LENGTH                 16      //Count in '\0'
#define     PASSWORD_MIN_LENGTH                 6

//#include <stdint.h>
//typedef     char                                int8_t;
typedef     short                               int16_t;
typedef     int                                 int32_t;
typedef     unsigned char                       uint8_t;
typedef     unsigned short                      uint16_t;
typedef     unsigned int                        uint32_t;
#ifdef __LINUX
#ifdef __32BIT
typedef     long long                           int64_t;
typedef     unsigned long long                  uint64_t;
#elif defined __64BIT
typedef     long                                int64_t;
typedef     unsigned long                       uint64_t;
#endif
#elif defined __WINDOWS
typedef     long long                           int64_t;
typedef     unsigned long long                  uint64_t;
#endif

#include <sys/stat.h>

#ifdef __LINUX
#define     GetFileInfo(name, info)             lstat(name, info)
typedef struct stat                             stat_t;
#elif defined __WINDOWS
#define     GetFileInfo(name, info)             _stati64(name, info)
typedef struct _stati64                         stat_t;
#endif

#ifdef __LINUX
#define ENABLE_DISP_COLOR
#endif

#ifdef ENABLE_DISP_COLOR
#define     DISP(format, args...) \
            do \
            { \
                fprintf(stdout, format, ##args); \
            }while(0) 
            
#define     DISP_ERR(format, args...) \
            do \
            { \
                fprintf(stderr, "\033[01;31mFatal error: "); \
                fprintf(stderr, format, ##args); \
                fprintf(stderr, "\033[0m"); \
            }while(0)
            
#define     DISP_WARNING(format, args...) \
            do \
            { \
                fprintf(stderr, "\033[01;33mWarning: "); \
                fprintf(stderr, format, ##args); \
                fprintf(stderr, "\033[0m"); \
            }while(0) 
#else
#define     DISP(format, args...) \
            do \
            { \
                fprintf(stdout, format, ##args); \
            }while(0) 
            
#define     DISP_ERR(format, args...) \
            do \
            { \
                fprintf(stderr, format, ##args); \
            }while(0)
            
#define     DISP_WARNING(format, args...) \
            do \
            { \
                fprintf(stderr, format, ##args); \
            }while(0) 
#endif

/************************************************************************/

#define     __I                                 volatile const
#define     __O                                 volatile
#define     __IO                                volatile
#define     ALIGN_4K                            __attribute__((aligned(4)))
#define     PRINTF_FORMAT                       __attribute__((__format__ (__printf__, 1, 2)))

typedef enum {
    FALSE = 0,
    TRUE = !FALSE,
}_BOOL_; //Name as _bool_ to differ bool and _bool defined in curses.h on WINDOWS platform

typedef enum {
    STAT_OK = 0,
    STAT_ERR,
    
    STAT_FATAL_ERR,
}ALIGN_4K G_STATUS;

typedef struct OptInfoStruct{
    char LanguageFlag;
    char IPAddrFlag;
    char UserNameFlag;
    char PasswordFlag;
}OptInfo_t;

typedef struct ArgInfoStruct{
    char IPAddr[IP_ADDR_MAX_LENGTH];
    char UserName[USER_NAME_MAX_LENGTH];
    char password[PASSWORD_MAX_LENGTH];
}ArgInfo_t;

G_STATUS CreateFile(const char *pFileName, mode_t mode);
int GetFileSize(const char *pFileName);
void ExitDaemon(void);

#endif
