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
# 1. 什么是list.
C++ 标准模板库（STL）中的 std::list是一个基于带头结点的双向循环链表实现的序列容器。这意味着它的元素在内存中并非连续存储，而是通过指针相互链接。这种独特的底层结构赋予了它两大核心特性：一是在序列的任意已知位置进行插入和删除操作都异常高效，时间复杂度为 ​O(1)​，因为只需调整相邻节点的指针，无需像 vector或 deque那样移动大量元素；二是它不支持随机访问，即无法通过下标直接访问元素，定位特定位置的元素需要从头部或尾部开始顺序遍历，时间复杂度为 O(n)。
那么我们该如何使用和学习LIst呢，我们讲通过以下几点来完成学习。
![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/5cb1e025cc874fd2bb8bf59ccd79ce53.png)
超级多的接口，不着急逐步分析。



---

# 2. list的结构及其接口详解
## 2.1.list 结构设计：
再c++中list就是一个带头的双向链表，这种结构近乎完美，我们来尝试搭建自己的结构：

- 链表是有节点来组成的，我们可以尝试先构建节点，一般节点后续需要处理，我们可以直接公开，使用struct来完成封装。
- 定义好节点过后，我们便可以定义链表的结构了。

这样就有了以下代码：

```cpp
	template <class T>
	struct ListNode {

		//定于节点的结构
		T _data;
		ListNode<T>* _next;
		ListNode<T>* _prev;
	};
```

```cpp
	template <class T>
	class list{
		typedef LIstNode Node;
	public:
		
	
	private:
		Node* _head;//哨兵
		size_t _size;//计数器
	};
```
做出以下结构，后续我们需要插入数据，只需初始化节点，并且连接即可。我们来尝试写出链表的初始化：

```cpp
	template <class T>
	struct ListNode {
		ListNode(const T& val = T())
			:_data(val),
			_prev(nullptr),
			_next(nullptr)
		{}

		//定于节点的结构
		T _data;
		ListNode<T>* _next;
		ListNode<T>* _prev;
	};

	template <class T>
	class list{
		typedef LIstNode Node;
	public:
		list()
		{ 
			//完成哨兵的初始化，利用的是Node这个结构。
			_head = new Node;
			_head->prev = _head;
			_head->next = _head;
			_size = 0;
		}
	
	private:
		Node* _head;//哨兵
		size_t _size;//计数器
	};
```
我们先初始化节点，然后再利用节点初始化链表，注意此时链表为空，但是有一个哨兵在里面，此时_size仍然为0.

## 2.2 迭代器的实现（重点）：
### 2.2.1 插曲：push_back
为了方便后续的测试，我们先实现初版的push_back，这个是为了后续的测试方便：

```cpp
		void push_back(const T& val)
		{
			Node* node = new Node(val);

			Node* tail = _head->_prev;
			tail->_next = node;
			node->_next = _head;
			node->_prev = tail;
			_head->_prev = node;

			++_size;
		}
```

由于我们没有实现迭代器，我们可以尝试调试，来完成观察：
![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/1063560beb58430192dcca4bb2dead9d.png)
我们再试试其他的类型，同时这是链表我们不在乎扩容的事情，这就很方便了。
![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/e6028968f7b946ecaf30211fe6e13372.png)
通过观察我们发现string类也是没有问题的。好的我们回归正常的问题，list的迭代器的完成：
### 2.2.2正式工作：迭代器的结构：
我们发现如果再次定义指针的化，是无法``++``和``--``者两个运算符的，这是因为链表再内存不是连续的地址。这也就扯到了迭代器的分类，如下图表格：
| 迭代器类别                                   | 核心功能描述         | 支持的操作（示例）                                             | 典型应用容器                                           |
| --------------------------------------- | -------------- | ----------------------------------------------------- | ------------------------------------------------ |
| ​**输入迭代器 (Input Iterator)​**​           | 只读、单向、一次遍历     | `++`, `*`(仅取值), `==`, `!=`                            | `istream_iterator`                               |
| ​**输出迭代器 (Output Iterator)​**​          | 只写、单向、一次遍历     | `++`, `*`(仅赋值)                                        | `ostream_iterator`, `inserter`                   |
| ​**前向迭代器 (Forward Iterator)​**​         | 可读写、单向、可重复遍历   | 包含输入/输出迭代器全部功能，并可多次访问同一元素                             | `forward_list`, `unordered_set`, `unordered_map` |
| ​**双向迭代器 (Bidirectional Iterator)​**​   | 可读写、双向移动、可重复遍历 | 在前向迭代器基础上增加 `--`操作                                    | `list`, `set`, `map`及其多重版本                       |
| ​**随机访问迭代器 (Random Access Iterator)​**​ | 功能最强大，支持随机访问   | 包含双向迭代器所有功能，并支持 `+`, `-`, `+=`, `-=`, `<`, `>`, `[]`等 | `vector`, `deque`, `string`, 原生数组指针              |



