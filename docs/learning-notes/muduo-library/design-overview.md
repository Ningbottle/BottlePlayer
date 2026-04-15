# 1. 前言：
我们在之前的课程学习过什么是tcp的链接，不过我们写的都是小打小闹，这次我们需要写一个高性能的服务器，我们立马想到了非阻塞的多轮转的epoll模型
1. 先来回顾一下什么是一个正常的网络连接和发送，这里先以服务器端口来说明：
		-  先创建套接字： 创建了一个网络通讯的节点，本质是一个fd，我们可以通过这个fd来完成后续的操作。
		-  我们有了套接字，接下来需要干什么，我们需要开始绑定端口号了，有了端口号，主机才知道这些数据要交给哪些程序去使  用，`bind (sockfd,port) `具体是怎么回事：以后发到这个 `IP:端口` 的数据，都交给我这个 `sockfd`。数据到达服务器后，操     作系统内核会查：8080 这个端口归哪个 socket 管？查到后，就把连接交给对应的服务程序。
		-  随后就开始监听了，这个就像门外的服务员，只管照顾人进来，监听有没有新的连接到来。如果有进行下一步。
		-  就开始accept了，这里就像店内的服务员，是专门针对本次连接的处理，此时两人建立新的fd:connfd,(也是sockfd)，这个是为后  面的recv和send做准备了。
2. 再来讲讲epoll 模型：
		我们如果每个connfd都需要我们关注，你不可能一个个 `recv()` 去试，因为没数据的 socket 会卡住或者浪费 CPU。所以 `epoll` 的作用是：**帮你监控很多 fd，哪个 fd 有事件，就通知你处理哪个。** 这样就很快就可以进行收发消息。

我们补充了上面的两个知识，我们就可以进行开始设计了

# 2. 我是怎么设计muduo库的

## 0. 起点
```cpp
	sockfd -> bind() -> listen -> accept -> recv/send
```
这个很简单，但是我们发现问题：阻塞 + 一连接一线程，扛不住高并发

## 1. 想办法掌握多个fd:
我们立马就联想到了select和poll还有epoll，这里我们选择epoll：
select/poll 每次调用都要把**全量 fd 集合**从用户态拷进内核、并在内核里**线性遍历**，O(n)；epoll 用红黑树管 fd、就绪的塞进一个就绪链表，`epoll_wait` 只返回就绪的那些，**开销跟活跃数相关而非总数**。
它可以掌握多个fd有没有继续，我们接下来想的是要不要阻塞还是设置非阻塞。这里肯定是非阻塞的。我们不可能因为一个没有就绪就等待她。
IO 多路复用 epoll，用 epoll 一次监控大量 fd。
那么是选择LT还是ET模式呢？
1. LT 来一点数据就读一段，好写，简单
2. ET 只有“状态发生变化”的那一刻通知你一次。这一次你必须 `while` 循环读到 `EAGAIN` 把它读干净，否则剩下的要等下一批新数据才会再触发，可能饿死。

- LT(我用的)：有数据就一直通知，能读一点算一点 → handleRead 只 recv 一次就行。
- ET：来数据那一刻只通知一次，必须循环读到 EAGAIN。
- 我没设 EPOLLET，所以是 LT，选它是因为正确性优先、不怕"没读完饿死"。

## 2.  那么sockfd就绪了应该怎么办？
我们都知道epoll掌控了这些fd之后，epoll 返回一堆 fd，每个 fd 该干嘛？ → 【Channel】，我们可以封装一个channel，这个channel可以怎么翻译呢？fd 事件封装器，这是最好的理解，也可以翻译成：事件通道。

这个channel应该怎么设计呢？ 
1. `sockfd` 这个一点是需要的，确定这个是什么事件，也可以命名为：fd
2. 还需要`int events_; // 关心什么事件：读/写/错误`  `int revents_; // epoll 实际返回了什么事件`，这两个是必备的
3. 还需要事务处理的函数，比如：ReadCallback，可读的时候要干什么！
```cpp
int _fd;              // 监听哪个 fd
int _events;          // 关心什么事件：读/写/错误
int _revents;         // epoll 实际返回了什么事件
EventLoop* _loop;     // 属于哪个事件循环
ReadCallback;         // 可读时干什么
WriteCallback;        // 可写时干什么
CloseCallback;        // 关闭时干什么
ErrorCallback;        // 出错时干什么
```
我们在来详细的讲讲： `events`和`revents`这两个。
先来看看有哪些事件：
```cpp
EPOLLIN   // 可读事件
EPOLLOUT  // 可写事件
EPOLLERR  // 错误事件
EPOLLHUP  // 对端关闭
```
本质是：
```cpp
#define EPOLLIN   0x001
#define EPOLLOUT  0x004
#define EPOLLERR  0x008
```
其实就是不同位置上的1，第一个是0001，第二个是0010，第三个是0100，大致是这样子的来区分的。
大致可以这样来看： 

|事件|含义|
|---|---|
|`EPOLLIN`|fd 可读，有数据来了|
|`EPOLLOUT`|fd 可写，可以发送数据|
|`EPOLLERR`|fd 出错|
|`EPOLLHUP`|对端挂断|
|`EPOLLET`|使用 ET 边沿触发模式|
|`EPOLLONESHOT`|事件只触发一次|

