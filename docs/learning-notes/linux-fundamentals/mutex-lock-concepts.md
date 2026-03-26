# 1. 前言：
在我们之前学习的代码种，就是在建造多线程的路上，我们可以看到出现了乱码或者抢占输出，这是为什么呢？
本章将带着这个问题来带你思考：
 1. 一个例子先来领略问题的所在。
 2. 什么是线程互斥.
 3. 见识互斥锁。
 4. 使用互斥锁


# 2. 一个买票的例子：
假设我们有100张电影票，我们同时抢票会出现什么，我们来尝试写代码来看看：
```cpp
#include <iostream>
#include <thread>
#include <vector>
#include <string>
#include <cstdio>
#include <unistd.h>

int ticket = 100;

void routine(std::string name)
{
    while (1)
    {
        if (ticket > 0)
        {
            usleep(1000);
            // 说明可以开始抢票：
            ticket--;
            printf("%s shell ticket,now tickets number:%d\n", name.c_str(), ticket);
        }
        else
        {
            std::cout << ticket << std::endl;
            break;
        }
    }
    return;
}

int main()
{
    std::vector<std::thread> threads;
    for (int i = 0; i < 5; i++)
    {
        std::string name = "thread-";
        name += std::to_string(i);
        threads.emplace_back(routine, name);
    }

    for (auto &thread : threads)
    {
        thread.join();
    }

    return 0;
}

```
这里的公共的资源是ticket，很显然是五个线程去抢这个票数，其中我们用`usleep（1000）`来表示抢票消耗的时间。按照常理来说，我们一旦没票了就应该停止。
让我们运行来看看：
![image.png|644](https://cdn.jsdelivr.net/gh/Ningbottle/blog-images@main/img/20260313085013883.png)
结果会运行到 -4，我的票都没有了，这为什么会运行成为这样嘞：

## 2-1 原因：
如果是单线程来说，是不会发生这件事的，但是这里是多线程，多线程很大的一个特点就是竞争。
我们来看我们的代码，我们每个线程进入这个函数，都会拿到ticket的数量。随后休息一秒钟，在进行对其减减。

我们放慢过程，详细的来看看当票数为1的时候的情况：我们可以假设线程1拿到`ticket`票之后，发现是1，随后休息1秒，随后线程2启动，发现这个`ticket`也是1，也是可以进行减减。其中线程2也会休息一秒。我们线程1在拿入`ticket`，在进行减减，导致变成0。关键的来了，由于线程2之前做过了判断。可以进行减减，我们在对`ticket`进行减减，就导致变成了-1。

为什么是这样的过程，我来大致写写：
```asm
; if (ticket > 0)
LOAD R1, [ticket]       ; R1 = ticket
CMP  R1, 0              ; 比较 R1 和 0
JLE  END_IF             ; 如果 <= 0，跳走

; usleep(1000)
CALL usleep

; ticket--
LOAD R2, [ticket]       ; R2 = 当前 ticket
SUB  R2, 1              ; R2 = R2 - 1
STORE [ticket], R2      ; 写回 ticket

END_IF:
```
注意这里最重要的一点：
> **判断时用的是 `R1`，真正减法时又重新 `LOAD R2, [ticket]` 读了一次内存**

我上面写的可能有歧义，但是我们在联系汇编来详细的讲讲：
1. 线程1启动，发现`ticket`是1，可以进行减减，执行`usleep（1000）`。注意这里比较分三步：进入寄存器，比较，从寄存器种写回。
2. 线程2启动，发现`ticket`是1，可以进行减减，执行`usleep（1000）`。
3. 关键的来了，线程1对其减减。这个减减是进入寄存器，对进行减减，在写回ticket，这个`ticket`已经发生改变了，那么线程2，拿到的`ticket`就是已经被线程1改变的`ticket`了
4. 线程2对之后写回的`ticket`进行减减

这个就是经典的 **check-then-act race**。

# 3. 引入锁的概念：
为了防止上面的乌龙的事件，我们引入了锁的概念，先不说是什么，我们先来看看他的威力：
```cpp
#include <iostream>
#include <thread>
#include <vector>
#include <string>
#include <cstdio>
#include <unistd.h>

int ticket = 100;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

void routine(std::string name)
{
    while (1)
    {
        pthread_mutex_lock(&lock);
        if (ticket > 0)
        {
            usleep(1000);
            //说明可以开始抢票：
            ticket--;
            printf("%s shell ticket,now tickets number:%d\n", name.c_str(), ticket);
            pthread_mutex_unlock(&lock);
        }
        else
        {
            pthread_mutex_unlock(&lock);
            break;
        }
    }
    return;
}

int main()
{
    std::vector<std::thread> threads;
    for (int i = 0; i < 5; i++)
    {
        std::string name = "thread-";
        name += std::to_string(i);
        threads.emplace_back(routine, name);
    }

    for (auto &thread : threads)
    {
        thread.join();
    }

    return 0;
}
```
我们来看看代码运行的情况：
![image.png](https://cdn.jsdelivr.net/gh/Ningbottle/blog-images@main/img/20260313094916758.png)
我们可以看到，这个是没有问题的，的确完成了检票的任务。
那么这里的锁是什么？互斥锁 (Mutex)：
- **特点：** “互斥”即其名，同一时间只有一个线程能持有锁。
- **用法：** `pthread_mutex_lock()` 加锁，`pthread_mutex_unlock()` 解锁。

这里就是全局锁的初始化，和上锁和解锁。
## 3-1互斥锁上锁的位置：
我们先来回忆为什么需要上锁，是不是由于线程出现竞争导致公共资源出现混乱，所以一切访问公共资源的地方都需要上锁，一次只允许一个线程去访问，使用。
那么我讲的这些就是临界区的概念， 什么是临界区 (Critical Section)？
**临界区**是指代码中**访问共享资源**（如全局变量、外部文件、共享内存等）的那一部分程序段。
- **核心规则**：同一时刻，只允许一个线程进入临界区。
- **如果不保护**：就会发生“竞态条件”（Race Condition），导致数据毁坏。
- **保护方式**：进入临界区前**加锁（Lock）**，离开临界区后**解锁（Unlock）**。

## 3-2 解锁的时机：
我们可以看到我的代码无论是在if还是else，我们都会解锁，就是解除锁，有人就说了为什么不像后面这个代码一样，直接解除锁呢？
```cpp
        pthread_mutex_lock(&lock);
        if (ticket > 0)
        {
            //说明可以开始抢票：
            ticket--;
            printf("%s shell ticket,now tickets number:%d\n", name.c_str(), ticket);
            //pthread_mutex_unlock(&lock);
        }
        else
        {
            //pthread_mutex_unlock(&lock);
            break;
        }
        pthread_mutex_unlock(&lock);
```

那么else就永远不会解锁，他直接break，这就会导致出现另一个问题：这正是一种典型的死锁诱因：**一个线程在持有锁的情况下直接退出（如 break、return 或异常），而未释放锁**，导致其他需要该锁的线程永远等待。
 死锁产生的四个必要条件（Coffman 条件）：

1. **互斥**（Mutual Exclusion）：资源只能被一个线程独占。
2. **占有并等待**（Hold and Wait）：线程已持有至少一个资源，并等待获取其他资源。
3. **不可剥夺**（No Preemption）：资源只能由持有者主动释放。
4. **循环等待**（Circular Wait）：线程之间形成一条循环等待资源链。


## 3-3 线程拿着锁睡觉：
这是我们这个代码的另一个问题：
我们里面的usleep，应该删除掉，避免锁拿着线程进行睡觉，这是非常不合理的：
所以综合下来，我们的程序应该是这样的：
```cpp
#include <iostream>
#include <thread>
#include <vector>
#include <string>
#include <cstdio>
#include <unistd.h>

int ticket = 100;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

void routine(std::string name)
{
    while (1)
    {
        pthread_mutex_lock(&lock);
        if (ticket > 0)
        {
            //说明可以开始抢票：
            ticket--;
            printf("%s shell ticket,now tickets number:%d\n", name.c_str(), ticket);
            pthread_mutex_unlock(&lock);
        }
        else
        {
            pthread_mutex_unlock(&lock);
            break;
        }
    }
    return;
}

int main()
{
    std::vector<std::thread> threads;
    for (int i = 0; i < 5; i++)
    {
        std::string name = "thread-";
        name += std::to_string(i);
        threads.emplace_back(routine, name);
    }

    for (auto &thread : threads)
    {
        thread.join();
    }

    return 0;
}
```


## 3-4 一个现象：
![image.png](https://cdn.jsdelivr.net/gh/Ningbottle/blog-images@main/img/20260313101435559.png)

我们发现，一直是线程2在进行抢票：
>这一段里一直是 `thread-2` 在卖票，说明这段时间里它反复拿到了 CPU，并且每次也都先抢到了那把锁。

它先抢到 CPU，于是更有机会再次执行到 `pthread_mutex_lock`；而锁一旦被它释放，它又很快再次抢回来了。

所以互斥锁，并不能保证公平。


# 4 总结：

这篇文章从一张"神奇的负数车票"开始，带我们走进了多线程编程中最头疼的问题——竞态条件。当我们用五个线程同时去抢那100张票时，本该在票数为0时就停止的程序，竟然一路狂奔到了-4。这背后的元凶就是经典的 **check-then-act race**：线程A刚判断完票数大于0，还没完成减减操作，就被线程B抢占了CPU；等A回来继续执行时，手里的"旧情报"已经失效了，却还要对已经变了的票数再做一次减减。这种对公共资源的并发访问，如果不加以保护，数据就会像脱缰的野马一样乱套。

为了解决这个问题，我们引入了互斥锁（Mutex）这个"交通警察"。它保证同一时间只有一个线程能进入临界区——也就是访问共享资源的那段代码。加锁和解锁的时机很有讲究：锁的范围要刚好覆盖对公共资源的操作，但不能太大（比如不能把`usleep`也包进去，否则就是"拿着锁睡觉"，白白浪费别人的时间）；同时每一个分支路径都要记得解锁，不然就会触发死锁，让其他线程永远等在那里。文章最后也提了一个有趣的现象：即便有了锁，线程2还是能把票抢光——这说明互斥锁只保证互斥，不保证公平，谁抢到CPU谁就有机会先拿到锁。

总的来说，线程互斥是多线程编程的必修课。理解临界区、掌握锁的粒度、警惕死锁的四个必要条件，这些基本功打扎实了，才能写出既高效又安全的多线程程序。毕竟，在这个并发为王的时代，让线程们"有序竞争"比"野蛮抢食"要靠谱得多。