此时我们就该尝试使用封装和重载来帮助我们实现迭代器：此时我们先用一个节点的指针作为迭代器，作为结构体的主要成员。后续的工作都是围绕他来行动

```cpp
	template <class T, class ref, class ptr>
	struct List_iterator {
		typedef List_iterator<T, ref, ptr> self;
		typedef ListNode<T> Node;//重命名

		List_iterator(Node* node)
			:_node(node)
		{
		}
				Node* _node;//只需封装 node节点，这是地址。
	};
```
### 2.2.3 迭代器的核心：重载``*``和``->``函数：
由于之前容器的函数都是指针，可以直接解引用直接使用，但是我们解引用还是一个node节点，所以我们需要做出调整：

1. ``*``函数：

```cpp
		ref operator*()
		{
			return _node->_data;
		}
```
返回数值的拷贝，即ref是T&，与之前的迭代器风格保持一致，这是很重要的。

2. ``->``函数：

```cpp
		ptr operator->()
		{
			return &(_node->_data);
		}
```
这次放回是是数值的指针，​operator->返回指针​：当你对指针使用->操作符访问成员时，其底层逻辑是(pointer)->member。为了模拟这一行为，迭代器的operator->需要返回一个指针，这样iter->才能被解析为通过指针访问成员。

**小总结**：
在C++迭代器设计中，`operator*`（解引用操作符）和`operator->`（成员访问操作符）虽然都涉及对数据的访问，但它们的**返回类型**和**使用场景**有明确的区别。简单来说，`operator*`返回的是数据对象本身，而`operator->`返回的是数据对象的指针，这是为了模拟原始指针的行为并满足不同的语法需求。


|特性|`operator*`(解引用)|`operator->`(成员访问)|
|---|---|---|
|​**主要功能**​|获取迭代器当前指向的**数据对象本身**​|获取指向数据对象成员的**指针**，以便访问其成员|
|​**返回类型**​|`Ref`(通常是 `T&`或 `const T&`)|`Ptr`(通常是 `T*`或 `const T*`)|
|​**使用语法**​|`*iter`|`iter->member`|
|​**编译器处理**​|直接返回引用|编译器会**隐式地再次应用`->`**，使`iter->member`等价于`(&(*iter))->member`|

### 2.2.3 ``++``函数：
为了实现通过迭代器来完成遍历容器，我们还是需要重载这两个运算符，看一下代码：

```cpp
		self& operator++()
		{
			_node = _node->_next;//往后走
			return *this;//记得解引用，this是指针，返回迭代器
		}

		self operator++(int)
		{
			self tmp = *this;
			++*this;
			return tmp;
		}
```
这是两个都是``++``，但是不同的是第一个是是前置，第二个是后置。
它们最核心的区别在于：​前置++直接对迭代器自身进行递增并返回自身的引用；后置++则需要先创建当前迭代器状态的副本，然后对原迭代器进行递增，最后返回那个副本。下面的表格帮你快速把握它们的主要区别：
| 特性         | 前置 `++`(`operator++()`)                              | 后置 `++`(`operator++(int)`)   |
| ---------- | ---------------------------------------------------- | ---------------------------- |
| ​**函数签名**​ | `self& operator++()`                                 | `self operator++(int)`       |
| ​**参数**​   | 无                                                    | 一个`int`类型的哑元参数，仅用于区分         |
| ​**核心操作**​ | `_node = _node->_next;`                              | `self tmp = *this; ++*this;` |
| ​**返回类型**​ | 当前迭代器类型的引用 (`self&`)                                 | 当前迭代器类型的值 (`self`)           |
| ​**返回值**​  | 递增**后**的迭代器本身 (`*this`)                              | 递增**前**的迭代器副本 (`tmp`)        |
| ​**常见用法**​ | `for (auto it = c.begin(); it != c.end(); **++it**)` | 需要获取递增前状态的场景                 |
| ​**性能考量**​ |  ​**更高**​（无临时对象开销）                                 | **较低**​（需构造和返回临时副本）      |

