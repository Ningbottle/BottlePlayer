![请添加图片描述](https://i-blog.csdnimg.cn/direct/388e57244ea04b9db7c5ed79c74f5c54.gif)

> [!note] 关联入口
> - 上位入口：[[1.c++学习/00-学习入口]]
> - 相关笔记：[[1.c++学习/22.【c++基础】c++的类和对象]]、[[6.4月C++岗位面试冲刺/02. 2026-04-02 - 引用const重载与缺省参数八股]]
> - 下一步：[[1.c++学习/22.【c++基础】c++的类和对象]]


#  1. C++的第一个程序：
我们先来看我们今天写的第一个代码：
![在这里插入图片描述|816](https://i-blog.csdnimg.cn/direct/976464a0bc8648019ca4039c33b28934.png)
会发现有很多错误，这是为什么呢？我们继续进行改进：
![在这里插入图片描述|774](https://i-blog.csdnimg.cn/direct/f97cc304b181424abd4a500d9f4ee8df.png)
我们又可以看到这里又ok了，原因就``namespace``中，接下来我们要详细介绍
## 1.1``namespace``的作用:
### 1.1.1先简单的介绍一下namespace
C++的namespace（命名空间）是一种封装机制，用于解决代码中的命名冲突问题，尤其在大规模项目或多团队协作中至关重要。比如：
1. 当不同库或模块定义了同名变量/函数时，编译器无法区分。命名空间通过限定作用域隔离这些标识符：

```cpp
namespace LibA { void print() { /* ... */ } }
namespace LibB { void print() { /* ... */ } }
int main() {
    LibA::print(); // 明确调用LibA的print
    LibB::print(); // 明确调用LibB的print
}
```
2. 命名空间可将相关类、函数、变量等逻辑分组（如Math::Vector、FileSystem::Path），提升代码可读性和模块化。
### 1.1.2.为什么需要``using namespce std`` 
1. std是C++标准库的命名空间​：
所有标准库组件（如cout、endl、vector）均封装在std中，避免与用户自定义标识符冲突，这里我需要这个命名空间，不然找不到这个cout这个关键词。
2. 我们不使用``using namespace std``可以使用``std::``。如下图：
![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/cdd2b3a03ca242e19ba898cdd3355d5f.png)
运行结果也没有问题。
![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/fadcae2132784393ae0b10638f447cf2.png)
## 1.2 C++的输出和输入流：
###  1.2.1.粗略的介绍：
我们来看头文件：``iostream``是Input Output Stream 的缩写，是标准的输入输出流，很类似C语言的各种流
![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/db0db2cc9f30411c8b41a3fbc0934d4f.png)
这里我们就可以看到cout和cin的作用，其中cin是输入流，从键盘读取，由用户输入，cout由是输出流。
面向对象​：通过对象（cin/cout）和运算符重载（<</>>）实现数据流式传输，语法直观

```cpp
int num;
std::cout << "Enter: ";
std::cin >> num;  // 自动识别类型
```
### 1.2.2详细对比：
先看一段代码：

```cpp
#include <iostream>
int main() {
    int num = 42;
    double pi = 3.14159;
    std::string name = "Alice";
    std::cout << "整数: " << num << "\n";    // 输出整数
    std::cout << "浮点数: " << pi << '\n';   // 换行符（'\n' 比 endl 更高效）
    std::cout << "字符串: " << name << std::endl; // endl 换行并刷新缓冲区
}
```
c语言中的printf和scanf依赖格式化字符串指定类型（如 %d、%s），需严格匹配参数，而cout和cin无需匹配格式化字符，自动匹配。
![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/fa7aebefa772491da4238306fe6130f3.png)
# 2. c++的函数设计：
## 2.1 缺省参数：
### 2.1.1.缺省参数的简介：
缺省参数在函数声明或定义时指定，语法为 参数类型 参数名 = 默认值。调用时若省略实参，则使用默认值，我们来看一段代码：

```cpp
void print(int a = 0) {
    cout << a << endl;  // 未传参时输出 0
}
```
正如上述代码，如果不给a传入参数默认打印0，缺省函数的作用有：
避免重复代码：简化高频调用场景。
增强接口灵活性：支持部分参数可选，减少**重载函数**数量。

### 2.1.2缺省参数的分类：
1. **全缺省参数**​：所有参数均有默认值，调用时可传递 0 至全部实参。例子如下：

```cpp
void func(int a = 10, int b = 20, int c = 30) {
    cout << a << ", " << b << ", " << c << endl;
}
// 调用示例：func(), func(1), func(1, 2)
```
如果什么都不传，就是全部调用默认值，，其中func(1)是给a赋值，我们可以依次来看结果，如图：
![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/3d8f419aa9084ef8b861de079c96fc4e.png)

2. **半缺省参数​**部分参数有默认值，​必须从右向左连续设置，左侧参数不可单独缺省。例子如下：

```cpp
void func(int a, int b = 20, int c = 30);  // 正确：b、c有默认值
void func(int a = 10, int b, int c = 30);  // 错误：左侧a缺省但中间b未缺省
```
其中第二个是错误，我们的参数赋值必须是连续的，而前一定是有右向左的连续,再看一个例子：

```cpp
void f(int a, int b = 5, int c = 10);  // 合法
void f(int a = 1, int b, int c = 10);   //  非法
```
### 2.1.3 缺省函数的小规定：
1. **声明与定义分离**
 - 只能在声明或定义中一处指定默认值，通常建议在声明中指定（头文件）。
 - 若两处同时指定且值不同，引发编译歧义。

```cpp
// 头文件（声明处）
void foo(int x = 10);  
// 源文件（定义处）
void foo(int x) { ... }  // 正确：定义处不再指定
```

2. **默认值类型限制**
默认值必须是常量、全局变量或常量表达式，不可为局部变量。

```cpp
const int kDefault = 42;
void bar(int val = kDefault);  // 合法
```
3. **避免与函数重载冲突**
避免与函数重载冲突，例子如下：这样就造成了歧义。：

```cpp
void log(int x);                   // 重载1
void log(int x, int y = 0);        // 重载2
log(5);  //  歧义：两个函数均匹配
```
## 2.2 函数重载：
允许在同一作用域内定义多个同名函数，但要求它们的 ​参数列表（参数类型、数量或顺序）必须不同，这样就构成了多态，这是C语言做不到的，笔者在学习c++的时候就能明显感受到c++的方便性。
我们来看函数重载在c++中有多方便吧：
![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/33818612729d49c68e11a1a502f1f3b1.png)
如果是在C语言中的化，那么，我们需要重新命名这些函数，而c++就很方便了这样也是c++的新特性。我们继续深究函数重载。
比如有下面几种：
1.  ``void process(int a, double b);  // 先 int 后 double``
	``void process(double a, int b);   // 先 double 后 int``
	这样是函数的重载是为顺序不同。
2. `` void display();          // 无参版本``
	``void display(int a);     // 单参版本[2,5](@ref)``
	这样就是数量不同，也构成了函数的重载。

# 3 引用
引用时c++相比与C语言中类似的就时指针，那么引用有什么特殊的。我来详细介绍。
## 3.1 引用的特性：
第一点最重要的就时引用时相当于给变量取了一个新的外号，仍然时控制原有的变量，你可以给一个变量取更多的外号，这都是无关的，比如
![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/daf48feb99874f4abc0d45617f796f3f.png)
这里就相当于给a取看一个新的名字叫b，通过b来改变了a的值。同时注意在引用的时候时不创建新的地址的，而是关联老地址。同时还要注意，不能对nullptr进行引用。同时作为小名引用了，不能借给其他人使用了。
## 3.2分类：
1. 
```cpp
void swap(int& a, int& b) { // 经典交换函数
    int temp = a;
    a = b;
    b = temp;
}
```
这样就方便了很多，比起C语言也好了很多。

2. 

```cpp
void print(const std::string& str) { 
    std::cout << str;  // 可接受字面量或变量
}
print("Hello");  // 临时字符串生命周期被延长
```


