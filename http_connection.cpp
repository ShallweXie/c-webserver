#include "http_connection.h"
#include <iostream>
using namespace std;

// 根目录
const char * root_path = "/home/xxw/test/webserver/www/";

// HTTP响应的一些信息
const char * err_400 = "Bad Request";
const char * err_400_info = "There are something error with your request";
const char * err_403 = "Forbidden";
const char * err_403_info = "You're not allowed to visite these resources";
const char * err_404 = "Not Found";
const char * err_404_info = "Resources not found in this server";
const char * err_500 = "Internal Error";
const char * err_500_info = "There was an error in the server";
const char * ok_200 = "OK";



int http_connection::m_epfd = -1; // 公用的epollfd
unsigned int http_connection::m_usr_cnt = 0; // 统计类被创建的次数，即连接上epfd的用户的数量

// 向epfd中添加监听的事件，oneshot表示是否设置socket只触发一次
void addEpollEvent(int epfd, int fd, int oneshot){
    epoll_event eve;
    eve.data.fd = fd;
    eve.events = EPOLLIN | EPOLLET |EPOLLRDHUP; // 监听读事件 和 客户端断开事件 设置为边缘触发
    if(oneshot){
        eve.events |= EPOLLONESHOT;
    }
    epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &eve);
}

// 从epfd中删除监听的事件
void delEpollEvent(int epfd, int fd){
    epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
    close(fd);
}

// 修改epfd中fd的监听事件，为其重置 oneshot 和 rdhup
// 确保下一次有数据时，fd能被触发
void modifyEpollEvent(int epfd, int fd, int eve){
    epoll_event neweve;
    neweve.data.fd = fd;
    neweve.events = eve | EPOLLONESHOT |EPOLLRDHUP | EPOLLET;
    epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &neweve);
}

http_connection::http_connection(){
    m_sockfd = -1;
    m_read_idx = 0;
    m_write_idx = 0;
    // ++m_usr_cnt;
}

http_connection::~http_connection(){
    // --m_usr_cnt;
}

// 初始化，将自身的sockfd和addr初始化为nfd和naddr
// 同时将自身的sockfd添加到epoll监听列表中
// 同时，设置sockfd非阻塞，防止EPOLL在ET模式下收不到所有的数据
void http_connection::init(int nfd, const sockaddr_in & naddr){
    m_sockfd = nfd;
    m_addr = naddr;
    // 设置端口复用
    int re = 1;
    setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &re, sizeof re);

    // 添加到epoll监听的事件列表中
    addEpollEvent(m_epfd, m_sockfd, 1);
    setNonBlocking(m_sockfd);
    // 用户数++
    ++m_usr_cnt;
    init();
}

// 初始化其余的连接
void http_connection::init(){
    m_check_state = CHECK_STATE_REQUESTLINE; // 初始化本连接的状态为：检查请求首行
    m_check_idx = 0; // 还未检查数据
    m_startOfLine = 0; // 首行的开始位置为0
    m_read_idx = 0; // 当前读到了ReadBuf的 0 位置
    m_write_idx = 0; // 当前写到了WriteBuf的 0 位置

    memset(m_readBuf, 0, READ_BUF_SIZE);
    memset(m_writeBuf, 0, WRITE_BUF_SIZE);
    memset(m_real_file_path, 0, FILE_PATH_LEN);

    METHOD m_request_method = GET; // 请求方法
    m_http_version = nullptr; // HTTP版本
    m_req_url = nullptr; // 请求的url
    m_host = nullptr;
    m_keepLive = false;
    m_write_array_cnt = 0;

    m_bytes_to_send = 0;
    m_bytes_have_send = 0;
}

// 关闭连接 将fd移出epoll的监听列表，然后让sockfd = -1
// 因为在init()中，设置了端口复用，所以这里好像不用close(m_sockfd)
void http_connection::close_conn(){
    // 首先m_sockfd应该是正常的
    if(m_sockfd != -1){
        // 从epfd中移除
        delEpollEvent(m_epfd, m_sockfd);
        m_sockfd = -1;
        --m_usr_cnt;// 用户数--
    }
    
}

