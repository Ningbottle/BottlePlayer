# 1. 前言：
在我们之前学习了线程的两个比较简单的接口，一个是`pthread_create`,另一个则是`pthread_join`,还讲述了线程在不同平台的实现，同时也详细的讲述了页表是怎么设计的。

这次本篇文章将瞄准着：
1. 线程的性质。
2. c++是怎么控制线程的
3. 线程的封装。

# 2. 线程的性质：
我们来回顾一下线程的特点：
1. **轻量级执行流**：线程是进程内部的一个执行路线（控制序列）。在Linux内核看来，线程是比传统进程更**轻量级的进程**，使用 `task_struct` 结构体管理。
2. **共享地址空间**：线程在**进程的地址空间内运行**，共享进程的绝大部分资源，如代码段、数据段、堆、文件描述符表、信号处理方式等。
3. **独立调度单位**：**进程是资源分配的基本单位，而线程是CPU调度的基本单位**。一个进程可以包含多个并发执行的线程。
4. **开销小**：创建新线程、切换线程的开销远小于创建和切换进程。因为线程切换时，虚拟地址空间不变，无需刷新TLB（快表），对CPU缓存更友好。


## 2-1 线程资源的共享：
我们一直在强调线程是共享资源的，原因是我们一直在强调线程是轻量化的：在我们切换线程的时候，我们是不需要切换页表，由于大部分的资源是共享的，在创建线程的时候，也不需要大量的参数。

那么线程的切换为什么要比线程要快很多：

| 开销项                 | 进程切换（不同地址空间）              | 线程切换（共享地址空间）         | 为什么线程更快（额外作用）              |
| ------------------- | ------------------------- | -------------------- | -------------------------- |
| **页表切换（CR3寄存器）**    | 是：`switch_mm()` 写新CR3指针   | 否：完全跳过 `switch_mm()` | 省几百个CPU周期（最大差异）            |
| **TLB flush（快表失效）** | 是（老CPU全刷；新CPU改PCID仍有开销）   | 完全不碰TLB（同一虚拟地址映射不变）  | 进程切换后大量TLB miss，后续内存访问变慢几倍 |
| **CPU缓存（L1/L2）**    | 冷启动：不同进程数据不共享             | 热缓存：共享代码、堆、全局变量      | 线程切换后立即高速执行；进程有“缓存污染”惩罚    |
| **内核内存管理结构**        | 要切换 mm_struct、active_mm 等 | 零开销（复用同一 mm_struct）  | 少函数调用、少检查                  |
| **寄存器+栈切换**         | 相同（`switch_to()` 汇编完成）    | 相同                   | 无差异（两者都只换这些）               |
| **其他（PCB、文件等）**     | 更多状态（不同PID、凭证等）           | 复用进程级资源              | 小幅节省                       |

总的来说就是：
1. 页表需要切换，寄存器cr3压力大
2. 快表失效，命中率变低。
3. 上下文不同导致CPU缓存变慢
## 2-2 线程强调栈资源独立：
为什么后面又开始强调每个线程的栈空间是独立的，一般情况，两个线程是不会乱访问线程各自的栈空间。
就像我们写的每个routine函数一样，每个函数都是一个独立的栈空间。这样就巧妙的为每一个线程分配每一个栈空间。
如果每个线程不具有独立的栈空间，相互之间的操作是混乱的，比如我定义一个变量，很有可能改变了其他的线程的变量，这样每个线程的工作是混乱的，无法高效的独立解决多任务。

1. **函数调用必须有自己的栈帧** 每个线程在执行函数时都要：
    - 存局部变量
    - 存函数参数
    - 存返回地址
    - 维护栈帧（frame）
	如果多个线程共用一个栈，A线程刚把 int x=5 压栈，B线程紧接着就覆盖它，导致栈混乱、程序直接崩溃。
2. **每个线程必须能独立递归/调用深层函数** 一个线程递归1000层，另一个线程同时也递归1000层——不可能共用同一块栈内存。
3. **Linux中实际实现**（你可以自己验证）：
    - 默认每个线程栈大小是 **8MB**（可通过 `ulimit -s` 或 `pthread_attr_setstacksize` 修改）。
    - 在 `/proc/<pid>/task/<tid>/` 目录下，每个线程都有自己的 `stack` 映射记录。
    - 用 `cat /proc/self/maps `或者` pmap <pid>` 就能看到多个 `stack` 段。


总的来说：
共享的是“进程级资源”（地址空间、堆、文件等）→ 实现“轻量”， 独立的是“执行上下文”（栈、寄存器、PC）→ 保证每个线程能安全独立运行。
这就是为什么教科书在说“线程是轻量级进程”的同时，一定要补充“**但拥有独立的栈空间**”——少了独立栈，线程就根本跑不起来！


## 2-3 线程的可分离：
我们上次讲述了，线程既然建立了，我们就必须要等待，不然就会僵尸进程一样出现资源的浪费和无法释放。

这是为什么呢？在前文中，我们讲过，线程除了在内核中在创建一个`struct_task`,还会在虚拟地址空间中占据共享区一段内存，如果不join，这些资源（`struct pthread`）不会自己释放，里面还会存贮一些栈空间、退出状态，无法释放。虽然小，但是一单积累多起来就会出现问题，同时，内核里面它会变成 zombie thread（线程僵尸），只保留退出状态（exit code）、线程 ID 等少量信息。

