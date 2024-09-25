//#define _GNU_SOURCE
#include "HttpRequest.h"
#include <stdio.h>
#include <strings.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include "TcpConnection.h"
#include <assert.h>
#include <ctype.h>

//解析行中的子字符串，回调处理子字符串前的数据，并返回子字符串地址
char* HttpRequest::splitRequestLine(const char* start, 
                                    const char* end, 
                                    const char* sub, 
                                    function<void(string)> callback)
{
    char* space = const_cast<char*>(end);
    if (sub != nullptr)
    {
        space = static_cast<char*>(memmem(start, end - start, sub, strlen(sub)));//space指向子字符串首地址
        assert(space != nullptr);
    }
    int length = space - start;
    callback(string(start, length));
    return space + 1;
}

// 将字符（十六进制）转换为整形数（十进制），
int HttpRequest::hexToDec(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;

    return 0;
}

HttpRequest::HttpRequest() //构造函数
{ 
    reset();
}

HttpRequest::~HttpRequest() {}

void HttpRequest::reset()
{
    m_curState = PrecessState::ParseReqLine;
    m_method = m_url = m_version = string(); //外部传入
    m_reqHeaders.clear();
}
//增加请求头键值对
void HttpRequest::addHeader(const string key, const string value)
{
    if (key.empty() || value.empty())
    {
        return;
    }
    m_reqHeaders.insert(make_pair(key, value));
}
//获取请求头的value
string HttpRequest::getHeader(const string key)
{
    auto item = m_reqHeaders.find(key);
    if (item == m_reqHeaders.end())
    {
        return string();
    }
    return item->second;
}

//解析请求行
bool HttpRequest::parseRequestLine(Buffer* readBuf)
{
    // 读出请求行, 保存字符串结束地址
    char* end = readBuf->findCRLF();  //findCRLF返回"\r\n"的地址
    // 保存字符串起始地址
    char* start = readBuf->data();  //m_data + read_pods 读取地址
    // 请求行总长度
    int lineSize = end - start;

    if (lineSize > 0)
    {
        auto methodFunc = bind(&HttpRequest::setMethod, this, placeholders::_1);//回调函数就是set 请求头属性值
        start = splitRequestLine(start, end, " ", methodFunc);

        auto urlFunc = bind(&HttpRequest::seturl, this, placeholders::_1);
        start = splitRequestLine(start, end, " ", urlFunc);

        auto versionFunc = bind(&HttpRequest::setVersion, this, placeholders::_1);
        splitRequestLine(start, end, nullptr, versionFunc);

        // 为解析请求头做准备
        readBuf->readPosIncrease(lineSize + 2);  //读取地址后移俩个单位---》换行
        // 修改状态
        setState(PrecessState::ParseReqHeaders);//解析完请求行，开始解析请求头
        return true;
    }
    return false;
}

bool HttpRequest::parseRequestHeader(Buffer* readBuf)  //解析头
{
    char* end = readBuf->findCRLF();  //findCRLF返回"\r\n"的地址
    if (end != nullptr)
    {
        char* start = readBuf->data();
        int lineSize = end - start;
        // 基于: 搜索字符串
        char* middle = static_cast<char*>(memmem(start, lineSize, ": ", 2));  //将：的地址赋值给middle
        if (middle != nullptr)
        {
            int keyLen = middle - start;   //键
            int valueLen = end - middle - 2;  //值
            if (keyLen > 0 && valueLen > 0)
            {
                string key(start, keyLen);
                string value(middle + 2, valueLen);
                addHeader(key, value);
            }
            // 移动读数据的位置
            readBuf->readPosIncrease(lineSize + 2);///换行
        }
        else
        {
            // 请求头被解析完了, 跳过空行
            readBuf->readPosIncrease(2);
            // 修改解析状态
            // 忽略 post 请求, 按照 get 请求处理
            setState(PrecessState::ParseReqDone);
        }
        return true;
    }
    return false;
}

