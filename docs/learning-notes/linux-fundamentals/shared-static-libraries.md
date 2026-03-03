# 1. 前言和知识回顾：
在前几篇文章中，我们写我们是打开磁盘上的一个文件，我们从内存到磁盘，我们详细的了解了每一个过程，了解了Linux下的一切皆文件的哲学思想的背后的基地：虚拟文件系统。

这篇文章我们详细的讲述的一个C语言文件时怎么编译和链接的，主要来讲Linux平台的链接和编译。在这个之前，我们还需要讲一下软硬连接。

本文主要目标：
1. 什么时软连接，什么时硬连接
2. 什么时动态库，什么时静态库
3. 变成可执行程序的过程（回顾 + 补充）


# 2.软硬连接的概念和作用：
在我们的之前的文章我们中讲过了什么是inode ，每一个文件都有自己的inode。系统在找文件，其实也不是找文件名，而是找文件的inode。
![image.png](https://cdn.jsdelivr.net/gh/Ningbottle/blog-images@main/img/20260203163144987.png)
我们可以利用 `ls -li`来看文件名的inode。

## 2-1 硬链接：
要想建立硬链接可以借助命令：`ln` 来完成。我们可以尝试以下：
![image.png](https://cdn.jsdelivr.net/gh/Ningbottle/blog-images@main/img/20260203163615975.png)
我们可以看到log.txt和log_hard是一个`inode`，都是543899.说明其实本质他是一个文件。
这里我们可以像像，这里就是他就是log.txt的别名。
如果我们往log.txt里面写入文字，按照我们上面的依靠，我们打开log_hard,应该也能看到。
![](https://cdn.jsdelivr.net/gh/Ningbottle/blog-images@main/img/20260203164241792.png)
在打开log_hard,我们会发现，随后在删除呢
![image.png](https://cdn.jsdelivr.net/gh/Ningbottle/blog-images@main/img/20260203164552591.png)
我们会惊奇的发现，我们无论是改变里面谁的内容，另一个都会随着改变。但是删除，建立的硬连接，反而是不会消失的


### 2-1-1 不会删除的原因：
其本质，这里面用到了我们在c++中常说的引用计数，我们的磁盘上面存贮了他的inode和数据。（在前面提到过）
1. **数据实体 (Data Blocks)：** 硬盘上真正存内容的块。
2. **索引节点 (Inode)：** 记录文件的权限、所有者和**硬链接数（Link Count）**的元数据。

每个文件都有一个 `i_nlink` 计数器。
- 当你创建一个硬链接时，计数器 $+1$。
- 当你删除（`rm`）一个硬链接时，计数器 $-1$

这样就很好理解了，只要你不全部删除，这个文件会删除的，因为他还有备份。
所以对于这个删除，我们也可以用：`unlink`这个命令来完成删除其中一个
>在Linux中建立硬链接，来完成备份，这个会更快，而不需要是用 `mv`命令。


## 2-2 软链接：
在上面的命令后面 + `-s`，意义为软的连接，我们可以来尝试一下：
![image.png](https://cdn.jsdelivr.net/gh/Ningbottle/blog-images@main/img/20260203165840806.png)
我们可以到了，完全不一样了，在前面原本是普通文件标识的 `-`变成了 `l`，我们再来看看它的inode，是不是不一样的：

![image.png](https://cdn.jsdelivr.net/gh/Ningbottle/blog-images@main/img/20260203170007490.png)
我们可以看到inode 是不一样的，我们不妨在观察的仔细点，为什么软连接后的新闻界，只有：8 个字节，其实还是很有说法的。
软链接和硬链接不同，它是一个**独立的新文件**。它的内容不是原文件的数据，而是**原文件的路径字符串**。
让我们数一下：
- 原文件名是：`l` `o` `g` `_` `h` `a` `r` `d`
- 字符个数：**8个**。

由于我们这里的路径是都在本目录里面的，所以，这里不需要加其他的路径，如果你是`ln -s /usr/bin/python3 my_py` ，这里它的字节数是不是正好等于 `/usr/bin/python3` 的长度。
这个很类似与windows平台下的快捷方式：
![image.png](https://cdn.jsdelivr.net/gh/Ningbottle/blog-images@main/img/20260203170703534.png)

### 2-2-1删除之后是什么样的：
我们来尝试把源给删除，这里会变成什么，其实你已经知道了，这里的大概变成一个无用的软连接。
![image.png](https://cdn.jsdelivr.net/gh/Ningbottle/blog-images@main/img/20260203170912481.png)
就是这样导致，我们变成了变成了红色。也无法打开了。
![image.png](https://cdn.jsdelivr.net/gh/Ningbottle/blog-images@main/img/20260203171015324.png)

## 2-3 继续深入发现：
![image.png](https://cdn.jsdelivr.net/gh/Ningbottle/blog-images@main/img/20260203171228743.png)
观察图片圈成红色部分，我们发现 `.`竟然和我们的本文件是一个inode ,说明了这两个文件其实本质是硬连接.我们在看看还有没有其他的发现:
![image.png](https://cdn.jsdelivr.net/gh/Ningbottle/blog-images@main/img/20260203171623473.png)
这两个竟然是一致的,说明 `..`正是 `lesson10`的上级目录.也是他的硬连接.这么说,我们也可以用目录来建立硬连接吗?
![image.png](https://cdn.jsdelivr.net/gh/Ningbottle/blog-images@main/img/20260203172048151.png)
结果是不被允许的,这是因为:单直接的回答是：**为了防止文件系统出现“无限死循环”，系统禁止用户手动创建目录的硬链接；而 `.` 和 `..` 是系统在初始化目录时自动创建的“受控硬链接”。**

### 2-3-1 为什么不被允许:
如果允许你手动执行 `ln ./dirA ./dirA/link_to_self`，会发生极其恐怖的事情：
- **环形结构（Cycle）：** 目录结构会变成一个圈。
- **死循环：** 当系统工具（比如 `find` 或 `du`）尝试递归扫描目录时，它们会陷入这个圈里不停地转下去，直到内存溢出或程序崩溃。
- **无法删除：** 如果两个目录互相硬链接，它们的引用计数永远不会减到 0，这会导致这部分磁盘空间永远无法回收。

那为什么 `..`和 `.` 可以存在嘞,我们观察发现有:它们确实是硬链接，你可以从你的第一张图中得到证据：
- 观察 `./` 那一行：它的硬链接数是 **2**。
    - 第一个链接是它的名字 `lesson10`。
    - 第二个链接是它内部的 `.`。
- 观察 `../` 那一行：它的硬链接数是 **11**。
    - 这意味着上级目录里有 9 个子目录（每个子目录里的 `..` 都指向它，再加上它自己的名字和它父目录里的 `.`）。


那为什么系统可以建立硬连接呢?
1. **受控性：** 它们的行为由操作系统内核严格定义，不会形成乱七八糟的环。 
2. **必然性：** `.` 方便程序指代当前位置，`..` 方便程序回溯。这是文件系统导航的基石。

# 3. 动静态库部分:
我们要讲动静态库,我们必须要回顾之前的gcc如何一步一步走到可执行程序的,为了更好的讲后面的内容,我们先来回顾一下:
![image.png](https://cdn.jsdelivr.net/gh/Ningbottle/blog-images@main/img/20260203191151244.png)
```bash
gcc -E hello.c -o hello.i # 预处理：-E 大写 
gcc -S hello.i -o hello.s # 编译：-S 大写 
gcc -c hello.s -o hello.o # 汇编：-c 小写 
gcc hello.o -o hello # 链接：-o 小写
```
这里还是很好记忆的，其中 `-o` 是目标成为什么，而前面是什么，怎么记住大小写，我们可以看到：

|选项|大小写|助记|
|:--|:--|:--|
|`-E`|大写|**E**xpand（展开）|
|`-S`|大写|**S**ource（源代码→汇编）|
|`-c`|小写|**c**ompile（编译单个文件）|
|`-o`|小写|**o**utput（输出文件名）|

我们今天讲的就是链接的事情。
![image.png](https://cdn.jsdelivr.net/gh/Ningbottle/blog-images@main/img/20260203192650746.png)
我们可以看到链接成功 ，并且我们使用 `ldd + 可执行程序`就可以看到，里面的链接情况。

## 3-1 动静态库的概念
库是预先编写好的、成熟的、可复用的代码集合。程序开发依赖许多基础库，避免了从零开始编码。库本质上是一种可执行代码的二进制形式，可以被操作系统载入内存执行.

1. 静态库： `.a[Linux]、.lib[windows]`
2. 动态库： `.so[Linux]、.dll[windows]`

其实我们在日常也是可能遇到这个错误的，你可能在打游戏的时候，会看到这样的错误：
![image.png](https://cdn.jsdelivr.net/gh/Ningbottle/blog-images@main/img/20260203193413281.png)
很有可能就是由于动态库的缺失导致失败的。

由于我的系统是`ubuntu 22.04`的系统，我们采用：`ls /usr/lib/x86_64-linux-gnu`来常看我们这个位置底下到底放了多少动静态库吧！
![image.png](https://cdn.jsdelivr.net/gh/Ningbottle/blog-images@main/img/20260205144004621.png)
我们可以看到，的确有很多动态库。下面我们来详细的讲解如何制作动静态库吧！

## 3-2 制作静态库：
静态库还是比较简答的，我们先来看看，我们之前写过的一份代码，分别是`mystudio.c`和 `mystudio.h`这两个代码，我们在自己手写一份，我们自己的 `strlen`实现，这样有了：
第一个部分`mystudio.c`:
```c
#include "MyStudio.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>


my_file* Buyfile(int fd,int flag){
    my_file* f = (my_file*)malloc(sizeof(my_file));
    f->fileno = fd;
    f->flag = flag;
    f->flush_flag = LINE_FLUSH;
    f->bufflen = 0;
    memset(f->outbuff,0,sizeof(f->outbuff));
    //把这些这么多全部设置为0。
    return f;
}

my_file* my_fopen(char* filename,char* mode){
    int fd = -1;
    int flag = 0;
    if(strcmp(mode,"r") == 0){
        flag = O_RDONLY;
        fd = open(filename,flag);
    }
    else if(strcmp(mode,"w") == 0){
        flag = O_WRONLY | O_CREAT | O_TRUNC;
        fd = open(filename,flag,0666);
    }
    else if(strcmp(mode,"a") == 0){
        flag = O_WRONLY | O_CREAT | O_APPEND;
        fd = open(filename,flag,0666);
    }
    if(fd < 0){
        perror("open error");
        return NULL;
    }
    return Buyfile(fd,flag);
}

void my_flush(my_file* file){
    int fd = file->fileno;
    ssize_t ret = write(fd,file->outbuff,file->bufflen);
    if(ret < 0){
        perror("write error");
        exit(2);
    }
    //其实写进去系统的缓冲区，我们就可以将：
    file->bufflen = 0;
}

void my_fclose(my_file * file){
    if(file->fileno  < 0) return;
    my_flush(file);
    close(file->fileno);
    free(file);
    file  = NULL;
}

int my_fwrite(my_file* file,void* str,int len){
    //先把str写到缓冲区中。
    if(len + file->bufflen > MAX){
        my_flush(file);
        file->bufflen = 0;
        //如果位置不够进行先给的文件刷新,并让bufflen = 0
    }
    
    if(len > MAX){
        //如果比缓冲区都要大，那么就直接写进系统的缓冲区,放回len的长度
        ssize_t ret =  write(file->fileno,str,len);
        if(ret < 0){
            perror("write fail");
            exit(2);
        }
        return len;
    }

    memcpy(file->outbuff + file->bufflen,str,len);
    file->bufflen += len;
    //我们已经把str ，写进去了my_file的长度中了，这次写成了，看看要不要刷新：
    if(file->outbuff[file->bufflen - 1]  == '\n' && file->flush_flag == LINE_FLUSH){
        //如果遇到了\n并且刷新模式行刷新
        my_flush(file);
    }
    return len;

}
```
第二个部分，`mystudio.h`：
```c
#pragma once

#include <stdio.h>

#define NONE_FLUSH 1
#define LINE_FLUSH 2
#define FULL_FLUSH 3
#define MAX 2048

struct IO_file {
    int fileno;//fd
    int flag;//是行还是满刷新
    char outbuff[MAX];//缓冲区
    char flush_flag;
    int bufflen;
};

typedef struct IO_file my_file;

my_file* my_fopen(char* filename,char* mode);
int my_fwrite(my_file* ,void* str,int len);
void my_close(my_file*);
void my_flush(my_file* file);
```
第三部分：


