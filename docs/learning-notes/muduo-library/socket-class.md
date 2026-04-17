# 1. 知识准备：
## 1.1什么是socket？
**socket 是操作系统提供的一个通信端点抽象**。本质上它是一个文件描述符（fd），背后对应一个内核缓冲区对。你对它做 `read/write`，内核负责把数据发到另一端——可以是同机器另一个进程，也可以是网络上另一台机器。
为什么需要它？进程的虚拟地址空间是完全隔离的，A 进程直接读 B 进程的内存会触发 segfault。socket 是内核开凿的一条受控通道，让两个进程能安全地交换数据，而且这套接口（`socket/bind/listen/accept/connect/send/recv`）是 POSIX 标准，跨语言、跨机器都通用。

## 1.2 大致的流程：
![image.png](https://raw.githubusercontent.com/Ningbottle/blog-images/main/%20obsidian/20260608170722236.png)

所以，我们需要提供一些封装好的函数来完成调用，这样便于后面的操作：
1. 创建套接字
2. 开始绑定
3. 开始监听
4. 进行连接
5. 接收套接字，放回服务员
6. 接收不同的消息
7. 发送消息
8. 设置非阻塞和套接字复用
9. 快捷创建客户端和服务器


# 2. 开始实现：
## 2.1 创建套接字
```cpp
    bool create()
    {
        _sockfd = ::socket(AF_INET,SOCK_STREAM,0);
        if(_sockfd < 0)
        {
            ERR_LOG("socket error");
            return false;
        }
        INF_LOG("socket success");
        return true;
    }
```
第一个比较简单，还是比较容易，利用socket来创建套接字，AF_INET 表示是网络通讯，其中SOCK_STEAM表示自己是字节流，我们可以在稍微介绍一下这三个参数：

|参数|含义|这里的作用|
|---|---|---|
|`AF_INET`|地址族|使用 **IPv4**|
|`SOCK_STREAM`|套接字类型|使用 **面向连接、可靠、字节流** 的通信方式|
|`0`|协议类型|让系统自动选择默认协议|
其实还有一种常用的套接字的类型就是： SOCK_DGRAM

| 类型            | 含义     | 对应协议 |
| ------------- | ------ | ---- |
| `SOCK_STREAM` | 字节流套接字 | TCP  |
| `SOCK_DGRAM`  | 数据报套接字 | UDP  |
其实本质就是TCP和UDP的区别。

## 2.2 开始绑定:

```cpp
    bool Bind(std::string ip, uint16_t port)
    {
        struct sockaddr_in addr;
        bzero(&addr, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = inet_addr(ip.c_str());
        socklen_t len = sizeof(addr);
        int n = ::bind(_sockfd, (sockaddr *)&addr, len);
        if (n == -1)
        {
            ERR_LOG("bind error");
            return false;
        }
        INF_LOG("bind success");
        return true;
    }
```
先创建结构体 `sockaddr_in`，随后再使用进行填充，随后就进行绑定即可，
`struct sockaddr_in` 是 IPv4 专用地址结构体，大概可以理解成：
```
struct sockaddr_in
{
    sa_family_t sin_family; // 地址族    
    in_port_t sin_port;     // 端口号    
    struct in_addr sin_addr; // IP 地址
};
```
所以它主要存三样东西：IPv4 / IP / 端口。随后利用bind即可完成：以后有客户端访问这个 IP + port，就交给我的 `_sockfd` 处理。

## 2.3 监听函数：
```cpp
    bool Listen(int backlog = MAX_BACKLOG)
    {
        int ret = ::listen(_sockfd, backlog);
        if (ret == -1)
        {
            ERR_LOG("listen error");
            return false;
        }
        ERR_LOG("listen success");
        return true;
    }
```
这个监听是比较简单的，其中的backlog 是设置监听的最大数，能同时监听的数量。设置服务器“等待被 accept 的连接队列”最大长度，即只要accept够快，其实能连接的主机远远多于backlog。

## 2.4 监听之后就可以开始连接和accept了：
我们先说客户端应该做什么：向服务器发送连接：
```cpp
    bool Connect(std::string ip, uint16_t port)
    {
        struct sockaddr_in peer;
        bzero(&peer, sizeof(peer));
        peer.sin_family = AF_INET;
        peer.sin_port = htons(port);
        peer.sin_addr.s_addr = inet_addr(ip.c_str());
        socklen_t len = sizeof(peer);
        int n = ::connect(_sockfd, (sockaddr *)&peer, len);
        if (n == -1)
        {
            printf("%d", errno);
            ERR_LOG("connect error");
            return false;
        }
        INF_LOG("connect success");
        return true;
    }
```
在这里客户端主动访问客户端，同样我们也要知道peer（远端服务器）的地址和port才能进行连接。

随后我们再来看：accept服务器应该怎么办：
```cpp
    int Accept()
    {
        int sockfd = ::accept(_sockfd, nullptr, nullptr);
        if (sockfd == -1)
        {
            ERR_LOG("accept error");
            return -1;
        }
        INF_LOG("accept success");
        return sockfd;
    }
```
我们监听之后，会得到一个新的sockfd，随后这个sockfd就是专门处理这个连接的。

## 2.5 发送消息和接收消息
```cpp
    ssize_t Recv(void* buf,size_t len,int flag = 0)
    {
        ssize_t ret = ::recv(_sockfd, buf,len, flag);
        if(ret > 0)
        {
            // 成功了，直接返回n
            return ret;
        }
        if(ret == 0)
        {
            // 说明上端挂断了：
            INF_LOG("PEER CLOSED CONNECTION");
            return -1; // 让上层走 shutdown / Release
        }
        if(errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
        {
            //errno == EAGAIN || errno == EWOULDBLOCK 没有消息了，稍后再读
            // errno == EAGAIN,可以再次尝试读取
            return 0; // 接下来再次继续读取
        }
        //找不到上面的任何一种情况，就直接退出
        ERR_LOG("SOCKET RECV FAILED!!");
        return -1;
    }
};
```
当我们收到消息，我们需要buf去接收，这个也是输出型参数，用来接收发送过来的消息。对recv的返回值我们一般做出
1. ＞ 0 ，成功了
2. 等于 0 ，说明对端进行挂断，我们的返回值设为-1，交给上层处理
3. 如果是被信号打断，或者是阻塞住了，那么返回0，表示再次尝试

同时再提供一个函数。非阻塞发送：
```cpp
    ssize_t NoBlockRecv(void *buf, size_t len)
    {
        return Recv(buf, len, MSG_DONTWAIT);
    }
```

###  接下来是发送消息了：
```cpp
    ssize_t Send(const void *buf, size_t len, int flag = 0)
    {
        ssize_t n = ::send(_sockfd, buf, len, flag);
        if (n <= 0)
        {
            if (errno == EINTR || errno == EAGAIN)
                return 0;
            ERR_LOG("SOCKET SEND FAILED!!");
            return n;
        }
        INF_LOG("SOCKET SEND SUCCESS!");
        return n;
    }
    ssize_t NoBlockSend(const void *buf, size_t len)
    {
        return Send(buf, len, MSG_DONTWAIT);
    }
```
和上面的接收消息，我们需要提供需要发送的数据和字节大小，还有什么模式：
如果n < 0;那么开始查，如果是还是阻塞的，就放回0，等下epoll模型提醒再次seed.

## 2.6 设置非阻塞和套接字复用
为什么需要这样设置：
结论：在 muduo 这种 **Reactor 网络库**里：
- **套接字复用**：主要是为了服务器重启时不容易出现 `Address already in use`
- **非阻塞 socket**：为了保证 `EventLoop` 不会被某个连接卡死

```cpp
    void NoBlock()
    {
        int flag = fcntl(_sockfd, F_GETFL, 0);
        fcntl(_sockfd, F_SETFL, flag | O_NONBLOCK);
    }

    void ReuseAdress()
    {
        int val = 1;
        setsockopt(_sockfd, SOL_SOCKET, SO_REUSEADDR, (void *)&val, sizeof(int));
        val = 1;
        setsockopt(_sockfd, SOL_SOCKET, SO_REUSEPORT, (void *)&val, sizeof(int));
    }
```
两个函数一个是设置为非阻塞状态，一个是要求地址复用，我们来详细的看看：
第一个是修改标志位，即使用了`F_GETFL`，随后在通过fcntl进行控制修改为非阻塞状态，注意使用的是 `|`.
第二个函数则是设置地址和port的复用，即`val ==1` ，将addr和prot复用。

|选项|主要作用|
|---|---|
|`SO_REUSEADDR`|解决端口处于残留状态时 bind 失败的问题|
|`SO_REUSEPORT`|允许多个进程/线程同时监听同一个端口|

## 2.7 提供便捷的创建函数：
我们需要提供快捷创建客户端和服务器的函数，即结合上面封装的函数来完成：
1. 创建服务器端：
	```cpp
		bool CreateServer(uint16_t port, const std::string ip = "0.0.0.0", bool BLockFlag = false)
    {
        if (Create() == false)
            return false;
        ReuseAdress(); // 复用
        if (BLockFlag)
            NoBlock();
        if (Bind(ip, port) == false)
            return false;
        if (Listen() == false)
            return false;
        return true;
    }	
	```
	即先创建套接字，随后进行绑定，和监听。
2. 创建客户端：
	```cpp
		bool CreateClient(std::string ip, uint16_t port)
    {
        if (Create() == false)
            return false;
        if (Connect(ip, port) == false)
            return false;
        return true;
    }	
	```
	这个就比较简单了，先创建套接字，随后进行连接就可以了。

最后关闭套接字，也放在这里了：
```cpp
    void Close()
    {
        ::close(_sockfd);
        _sockfd = -1;
    }
```



# 总结：
本篇把原生socket接口封装成一个可复用的Socket类，核心是把create/bind/listen/connect/accept/send/recv这套POSIX流程包成易用的成员函数。

- socket本质是一个fd，背后对应内核缓冲区，是内核开凿的受控通道，让隔离的进程间安全交换数据。
- create：用AF_INET+SOCK_STREAM建TCP套接字，SOCK_STREAM对应TCP、SOCK_DGRAM对应UDP。
- Bind：填sockaddr_in（IP+端口）后绑定到fd。
- Listen：开始监听，backlog控制待accept队列长度。
- Connect/Accept：客户端主动连远端，服务端accept返回专门处理该连接的新fd。
- Recv：>0成功；0对端挂断返回-1交上层shutdown/Release；EAGAIN/EWOULDBLOCK/EINTR返回0待重试。
- Send：失败且阻塞返回0，等epoll再触发重发。
- NoBlock+ReuseAdress：非阻塞防EventLoop被单连接卡死；SO_REUSEADDR治重启端口残留，SO_REUSEPORT允许多进程/线程监听同端口。
- CreateServer/CreateClient：组合上述函数一键建端，Close负责收尾关闭。