// 设置fd非阻塞
void setNonBlocking(int fd){
    int flg = fcntl(fd, F_GETFL);
    flg |= O_NONBLOCK;
    fcntl(fd, F_SETFL, flg);
}

// 一次性读完所有数据,循环读取
bool http_connection::read(){
    if(m_read_idx >= READ_BUF_SIZE){
        return false;
    }
    int read_bytes = 0;
    while(1){
        read_bytes = recv(m_sockfd, m_readBuf + m_read_idx, READ_BUF_SIZE - m_read_idx, 0);
        if(read_bytes < 0){
            if(errno == EAGAIN || errno == EWOULDBLOCK){
                // 说明没数据了
                break;
            }
            else{
                return false;
            }
        }
        else if(read_bytes == 0){
            // 对方断开连接
            return false;
        }
        else{
            // cout << "读取到：" << m_readBuf + m_read_idx << endl;
            m_read_idx += read_bytes;
        }
    }
    return true;
}

// 一次性写完所有数据
// 如果不返回资源文件，则只有一个m_writeBuf在m_write_array中
// 否则，m_write_array的长度为2，分别是m_writeBuf 和 m_file_map
bool http_connection::write(){
    // 通过分散写函数，实现多段地址的写
    int tmp = 0;

    if(m_bytes_to_send == 0){
        // 这个请求不需要发数据
        // 重置ONESHOT
        modifyEpollEvent(m_epfd, m_sockfd, EPOLLOUT);
        // 初始化这个连接的状态，继续为接收数据做准备
        init();
        return true;
    }
    // 有数据，开始循环写数据
    while(1){
        // 成功 - writev返回写的字节数  失败 - -1
        tmp = writev(m_sockfd, m_write_array, m_write_array_cnt);
        if(tmp == -1){
            // 判断是不是写的sock满了，等待下一次EPOLLOUT事件的触发
            // 在此阶段，服务器无法立刻接收到同一客户的下一次请求，但可以保证连接的完整性。???
            if(errno == EAGAIN){
                modifyEpollEvent(m_epfd, m_sockfd, EPOLLOUT);
                return true;
            }
            unmap_req_file(); // 解除对请求文件的响应
            return false;
        }
        m_bytes_to_send -= tmp;
        m_bytes_have_send += tmp;
        if ( m_bytes_have_send > m_write_array[0].iov_len ) {
            // 发送HTTP响应头成功
            m_write_array[0].iov_len = 0;
            m_write_array[1].iov_base = m_req_file_map + (m_bytes_have_send - m_write_idx);
            m_write_array[1].iov_len = m_bytes_to_send;
        }
        else{
            m_write_array[0].iov_base = m_writeBuf + m_bytes_have_send;
            m_write_array[0].iov_len -= tmp;
        }
        if(m_bytes_to_send <= 0){
            // 根据HTTP请求中的Connection字段决定是否立即关闭连接
            unmap_req_file();
            modifyEpollEvent(m_epfd, m_sockfd, EPOLLIN);

            if(m_keepLive){
                init();
                return true;
            }
            else{
                return false;
            }
        }
    }// end of while

    return true;
}

// 由线程池中的工作线程调用，处理客户端的HTTP请求
void http_connection::process(){
    // 解析请求
    // cout << "开始解析请求..." << endl;
    HTTP_CODE ret = process_readBuf();
    // cout << "解析请求完成，结果为：" << ret << endl;
    if(ret == NO_REQUEST){
        // 还有数据需要读，重置m_sockfd的状态
        modifyEpollEvent( m_epfd, m_sockfd, EPOLLIN );
        return;
    }

    // 解析请求完成
    // 生成响应
    // cout << "开始生成响应..." << endl;
    bool write_ret = process_write( ret );
    // cout << "生成响应成功..." << endl;
    if(!write_ret){
        // 写数据失败，关闭连接
        close_conn();
    }
    // 响应生成成功
    // 恢复写的状态
    modifyEpollEvent(m_epfd, m_sockfd, EPOLLOUT);

}

