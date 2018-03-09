/*************************************************************************
	> File Name: completion_code.h
	> Author: 
	> Mail: 
	> Created Time: 2018年02月07日 星期三 10时42分07秒
 ************************************************************************/

#ifndef __CC_H
#define __CC_H

typedef enum {
    CC_NORMAL = 0,

    CC_INVALID_FD,
    CC_PERMISSION_DENIED,
    CC_USER_HAS_BEEN_EXIST,
    CC_USER_DOES_NOT_EXIST,
    CC_USER_LIST_IS_FULL,
    CC_FAIL_TO_OPEN,
    CC_FAIL_TO_WRITE,
    CC_FAIL_TO_READ,
    CC_FAIL_TO_UNLINK,
    CC_FAIL_TO_MALLOC,
    CC_PASSWORD_IS_TOO_SHORT,
    CC_SESSION_IS_NOT_FOUND,
    CC_FAIL_TO_CLOSE_SESSION,
    
    CC_MAX,
}COMPLETION_CODE;

extern char *g_CompletionCodeTable[CC_MAX];

void InitErrorCodeTable(void);



static inline char *GetErrorDetails(COMPLETION_CODE code)
{
    if((0 >= code) || (CC_MAX <= code))
        return "Unknown error";
    
    return g_CompletionCodeTable[code];
}

#endif
