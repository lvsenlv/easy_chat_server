/*************************************************************************
	> File Name: completion_code.c
	> Author: 
	> Mail: 
	> Created Time: 2018年02月07日 星期三 11时11分32秒
 ************************************************************************/

#include "completion_code.h"

char *g_CompletionCodeTable[CC_MAX];

void InitErrorCodeTable(void)
{
    g_CompletionCodeTable[CC_INVALID_FD]                = "Invalid fd value";
    g_CompletionCodeTable[CC_PERMISSION_DENIED]         = "Permission denied";
    g_CompletionCodeTable[CC_USER_HAS_BEEN_EXIST]       = "User has been exist";
    g_CompletionCodeTable[CC_USER_DOES_NOT_EXIST]       = "User does not exist";
    g_CompletionCodeTable[CC_USER_LIST_IS_FULL]         = "User list is full";
    g_CompletionCodeTable[CC_FAIL_TO_CREATE_USER_ID]    = "Fail to create user id";
    g_CompletionCodeTable[CC_FAIL_TO_OPEN]              = "Fail to open file";
    g_CompletionCodeTable[CC_FAIL_TO_WRITE]             = "Fail to write to file";
    g_CompletionCodeTable[CC_FAIL_TO_READ]              = "Fail to read from file";
    g_CompletionCodeTable[CC_FAIL_TO_UNLINK]            = "Fail to delete file";
    g_CompletionCodeTable[CC_FAIL_TO_MALLOC]            = "Fail to malloc";
    g_CompletionCodeTable[CC_PASSWORD_IS_TOO_SHORT]     = "Password is to short";
    g_CompletionCodeTable[CC_SESSION_IS_NOT_FOUND]      = "Session is not found";
}