观察上表我们得出前置++更加高效，所以我们也更加推荐使用使用前置++。
我们来测试以下，遍历以下结果：

![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/1a25007d8fd340eb9445eb73b8c3178f.png)
### 2.2.4``--``函数：
同理我们也给出我们自己的函数：

```cpp
		self& operator--()
		{
			_node = _node->_prev;
			return *this;
		}

		self operator--(int)
		{
			self tmp = *this;
			--*this;
			return tmp;
		}
```
| 特性         | 前置 `--`(`operator--()`)          | 后置 `--`(`operator--(int)`)                |
| ---------- | -------------------------------- | ----------------------------------------- |
| ​**函数签名**​ | `self& operator--()`             | `self operator--(int)`                    |
| ​**参数**​   | 无                                | 一个`int`类型的**哑元参数**，仅用于编译器区分重载             |
| ​**核心操作**​ | `_node = _node->_prev;`(指向前一个节点) | `self tmp = *this; --*this;`(先保存副本，再递减自身) |
| ​**返回类型**​ | 当前迭代器类型的**引用**​ (`self&`)        | 当前迭代器类型的**值**​ (`self`)                   |
| ​**返回值**​  | 递减**后**的迭代器本身 (`*this`)          | 递减**前**的迭代器副本 (`tmp`)                     |
| ​**性能考量**​ |  ​**更高**​（无临时对象开销）             |  ​**较低**​（需构造和返回临时副本）                   |

测试结果如下：
![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/ace06e1eff1d45b5a37a3698c8505433.png)

### 2.2.4 ``==``和``!=``函数：

```cpp
		bool operator==(self it)
		{
			return _node == it._node;
		}

		bool operator!=(self it)
		{
			return !(*this == it);
		}
```
这两个比较简单，我们就不在这里仔细的讲了。



## 2.3 主要函数接口：
### 2.3.1 begin和end函数

```cpp

		iterator begin()
		{
			return this->_head->_next;//对象拷贝临时变量转换成 iterator
		}

		iterator end()
		{
			return this->_head;
		}
```
注意的是end返回的是头节点（有效数值的下一个位置），遵循左闭右开原则。


### 2.3.2 insert函数：
我们实现了迭代器过后，我们就可以着手实现insert函数，我们来看
![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/637b53a29416405ba13c911a8f8deb79.png)
我们传入pos迭代器时，注意迭代器本身是一个封装了 node指针的自定义类对象，我们可以通过.运算符来完成取出内部成员_node.随后后去前一个元素，完成插入。代码如下：

```cpp
		iterator insert(iterator pos, const T& val)
		{
			Node* prev = pos._node->_prev;
			Node* cur = new Node(val);

			cur->_next = pos._node;
			cur->_prev = prev;
			prev->_next = cur;
			pos._node->_prev = cur;
			
			_size++;
			return iterator(cur);
		}
```

### 2.3.3 push_back 函数：
之前哦我们已经实现过了这个函数，有了这个函数我们可以进一步完善改善他：

```cpp
		void push_back(const T& val)
		{
			//Node* node = new Node(val);

			//Node* tail = _head->_prev;
			//tail->_next = node;
			//node->_next = _head;
			//node->_prev = tail;
			//_head->_prev = node;

			//++_size;

			insert(end(), val);

		}
```

### 2.3.4 erase函数：
有了插入，我们也会有删除函数：

```cpp
		iterator erase(iterator pos)
		{
			Node* next = pos._node->_next;
			Node* prev = pos._node->_prev;
			next->_prev = prev;
			prev->_next = next;

			delete pos._node;
			_size--;
			return iterator(next);
		}

```
我在这里少了对于pos位置的判断，我们可以加上对于这个位置的判断。