|状态|内核 LWP (task_struct)|用户空间栈 + TCB|什么时候释放|资源是否浪费（进程活着时）|
|---|---|---|---|---|
|**正常 join**|立即释放|join 时释放|pthread_join() 调用后|无|
|**不 join（zombie）**|保持 zombie|保持映射|**只有进程结束时**|是（推荐避免）|
|**pthread_detach**|线程退出时自动释放|退出时自动释放|线程 return/exit 后立即|无（最推荐短任务）|
|**进程结束**|全部释放|全部释放|进程终止（exit）|-|

这张表格就很详细的解释了不同的线程状态的资源是否浪费。

尝试使用设置线程分离，大概有两种方式，这里我们展示，创建之后立马分离：
![image.png](https://cdn.jsdelivr.net/gh/Ningbottle/blog-images@main/img/20260311180835887.png)

```cpp
#include <iostream>
#include <string>
#include <unistd.h>
#include <pthread.h>

void* threadRun(void * arg)
{
    std::cout << *(static_cast<std::string*>(arg));
    pthread_t tid = pthread_self();
    printf("%p\n",tid);
    return nullptr;
}

int main()
{
    pthread_t tid;
    std::string arg = "Thread-";
    pthread_create(&tid,nullptr,threadRun,&arg);
    int n = pthread_detach(tid);
    if(n == 0)
        std::cout << "分离成功" << std::endl;
    sleep(1);
    return 0;
}
```
我们可以看到运行结果为：
![image.png](https://cdn.jsdelivr.net/gh/Ningbottle/blog-images@main/img/20260311181817169.png)


# 3.线程的控制（c++）
```cpp
#include <iostream>
#include <string>
#include <thread>

void Hello()
{
    std::string name = "c++thread - 1";
    std::cout << name << "tid:"
              << std::this_thread::get_id() << std::endl;
}

int main()
{
    //方式1 ： 
    std::thread t1(Hello);

    //方式2 ：利用lambda函数来完成
    auto print_sum = [](int a, int b) {
        std::cout << a << " + " << b << " = " << a + b << '\n';
    };
    std::thread t2(print_sum, 10, 20);

    t1.join();
    t2.join();

    return 0;
}
```


# 4. 封装线程的接口：

我们先来尝试写一下这个代码：
```cpp
#include <iostream>
#include <pthread.h>
#include <string>
#include <functional>

namespace ThreadMoulde
{
    // bug：
    static uint32_t number = 0;

    class Thread
    {
        /* 为什么这里可以设置这样的：
          是将 原本为void* (*)(void*）分装成函数 */
        using func_t = std::function<void *()>;

    private:
        static void *routiue(void *arg)
        {
            Thread *self = static_cast<Thread *>(arg);
            // 错误点1： 重复赋值：self->_tid = pthread_self();

            // bug
            self->EnableRun();
            if (self->_isDetach)
                self->Detach();
            pthread_setname_np(self->_tid, self->_name.c_str());
            return self->_func();
        }

        void EnableRun()
        {
            _isRun = true;
        }

        void EnableDetach()
        {
            std::cout << "分离被成功了" << std::endl;
            _isDetach = true;
        }

    public:
        Thread(func_t func)
            : _tid(0), _isRun(false), _isDetach(false), _res(nullptr), _func(func)
        {
            _name = "thread-" + std::to_string(++number);
        }

        ~Thread()
        {
        }

        bool Start()
        {
            if (_isRun)
                return false; // 已经启动了，不需要启动了
            /*这里借助C语言的pthread_create 来完成创建
            这里更加重要的时，func时如何在routiue种定义的 */
            int n = pthread_create(&_tid, nullptr, routiue, this);
            if (n != 0)
            {
                std::cout << "create false" << std::endl;
                return false;
            }
            else
            {
                std::cout << "create true" << std::endl;
                return true;
            }
        }

        bool Join()
        {
            if (_isDetach)
                return false;
            int n = pthread_join(_tid, &_res);
            if (n != 0)
            {
                std::cout << "join false" << std::endl;
                return false;
            }
            else
            {
                std::cout << "join true" << std::endl;
                return true;
            }
        }

        bool Detach()
        {
            if (_isDetach)
                return false;
            if (_isRun)
                pthread_detach(_tid);
            EnableDetach();
            return true;
        }

        bool Stop()
        {
            if (_isRun)
            {
                int n = pthread_cancel(_tid);
                if (n != 0)
                {
                    std::cerr << "create thread error: " << std::endl;
                    return false;
                }
                else
                {
                    _isRun = false;
                    std::cout << _name << " stop" << std::endl;
                    return true;
                }
            }
        }
        private:
            pthread_t _tid;
            /* 接下俩是控制线程的性质的 */
            bool _isRun;
            bool _isDetach;

            void *_res; // 返回值
            func_t _func;
            std::string _name;
        };
    }
```

这个就封装了一个线程，但是这里面还有两个bug，我们后面来改，我们来写一个主函数来测试：
```cpp
#include "Thread.hpp"
#include <vector>
#include <unistd.h>
using namespace ThreadMoulde;

int main()
{
    std::vector<Thread> v1;
    for (int i = 0; i < 10; i++)
    {
        v1.emplace_back([]()
                        {
        while(true)
        {
            char buff[128];
            pthread_getname_np(pthread_self(),buff,sizeof(buff));
            std::cout << "我是一个新线程: " << buff << std::endl; // 我的线程的名字是什么呀？debug
            sleep(1);
        } 
        return nullptr;
    });
    }

    for(auto& thread: v1)
    {
        thread.Start();
    }

    for(auto& thread: v1)
    {
        thread.Join();    
    }

    return 0;
}
```

我们来看看运行结果似乎是有些问题：
![image.png](https://cdn.jsdelivr.net/gh/Ningbottle/blog-images@main/img/20260312152857155.png)

这个问题，我们下次来解决。