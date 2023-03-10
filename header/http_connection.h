#ifndef HTTPCONNECTION
#define HTTPCONNECTION

#include <iostream>
#include <sys/epoll.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <sys/uio.h>
#include <string>

class http_connection{
public:
    // HTTP请求方法，这里只支持GET
    enum METHOD {GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT};
    /*
        解析客户端请求时，main状态机的状态
        CHECK_STATE_REQUESTLINE:当前正在分析请求行
        CHECK_STATE_HEADER:当前正在分析头部字段
        CHECK_STATE_CONTENT:当前正在解析请求体
    */
    enum CHECK_STATE { CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT };
    /*
        服务器处理HTTP请求的可能结果，报文解析的结果
        NO_REQUEST          :   请求不完整，需要继续读取客户数据
        GET_REQUEST         :   表示获得了一个完成的客户请求
        BAD_REQUEST         :   表示客户请求语法错误
        NO_RESOURCE         :   表示服务器没有资源
        FORBIDDEN_REQUEST   :   表示客户对资源没有足够的访问权限
        FILE_REQUEST        :   文件请求,获取文件成功
        INTERNAL_ERROR      :   表示服务器内部错误
        CLOSED_CONNECTION   :   表示客户端已经关闭连接了
    */
    enum HTTP_CODE { NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION };
    // sub状态机的三种可能状态，即行的读取状态，分别表示
    // 1.读取到一个完整的行 2.行出错 3.行数据尚且不完整
    enum LINE_STATUS { LINE_OK = 0, LINE_BAD, LINE_OPEN };

    static int m_epfd; // 公用的epollfd
    static unsigned int m_usr_cnt; // 统计类被创建的次数，即连接上epfd的用户的数量
    static const int READ_BUF_SIZE = 2048; // 写缓冲区的大小
    static const int WRITE_BUF_SIZE = 1024; // 读缓冲区的大小
    static const int FILE_PATH_LEN = 200;

    http_connection();
    ~http_connection();
    void init(int nfd, const sockaddr_in & naddr); // 初始化自身的fd和addr
    void close_conn(); // 关闭连接
    bool read(); // 一次性读数据
    bool write(); // 一次性写数据
    void process(); // 处理客户端请求
    
private:
    int m_sockfd; // 本客户端的socket
    sockaddr_in m_addr; // 客户端的地址
    char m_readBuf[READ_BUF_SIZE]; // 读缓冲区
    int m_read_idx; // 标示读缓冲区的位置，即最后一个字节的下一个位置
    char m_writeBuf[WRITE_BUF_SIZE]; // 写缓冲区,只负责存储响应头信息
    int m_write_idx; // 标示写缓冲区的位置，即最后一个字节的下一个位置
    int m_check_idx; // 标识已经检查过的位置
    int m_startOfLine; // 标识当前行的起始位置
    CHECK_STATE m_check_state; // 主状态机的状态
    METHOD m_request_method; // 请求方法
    char * m_http_version; // HTTP版本
    char * m_req_url; // 请求的url
    char * m_host; // 请求头中的host信息，包含ip 和 port
    bool m_keepLive; // 保活状态
    size_t m_req_body_len; // 请求体的长度
    char m_real_file_path[FILE_PATH_LEN]; // 真实的资源路径
    struct stat m_req_file_stat; // 保存请求文件的状态信息
    char * m_req_file_map; // 请求文件的内存映射地址
    iovec m_write_array[2]; // 分散写数组，存储要写的地址，一个是响应头，一个是响应文件
    int m_write_array_cnt; // 计数要写的数据
    int m_bytes_to_send; // 要写的数据
    int m_bytes_have_send; // 已经写的数据


    HTTP_CODE process_readBuf(); // 解析读取到的http请求的整体函数
    HTTP_CODE parse_firstLine(char * txt); // 解析首行
    HTTP_CODE parse_head(char * txt);  // 解析请求头
    HTTP_CODE parse_body(char * txt);  // 解析请求体
    LINE_STATUS parse_oneLine(); // 解析一行数据，分析一行数据怎么样
    // 获取一行数据
    char * get_oneLine(){ return m_readBuf + m_startOfLine; }
    void init(); // 初始化其余连接
    HTTP_CODE do_request();
    bool process_write( HTTP_CODE readstat );
    bool add_response(char * formate, ...);
    bool add_response_firstline(int status_code, const char * status_info);
    bool add_response_header(int content_length);
    bool add_response_body(const char * info);
    bool add_content_length(int content_length);
    bool add_content_type();
    bool add_keep_alive(bool kplive);
    bool add_blank_line();
    void unmap_req_file();

};

void addEpollEvent(int epfd, int fd, int oneshot);

void delEpollEvent(int epfd, int fd);

void modifyEpollEvent(int epfd, int fd, int eve);

void setNonBlocking(int fd);
#endif