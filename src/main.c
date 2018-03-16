/*************************************************************************
	> File Name: main.c
	> Author: 
	> Mail: 
	> Created Time: 2018年01月29日 星期一 08时54分56秒
 ************************************************************************/

#include "server.h"
#include "log.h"
#include "message.h"
#include "completion_code.h"
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <sys/param.h>
#include <string.h>
#include <errno.h>
#include <sys/wait.h>

#define DAEMON_LOG_FILE                     "/var/log/easy_chat_daemon"
#define DAEMON_QUEUE                        "/var/easy_chat_queque"
#define MONITOR_SERVER_TIME_INTERVAL        5 //Unit: second
#define PROCESS_KEEP_ALIVE_TIME_INTERVAL    5
#define PROCESS_HANDSHAKE_SYMBOL            'Y'

FILE *g_DaemonLogFp;

#define DAEMON_LOG(format, args...) \
        do \
        { \
            LOG_DispLogTime(g_DaemonLogFp); \
            fprintf(g_DaemonLogFp, "[Daemon] "); \
            fprintf(g_DaemonLogFp, format, ##args); \
            fflush(g_DaemonLogFp); \
        }while(0)

#define DAEMON_EXIT() \
        do \
        { \
            DAEMON_LOG("daemon exit\n"); \
            exit(0); \
        }while(0)

void CHLD_SignalHandle(int SignalNum);
void PIPE_SignalHandle(int SignalNum);
static G_STATUS MonitorServerProcess(int ChildProcessID, int *pServerStartCount);
static G_STATUS ServerMain(int fd);
static inline G_STATUS KeepAlive(int fd);

int main(int argc, char **argv)
{
    int pid;
    int i;
    int ServerStartCount;
    int fd;
    
    signal(SIGTTOU, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGHUP,  SIG_IGN);
    //signal(SIGCHLD, SIG_IGN);
    
    pid = fork();
    if(0 < pid)
    {
        exit(0);
    }
    else if(0 > pid)
    {
        DISP_ERR("Fail to create daemon\n");
        return -1;
    }
    
    DISP("[Daemon] Start success, pid: %d\n", getpid());
    DISP("[Daemon] The log file locate in %s\n", DAEMON_LOG_FILE);
    
    setsid();
    chdir("/");
    umask(0);
    
    for(i = 0; i < NOFILE; i++);
    {
        close(i);
    }
    
    if(STAT_OK != CreateFile(DAEMON_LOG_FILE, 0600))
    {
        DISP_ERR("[Daemon] Fail to create log file: %s\n", DAEMON_LOG_FILE);
        return -1;
    }

    if(LOG_FILE_MAX_SIZE <= GetFileSize(DAEMON_LOG_FILE))
    {
        g_DaemonLogFp = fopen(DAEMON_LOG_FILE, "w+");
    }
    else
    {
#ifdef __LOG_CLEAR
        g_DaemonLogFp = fopen(DAEMON_LOG_FILE, "w+");
#else
        g_DaemonLogFp = fopen(DAEMON_LOG_FILE, "a+");
#endif
    }

    if(0 > g_DaemonLogFp)
    {
        DISP_ERR("[Daemon] Fail to open log file: %s\n", DAEMON_LOG_FILE);
        return -1;
    }
    
    DAEMON_LOG("Start success, pid: %d\n", getpid());
    DAEMON_LOG("The log file locate in %s\n", DAEMON_LOG_FILE);

    if(0 == access(DAEMON_QUEUE, F_OK))
    {
        if(0 != unlink(DAEMON_QUEUE))
        {
            fclose(g_DaemonLogFp);
            DISP_ERR("[Daemon] Fail to delete previous queue: %s\n", DAEMON_QUEUE);
            return -1;
        }
    }
        
    if(0 != (mkfifo(DAEMON_QUEUE, 0600)))
    {
        fclose(g_DaemonLogFp);
        DISP_ERR("[Daemon] Fail to create fifo: %s\n", DAEMON_QUEUE);
        return -1;
    }
    
    ServerStartCount = 0;

    while(3 > ServerStartCount)
    {
        pid = fork();
        if(0 < pid)
        {
            signal(SIGCHLD, CHLD_SignalHandle);

            if(0 == ServerStartCount)
            {
                DISP("[Server process] Start success, pid: %d\n", pid);
                DISP("[Server process] Log files locate in %s\n", LOG_PATH);
            }
            
            if(STAT_OK != MonitorServerProcess(pid, &ServerStartCount))
                DAEMON_EXIT();

            ServerStartCount++;
            sleep(1); //Avoid restart process too fast
            
            continue;
        }
        else if(0 > pid)
        {
            DAEMON_LOG("Fail to start child process\n");
            return -1;
        }
        
        signal(SIGTTOU, SIG_IGN);
        signal(SIGTTIN, SIG_IGN);
        signal(SIGTSTP, SIG_IGN);
        signal(SIGHUP,  SIG_IGN);
        signal(SIGCHLD, SIG_IGN);
        signal(SIGPIPE, PIPE_SignalHandle);
        //signal(SIGPIPE, SIG_IGN);
        
        setsid();
        chdir("/");
        umask(0);
        
        LOG_SysLog("Server process start success, pid: %d\n", getpid());
        LOG_SysLog("Server process log files locate in %s\n", LOG_PATH);
        DAEMON_LOG("Server process start success, pid: %d\n", getpid());
        DAEMON_LOG("Server process log files locate in %s\n", LOG_PATH);
        
        for(i = 0; i < NOFILE; i++);
        {
            close(i);
        }
        
        fd = open(DAEMON_QUEUE, O_RDWR);
        if(0 > fd)
        {
            LOG_SysLog("Fail to open daemon queue: %s\n", DAEMON_QUEUE);
            exit(0);
        }

        ServerMain(fd);
        close(fd);
        exit(0);
    }

    if(3 == ServerStartCount)
    {
        DAEMON_LOG("It is still failed after 3 times of consecutive restarting\n");
        DAEMON_EXIT();
    }

    return 0;
}

void CHLD_SignalHandle(int SignalNum)
{
    DAEMON_LOG("Catch child process crash signal\n");
    if(-1 == wait(NULL)) //Avoid zombie process
    {
        DAEMON_LOG("wait(): %s\n", strerror(errno));
        DAEMON_EXIT();
    }
}

void PIPE_SignalHandle(int SignalNum)
{
    LOG_ERROR("[Signal pipe] "
        "%s(error code: %d)\n", strerror(errno), errno);
}

void QUIT_SignalHandle(int SignalNum)
{
    pthread_exit(0);
}

static G_STATUS MonitorServerProcess(int ChildProcessID, int *pServerStartCount)
{
    int fd;
    struct timeval TimeInterval;
    fd_set fds;
    int res;
    int ReadDataLength;
    char ProcessHandshakeSymbol;

    fd = open(DAEMON_QUEUE, O_RDWR);
    if(0 > fd)
    {
        DAEMON_LOG("Fail to open fifo: %s\n", DAEMON_QUEUE);
        return STAT_ERR;
    }

    TimeInterval.tv_usec = 0;
    
    while(1)
    {
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        TimeInterval.tv_sec = MONITOR_SERVER_TIME_INTERVAL;
        
        res = select(fd+1, &fds, NULL, NULL, &TimeInterval);
        if(0 > res)
        {
            DAEMON_LOG("select(): error, child process may be killed\n");
            continue;
        }
        else if(0 == res)
        {
            DAEMON_LOG("Detect child process is no response, send kill signal to child process\n");
            
            if(0 == kill(ChildProcessID, 0)) //If child process is alive
            {
                 /*
                     If child process is still alive, kill the child process
                     The child process is alive but it was blocked
                 */
                if(0 != kill(ChildProcessID, SIGKILL))
                {
                    close(fd);
                    DAEMON_LOG("Fail to kill child process, child pid: %d\n", ChildProcessID);
                    return STAT_ERR;
                }
            }

            break;
        }
        
        if(!(FD_ISSET(fd, &fds)))
            continue;

        ReadDataLength = read(fd, &ProcessHandshakeSymbol, 1);
        if(0 >= ReadDataLength)
            break;

        if(PROCESS_HANDSHAKE_SYMBOL != ProcessHandshakeSymbol)
            break;
        
        *pServerStartCount = 0; //Clear server start count
    }
    
    close(fd);

    return STAT_OK;
}

static G_STATUS ServerMain(int fd)
{
    int TimeCount;
    int retry;
    int res;
    pthread_t MsgTaskID;
    pthread_t ServerTaskID;
    int MsgTaskLastRestartTime;
    int MsgTaskConsecutiveRestartCount;
    int ServerTaskLastRestartTime;
    int ServerTaskConsecutiveRestartCount;

    InitErrorCodeTable();
    signal(SIGQUIT, QUIT_SignalHandle);
    
    if(STAT_OK != LOG_InitLog(LOG_PATH))
        return STAT_ERR;

    if(STAT_OK != MSG_CreateTask(&MsgTaskID))
        return STAT_ERR;

    if(STAT_OK != SERVER_CreateTask(&ServerTaskID))
        return STAT_ERR;

    TimeCount = 0;
    MsgTaskLastRestartTime = 0;
    MsgTaskConsecutiveRestartCount = 0;
    ServerTaskLastRestartTime = 0;
    ServerTaskConsecutiveRestartCount = 0;
    
    while(1)
    {
        sleep(1);
        TimeCount++;

        if(0 == (TimeCount % PROCESS_KEEP_ALIVE_TIME_INTERVAL))
        {
            if(STAT_OK != KeepAlive(fd))
            {
                LOG_FATAL_ERROR("[Main task] Fail to keep alive, process exit\n");
                return STAT_ERR;
            }
        }
        
        if(0 == (TimeCount % (MSG_SELECT_TIME_INTERVAL+2)))
        {
            if(0 == g_MsgTaskLiveFlag)
            {
                LOG_FATAL_ERROR("[Main task] Detect msg task no response, restart msg task ...\n");
                if(MSG_TASK_RESTART_TIME_INTERVAL >= (TimeCount-MsgTaskLastRestartTime))
                {
                    MsgTaskConsecutiveRestartCount++;
                }
                else if((MSG_TASK_RESTART_TIME_INTERVAL*2) < (TimeCount-MsgTaskLastRestartTime))
                {
                    MsgTaskConsecutiveRestartCount = 0;
                }

                if(3 < MsgTaskConsecutiveRestartCount)
                {
                    LOG_FATAL_ERROR("[Main task] "
                        "Detect msg task restart over 3 times in a short time, process exit\n");
                    return STAT_ERR;
                }
                
                for(retry = 1; retry <= 3; retry++)
                {
                    res = pthread_kill(MsgTaskID, SIGQUIT);
                    if(res == ESRCH)
                        break;
                    
                    usleep(500*1000);
                }

                if(3 < retry)
                {
                    LOG_FATAL_ERROR("[Main task] Fail to kill msg task, process exit\n");
                    return STAT_ERR;
                }
                
                if(STAT_OK != MSG_CreateTask(&MsgTaskID))
                    return STAT_ERR;

                MsgTaskLastRestartTime = TimeCount;
                LOG_FATAL_ERROR("[Main task] success retart msg task\n");
            }

            g_MsgTaskLiveFlag = 0;
        }

        if(0 == (TimeCount % (SERVER_SELECT_TIME_INTERVAL+2)))
        {
            if(0 == g_ServerLiveFlag)
            {
                LOG_FATAL_ERROR("[Main task] Detect server task no response, restart server task ...\n");
                if(SERVER_TASK_RESTART_TIME_INTERVAL >= (TimeCount-ServerTaskLastRestartTime))
                {
                    ServerTaskConsecutiveRestartCount++;
                }
                else if((SERVER_TASK_RESTART_TIME_INTERVAL*2) < (TimeCount-ServerTaskLastRestartTime))
                {
                    ServerTaskConsecutiveRestartCount = 0;
                }

                if(3 < ServerTaskConsecutiveRestartCount)
                {
                    LOG_FATAL_ERROR("[Main task] "
                        "Detect server task restart over 3 times in a short time, process exit\n");
                    return STAT_ERR;
                }
                
                for(retry = 1; retry <= 3; retry++)
                {
                    res = pthread_kill(ServerTaskID, SIGQUIT);
                    if(res == ESRCH)
                        break;
                    
                    usleep(500*1000);
                }

                if(3 < retry)
                {
                    LOG_FATAL_ERROR("[Main task] Fail to kill server task, process exit\n");
                    return STAT_ERR;
                }

                if(0 < g_ServerSocketFd)
                {
                    close(g_ServerSocketFd);
                    g_ServerSocketFd = -1;
                }
                
                if(STAT_OK != SERVER_CreateTask(&ServerTaskID))
                    return STAT_ERR;

                ServerTaskLastRestartTime = TimeCount;
                LOG_FATAL_ERROR("[Main task] success retart server task\n");
            }

            g_ServerLiveFlag = 0;
        }

        if(0 == (TimeCount % LOG_MONITOR_TIME_INTERVAL))
        {
            if(STAT_OK != LOG_CheckLogFileSize())
            {
                LOG_FATAL_ERROR("[Main task] Fail to check log file size, process exit\n");
                return STAT_ERR;
            }
        }
    }
    
    return STAT_OK;
}

static inline G_STATUS KeepAlive(int fd)
{
    int WriteDataLength;
    char *pHandshakeSymbol = "Y";
    
    WriteDataLength = write(fd, pHandshakeSymbol, 1);
    if(1 != WriteDataLength)
        return STAT_ERR;

    return STAT_OK;
}
