![请添加图片描述](https://i-blog.csdnimg.cn/direct/d488c8283d5d4f3e947c150af954328e.jpeg)
关注我，学习c++不迷路:

[个人主页：爱装代码的小瓶子](https://blog.csdn.net/2301_80127108?spm=1011.2426.3001.5343)
专栏如下：
1. [c++学习](https://blog.csdn.net/2301_80127108/category_13027195.html?fromshare=blogcolumn&sharetype=blogcolumn&sharerId=13027195&sharerefer=PC&sharesource=2301_80127108&sharefrom=from_link)
2. [Linux学习](https://blog.csdn.net/2301_80127108/category_13061104.html?fromshare=blogcolumn&sharetype=blogcolumn&sharerId=13061104&sharerefer=PC&sharesource=2301_80127108&sharefrom=from_link)

后续会更新更多有趣的小知识，关注我带你遨游知识世界
![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/0a6a1e63b620424aa924e3fc8adec1ce.png)
期待你的关注。

![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/8b158c5e0c9343d0b2a5560e6914c25b.png)

---

@[toc] 


---
# 1. 什么是模板？
在之前的cpp的章节我们已经讲过了，这里我们重提一下：
模板是C++中支持泛型编程的核心工具，它允许你编写与数据类型无关的代码，就像一个可以创造出大量具体函数或类的“蓝图”。简单来说，模板让你只编写一次通用逻辑，编译器就能根据你使用的具体类型，自动生成对应的特定代码。

这正是cpp进步的地方，我们无需对每个类型都进行编写类或者函数。简单来说，我们利用编译器来完成最后的实例化。

我们再看：
| 特性维度       | 函数模板 (Function Template)                              | 类模板 (Class Template)                               |
| ---------- | ----------------------------------------------------- | -------------------------------------------------- |
| **核心概念**​  | 生成通用函数的蓝图。关注**操作**的通用性。                               | 生成通用类的蓝图。关注**数据结构**的通用性。                           |
| **主要用途**​  | 定义可处理多种类型的通用算法，如交换、比较等。                               | 定义可容纳多种类型的通用容器，如数组、队列等。                            |
| **实例化方式**​ | **隐式实例化**为主：编译器通常能根据传入的实参自动推断类型参数<br><br>。            | **显式实例化**为必须：使用时必须显式指定类型参数，如 `Stack<int>`<br><br>。 |
| **简单例子**​  | `template <typename T> void swap(T& a, T& b) { ... }` | `template <typename T> class Stack { ... };`       |



---

# 2. 模板的进阶用法：
>我们简单讲了模板，那么模板是否还具有其他值得深究的小特性呢？
我们来看：
## 2.1 非类型模板参数：
>顾名思义，传入的这个参数他并不是类型。在这之前，我们时常传入<class T>,j将T作为类型，当传入什么类型，他就实例化成什么类型。而这里，传入并不是类型，模板参数不仅可以传递类型，还可以传递一个具体的值（必须是编译时常量），比如一个整数。那这样有什么用呢？

一张表格理清楚：
| 特性维度       | 说明与示例                                                                                                                                                                                                        |
| ---------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| **基本概念**​  | 用一个常量作为类或函数模板的参数，在模板中可将其视为常量使用。例如 `template<class T, int N> class Array;`中的 `N`。                                                                                                                             |
| **主要用途**​  | 编译时确定大小或数值，提升性能与类型安全。例如实现固定大小的栈（`Stack<int, 20>`）或静态数组（`std::array<int, 100>`）。                                                                                                                              |
| **允许的类型**​ | - 整型（如 `int`, `size_t`）  <br>- 枚举  <br>- 指向对象/函数/成员的指针（需指向外部链接对象）<br>- 对象的左值引用<br>- `std::nullptr_t`                                                                                                         |
| **关键限制**​  |  **不支持浮点数**（`double`）和**类类型**（`std::string`）作为非类型模板参数。  <br>-参数必须是**编译时常量表达式**。例如 `Stack<int, 10>`有效，而 `int size=10; Stack<int, size> s;`无效（除非 `size`是 `constexpr`）。  <br>- 若为指针或引用，不能指向字符串常量、临时变量或内部链接对象。 |


这样通过这个我们可以定义一个固定长度的数组。例如：

```bash
#include<iostream>
using namespace std;

template<class T,size_t N = 100>
class stack {
public:
	void func
	{
		N++;
	}
private:
	int _a[N];
	int _top;
};
```
我们利用这个定义了一个定长数组，同时调用func也可以增加arr的数组的长度。其实也可以直接传入数字，来生成数组的长度。
 -  C++20之前，只允许整型做非类型模板参数
 -  C++20之后，可以支持double等其他内置类型做非类型模板参数。
 -  非类型的模板参数必须在编译期就能确认结果。

这也是模板的一个小作用。
>非类型模板参数是C++模板编程中的一项强大工具，它允许在编译期将常量值嵌入类型信息中，从而帮助开发者编写更高效、更灵活的代码。理解其允许的类型和关键限制，是正确使用它的前提


## 2.2 模板的特化：
我们在实战编译中发现并不是所有的模板实例化都能满足要求，这就引出了模板特化，那么他的概念是什么：
>模板特化是C++模板编程中一项重要的技术，它允许你为特定的类型或条件提供模板的定制版本，从而优化性能、处理特殊逻辑或适配特定接口,简单来说就是普通的模板已经不能适配新的类型，我们需要进行特化。

先看下图：![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/f63afb303f2949b99af899cd3b153572.png)
看起来似乎没有问题，我们再试试double和其它类型。当我们遇到指针，这里就蹊跷了。先看代码：

```cpp
#include<iostream>
using namespace std;

template <class T>
bool compare(const T& val1, const T& val2)
{
	return val1 > val2;
}

int main()
{
	cout << compare(1, 2) << endl;
	double a = 10.1;
	double b = 0.1;
	double* p1 = &a;
	double* p2 = &b;
	cout << compare(a, b) << endl;
	cout << compare(p1, p2) << endl;
}
```

再看结果：
![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/fe33f98a5fc046c79c3f8d2965b9e693.png)
这是因为，当我们传入指针的时候，他比较的是两个变量的地址。而且每次编译的结果还是不确定的，这是每次分配的地址也是不确定的。这时候就要使用模板的特化了。
当是我们这么写结果时错误的，编译器说这个不能精确匹配，这是为什么呢？
![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/9f073dbb502f43928a7d55965719ff54.png)
我们先看之前的变量吧，以前时``const T& ``，我们直接把T换成double*，在直接来试试：
![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/07b473c4317e4a0ab38195566688c03f.png)
但是这种写法时不对的。我们在详细分析一下：在C++中，const (double*)的等价写法是 double* const。这里的 const修饰的是 double*（即指针本身），而不是 double（指针指向的内容）。所以，最终的参数类型是 double* const &。那么我么在改变：
![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/f4ca3e21798b403dbaa433a6d3bd593a.png)
四个问题带你理清清楚为什么会是这样的:

1. 这下没有问题了，那么我们需要考虑的时const修饰的时什么了？
**在正确的模板特化种，const修饰的是指针，这个指针是无法改变指向的。**
2. 而基础模板种，const修饰的是什么呢？
**其实修饰的是T，所以后面再特化时候就要修饰指针变量**，成为指针常量。所以，在基础模板中，这个 const的作用是保证函数内部不会修改通过引用传入的原始对象。
3. 似乎逻辑不符合原来的特化吧？按照感觉应该要修饰的是变量本身才对。
其实还是自己搞混了，我们再完成特化最初的**目的是完成两个指针变量里面所指向的数据进行比较**。如果变量会发生变化，也是小错误。基础模板中的 const承诺的是“不修改传入的引用所绑定的那个对象”。当这个“对象”是一个指针时，指针本身就是这个对象。所以，特化版本中的 double* const &承诺的是“不修改传入的指针变量本身（即不改变它的指向）”，但它并没有承诺不修改指针所指向的数据。特化的目的：你特化这个函数的目的，恰恰就是因为默认的指针比较（比较地址）不符合预期，你希望比较指针指向的内容（*val1 > *val2）。如果你的特化版本将参数写成 const double* &（指向常量的指针），那么你在特化函数内部就连 *val1和 *val2都无法读取进行比较了（因为通过该指针访问的内容是只读的），这反而违背了特化的初衷
4. 逻辑是否矛盾？
特化版本保证不改变指针的指向（遵守基础模板的const T&约定），但为了实现特殊的比较逻辑，它需要并且被允许解引用指针，去访问和比较指向的数据。

## 2.3 特化所需理解的小知识点：
再上面我们已经介绍一点什么是特化，和特化的麻烦，似乎特化很不方便，甚至还引出来了const危机，简直对于初学者来说就是噩梦。所以必须补充一些知识点：
### 2.3.1还得是const：
> 我们不妨说这个的确是初学者的噩梦，我时常在写这些东西的时候也是感觉头昏脑胀。

理解 double* const ptr（指针常量）和 const double* ptr（常量指针）的区别确实是C++学习中的一个重点和难点。下面我为你梳理几种有效的记忆方法和理解技巧：
先看表格，没什么几乎每篇文章都有，我也给：
| 特性               | `double* const ptr`(指针常量) | `const double* ptr`(常量指针) |
| ---------------- | ------------------------- | ------------------------- |
| **中文名**​         | 指针常量                      | 常量指针（或指向常量的指针）            |
| **`const`修饰对象**​ | 指针本身（地址）                  | 指针所指向的数据                  |
| **指针指向（地址）**​    | **不可**改变                  | **可以**改变                  |
| **指向的数据值**​      | **可以**通过`*ptr`修改          | **不可**通过`*ptr`修改          |
| **记忆口诀**​        | 指针是常量，**指向不变，内容可变**​      | 指向常量，**指向可变，内容不变**        |

我们在c专家编程中可以看到：我们需要从右到左来完成阅读变量：

- double* const ptr: 从变量名 ptr开始往左读。
ptr是一个 const（常量）。
这个常量是一个 *（指针）。
它指向 double（双精度浮点数）。
结论：ptr是一个常量指针，指向 double。所以指针本身是常量，其指向不能变。
- const double* ptr: 从变量名 ptr开始往左读。
ptr是一个 *（指针）。
它指向 const double（常量双精度浮点数）。
结论：ptr是一个指向常量 double 的指针。所以数据是常量，其值不能通过指针改变。
- 值得注意的是，const double*和 double const*是完全等价的。const放在类型名 double之前或之后，修饰的都是它指向的数据。

还有一个值得注意的规律：
- const在 ``*``的右侧（如 double* const）：const修饰的是指针变量本身，表示指针是常量（指针常量）。
- const在 ``*``的左侧（如 const double*）：const修饰的是指针指向的数据类型，表示数据是常量（常量指针）。


### 2.3.2array和vector，谁是真爱：
![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/b7b2c0e187a24306af439d6fd4720e87.png)
先看图片，array也是数组，不过他是固定长度的数组。在使用他的时候，我们需要先包换头文件array。
我们先来看看：
![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/5802139dba324931acd5ade953ca12eb.png)
当我们尝试完成越界访问的时候立马会报错。
![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/442833d756ea4bd08b474c9152668538.png)
![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/d521dfe6802c44c7b265ade8f65007af.png)

在正常的数组中，并不会运行，在地下会弹出溢出，这是两者的区别：
我们再看vector和array：

| 特性维度       | `std::array`    | `std::vector`   |
| ---------- | --------------- | --------------- |
| **内存管理**​  | 栈上静态分配，固定大小     | 堆上动态分配，可扩容      |
| **大小灵活性**​ | 编译时固定，不可变       | 运行时可变，可插入/删除元素  |
| **性能特点**​  | 访问速度快，无动态开销     | 有动态调整开销，尾部操作高效  |
| **安全性**​   | 支持安全访问（如`at()`） | 支持安全访问（如`at()`） |
| **复制行为**​  | 可整体复制/赋值        | 可整体复制/赋值（需深拷贝）  |
| **迭代器失效**​ | 永不失效            | 扩容或插入/删除时可能失效   |

在这里还是更加推荐vector。栈的资源有溢出风。

### 2.3.3 普适性特化：
在这里再提一嘴，其实还有一种方式，不过并不推荐罢了：
![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/9d4e82aa1ad245aa872fd77412470f66.png)
代码如下：

```cpp
#include<iostream>
using namespace std;

template <class T>
bool compare(const T& val1, const T& val2)
{
    return val1 > val2;
}

// 通用指针特化
template<typename T>
bool compare(T* val1, T* val2)
{
    return *val1 > *val2;
}

int main()
{
    cout << compare(1, 2) << endl;
    
    double a = 10.1;
    double b = 0.1;
    double* p1 = &a;
    double* p2 = &b;
    
    int x = 5, y = 3;
    int* px = &x, *py = &y;
    
    cout << compare(a, b) << endl;    // 基础模板
    cout << compare(p1, p2) << endl; // double* 特化
    cout << compare(px, py) << endl;  // int* 特化
    
    return 0;
}
```

再这里还有声明一下啊，这不是特化，这是重载：

```cpp
// 基础函数模板
template <class T>
bool compare(const T& val1, const T& val2) { ... }

// 这是另一个函数模板（重载），而非第一个模板的特化
template <typename T>
bool compare(T* val1, T* val2) { ... }
```
# 3. 特化的几个类型：
## 3.1函数模板特化:
在上文讲了什么是函数特化，还讲了一大堆的const，很难的难点，这里也不提了，这里占个位置。

## 3.2类模板特化：
### 3.2.1 全特化：
先看图片：
![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/490a8d481ecf47af8de7f49f38516194.png)
我们发现：为模板的所有参数都指定具体的类型或值，提供一个完全特化的版本，template <>（尖括号为空），再后面出现类型。理解：
为一位特定身材的客人（如身高185cm）量身定制一件完全合身的衣服。
代码如下：

```cpp

template<class T>
class Print_Cons {
public:
	void print()
	{
		cout << "这是基本模板" << endl;
	}
};

template<>
class Print_Cons<int> {
public:
	void print()
	{
		cout << "这是全特化" << endl;
	}
};

int main()
{
	Print_Cons<double> p1;
	p1.print();
	Print_Cons<int> p2;
	p2.print();
}
```

### 3.2.2 偏特化：
嘿嘿，理解了全特化其实就很好理解偏特化，对于部分类型进行特化就可以了，那么我们来看看怎么写的吧。
代码如下：

```cpp
#include<iostream>
#include<typeinfo>
using namespace std;

template<class T1,class T2 >
class Data {
public:
	Data()
	{
		cout << "Data<T1,T2>原模板" << endl;
		cout << typeid(T1).name() << endl;
		cout << typeid(T2).name() << endl;
	}

private:
	T1 _d1;
	T2 _d2;
};
template<class T1>
class Data<T1 ,int> {
public:
	Data()
	{
		cout << "Data<T1,int>偏特化" << endl;
		cout << typeid(T1).name() << endl;
	}
private:
	T1 _d1;
	int _d2;
};

template<class T1 ,class T2>
class Data<T1*, T2*>
{
public:
	Data()
	{
		cout << "Data<T1,T2>指针偏特化" << endl;;
		cout << typeid(T1*).name() << endl;
		cout << typeid(T2*).name() << endl;
	}
private:
	T1* _d1;
	T2* _d2;
};



int main()
{
	Data<int, double> d1;
	Data<double, int> d2;
	Data<double*, int*> d3;
	Data<double**, int**> d4;
	return 0;
}
```

![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/934f6677c5964698a0d38abcde9280e6.png)
![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/b2158a8fc275483cb159e6deeabdbe92.png)
1. 理解一些为什么指针是偏特化：
偏特化”的“偏”字，精准描述了它的本质：只对模板参数施加部分约束，而非全部指定。
全特化：像为一个人量身定做衣服。template<> class Data<int, double>是死板的，它只服务于 int和 double这一种特定组合。
偏特化：像为一类人（例如“所有身高超过180cm的人”）设计服装版型。template <typename T> class Data<T*, T*>是灵活的，它服务于所有“两个指针类型相同”的模板实例，如 Data<int*, int*>、Data<MyClass*, MyClass*>等。
所以，指针特化是偏特化，因为它没有固定指针指向的具体类型（T仍然是泛化的），而是为一整类具有共同特征（是指针）的类型提供了通用解决方案，在通用性和特异性之间取得了很好的平衡。

2. 为什么d3和d4是一个答案，其实：
这条输出语句在模板中是一个固定的字符串字面量，无论 T1和 T2被推导成什么类型，它输出的内容都不会改变。
而第二行和第三行输出 typeid(T1*).name()和 typeid(T2*).name()的结果，在底层实际上是不同的。它们分别对应 double*/int*和 double**/int**的类型信息。之所以您可能觉得“结果一样”，可能是因为 typeid(...).name()返回的类型名称经过了编译器修饰（如GCC/Clang下可能显示为 Pd、Pi、PPd、PPi），可读性不强，导致一眼看去没有注意到差异 。如果您在调试器中检查或使用 cxxabi::__cxa_demangle等工具反修饰名称，就能清晰地看到它们的区别


# 4. 模板分离：

详细请看这篇文章：
[为什么C++编译器不能支持对模板的分离式编译](https://blog.csdn.net/pongba/article/details/19130)

---

# 5. 总结：
这篇文章我们主要讲了：
 
 1. 模板概念和什么是非类型模板
 2. 模板特化：
 	- 难的点是const，进准特化。
 	- 还有偏特化时如何特化。
 
 可能第二点我写的不太详细，但是这篇文章还是比较难度比较高的。笔者在理解时也感到吃力
 

