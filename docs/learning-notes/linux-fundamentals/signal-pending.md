# 1 前言：
在上一篇文章我们初步的认识的了信号的分类，详细的讲述了信号是怎么产生的。接下来我们要详细的讲述一下信号的保存和处理（捕获）。
![image.png](https://cdn.jsdelivr.net/gh/Ningbottle/blog-images@main/img/20260306134946076.png)
为了拓展对Linux系统的认识，这里还会详细介绍Linux的中断机制。
因此，这个部分，我们将分成三个部分来详细的介绍信号的保存和信号的处理，以及Linux系统的中断。
今天，我们主要讲的是信号的保存。
# 2. 信号的保存：
## 2-1 认识相关信号的基础概念：
你可以能听过下面的概念，我们逐步梳理：
1. 递达：信号被处理的这个动作被称为信号递达（Signal Delivery）。
2. 未决：这个指的是信号从产生到递达的之前的状态（Signal Pending）。
3. 阻塞：一直逗留在未决状态，开启阻塞之后是：如果一个信号被阻塞，即使它产生了，也只能停留在“未决”状态，无法“递达”。直到进程解除了对该信号的阻塞，它才能被处理。

上面的最容易弄混淆就是阻塞状态和忽略的状态。我们注意的是忽略其实递达的一种形式，一种处理方式。
<font color="#c0504d">阻塞不同于忽略。忽略是递达的一种方式（处理了，但选择不理会）；阻塞是压根不让信号递达。</font>

## 2-2 内核中的体现：
在上面我们认识这些概念，接下来，我们深入内核详细的探讨内核的体现：
![image.png](https://cdn.jsdelivr.net/gh/Ningbottle/blog-images@main/img/20260306150607090.png)
先看这个表格，我们在内核中有三个表格：
1. 第一个block表格：就是阻塞表格或者说是阻塞位图 (Block Set / Signal Mask)
	- **别名：** 信号屏蔽字。
	- **作用：** 记录哪些信号当前被进程屏蔽了。
	- **原理：** 这是一个位图（Bitmap），每一位对应一个信号。如果第 n 位为 1，说明第 n 号信号被阻塞。
2. 第二个表格就是peding表格：
	- **别名：** 未决信号集。  
	- **作用：** 记录哪些信号已经到达，但还没被处理。
	- **原理：** <font color="#9bbb59">同样是一个位图。当信号产生时，内核将该位设为 1；当信号递达后，该位由内核自动清零。</font>
	- 这个信号产生的时候，这个位图就会设置成为1，知道处理完成后由内核处理归0.
3. 第三个表格是handler表格，这个也是最简单的：
	- **作用：** 规定了信号递达时该做什么。
	- **原理：** 这是一个**函数指针数组**。数组下标对应信号编号，内容是对应处理函数的地址。
	    - `SIG_DFL`：执行默认动作。
	    - `SIG_IGN`：忽略该信号。
	    - 自定义 `handler`：跳转到用户写的代码执行。

这里也可以看到，这个三个表格相互配合就完成了控制了信号的保存和控制。

## 2-3 sigset_t关键词（信号集）：
先来解释他是什么，其本质是一个类型，这个类型是一个很长的数据结构（通常是数组或长整型），用来表示 64 个二进制位（Linux 有 64 种信号）。
`sigset_t` 是 Linux/POSIX 标准中定义的一个**C 语言数据类型**，它的中文名叫**信号集**。
**它的作用：** 作为一个“容器”，帮你把这 64 个开关（0 或 1）存起来，然后再整体传递给内核。

这里其实和umask很像，都是巧妙的使用最小的内存来完成控制。
在这里直接操作二进制还是容易出错的，所以提供了一套控制这个变量的方法：
- `sigemptyset(&set)`：把所有位清零（所有开关全关）。
- `sigfillset(&set)`：把所有位置 1（所有开关全开）。
- `sigaddset(&set, SIGINT)`：把特定信号（比如 Ctrl+C 产生的 `SIGINT`）的那个位设为 1。

## 2-4 信号集操作函数：
**在学习了`sigset_t`函数之后，我们需要知道我们应该怎么利用这个来改变阻塞表格（注意：在这里用户基本上只有能力改变阻塞表，是无法改变pending表的，但是我们可以通过函数接口来查看）**
### 2-4-1 `sigprocmask`函数：
![image.png](https://cdn.jsdelivr.net/gh/Ningbottle/blog-images@main/img/20260306155724569.png)
1. 第一个参数是`int how`: 主要给出怎么操作，具体看下面的表格。
2. 第二个参数是`const sigset_t  *set`:这个我们应该很熟悉了，主要是我们之前讲的信号集。
3. 第三个参数是`sigset_t  *oldset`，这个是一个输出型参数。会放回一个老的阻塞表。

这里我们就详细的了解了操作有哪些：
![image.png](https://cdn.jsdelivr.net/gh/Ningbottle/blog-images@main/img/20260306160204689.png)
- `SIG_BLOCK`：**追加阻塞**。把你 `set` 里的信号加到内核当前的阻塞表里（相当于 `mask = mask | set`）。
- `SIG_UNBLOCK`：**解除阻塞**。把你 `set` 里的信号从内核阻塞表中移除。
- `SIG_SETMASK`：**直接覆盖**。不管内核原来阻塞了啥，直接把阻塞表替换成你的 `set`（最常用）。

### 2-4-2 读取“未决表”的接口：`sigpending`
![image.png](https://cdn.jsdelivr.net/gh/Ningbottle/blog-images@main/img/20260306160541535.png)
由于我们无法改变未决表（这是因为我们无法直接在内核里面发送信号，这个需要操作系统来完成），但是我们仍然需要查看未决表，因此可以尝试用这个接口来完成查看pending表格。
读取当前进程的未决信号集,通过set参数传出。  调⽤成功则返回0,出错则返回-1  。


## 2-5 使用上面的函数开始实践：
我们可以尝试使用一个简单的程序来验证上面的接口：
```cpp
void Printsignal(const sigset_t& pending)
{
    printf("我是一个进程pid: %d \n",getpid());
    for(int signo = 31;signo > 0;signo--)
    {
        //为什么倒着减 :会最先打印高位，符合我们的习惯
        if(sigismember(&pending,signo))
            std::cout << "1";
        else
            std::cout << "0";
    }
    std::cout << std::endl;
}


int main()
{
    sigset_t set,pending;
    sigemptyset(&set);//完成了初始化
    sigaddset(&set,2);//还是初步认证2号信号
    sigprocmask(SIG_SETMASK,&set,nullptr);
    while(true)
    {
        std::cout << "正在运行" << std::endl;
        sigemptyset(&pending);
        sigpending(&pending);
        Printsignal(pending);
        sleep(2);
    }
    return 0;
}
```
按照我们的预期，我们的数组的倒数的第二位应该会由0变成1.表示二号进程会阻塞住。导致无法终止。
![image.png](https://cdn.jsdelivr.net/gh/Ningbottle/blog-images@main/img/20260306165118937.png)

有了这个我们还可以测试之前无法被自定义捕获的信号。我们也在这里测试：
我们改变我们写的代码，接下来会有：
```cpp
void Printsignal(const sigset_t &pending)
{
    printf("我是一个进程pid: %d \n", getpid());
    for (int signo = 31; signo > 0; signo--)
    {
        // 为什么倒着减 :会最先打印高位，符合我们的习惯
        if (sigismember(&pending, signo))
            std::cout << "1";
        else
            std::cout << "0";
    }
    std::cout << std::endl;
}

int main()
{
    sigset_t set, pending;

    for (int i = 1; i < 32; i++)
    {
        sleep(2);
        std::cout << "接下来发送这个信号 :" << i << std::endl;

        //初步阻塞 i号信号
        sigemptyset(&set); 
        sigaddset(&set, i); 
        sigprocmask(SIG_SETMASK, &set, nullptr);

        raise(i);
        //查看阻塞状况
        sigemptyset(&pending);
        sigpending(&pending);
        Printsignal(pending);
        sleep(1);
    }
    return 0;
}
```
这里会发生什么情况呢，为什么呢，我们来查看发生了什么：
![image.png|644](https://cdn.jsdelivr.net/gh/Ningbottle/blog-images@main/img/20260306170607071.png)
这是什么错误呢，我们发现我们的代码中第二次再次重置了这个信号集，再次进入就会导致信号1不在阻塞，从而被**Hangup** ，的字面意思是“**挂断**”。
因此我们还需要重新改变：
```cpp
void Printsignal(const sigset_t &pending)
{
    printf("我是一个进程pid: %d \n", getpid());
    for (int signo = 31; signo > 0; signo--)
    {
        // 为什么倒着减 :会最先打印高位，符合我们的习惯
        if (sigismember(&pending, signo))
            std::cout << "1";
        else
            std::cout << "0";
    }
    std::cout << std::endl;
}

int main()
{
    sigset_t set, pending;
    sigemptyset(&set);
    sigemptyset(&pending);
    for (int i = 1; i < 32; i++)
    {
        sleep(2);
        std::cout << "接下来发送这个信号 :" << i << std::endl;

        //初步阻塞 i号信号
        sigaddset(&set, i); 
        sigprocmask(SIG_SETMASK, &set, nullptr);

        raise(i);
        //查看阻塞状况
        sigemptyset(&pending);
        sigpending(&pending);
        Printsignal(pending);
        sleep(1);
    }
    return 0;
}
```
最后的结果，如预料一般会在9号信号上面终止：
![image.png](https://cdn.jsdelivr.net/gh/Ningbottle/blog-images@main/img/20260306171242635.png)
我们可以开始调整，按照之前的测试跳过9号和19号信号，再次测试：
![image.png](https://cdn.jsdelivr.net/gh/Ningbottle/blog-images@main/img/20260307145030067.png)
结果我们发现最后的结果，确有三个0，这是为什么呢？
这里我们通过查询Gemini 最后发现是 ：
>**当 `i = 20` 时（关键转折点！）：** 你调用了 `raise(20)`。20 号信号（`SIGTSTP`）是一个**暂停信号**。内核一看：“哟，要暂停了！”于是，**内核作为连带动作，直接无情地把你刚才放在未决表里的 18 号信号（`SIGCONT`）给清除了！**
>你看到的三个 `0` 分别是：
>- **第 9 位 (`SIGKILL`)：** 你代码里手动跳过了。
>- **第 19 位 (`SIGSTOP`)：** 你代码里手动跳过了。  
>- **第 18 位 (`SIGCONT`)：** 被后面产生的 20、21、22 号暂停信号**自动清理**掉了。
> 
> 其本质是因为：
>这背后隐藏着 Linux（以及所有遵循 POSIX 标准的 Unix 系统）对进程状态管理的特殊约定：**“暂停信号”和“继续信号”是水火不容的。**


通过上面的实验，只有9和19号信号，不能发生改变。在 Linux 中，9 号信号是 **`SIGKILL`**，19 号信号是 **`SIGSTOP`**。它们之所以绝对不能被自定义（不能被捕捉、不能被阻塞、不能被忽略），完全是出于**操作系统的自我保护机制**。


# 3. 总结：
最后，这个文章主要讲述了信号的保存。这篇文章详细讲解了Linux中进程信号是如何在内核中被保存和管理的。核心在于理解信号的三种关键状态：**递达**（信号被处理）、**未决**（信号已产生但尚未处理）和**阻塞**（阻止信号递达，使其停留在未决状态）。阻塞与忽略不同，忽略是递达后的一种处理选择，而阻塞是根本不让信号到达处理阶段。在内核中，这通过三个关键数据结构协同实现：**阻塞表（信号屏蔽字）** 记录哪些信号被屏蔽，**未决表** 记录已到达但未处理的信号，**handler表** 定义每个信号递达时的行为（默认、忽略或自定义）。

为了操作这些状态，系统提供了 `sigset_t`（信号集）类型及相关函数来管理信号集合。用户主要通过 `sigprocmask` 系统调用来修改进程的阻塞表，从而控制信号的阻塞与解除，并使用 `sigpending` 来读取当前的未决信号集。文章通过实验验证了这些机制，并特别指出 `SIGKILL`（9号）和 `SIGSTOP`（19号）信号由于其至关重要的系统管理角色，无法被阻塞、忽略或自定义处理，这是操作系统的一项基本保护机制。实验还揭示了信号之间可能存在依赖或互斥关系（如暂停与继续信号），这些由内核自动处理。

能看到这里，已经真的很厉害了。和我一起加油吧！