// 主状态机：
// 解析读取到的http请求，返回HTTP_CODE状态码
http_connection::HTTP_CODE http_connection::process_readBuf(){
    // 初始状态
    LINE_STATUS line_stat = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    // 循环解析一行
    char * ch = nullptr;
    // cout << "开始循环解析行..." << endl;
    while( 
        ( m_check_state == CHECK_STATE_CONTENT && (line_stat == LINE_OK) ) 
        || 
        ( ( line_stat = parse_oneLine() ) == LINE_OK ) 
    )
    {
        ch = get_oneLine();
        // cout << "获取到一行数据：" << ch << endl;
        m_startOfLine = m_check_idx; // 行开始的地方就是上次检查完的地方
        switch(m_check_state){
            case CHECK_STATE_REQUESTLINE:{
                ret = parse_firstLine(ch);
                if(ret == BAD_REQUEST){
                    return BAD_REQUEST;
                }
                break;
            }
            case CHECK_STATE_HEADER:{
                ret = parse_head(ch);
                if(ret == BAD_REQUEST)
                    return BAD_REQUEST;
                else if(ret == GET_REQUEST)
                    return do_request();
                break;
            }
            case CHECK_STATE_CONTENT:{
                ret = parse_body(ch);
                if(ret == GET_REQUEST)
                    return do_request();
                else line_stat = LINE_OPEN;
                break;
            }
            default:{
                return INTERNAL_ERROR;
            }
        }// end of switch
    } // end of while
    // cout << "循环解析完成..." << endl;
    return NO_REQUEST;
}

// 解析请求首行，获得 请求方法、URL、HTTP版本
http_connection::HTTP_CODE http_connection::parse_firstLine(char * txt){
    // GET / HTTP/1.1
    m_req_url = strpbrk(txt, " \t"); // 找到第一个 空格 或者 \t的位置
    *m_req_url = '\0'; // 分割 请求方法 和 整体的字符串
    ++ m_req_url; // 
    if(strcmp(txt, "GET") == 0){
        m_request_method = GET;
    }
    else{
        // 只支持GET方法，其他方法全返回BAD_REQUEST
        return BAD_REQUEST;
    }
    // / HTTP/1.1
    m_http_version = strpbrk(m_req_url, " \t");
    if(!m_http_version) return BAD_REQUEST;
    *m_http_version = '\0';
    ++m_http_version;
    if(strcasecmp(m_http_version, "HTTP/1.1") != 0){
        // 只支持 HTTP/1.1 版本
        return BAD_REQUEST;
    }

    //
    if(strncasecmp(m_req_url, "http://", 7) == 0){
        m_req_url += 7; // 删除http://
        m_req_url = strchr(m_req_url, '/'); // 继续寻找 / 符号
    }
    if(!m_req_url || m_req_url[0] != '/'){
        return BAD_REQUEST;
    }

    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;

}

