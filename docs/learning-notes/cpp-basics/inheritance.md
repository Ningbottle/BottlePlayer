![请添加图片描述](https://i-blog.csdnimg.cn/direct/d488c8283d5d4f3e947c150af954328e.jpeg)
关注我，学习c++不迷路:
>
>[个人主页：爱装代码的小瓶子](https://blog.csdn.net/2301_80127108?spm=1011.2426.3001.5343)
专栏如下：
>1. [c++学习](https://blog.csdn.net/2301_80127108/category_13027195.html?fromshare=blogcolumn&sharetype=blogcolumn&sharerId=13027195&sharerefer=PC&sharesource=2301_80127108&sharefrom=from_link)
>2. [Linux学习](https://blog.csdn.net/2301_80127108/category_13061104.html?fromshare=blogcolumn&sharetype=blogcolumn&sharerId=13061104&sharerefer=PC&sharesource=2301_80127108&sharefrom=from_link)

后续会更新更多有趣的小知识，关注我带你遨游知识世界
![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/0a6a1e63b620424aa924e3fc8adec1ce.png)
期待你的关注。

![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/8b158c5e0c9343d0b2a5560e6914c25b.png)

---

@[toc] 


---
# 1.什么是继承：
继承是面向对象编程（OOP）的核心机制，它允许你基于一个已有的类（称为基类或父类）来定义一个新的类（称为派生类或子类）。派生类会自动获得基类的特性（成员变量和成员函数），同时可以添加自己独有的特性，或修改继承来的行为，其实在生活中，我们能看到很多继承的关系，比如：“狗是哺乳动物，哺乳动物是动物”，这种关系可以通过继承链 Animal-> Mammal-> Dog来直观表达。在设计的时候，cpp之父也是考虑到了这种情况，引入了这一设计思想。

继承允许我们构建清晰的类层次结构，从而更好地组织和建模现实世界中的“是一类”关系。在中国，我们习惯叫原生的类叫做父类（或者基类），继承的就叫做子类（派生类）。也就是说：被继承的类是基类，新创建的类是派生类。派生类“是”一种特殊的基类，例如，“学生”是一种“人”。

在我们学习了C语言，在学c++的时候我们就能明显感受到这种设计的威力。
总而言之，C++中的继承是一种强大的工具，它通过代码复用、层次化建模和多态极大地提升了代码的可重用性、可维护性和可扩展性。理解其核心概念、设计初衷以及关键细节，是有效运用面向对象编程思想的基础。

---

# 2. 详细介绍：
## 2.1 一段小程序：
在我们社会中，学生是人的一，明显有着继承的关系，那我们开始写这个吧！

```cpp
class Person {
public:
	Person(string name = "xxx")
	:_name(name)
	{ }
private:
	string _name;
};
```
这个就是人，那么学生应该继承人，这一个情况，有以下代码：

```cpp
class student : public Person {
public:
	student(string name = "xxx", int id = 239074, int age = 18)
	:Person(name)
	,_id(id)
	,_age(age)
	{ }

	void Print()
	{
		cout << _age << " " << _id << endl;
	}

private:
	int _id;
	int _age;
};

```
在这里我们可以看到比正常的类多了一个冒号：，还有一个关键词public，这又是干什么的呢？同时还发现在子类中我们没有定义name，那么是否可以用呢？

我们先来看看：
![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/25a73d4dc33643af9361f3d4aa8e1273.png)
![在这里插入图片描述|819](https://i-blog.csdnimg.cn/direct/a402d151921241eab360d4bee33a7541.png)
但是我们可以看到student里面其实是有name的。
## 2.2 继承的权限问题：
在继承中我们的关键词与类中的一致，分别是：public，protected和private,我们一张表格完成介绍：


| 继承方式               | 基类 `public`成员变为          | 基类 `protected`成员变为       | 基类 `private`成员 |
| ------------------ | ------------------------ | ------------------------ | -------------- |
| **`public`继承**​    | 派生类的 **`public`**​ 成员    | 派生类的 **`protected`**​ 成员 | **在派生类中不可见**​  |
| **`protected`继承**​ | 派生类的 **`protected`**​ 成员 | 派生类的 **`protected`**​ 成员 | **在派生类中不可见**​  |
| **`private`继承**​   | 派生类的 **`private`**​ 成员   | 派生类的 **`private`**​ 成员   | **在派生类中不可见**   |

看似需要记忆的东西很多，其实也没有多少，我们先介绍一下三种继承方式：
1. 公有继承 (public)
	这是最常用、最符合直觉的继承方式，用于建立 "是一个 (is-a)"​ 的关系（例如，"学生" 是一种 "人"）。其核心规则是：基类的公有和保护成员在派生类中保持原有访问级别。
特点：派生类对象可以在类外访问基类的公有成员；派生类的成员函数可以访问基类的公有和保护成员。

2. 保护继承 (protected)
保护继承使用较少，它会使基类的公有成员在派生类中变为保护成员。
特点：经过保护继承后，基类所有的公有和保护成员都成为派生类的保护成员。这意味着，这些成员无法再通过派生类对象在类外直接访问，但可以在派生类内部以及该派生类的子类中访问。
影响：这切断了基类接口对外的公开性，后续的派生类可以继续使用这些成员，但类外无法直接使用。

3. 私有继承 (private)
私有继承表示一种实现上的组合关系，而非"是一个"的关系，它使用"有一个"的关系更常见。私有继承会使基类的公有和保护成员在派生类中全部变为私有成员。
特点：经过私有继承后，基类的所有非私有成员都成为派生类的私有成员。这意味着，这些成员只能在当前派生类内部访问，即使是该派生类的子类也无法再访问这些从基类继承来的成员。
用途：通常用于实现"根据一个已有的类来实现一个新类"，但又不希望暴露基类的接口。

我们在观察上面这个表格，我们会发现：所以的权限是取俩个的最小方式，也就是如果本来是public，后面遇到protected就会变成protected。

我们在比较一下，两个比较容易搞混淆的private和protected，这两个：
1. protected成员专门为继承体系设计。它们就像是家族的“内部信物”，对于外界是保密的，但家族成员（派生类）可以自由使用和查看。这使得你可以在基类中定义一些不希望被外部直接调用、但允许派生类根据需要覆盖或使用的“骨架”方法或数据，这个就像家庭保险柜，是整个家庭成员的。
2. private成员强调的是类的内部实现细节。无论是否涉及继承，它们都只能被当前类自己的成员函数或友元访问。这符合面向对象设计的最小权限原则。这个就相当于父亲的私房钱，既是你是子类，也无法拿到私房钱，也就是本文中的Person类里面的name。我们虽然继承了这个，但是我们本身业务法对其读取。

两个小建议：
1. 在写代码的时候我们需要**优先使用 private**：在大多数情况下，应优先考虑将成员设为 private。只将那些确实需要被派生类覆盖或直接使用的成员升格为 protected。这有助于维持更严格的封装和高内聚。
2. 需要注意的是，如果不显式指定继承方式，**使用 class关键字定义的派生类会默认是 private继承**，**而使用 struct关键字则默认是 public继承**。为了代码清晰，始终建议显式地写出继承方式（例如 class Derived : public Base）.

在我们这个代码如果想要读取这个name,我们可以把这个代码中的私有换成保护，这样就可以完成对其的支持。
![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/f46992a5eef74b388ace48185a272dca.png)
## 2.3 模板类的继承
在之前我们已经尝试使用过适配器版本来完成stack，这种方式也叫做组合，我们今天也带来一种全新的方式:

```cpp
namespace wwh {
	template<class T>
	class stack :public std::vector<T> {
	public:
		void push(const T& val)
		{
			//记得不要使用vector<T>.push_back(val),这是一个类，不是对象
			vector<T>::push_back(val);
		}

		void pop()
		{
			vector<T>::pop_back();
		}

		bool empty()
		{
			return vector<T>::empty();
		}

		const T& top()
		{
			return vector<T>::back();
		}

		size_t size()
		{
			return vector<T>::size();
		}
	};
}



int main()
{
	wwh::stack<int>st;
	st.push(1);
	st.push(2);
	st.push(3);
	cout << "size:" << st.size() << endl;
	cout << st.top() << endl;
	st.pop();
	cout << st.top() << endl;

	return 0;
}
```
这样我们也就完成了一个简单的栈。

## 2.4 基类和派生类间的转换
这一概念指的是派生类可以赋值给基类的指针或者引用对象，这是一种安全的向上转化，在转化过程中：当使用一个派生类对象来初始化或赋值一个基类对象时，会发生对象切片。编译器只会拷贝派生类对象中的基类部分，而派生类自定义的新成员会被“切掉”。之后，你得到的是一个纯粹的、独立的基类对象，它与原来的派生类对象再无关联。这是因为派生类中本身就含有父类。可以完成切片。
![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/507c1a654e53485f85b68a8d92c405cc.png)
而在后面还有一种不太安全的转化方式：
向下转型是将基类的指针或引用转换为派生类的表示形式。这是一个不安全的操作，因为基类指针可能并不指向一个派生类对象，如果强制转换，访问派生类特有的成员会导致未定义行为。

```cpp
class Person {
public:
	Person(int age,string name,string sex)
	:_age(age)
	,_name(name)
	,_sex(sex)
	{}

private:
	int _age;
	string _name;
	string _sex;
};

class student :public Person {
public:
	student(int age,string name,string sex,int id)
	:Person(age,name,sex)
	,_id(id)
	{ }

private:
	int _id;
};

int main()
{
	student s(18, "张三", "男", 001);
	Person* pobj = &s;
	return 0;
}
```
![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/9c51e05b663946e4923babf54a538e1e.png)
我们通过调试可以看到在pobj中是可以看到与s中是一致的信息。

## 2.5 继承中的作用域：
子类和父类都拥有自己的作用域，这种做法是为了区别子类和父类中相同的变量名。那如果遇到了相同的函数名和相同的变量该怎么办呢？
那么我们就讲到了这个隐藏这个关系。

```cpp
class Person {

protected:
	string _name = "王五";
	int _num = 1001;
};

class student :public Person {
public:
	void print()
	{
		cout << "姓名：" << _name << endl;
		cout << "学号：" << _num << endl;
		cout << "识别号：" << Person::_num << endl;
	}

private:
	int _num = 1002;
};

int main()
{
	student s;
	s.print();
	return 0;
}
```
这个代码中我们定义两个_num,如果不指定类域，默认打印出的是学生的学号1002,后面打印的才是1001，指定了前面的_num;
我们再来看一个非常有意思的代码：

```cpp

class A {
public:
	void func()
	{
		cout << "func()" << endl;
	}
};

class B :public A {
public:
	void func(int i)
	{
		cout << "func(int i)" << endl;
	}
};

int main()
{
	B b;
	b.func(10);
	b.func();
}

```
这两个func函数构成什么呢，很多同学认为他是重载，其实不然。其实这也是隐藏，如果函数名也一样，也会隐藏原本的func，进而取实现b的func，同时这段代码也是错误，正确写法如下：

![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/65a346aa606d4e2983f2096de0b45a39.png)
进而结果如下：
![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/28fc4c9dc3e34e8ea08970296533fe7a.png)

## 2.6派生类的默认函数
在之前有段代码，我们并没有完成他的构造函数，但是为什么能够有效初始化呢？我们来一探究竟：
核心原则：**继承自父类的成员，必须调用父类的相应成员函数来处理；子类自己的成员，则按照普通类的规则处理​** 。

| 默认成员函数                   | 子类中的关键行为                                                            |
| ------------------------ | ------------------------------------------------------------------- |
| **构造函数**​                | **必须调用父类构造函数**初始化父类部分。若父类无默认构造，**必须**在初始化列表中显式调用父类合适的构造函数<br><br>。  |
| **析构函数**​                | 会在自身调用结束后**自动调用父类析构函数**。不应显式调用父类析构函数，以保证**先子后父**的正确析构顺序<br><br>。    |
| **拷贝构造函数**​              | 应调用**父类的拷贝构造函数**来完成父类部分的拷贝初始化，参数为子类对象（会切片）<br><br>。                 |
| **赋值运算符 (`operator=`)**​ | 应调用**父类的赋值运算符**完成父类部分赋值。因**名称隐藏**，必须通过`父类名::operator=`显式调用<br><br>。 |
| **取地址运算符重载**​            | 编译器生成默认版本通常足够，行为与普通类一致。                                             |


![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/fd553b94b8454571a107fb12d99b6bbc.png)
下面代码带你看看究竟如何：
```cpp
class Person {
public:
	Person(string name = "张三")
		:_name(name)
	{
		cout << "父类的构造函数" << endl;
	}

	Person(const Person& p)
		:_name(p._name)
	{
		cout << "父类的拷贝构造函数" << endl;
	}

	Person& operator= (const Person& p)
	{
		//不要搞忘记cpp中支持连续赋值，所以返回 Person&
		cout << "父类的赋值的重载" << endl;
		if (this != &p)
		{
			//如果不是本身，将p的值赋值给本身
			_name = p._name;
		}
		return *this;
	}
	
	~Person()
	{
		cout << "父类的析构函数" << endl;
	}

protected:
	string _name;
};


class student :public Person {
public:
	student(string name,int id)
		:Person(name)
		,_id(id)
	{ 
		cout << "子类的构造函数" << endl;
	}

	student(const student& s)
		:Person(s)//应该可以直接转入s，发生转换
		,_id(s._id)
	{
		cout << "子类的拷贝函数" << endl;
	}

	student& operator=(const student& s)
	{
		cout << "子类的赋值重载" << endl;
		if (this != &s)
		{
			Person::operator=(s);
			_id = s._id;
		}
		return *this;
	}

	~student()
	{
		cout << "子类的析构函数" << endl;
	}
	
private:
	int _id;
};

int main()
{
	student s("xxx", 10);
	student s1 = s;
	student s2(s1);
	return 0;
}

```
运行结果如下：
![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/6828029da20646b6a51a382cf94fab48.png)
我们可以看到这几种情况：

详解子类默认成员函数
1. 构造函数
**子类对象的构造顺序是先父类后子类**。子类的构造函数（无论是编译器默认生成的还是你自己写的）都必须确保父类部分被正确初始化 。
**父类有默认构造函数时**：如果你没有在子类构造函数的初始化列表中显式调用父类构造函数，编译器会自动调用父类的默认构造函数 。
**父类没有默认构造函数时**：你必须在子类构造函数的初始化列表中显式调用父类提供的某个带参构造函数 。不允许在子类构造函数体内直接初始化从父类继承来的成员 。

2. 拷贝构造函数
子类的默认拷贝构造会依次处理：
父类部分：调用父类的拷贝构造函数。
子类部分：对于内置类型，进行值拷贝（浅拷贝）；对于自定义类型的成员，调用其自身的拷贝构造函数 。
当你需要自己编写子类的拷贝构造函数时（例如涉及深拷贝），务必在初始化列表中调用父类的拷贝构造函数。

3. 赋值运算符 (operator=)
赋值运算符的重载需要注意名称隐藏（Name Hiding）​ 问题。由于子类和父类中的operator=函数名相同，它们会构成隐藏关系。这意味着在子类的operator=函数内部，直接写operator=(s)会导致递归调用子类自身的赋值运算符，从而引发栈溢出 。正确的做法是显式指定父类的作用域。原则依旧是“谁的成员谁负责”，父类的成员交给父类的operator=去处理 。

4. 析构函数
析构顺序与构造顺序相反，**是先子类后父类**​ 。
自动调用：子类的析构函数在执行完自身的代码后，会自动调用父类的析构函数。因此，你不应该也不需要在子类析构函数中显式调用父类的析构函数（如Person::~Person();），这样做会导致父类析构函数被调用两次，是未定义行为 
。名称隐藏：为了支持多态，**所有类的析构函数名在底层都会被统一处理成destructor()。**因此，父类和子类的析构函数也构成隐藏关系 。

5.  何时需要自己编写？
在以下情况下，你需要为子类显式定义这些默认成员函数 ：
	- 父类没有默认构造函数：你必须为子类编写构造函数，并在初始化列表中显式调用父类的某个构造函数。
	- 子类含有需要深拷贝的资源：你需要自己编写拷贝构造函数和赋值运算符重载，并在其中正确调用父类的对应函数。
	- 子类有需要手动释放的资源（如动态内存、文件句柄）：你需要编写析构函数来释放这些资源。记住，父类部分的清理会自动进行。


---

# 3. 总结：
继承是面向对象编程中实现代码复用的核心机制，允许派生类在保留基类特性的基础上进行扩展，通过public、protected或private继承方式控制成员访问权限，其中基类私有成员在派生类中不可见。继承体系存在独立作用域，同名成员会引发隐藏；派生类的默认成员函数需显式处理基类部分，而多继承可能引发菱形继承问题，导致数据冗余和二义性，因此建议优先使用组合（has-a关系）而非继承（is-a关系）以降低耦合度，提升代码维护性。
我们在这里还没有将菱形继承以及他的部分解决方法，最后脑图附上，有兴趣的可以点上。
![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/dd31af61f123412f89e8e5848f58eefe.png)

