# 1. 前言：
为了便于管理 sockfd(包含 listenfd 和 connectfd),我们需要对 fd 进行封装。
这是因为 epoll_wait 返回的只是一个 fd 加一组就绪事件,它并不知道这个 fd 该做什么。于是「fd」「它关心哪些事件」「事件就绪后怎么处理」这三者是分离的。Channel 就是把它们绑在一起的封装。
因此一个 Channel 需要明确两类事件:**我关心的事件**(读?写?)和**实际就绪的事件**。这是两个不同的概念,前者由我们设置并注册给 epoll,后者由 epoll 在事件触发后回填——后面会看到它们对应两个不同的成员。
同时,为了在事件就绪后能直接分发处理,Channel 还要持有一组回调函数,比如读事件就绪该做什么、写事件就绪该做什么……

# 2. 开始设计：
先考虑成员变量，channel本身就是fd的封装，还需要让他的注意什么事件，什么事件已经就绪了，那么就可以给出：
1. `int _fd` 可以是listenfd，也可以是connfd
2. `uint32_t _events`： 是需要这个fd让epoll注意的事情
3. `uint32_t _revents;`哪些事件好了，需要返回

我们还需要注册一些回调函数：using EventCallBack = std::function<void()>;
    1. `EventCallBack _read_event;` 为 读取事件
    2. `EventCallBack _write_event;`为 写入事件
    3. `EventCallBack _error_event;` 
    4. `EventCallBack _close_event;`
    5. `EventCallBack _all_event;`

先私有成员，先考虑这么多，我们后面开始进行下一步：

## 2.1 构造函数：
```cpp
    Channel(int fd, EventLoop *loop) : _fd(fd), _loop(loop), _events(0), _revents(0) {}
    int Fd() { return _fd; }
    uint32_t Event() { return _events; }
```

我们构建了channel，为什么还需要_loop呢，我在上面的私有成员也没有开始讲述，等我讲完一些简单就可以直接来讲这个。

## 2.2设置回调函数：
```cpp
    // 1. 设置回调函数，包括：读写，关闭错误，和任意时间
    void SetReadCallBack(const EventCallBack &cb) { _read_event = cb; }
    void SetWriteCallBack(const EventCallBack &cb) { _write_event = cb; }
    void SetErrorCallBack(const EventCallBack &cb) { _error_event = cb; }
    void SetCloseCallBack(const EventCallBack &cb) { _close_event = cb; }
    void SetEventCallBcak(const EventCallBack &cb) { _all_event = cb; }
```
这些回调函数都是通过上层来进行传入的，我们将设置进入channel中。

## 2.3 看看关注了什么
```cpp
    // 2. 是否设置了读写事件的关心
    bool ReadAble() { return (_events & EPOLLIN); }
    bool WriteAble() { return (_events & EPOLLOUT); }
```
这个是怎么做到的？
只要 `_events` 里面的 EPOLLIN 那一位是 1，就说明关注了读事件。（表示读事件就绪，可以读取了）。

|事件|含义|
|---|---|
|`EPOLLIN`|可读事件|
|`EPOLLOUT`|可写事件|
|`EPOLLERR`|出错|
|`EPOLLHUP`|对端关闭/挂起|
为什么这里不关注其余的两个事件呢？
因为 **`EPOLLERR` 和 `EPOLLHUP` 不需要主动注册，出错或断开时 epoll 会自动返回它们**。
一句话muduo 主动关注读写事件，因为网络程序主要关心“什么时候收、什么时候发”；错误和关闭事件不用主动关注，epoll 会自动通知。
## 2.4 设置关注的事件和取消关注
```cpp
    // 3. 启动对各个事件的关心
    void EnableRead()
    {
        _events |= EPOLLIN;
        Update();
    }
    void EnableWrite()
    {
        _events |= EPOLLOUT;
        Update();
    }
```
这两个就是关注读取，和写入。在是event之后，我们需要记得更新（update）本次的启动。
```cpp
    // 5. 关闭对各个事件的关心:读写
    void DisableRead()
    {
        _events &= ~EPOLLIN;
        Update();
    }
    void DisableWrite()
    {
        _events &= ~EPOLLOUT;
        Update();
    }
    // void DisableClose();
    // void DisableError();
    void DisableEAll()
    {
        _events = 0;
        Update();
    }
```
让上面的位置变成0，就可以了。这样就取消了对事件的关心


## 2.5 设置处理函数：
```cpp
    void HandlerEvent()
    {
        if ((_revents & EPOLLIN) || (_revents & EPOLLHUP) || (_revents & EPOLLPRI))
        {
            if (_read_event)
                _read_event(); // 只要读事件就绪或者挂断还是优先级都要读取读取上来了
        }
        if (_revents & EPOLLOUT)
        {
            if (_write_event)
                _write_event();
        }
        else if (_revents & EPOLLERR)
        {
            if (_error_event)
                _error_event();
        }
        else if (_revents & EPOLLHUP)
        {
            if (_close_event)
                _close_event();
        }
        // 无论如何任意事件都去调用这个
        if (_all_event)
            _all_event();
    }
```
需要注意的是，只要读事件或者挂断事件还是有紧急数据（EPOLLPRI）都是需要先启动读事件。
随后各个事件，自动执行。

## 2.6 类外实现：
```cpp
void Channel::Remove() { _loop->RemoveEvent(this); }
void Channel::Update() { _loop->UpdateEvent(this); }
```
需要详细的来讲讲，为什么放在类外，还有为什么需要_loop来执行：
因为 **真正管理 epoll 的不是 Channel，而是 EventLoop / Poller**。
它们大概关系是：
```
Channel：封装一个 fd 关心什么事件。
EventLoop：负责事件循环
Poller/Epoller：真正调用 epoll_ctl / epoll_wait
```
也就是说：
```
Channel 不直接 epoll_ctlChannel 只告诉 EventLoop：我的事件变了EventLoop 再让 Poller 去修改 epoll
```

可能讲到这里你还没有理解，那么我们来看看：