bool HttpRequest::parseHttpRequest(Buffer* readBuf, HttpResponse* response, Buffer* sendBuf, int socket)
{
    bool flag = true;
    while (m_curState != PrecessState::ParseReqDone)
    {
        switch (m_curState)
        {
        case PrecessState::ParseReqLine:
            flag = parseRequestLine(readBuf);
            break;
        case PrecessState::ParseReqHeaders:
            flag = parseRequestHeader(readBuf);
            break;
        case PrecessState::ParseReqBody:
            break;
        default:
            break;
        }
        if (!flag)
        {
            return flag;
        }
        // 判断是否解析完毕了, 如果完毕了, 需要准备回复的数据
        if (m_curState == PrecessState::ParseReqDone)
        {
            // 1. 根据解析出的原始数据, 对客户端的请求做出处理
            processHttpRequest(response);
            // 2. 组织响应数据并发送给客户端
            response->prepareMsg(sendBuf, socket);
        }
    }
    m_curState = PrecessState::ParseReqLine;   // 状态还原, 保证还能继续处理第二条及以后的请求
    return flag;
}

bool HttpRequest::processHttpRequest(HttpResponse* response)
{
    if (strcasecmp(m_method.data(), "get") != 0) //如果method不是get，报错
    {
        return -1;
    }
    m_url = decodeMsg(m_url);   ///十六进制转十进制
    // 处理客户端请求的静态资源(目录或者文件)
    const char* file = NULL;
    if (strcmp(m_url.data(), "/") == 0)
    {
        file = "./";
    }
    else
    {
        file = m_url.data() + 1;
    }
    // 获取文件属性
    struct stat st;
    int ret = stat(file, &st);
    if (ret == -1)
    {
        // 文件不存在 -- 回复404
        //sendHeadMsg(cfd, 404, "Not Found", getFileType(".html"), -1);
        //sendFile("404.html", cfd);
        response->setFileName("404.html");
        response->setStatusCode(StatusCode::NotFound);
        // 响应头
        response->addHeader("Content-type", getFileType(".html"));
        response->sendDataFunc = sendFile;
        return 0;
    }

    response->setFileName(file);
    response->setStatusCode(StatusCode::OK);
    // 判断文件类型
    if (S_ISDIR(st.st_mode))
    {
        // 把这个目录中的内容发送给客户端
        //sendHeadMsg(cfd, 200, "OK", getFileType(".html"), -1);
        //sendDir(file, cfd);
        // 响应头
        response->addHeader("Content-type", getFileType(".html"));
        response->sendDataFunc = sendDir;
    }
    else
    {
        // 把文件的内容发送给客户端
        //sendHeadMsg(cfd, 200, "OK", getFileType(file), st.st_size);
        //sendFile(file, cfd);
        // 响应头
        response->addHeader("Content-type", getFileType(file));
        response->addHeader("Content-length", to_string(st.st_size));
        response->sendDataFunc = sendFile;
    }

    return false;
}

string HttpRequest::decodeMsg(string msg)
{
    string str = string();
    const char* from = msg.data();
    for (; *from != '\0'; ++from)
    {
        // isxdigit -> 判断字符是不是16进制格式, 取值在 0-f
        // Linux%E5%86%85%E6%A0%B8.jpg
        if (from[0] == '%' && isxdigit(from[1]) && isxdigit(from[2]))
        {
            // 将16进制的数 -> 十进制 将这个数值赋值给了字符 int -> char
            // B2 == 178
            // 将3个字符, 变成了一个字符, 这个字符就是原始数据
            str.append(1, hexToDec(from[1]) * 16 + hexToDec(from[2]));

            // 跳过 from[1] 和 from[2] 因此在当前循环中已经处理过了
            from += 2;
        }
        else
        {
            // 字符拷贝, 赋值
            str.append(1, *from);
        }
    }
    str.append(1, '\0'); //字段追加'\0'
    return str;  //返回将16进制处理成的10进制字符串
}