// 解析请求头
http_connection::HTTP_CODE http_connection::parse_head(char * txt){
    if(txt[0] == '\0'){
        // 遇到空行了
        // 如果请求体为空，则请求解析完成
        if( m_req_body_len == 0 ){
            return GET_REQUEST;
        }
        // 否则转入 检查请求体 状态
        else{
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
    }
    else if(strncasecmp(txt, "Host:", 5) == 0){
        // 获取服务器信息
        txt += 5;
        txt += strspn(txt, " \t");
        m_host = txt;
    }
    else if(strncasecmp(txt, "Connection:", 11) == 0){
        // 设置连接状态
        txt += 11;
        txt += strspn( txt, " \t" ); // 删除前面的 \t
        if(strcasecmp(txt,"keep-alive") == 0){
            m_keepLive = true;
        }
    }
    else if(strncasecmp(txt, "Content-Length:", 15) == 0){
        // 设置请求体长度
        txt += 15;
        txt += strspn( txt, " \t");// 删除前面的 \t
        m_req_body_len = atol(txt);
    }
    return NO_REQUEST;
}

// 解析请求体
// 这里并不解析，只是表示读出来了
http_connection::HTTP_CODE http_connection::parse_body(char * txt){
    if( m_read_idx >= ( m_check_idx + m_req_body_len ) ){
        txt[ m_req_body_len ] = '\0'; // 截断
        return GET_REQUEST;
    }
    // 说明 还有数据没读到，继续读
    return NO_REQUEST;
}

// 解析一行数据，看看是否符合 以'\\r\\n'为结尾 的格式，
// 如果规范，则返回LINE_OK
// 如果可能有数据，则返回LINE_OPEN
// 如果不对，则返回LINE_BAD
http_connection::LINE_STATUS http_connection::parse_oneLine(){
    char temp;
    for( ; m_check_idx < m_read_idx; ++ m_check_idx){
        temp = m_readBuf[m_check_idx];
        if(temp == '\r'){
            // 如果没有下一个数据，则数据没读完
            if(m_check_idx + 1 == m_read_idx) 
                return LINE_OPEN;
            if(m_readBuf[m_check_idx + 1] == '\n'){
                // 读完了数据，且格式规范，把\r\n替换成\0
                m_readBuf[m_check_idx++] = '\0'; // 替换\r
                m_readBuf[m_check_idx++] = '\0'; // 替换\n
                return LINE_OK;
            }
            // 其他情况，格式不规范
            return LINE_BAD;
        }
        if(temp == '\n'){
            // 必须有前一个数据，且是\r
            if(m_check_idx > 0 && (m_readBuf[m_check_idx - 1] == '\r')){
                m_readBuf[m_check_idx - 1] = '\0'; // 替换\r
                m_readBuf[m_check_idx++] = '\0'; // 替换\n
                return LINE_OK;
            }
            return LINE_BAD;
        }
        // return LINE_OPEN;
    }
    return LINE_OPEN;
}

// 进行具体的请求响应
// 获取了一个完整的请求，对请求进行响应
// 如果目标文件存在、对所有用户可读、不是目录，则使用mmap映射
// 映射到m_file_address处，并告诉调用者获取文件资源成功
http_connection::HTTP_CODE http_connection::do_request(){
    // 对文件路径进行拼接，m_real_path将保存着请求文件的绝对路径
    strcpy(m_real_file_path, root_path);
    int len = strlen(root_path);
    strncpy(m_real_file_path + len, m_req_url, FILE_PATH_LEN - len - 1);
    // 检查文件状态
    if( stat(m_real_file_path, &m_req_file_stat) < 0){
        // 状态不对
        return NO_RESOURCE;
    }
    // 然后通过 m_req_file_stat 判断权限等信息
    // 判断是否有 其他用户可读 的权限
    if( !(m_req_file_stat.st_mode & S_IROTH) ){
        // 没有，返回禁止访问FORBIDDEN_REQUEST
        return FORBIDDEN_REQUEST;
    }

    // 判断是否是个目录
    if( S_ISDIR( m_req_file_stat.st_mode ) ){
        return BAD_REQUEST;
    }

    // 可以打开了，以只读方式打开
    int fd = open(m_real_file_path, O_RDONLY);
    // 创建内存映射
    m_req_file_map = (char *)mmap(nullptr, m_req_file_stat.st_size,
                    PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}

// 根据读的解析状态码 生成回写的数据
bool http_connection::process_write(HTTP_CODE readstat){
    int flg = true;
    switch(readstat){
        case INTERNAL_ERROR:{
            // 服务器内部错误
            flg = add_response_firstline(500, err_500);
            flg = add_response_header(strlen(err_500_info));
            flg = add_response_body(err_500_info);
            if(flg == false){
                return false;
            }
            break;
        }
        case BAD_REQUEST:{
            // 请求格式错误
            flg = add_response_firstline(400, err_400);
            flg = add_response_header(strlen(err_400_info));
            flg = add_response_body(err_400_info);
            if(flg == false){
                return false;
            }
            break;
        }
        case NO_RESOURCE:{
            // 找不到资源
            flg = add_response_firstline(404, err_404);
            flg = add_response_header(strlen(err_404_info));
            flg = add_response_body(err_404_info);
            if(flg == false){
                return false;
            }
            break;
        }
        case FORBIDDEN_REQUEST:{
            // 无权访问
            flg = add_response_firstline(403, err_403);
            flg = add_response_header(strlen(err_403_info));
            flg = add_response_body(err_403_info);
            if(flg == false){
                return false;
            }
            break;
        }
        case FILE_REQUEST:{
            // 正常的资源请求
            flg = add_response_firstline(200, ok_200);
            flg = add_response_header(m_req_file_stat.st_size);
            // 把请求资源的磁盘映射地址，添加到io数组中
            m_write_array[0].iov_base = m_writeBuf;
            m_write_array[0].iov_len = m_write_idx;
            m_write_array[1].iov_base = m_req_file_map;
            m_write_array[1].iov_len = m_req_file_stat.st_size;
            m_write_array_cnt = 2;
            m_bytes_to_send = m_write_idx + m_req_file_stat.st_size;
            if(flg == false){
                return false;
            }
            return true;
        }
        default:{
            // 其他状态
            return false;
        }

    }// end of switch
    // 从switch跳出之后，说明只有一个响应头需要写
    // 设置write数组
    m_write_array[0].iov_base = m_writeBuf;
    m_write_array[0].iov_len = m_write_idx;
    m_write_array_cnt = 1;
    m_bytes_to_send = m_write_idx;
    return true;
}

// 一个基础函数，按照传入的格式进行拼接
// format是sprintf格式字符串
// 可变参数是传递的信息
bool http_connection::add_response(char * formate, ...){
    // 向m_writeBuf中插入要写的数据
    // 先检查是否溢出
    if(m_write_idx >= WRITE_BUF_SIZE) return false;
    va_list arg_list;
    // 初始化srg_list为formate的下一位置
    va_start(arg_list, formate);
    // 通过va_list来实现可变参数的输出
    // 返回值是输出的字节数，如果返回值大于等于要写的最大长度，则说明输出有阻塞
    int len = vsnprintf(m_writeBuf + m_write_idx, WRITE_BUF_SIZE - m_write_idx - 1, formate, arg_list);
    // 阻塞了，写不下，返回false
    if(len >= WRITE_BUF_SIZE - m_write_idx - 1) return false;
    m_write_idx += len;
    // 回收arg_list中的资源
    va_end(arg_list);
    return true;
}

// 添加回应首行信息
// HTTP/1.1 200 OK
bool http_connection::add_response_firstline(int status_code, const char * status_info){
    return add_response( "%s %d %s\r\n", m_http_version, status_code, status_info);
}

// 添加回应头信息
// 只添加content_length、content_type、keep-alive、空行
bool http_connection::add_response_header(int content_length){
    bool flg = true;
    // 添加Connection keep-alive信息
    flg = add_keep_alive(m_keepLive);
    // 添加content_type
    flg = add_content_type();
    // 添加content_length
    flg = add_content_length(content_length);
    // 添加空行
    flg = add_blank_line();
    return flg;
}

// 添加回应头中的content-length
// Content-Length: 255
bool http_connection::add_content_length(int content_length){
    return add_response("Content-Length: %d\r\n", content_length);
}

// 添加回应头中的content_type 仅支持text/html
// Content-Type: text/html; charset=utf-8
bool http_connection::add_content_type(){
    return add_response("Content-Type: %s\r\n","text/html");
}

// 添加回应头中的keep-alive
// Connection: keep-alive / close
bool http_connection::add_keep_alive(bool kplive){
    if(kplive){
        return add_response("Connection: %s\r\n", "keep-alive");
    }
    return add_response("Connection: %s\r\n","close");
}

// 添加回应头中的\r\n空行
bool http_connection::add_blank_line(){
    return add_response("%s", "\r\n");
}

// 添加回应体信息
bool http_connection::add_response_body(const char * info){
    return add_response("%s\r\n", info);
}

// 解除请求文件的内存映射
void http_connection::unmap_req_file(){
    munmap(m_req_file_map, m_req_file_stat.st_size);
}

