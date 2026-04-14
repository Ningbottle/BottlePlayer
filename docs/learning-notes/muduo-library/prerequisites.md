# 1.前置知识库准备：
## 1.1 日志库：
在我最早做这个项目的时候，我还不知道有哪些有日志库，为了读者和自己理解我需要简单介绍一下cpp的日志库，还有日志库是做什么的和有哪些日志库：
### 1. 目前值得学习的日志库：
1. **spdlog**  
    现代 C++ 项目里很常见，轻量、快、API 简单，支持 console/file/async logging，格式化风格接近 fmt。学生做项目、桌面软件、服务端小工具都很适合。  
    官网：[spdlog.org](https://spdlog.org/)；GitHub：[gabime/spdlog](https://github.com/gabime/spdlog) 
2. **fmt**  
    严格说它不是日志库，而是格式化库，但 spdlog 深度依赖/借鉴它的格式化风格。学会 fmt::format，你写日志、字符串拼接、错误信息都会舒服很多。  
    GitHub：[fmtlib/fmt](https://github.com/fmtlib/fmt)
3. **glog**  
    Google 风格的 C++ 日志库，老牌、工程味比较重。值得了解 LOG(INFO)、CHECK() 这种写法，但新个人项目我一般不会首选它。  
    GitHub：[google/glog](https://github.com/google/glog)；文档：[Google Logging Library](https://google.github.io/glog/0.7.1/logging/)
4. **Boost.Log**  
    功能强，但学习成本明显更高。适合以后遇到大型 C++/Boost 项目时再学，不建议一开始就啃。  
    文档：[Boost.Log](https://www.boost.org/library/latest/log/)
这些库里面我只用过spdlog，我也是在学习第二个项目的时候才了解这些日志库的。

### 2. 那么日志库是做什么的呢？
我之前做的小项目都是自己写的日志库，我都是使用策略模式来设计的：
先用宏 `LOG(level)` 把日志等级、文件名、行号传给全局 `logger`，然后 `logger()` 返回一个临时的 `LogMessage` 对象；`LogMessage` 构造时先拼好日志左半部分，比如时间、等级、pid、文件名、行号；后面通过重载 `operator<<` 像 `cout` 一样不断追加日志内容；等这一整行代码执行完，临时对象自动析构，在析构函数里调用当前日志策略的 `SyncLog()`，把完整日志输出到控制台或者文件。这个手法本质是“临时对象 + 运算符重载 + RAII 析构自动提交”。
这样便于去调试，也可以选择在哪个文件夹下面的文件去查阅文件，这样便于我们查找和调试。
我们可以在一切程序完成或者异常之后根据等级发出信息，这样便于我们快速找到哪里是错误的。

## 1.2 any 类：
### 1. any类大致介绍：
这是我们也需要准备的一个类：any类，它可以装不止一种类型，在我们的muduo库中我们需要使用：其实在C++17中已经实现了。我们在实现这个的时候，是为了考虑到muduo的兼容性，才这样设计的。
先来简单介绍一下这个类，再回到一下为什么不能用模板来设计：
1. 我们可以一会装入`string`，一会来装`int`。那么我们可以在类内设计一个`holder`和`placeholder`，其中`placeholder`是真正的存放数据的地方，我们可以利用类内类完成类型擦除。到时候设计的时候再说吧！
2. 为什不能直接使用模板来设计呢，如果直接初始化了，那么这个只能是这一个类型了。

### 2. 在muduo库中的作用：
先说结论：在 **muduo** 里，`Any` 主要用来给 `TcpConnection` 挂一个**任意类型的上下文 context**，让每个连接都能保存自己的业务状态。
我们知道，在链接的时候，我们会有很多不同的信息，比如一会需要接受id，一会接受信息，这种类型复杂，直接变可以通过any类完成承接：这个 `context` 可以保存和连接绑定的任意数据，比如 **connection id、最后一次收到数据的时间、用户名** 等，这样业务代码不需要继承 `TcpConnection`，也不用搞复杂的 `TcpConnectionFactory`。


# 2. 开始设计
## 2.1 先来简单的any类：

先设计any类中的类，也是最主要的实现，这个主要是存放类中的信息的：
```cpp
    class holder
    {
    public:
        virtual ~holder() = default; // 虚析构函数
        virtual type_info type() const = 0; // 纯虚函数，返回类型信息 ,为什么需要const？
        virtual holder* clone() const = 0; // 纯虚函数，返回克隆的指针
    } // 这个类是基类，不是实现，实现放在子类中
```

```cpp
    template <typename T>
    class placeholder : public holder
    {
    public:
        placeholder(T val) :_val(val) {}
        virtual const std::type_info& type() const override { return typeid(T); }
        virtual holder* clone() const override { return new placeholder<T>(_val); }
    private:
        T _val;
    };  // 这个就是any类实现的主要体现
```
我们可以看到，这里是子类，里面需要存储什么类型，都是随着T来变化的。你可以随后改变，同时我们也要给clone，防止两个指向同一个地址。

随后就可以开始写准备拼凑一个完成的Any类了：
类内成员为 holder的为指针的成员，我们需要对外提供，构造和赋值。

### 1. 先看构造函数和析构函数：
```cpp
    Any() : _content(nullptr) {}
    ~Any() { delete _content; } // 所有权归Any对象，所以需要删除
```
这是两个最简单的函数，由于_content是我们new 出来的，所以记得删除，随着类的销毁而销毁。符合RAII的思想。
随后是拷贝构造函数，利用左值和移动拷贝：
```cpp
    Any(const Any& other) : _content(other._content ? other._content->clone() : nullptr) {}
    Any(Any&& other) noexcept : _content(other._content) { other._content = nullptr; }
    template <typename T>
    Any(T val) : _content(new placeholder<T>(val)) {}
```
先来看看比较简单的左值拷贝：如果不为空（true）就利用content的成员函数进行赋值，详见`clone`函数。
在来说说右值拷贝吧，为什么不需要进行判空呢？
- 左值必须要判空，是因为如果为空，直接进行使用clone会出现`nullptr->clone`,因为`_val` 为`nullptr`
- 而右值，或者将移动拷贝，是直接交换两边的指针，如果为空，也交换了，并没有出现这样的事情。

|构造函数|行为|是否需要判空|
|---|---|---|
|`Any(const Any& other)`|调用 `clone()` 深拷贝|需要，因为空指针不能调用函数|
|`Any(Any&& other)`|直接转移指针|不需要，因为只是复制指针值|

### 2. 再来看看赋值函数：
```cpp
// 赋值运算符
    Any& operator= (const Any& other)
    {
        // 构建other临时对象，随后交换，临时变量出来作用域直接销毁
        Any(other).swap(*this);
        return *this;
    }
    Any& operator= (Any&& other) noexcept
    {
        Any(std::move(other)).swap(*this);
        return *this;
    }
    template <typename T>
    Any& operator= (T val)
    {
        Any(val).swap(*this);
        return *this;
    }
```
这里面有3个情况，我个人可以分成两大类，一类是直接利用any来赋值的，还有一种则是利用val来赋值的
1. `Any& operator= (const Any& other)` ：
	左值any赋值，我们可以利用左值来构造一个临时变量，随后交换this本身来完成赋值，随着类消失临时自动析构
2. `Any& operator= (Any&& other) noexcept`：
	右值any 赋值，需要注意需要转换成右值，随后在进行交换，这里会更快一点，这是因为是右值构造，直接交换两个指针来
3.  `Any& operator= (T val)`：
	直接用val 来进行赋值，很简单，利用第三种val的构造在交换，完成赋值。

## 2.2 看看总体：
```cpp
#pragma once
#include <typeinfo>
#include <utility>

class Any
{
private:
    class holder
    {
    public:
        virtual ~holder() = default; // 虚析构函数
        virtual const std::type_info& type() const = 0; // 纯虚函数，返回类型信息 ,为什么需要const？
        virtual holder* clone() const = 0; // 纯虚函数，返回克隆的指针
    }; // 这个类是基类，不是实现，实现放在子类中
    template <typename T>
    class placeholder : public holder
    {
    public:
        placeholder(T val) :_val(val) {}
        virtual const std::type_info& type() const override { return typeid(T); }
        virtual holder* clone() const override { return new placeholder<T>(_val); }
    private:
        T _val;
    };  // 这个就是any类实现的主要体现
private:
    holder* _content; 
private:
    Any& swap(Any& other)
    {
        std::swap(_content, other._content);
        return *this;
    } // 交换两个any对象的值,利用copy-and-swap原则，避免异常安全问题
public:
    Any() : _content(nullptr) {}
    ~Any() { delete _content; } // 所有权归Any对象，所以需要删除
    // 左值和右值的构造函数
    Any(const Any& other) : _content(other._content ? other._content->clone() : nullptr) {}
    Any(Any&& other) noexcept : _content(other._content) { other._content = nullptr; }
    // 赋值运算符
    Any& operator= (const Any& other)
    {
        // 构建other临时对象，随后交换，临时变量出来作用域直接销毁
        Any(other).swap(*this);
        return *this;
    }
    Any& operator= (Any&& other) noexcept
    {
        Any(std::move(other)).swap(*this);
        return *this;
    }
    template <typename T>
    Any(T val) : _content(new placeholder<T>(val)) {}
    template <typename T>
    Any& operator= (T val)
    {
        Any(val).swap(*this);
        return *this;
    }
    template <typename T>
    const T* get() const
    {
        // 如果类型匹配，返回值的指针
        if (_content && _content->type() == typeid(T))
            return &static_cast<placeholder<T>*>(_content)->_val;
        // 如果类型不匹配，返回nullptr
        return nullptr;
    }// 获取值的指针
};
```
我们就巧妙的完成了对any类的设计。

```cpp
    const T* get() const
    {
        // 如果类型匹配，返回值的指针
        if (_content && _content->type() == typeid(T))
            return &static_cast<placeholder<T>*>(_content)->_val;
        // 如果类型不匹配，返回nullptr
        return nullptr;
    }// 获取值的指针
```
关于这个，我们为什么需要获取他的指针？
因为 `get<T>()` 的目的不是“复制一个值出来”，而是：**从 `Any` 内部找到原来存的对象，并返回它的位置。**


## 2.3 再来看muduo库的日志：
在前文中我们说需要一个日志库，既然我们已经写了一个any 库我们就需要自己来写一个 日志库吧：
我们也不做的那么麻烦：也不做策略模式，只是简单的做一个向显示屏上打印的日志库。
### 1. 先定义日志等级：
```cpp
#define INFO 0
#define DEB 1
#define ERR 2
#define LOG_LEVEL INFO
```
我们默认0为`INFO`，从后往前推则是等级提升，这个还是比较简单，所有定义基础等级，如果低于这个等级则不打印信息，在我们debug的时候一般都是INFO。

### 2. 如何格式化输出：
我们先讨论这个格式，由于是我们写的是muduo库，我们也需要讨论不同的线程之间的问题，所有必须要有pid，同时便于我们找到是哪个文件，是哪一行，所以也必须要有 `__FILE__` 和 `__LINE__`这个两个，同时还需要时间，我们需要几时几分，所以我们也需要获取当前的时间：
经过刚刚的分析，我们就知道了我们的格式大概是： [[0x7f8b6c001700,19:32:10,main.cc,88]server start, port=8080]，大概就是这样。
```cpp
#define LOG(level, format, ...)                                                                                       \
    do                                                                                                                \
    {                                                                                                                 \
        if (level < LOG_LEVEL)                                                                                        \
            break;                                                                                                    \
        time_t t = time(nullptr);                                                                                     \
        struct tm *ltm = localtime(&t);                                                                               \
        char tmp[32] = {0};                                                                                           \
        strftime(tmp, 31, "%H:%M:%S", ltm);                                                                           \
        fprintf(stdout, "[%p,%s,%s,%d]" format "\n", (void *)pthread_self(), tmp, __FILE__, __LINE__, ##__VA_ARGS__); \
    } while (0)
```

|占位符|实际内容|
|---|---|
|`%p`|当前线程 ID|
|`%s`|当前时间字符串，比如 `19:32:10`|
|`%s`|当前源文件名，来自 `__FILE__`|
|`%d`|当前代码行号，来自 `__LINE__`|
|`format`|你自己传进去的日志格式|
|`##__VA_ARGS__`|你额外传进去的参数|

你可能还有疑问就是当前是怎么获得时间的，（其实也是我的小疑惑），在复习这个项目的时候，我自己也没有理清楚，这个是怎么回事？
1. 先获得时间戳：`time_t t = time(nullptr);` 这里的t = 一个很大的数字，可以理解成到某个特定时间的间隔
2. `struct tm *ltm = localtime(&t);` 把t进行转换了成为一个结构体，这个结构体里面有时间，可以看后面的解释。
3. `strftime(tmp, 31, "%H:%M:%S", ltm);`最后进行转换成字符串。时分秒，就凑齐了，注意转换成了字符串、

```cpp
tm_hour  // 小时
tm_min   // 分钟
tm_sec   // 秒
tm_year  // 年
tm_mon   // 月
tm_mday  // 日
```

# 总结：
今天为我们的 muduo 库前置知识做了准备，主要实现了两个东西：一个是 **Any 容器**，一个是 **简易日志库**。为了让 muduo 库保持简单、减少外部依赖，我们都选择自己手写实现。

## 核心要点回顾

- **Any 类**：通过「`holder` 基类 + 模板子类 `placeholder` + 类型擦除」实现可以存放任意类型的容器。
    - 拷贝构造需要判空（空指针不能调用 `clone()`），移动构造不需要判空（只是交换指针）。
    - 赋值统一采用 **copy-and-swap** 思想：先构造临时对象，再 `swap`，保证异常安全。
    - `get<T>()` 返回的是内部对象的指针（而非拷贝），目的是定位原对象的位置。
    - 在 muduo 中用作 `TcpConnection` 的任意类型上下文 `context`，让每个连接保存自己的业务状态（如 connection id、最后收数据时间、用户名等），无需继承 `TcpConnection`。
- **日志库**：不做策略模式，只做一个向屏幕打印的简易日志。
    - 通过 `LOG_LEVEL` 控制等级（INFO / DEB / ERR），低于阈值不打印。
    - 利用宏 `LOG(level, format, ...)`，结合 `__FILE__`、`__LINE__`、`pthread_self()` 和时间，输出形如 `[线程ID,时间,文件,行号]内容` 的日志。
    - 时间获取流程：`time()` 拿时间戳 → `localtime()` 转结构体 → `strftime()` 格式化为时分秒字符串。

## 两大组件对比

|组件|是什么|为什么需要|核心实现技术|在 muduo 中的作用|
|---|---|---|---|---|
|**Any 类**|能存放任意类型的容器（类似 C++17 `std::any`）|连接中需要承接复杂多变的数据类型，避免继承和复杂工厂|类型擦除（holder + 模板 placeholder）、虚函数 `clone()`、RAII、copy-and-swap|作为 `TcpConnection` 的上下文 `context`，保存每个连接的业务状态|
|**日志库**|向屏幕打印的简易日志工具|方便调试、按等级定位错误、追踪文件与行号|宏 `LOG`、可变参数 `##__VA_ARGS__`、`__FILE__`/`__LINE__`、`pthread_self()`、`time`/`localtime`/`strftime`|输出带线程 ID、时间、文件、行号的日志，辅助多线程网络库调试|
