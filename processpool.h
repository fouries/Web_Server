// filename: processpool.h
#ifndef PROCESSPOOL_H
#define PROCESSPOOL_H

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>

/* 描述一个子进程的类，m_pid 是目标子进程的 PID，m_pipefd 是父进程和子进程通信用的管道 */
class process
{
public:
    process() : m_pid(-1) {}
public:
    pid_t m_pid;
    int m_pipefd[2];
};

/* 进程池类，将它定义为模板是为了代码复用。其模板参数是处理逻辑任务的类 */
template< typename T >
class processpool
{
private:
    /* 将构造函数定义为私有的，因此我们只能通过后面的 creat静态函数来创建processpool 实例 */
    processpool(int listenfd, int process_number = 8);
public:
    /* 单体模式，以保证程序最多创建一个 processpool实例，这是程序正确处理信号的必要条件 */
    static processpool<T>* create(int listenfd, int process_number = 8) 
    {
        if( !m_instance )
        {
            m_instance = new processpool<T> (listenfd, process_number);
        }
        return m_instance;
    }
    ~processpool()
    {
        delete [] m_sub_process;
    }
    /* 启动进程池 */
    void run();

private:
    void setup_sig_pipe();
    void run_parent();
    void run_child();

private:
    /* 进程池允许的最大子进程数量 */
    static const int MAX_PROCESS_NUMBER = 16;
    /* 每个子进程最多能处理的客户数量 */
    static const int USER_PER_PROCESS = 65536;
    /* epoll最多能处理的事件数 */
    static const int MAX_EVENT_NUMBER = 10000;
    /* 进程池中的进程总数 */
    int m_process_number;
    /* 子进程在池中的序号，从 0开始 */
    int m_idx;
    /* 每个进程都有一个 epoll内核时间表，用 m_epollfd标识 */
    int m_epollfd;
    /* 监听 socket */
    int m_listenfd;
    /* 子进程通过 m_stop来决定是否停止运行 */
    int m_stop;
    /* 保存所有进程的描述信息 */
    process* m_sub_process;
    /* 进程池静态实例 */
    static processpool<T>* m_instance;
};

template <typename T>
processpool<T>* processpool<T>::m_instance = NULL;

/* 用于处理信号的管程，以实现统一事件源。后面称之为信号管道 */
static int sig_pipefd[2];

static int setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

static void addfd(int epollfd, int fd) 
{
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

/* 从 epollfd标识的 epoll内核事件表中删除 fd上的所有注册事件 */
static void removed(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

static void sig_handler(int sig)
{
    int save_errno = errno;
    int msg = sig;
    send(sig_pipefd[1], (char*)&msg, 1, 0);
    errno = save_errno;
}

static void addsig(int sig, void(handler)(int), bool restart = true) 
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if(restart)
    {
        sa.sa_flags |= SA_RESTART;
    }
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

/*  */

#endif