const string HttpRequest::getFileType(const string name)
{
    // a.jpg a.mp4 a.html
    // 自右向左查找‘.’字符, 如不存在返回NULL
    const char* dot = strrchr(name.data(), '.');
    if (dot == NULL)
        return "text/plain; charset=utf-8";	// 纯文本
    if (strcmp(dot, ".html") == 0 || strcmp(dot, ".htm") == 0)
        return "text/html; charset=utf-8";
    if (strcmp(dot, ".jpg") == 0 || strcmp(dot, ".jpeg") == 0)
        return "image/jpeg";
    if (strcmp(dot, ".gif") == 0)
        return "image/gif";
    if (strcmp(dot, ".png") == 0)
        return "image/png";
    if (strcmp(dot, ".css") == 0)
        return "text/css";
    if (strcmp(dot, ".au") == 0)
        return "audio/basic";
    if (strcmp(dot, ".wav") == 0)
        return "audio/wav";
    if (strcmp(dot, ".avi") == 0)
        return "video/x-msvideo";
    if (strcmp(dot, ".mov") == 0 || strcmp(dot, ".qt") == 0)
        return "video/quicktime";
    if (strcmp(dot, ".mpeg") == 0 || strcmp(dot, ".mpe") == 0)
        return "video/mpeg";
    if (strcmp(dot, ".vrml") == 0 || strcmp(dot, ".wrl") == 0)
        return "model/vrml";
    if (strcmp(dot, ".midi") == 0 || strcmp(dot, ".mid") == 0)
        return "audio/midi";
    if (strcmp(dot, ".mp3") == 0)
        return "audio/mpeg";
    if (strcmp(dot, ".ogg") == 0)
        return "application/ogg";
    if (strcmp(dot, ".pac") == 0)
        return "application/x-ns-proxy-autoconfig";

    return "text/plain; charset=utf-8";
}

void HttpRequest::sendDir(string dirName, Buffer* sendBuf, int cfd)
{
    char buf[4096] = { 0 };
    sprintf(buf, "<html><head><title>%s</title></head><body><table>", dirName.data());
    struct dirent** namelist;
    int num = scandir(dirName.data(), &namelist, NULL, alphasort);
    for (int i = 0; i < num; ++i)
    {
        // 取出文件名 namelist 指向的是一个指针数组 struct dirent* tmp[]
        char* name = namelist[i]->d_name;
        struct stat st;
        char subPath[1024] = { 0 };
        sprintf(subPath, "%s/%s", dirName.data(), name);
        stat(subPath, &st);
        if (S_ISDIR(st.st_mode))
        {
            // a标签 <a href="">name</a>
            sprintf(buf + strlen(buf),
                "<tr><td><a href=\"%s/\">%s</a></td><td>%ld</td></tr>",
                name, name, st.st_size);
        }
        else
        {
            sprintf(buf + strlen(buf),
                "<tr><td><a href=\"%s\">%s</a></td><td>%ld</td></tr>",
                name, name, st.st_size);
        }
        // send(cfd, buf, strlen(buf), 0);
        sendBuf->appendString(buf);
#ifndef MSG_SEND_AUTO
        sendBuf->sendData(cfd);
#endif
        memset(buf, 0, sizeof(buf));
        free(namelist[i]);
    }
    sprintf(buf, "</table></body></html>");
    // send(cfd, buf, strlen(buf), 0);
    sendBuf->appendString(buf);
#ifndef MSG_SEND_AUTO
    sendBuf->sendData(cfd);
#endif
    free(namelist);
}

void HttpRequest::sendFile(string fileName, Buffer* sendBuf, int cfd)
{
    // 1. 打开文件
    int fd = open(fileName.data(), O_RDONLY);
    assert(fd > 0);
#if 1
    while (1)
    {
        char buf[1024];
        int len = read(fd, buf, sizeof buf);
        if (len > 0)
        {
            // send(cfd, buf, len, 0);
            sendBuf->appendString(buf, len);
#ifndef MSG_SEND_AUTO
            sendBuf->sendData(cfd);
#endif
        }
        else if (len == 0)
        {
            break;
        }
        else
        {
            close(fd);
            perror("read");
        }
    }
#else
    off_t offset = 0;
    int size = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    while (offset < size)
    {
        int ret = sendfile(cfd, fd, &offset, size - offset);
        printf("ret value: %d\n", ret);
        if (ret == -1 && errno == EAGAIN)
        {
            printf("没数据...\n");
        }
    }
#endif
    close(fd);
}
