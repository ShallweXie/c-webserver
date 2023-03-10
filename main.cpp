#include <iostream>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/signal.h>
#include "locker.h"
#include "threadpool.h"
#include "http_connection.h"


#define MAX_CLIENT_NUM 65535
#define MAX_LISTEN_NUM 10000

using namespace std;

// 添加信号捕捉器，捕捉sig并用handFunc处理
void addsig(int sig, void(*handFunc)(int)){
    struct sigaction sa;
    sa.sa_flags = 0 ;  // 0 表示 使用sa_handler处理信号
    sigfillset(&sa.sa_mask);// 临时阻塞所有信号
    sa.sa_handler = handFunc;
    sigaction(sig, &sa, NULL);
}
extern void addEpollEvent(int epfd, int fd, int oneshot);
extern void delEpollEvent(int epfd, int fd);
extern void modifyEpollEvent(int epfd, int fd, int eve);

int main(int argc, char * argv[]){
    if(argc <= 1){
        cout << "输入格式为：" << argv[0] << " port_num" << endl;
        exit(-1);
    }
    int port = atoi(argv[1]);

    // 对SIGPIPE信号进行捕捉处理
    addsig(SIGPIPE, SIG_IGN);
    // 创建线程池
    threadpool<http_connection> * thdpool = nullptr;
    try{
        thdpool = new threadpool<http_connection>;
    } catch(...){
        exit(-1);
    }
    // 创建一个http_conn 数组来保存所有的客户端信息
    http_connection * clients = new http_connection[MAX_CLIENT_NUM];

    // 监听连接请求的套接字
    int lsfd = socket(AF_INET, SOCK_STREAM, 0); // HTTP
    // 设置端口复用
    int reuse = 1; // 1 表示允许复用
    setsockopt(lsfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof reuse);
    // 绑定本地端口
    sockaddr_in myaddr;
    myaddr.sin_family = AF_INET;
    myaddr.sin_addr.s_addr = INADDR_ANY;
    myaddr.sin_port = htons(port);
    bind(lsfd, (sockaddr *)&myaddr, sizeof myaddr);
    // 监听 ??? 为什么是5
    listen(lsfd, 5);
    // 创建epoll事件数组
    epoll_event events[MAX_LISTEN_NUM];
    int epfd = epoll_create(1);
    // 将 监听socket添加到epfd中
    addEpollEvent(epfd, lsfd, 0);
    // LT模式
    epoll_event lsfdeve;
    lsfdeve.data.fd = lsfd;
    lsfdeve.events = EPOLLIN | EPOLLRDHUP;
    epoll_ctl(epfd, EPOLL_CTL_MOD, lsfd, &lsfdeve);
    http_connection::m_epfd = epfd;
    while(1){
        int trigger_num = epoll_wait(epfd, events, MAX_LISTEN_NUM, -1); 
        if(trigger_num < 0 && errno != EINTR){ // 调用失败了
            perror("epoll_wait fails");
            break;
        }
        // 循环遍历数组进行处理
        for(int i = 0 ; i < trigger_num ; ++ i){
            int tpfd = events[i].data.fd;
            if(tpfd == lsfd){
                // 是监听fd，创建新的fd，并添加到epfd中
                sockaddr_in naddr;
                socklen_t len = sizeof naddr;
                int nfd = accept(lsfd, (sockaddr *)&naddr, &len);
                if(http_connection::m_usr_cnt >= MAX_LISTEN_NUM){
                    // 连接数已满，稍后再说
                    close(nfd);
                    continue;
                }
                // cout << "建立连接。。。" << endl;
                clients[nfd].init(nfd, naddr);
            }
            else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)){
                // 说明客户端断开连接了，或者fd发生了错误，这个时候需要关闭这个fd
                // cout << "客户端断开连接等错误..." << endl;
                clients[tpfd].close_conn();
            }
            else if(events[i].events & EPOLLIN){
                // 把所有的数据都读出来
                if(clients[tpfd].read()){
                    // 把任务交给线程池
                    // cout << "读到数据，交给线程池..." << endl;
                    thdpool->addqueue(clients+tpfd);
                }
                else{
                    // 读失败了，关闭连接
                    // cout << "读取数据失败..." << endl;
                    clients[tpfd].close_conn();
                }
            }
            else if(events[i].events & EPOLLOUT){
                // 检测到写事件
                if(clients[tpfd].write()){
                    // cout << "写成功..." << endl;
                    // 交给线程池 ??? 这里为什么不用交给线程池执行
                    // thdpool->addqueue(clients+tpfd);
                }
                else{
                    // 写失败了，关闭连接
                    // cout << "写失败..." << endl;
                    clients[tpfd].close_conn();
                }
            }

        }
    }
    close(epfd);
    close(lsfd);
    delete [] clients;
    delete thdpool;
    return 0;
}