## 3. 谁来管理这些channel
虽然说channel设置了很多处理函数，但是怎么管理这些channel，我们就引入了新的结构：谁调 epoll_wait、谁管这些 Channel？ → 【Poller】这里的poller我们可以翻译成为：事件轮询器。
- 封装 epoll_create / epoll_ctl(ADD/MOD/DEL) / epoll_wait。 
- 维护 unordered_map<fd, Channel*>。
- Poll()：把活跃 fd 找回对应 Channel，填进 active 数组返回。

这里可以详细的来讲讲，为什么需要返回一个active数组：
这里我们可以理解成，poller只是一个事务轮询起，他是不参与已经活跃的连接应该要干什么，比如有1000个事件在poller里面等待，只有3个就绪了，就返回`std::vector<Channel*> activeChannels;`交给下一步来处理，简单来说因为 **EventLoop 不想直接处理 fd，它只想处理“已经有事件的 Channel”**。


## 4. 活跃事件应该怎么处理：
要有个循环不停"等事件→分发→干活" → 【EventLoop】，Poller 负责从内核拿事件，activeChannels 负责把“有事件的 Channel”交给 EventLoop 处理。

- loop：Poll() → 遍历 active 调 HandlerEvent() → RunAllLoop() 跑任务队列。
- one loop per thread：一个 loop 绑死一个线程，连接的事件只在它的 loop 里处理 → 基本无锁。 
- 用 _thread_id 做"我在不在本线程"的断言。

为什么需要在一个线程中运行呢？
我们可以想想eventloop在干什么：当活跃连接来的时候，开始处理这些活跃连接，他一直在处理：等事件 → 拿到活跃 Channel → 执行回调这个循环，天生就适合在一个线程中做，如果在两个线程还会有竞争压力，需要锁来消耗时间。

## 5.  单线程吃不满多核 
引出了【LoopThread / LoopThreadPool】
我们的主线程不做任何执行active队列的任务，我们的架构为主从reactor，main loop只做IO转接，就是只负责accept 。
 LoopThread = 一线程 + 一 EventLoop；Pool 用 RR 轮流分配。

## 6. 多线程怎么配合呢？
- RunInLoop / QueInLoop - accept 在 main 线程，却要把"给新连接注册读事件"丢到 sub 线程做。
- RunInloop：在本线程直接执行，否则 QueInloop 入队 + 唤醒。

## 7. epoll_wait 在阻塞，怎么叫醒它去执行新任务？
- 每个 EventLoop 一个 wakeupFd(eventfd)，注册进自己的 Poller。
- 入队后往它 write 8 字节 → epoll_wait 返回 → 执行任务队列。 
- 为什么用 eventfd 不用 pipe：只占 1 个 fd、开销小、纯通知。

这个就是muduo库中独特的线程通讯机制，跨线程唤醒 EventLoop，让它从 epoll_wait 里出来执行 pendingFunctors。

## 8. 一条连接的读/写/缓冲/状态/回调谁统一管？
我们已经封装了channel，那么channel只有一个事件就绪之后应该干啥！但是一个连接的读/写/缓冲/状态/回调，为什么需要这个？为什么要命名成为connection？
Channel 只知道“哪个 fd 发生了什么事件”，但它不知道“这个 TCP 连接该怎么处理”。所以需要 `Connection` 来封装一个完整连接的上下文。
一个Tcp需要知道下面这些情况：
```
这个连接属于哪个 EventLoop
这个连接的 socket fd 是多少
这个连接当前状态是什么
读缓冲区 inputBuffer
写缓冲区 outputBuffer
连接建立回调 connectionCallback
消息到达回调 messageCallback
写完成回调 writeCompleteCallback
关闭回调 closeCallback
```
Channel：负责感知 fd 事件
Connection：负责处理这个连接的完整生命周期

那么 connection 有：
- 持有 Socket + Channel + in/out Buffer + 状态机 + 回调。
- 状态机：CONNECTING → CONNECTED → DISCONNECTING → DISCONNECTED。
- 优雅关闭：shutdown 时若还有数据没发完，转 DISCONNECTING，发完再真 Release

# 3. 暂时我们先考虑这么多
其实我们的muduo还缺少缓冲区，用于收发消息的位置，还缺少时间轮，这些都是muduo库不可缺少的组件，我们可以稍微回顾文章是怎么做到的。
1. 想要同时管理多个事件是否就绪： epoll 模型：fd就绪进行返回。
2. fd就绪，怎么知道要怎么坐什么，封装了channel，里面有事务的读写错误还有任意事件的回调，还有关闭时要做什么
3. 怎么管理这些事件呢？（先组织后管理）poller，他是Poller 是「事件监听/检测器」（IO 多路复用器），看谁活跃了，进行添加进入活跃队列
4. 活跃队列就绪了，poller 负责从内核拿事件，activeChannels 负责把“有事件的 Channel”交给 EventLoop 处理。
5. 什么样的架构，主从reactor，main loop 只负责 accept，其余通过轮询RR 分配任务。
6. 既然要分配，那么就引入了pool，规定谁是主线程，附属线程有哪些。

我们大致考虑了一下，大概就是这个样子的。