```cpp
iterator erase(iterator pos)
{
    // 添加断言检查
    assert(pos != end()); // 确保pos不是尾后迭代器
    assert(pos._node != nullptr); // 确保迭代器内部的节点指针有效

    Node* next = pos._node->_next;
    Node* prev = pos._node->_prev;
    next->_prev = prev;
    prev->_next = next;

    delete pos._node;
    _size--;
    return iterator(next);
}
```
非常重要的一点是，在成功调用 erase函数后，传入的迭代器 pos会立即失效​（因为它所指向的节点已经被销毁）。您绝不能再使用这个失效的迭代器进行解引用（*pos）或自增（pos++）等操作。

### 2.3.5.size函数和empty函数：
这两个也比较简单，不做详细的解释来看：

```cpp
		bool empty()
		{
			renturn size == 0;
		}

		size_t size()
		{
			return _size;
		}

```

## 2.3其余重要函数：
我们还有几个比较重要的函数就是拷贝构造函数和析构函数：
### 2.3.1拷贝构造函数：

```cpp
		void empty_init()
		{
			_head = new Node;
			_head->_prev = _head;
			_head->_next = _head;
			_size = 0;
		}

		list(const list<T>& lt)
		{
			empty_init();
			for (auto& e : lt)
			{
				push_back(e);
			}
		}

```
先完成空链表的初始化，随后在逐步进行增加元素。
### 2.3.2赋值运算符重载（新方法）：
当我们再调用传值函数生成临时变量，自动调用。为此我们想到一个绝妙的方法，代码如下：

```cpp
		void swap(list<T>& lt)
		{
			std::swap(_head, lt._head);
			std::swap(_size, lt._size);
		}

		list<T>& operator=(list<T> lt)//注意这里是传值
		{
			swap(lt);
			return *this;
		}
```
我们利用这个机制来完成编写代码：这个机制的核心确实在于利用C++的函数参数传递和局部对象生命周期管理。当使用传值方式时，编译器会在调用赋值运算符时自动创建源对象的副本，这个副本就是用户提到的"临时变量"。交换操作只是简单地交换两个对象的内部状态指针，而临时对象在函数结束时会被自动析构，从而清理原来的资源。
在前面的文章我没有写，这里我来介绍一下这种：拷贝-交换惯用法”（Copy-and-Swap Idiom）​​ 的经典实现，它是一种兼具异常安全性、正确性和简洁性的优雅技巧。

### 2.3.3析构函数：
我们完成了大部分list的接口，还有一个函数没有完成，那就是析构函数，这是非常重要的用来完成内存清理的函数：

```cpp
		~list()
		{
			iterator it = begin();
			while (it != end())
			{
				it = erase(it);
			}
			delete _head;
			_head = nullptr;
		}
```
这是我想的第一版，一开始要注意迭代器失效，不能直接对``it``进行``++``，我们需要重新返回新的it。看似没有问题了，但是是对于_head的next和prev指指针没有维护，我们可以再理出一个函数：

```cpp
		void clear()
		{
			iterator it = begin();
			while (it != end())
			{
				it = erase(it);
			}
			_head->_next = _head;
			_head->_prev = _head;
		}

		~list()
		{
			clear();
			delete _head;
			_head = nullptr;
		}
```


---

# 3. 总结：
从使用来看，list其实和vector相差不大。但是实现起来相比，list难度还是很大的，主要原因还是迭代器的难度。我们借用一段话：
>List 迭代器的核心结构是一个对节点指针进行封装的类，它通过重载 ++、--、*、->等运算符，模拟了指针的行为，使得用户能够像使用指针遍历数组一样，轻松地在非连续存储的链表节点之间进行移动和访问操作。为了同时实现普通的 iterator 和 const_iterator，通常会采用包含三个模板参数（T, Ref, Ptr）的类模板，通过传入不同的引用和指针类型（如 T&与 const T&）来复用同一份代码，从而区分迭代器的读写和只读属性.

送你一只小奶龙，给个关注吧。![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/28cf4406eb844bb9abb61e6b2cd610b1.png)


