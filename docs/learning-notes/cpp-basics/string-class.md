

![请添加图片描述](https://i-blog.csdnimg.cn/direct/b42f55431adf498186b3e4d7369cccb7.gif)

# 1. string的构建：
C语言中，字符串是以'\0'结尾的一些字符的集合，为了操作方便，C标准库中提供了一些str系列的库函数，但是这些库函数与字符串是分离开的，不太符合OOP的思想，而且底层空间需要用户自己管理，稍不留神可能还会越界访问。
string是c++的一个类，也是很接近STL库的一个类，有很多思想值得我们学习，接下来我们将要介绍：
![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/6776c3d4d50145ddb965af38bbbe09e6.png)
以上就时几种构建方式：第一种就时最基本的构造，传入字符串，进行初始化，第二种则就是拷贝构造，第三种是存在隐式改变，在进行拷贝构造。
# 2. string的遍历：
## 2.1 迭代器：
迭代器是STL中的重要组成部分。它可以用于迭代字符串，我们来看：

```cpp
#include<iostream>
#include<string>

using namespace std;

int main()
{
	string str1("hello");
	string::iterator it = str1.begin();
	while (it != str1.end())
	{
		cout << *it;
		++it;
	}
	cout << endl;
}
```
这里就是迭代器的作用，其中迭代器很类似指针，但是其实并不是指针，这个可以在反向迭代器中可以看到。其中迭代器是可以改变里面的内容的，若不想要做出改变可以有：
![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/e143f4dd316a46a199bdcdc247b1e9a5.png)
这里就可以看出这是错误的，无法进行改变，注意const是加在iterator前面的变成``const_iterator``。
我们再来看反向迭代器：
![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/e7b8090231ac41bda37a6f4344b17830.png)
这里也可以详细看出来迭代器并不是指针，但是很类似于指针。不然++是无法解释的。我们来看begin是字符串的第一个位置即0，而end是最后一个的下一个，这里可以理解在是\0；
## 2.2 范围for：
### 2.2.1 auto
在这个之前我们将了解``auto``这个关键字，这个关键字是自动识别类型，在c11之前其实并不是的。我们来看代码：
![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/9f27f5984217466a9787c2115fd57cfb.png)
我们可以看auto自动识别类型并在编译时候进行转换，虽然破坏了可读性，但是还是很方便的，在一些类型很长的名字可以减少写代码的时间。
同时还有以下几点需要注意：

- auto不能直接用来声明数组
- auto不能作为函数的参数，可以做返回值，但是建议谨慎使用（会破坏可读性，导致误会）
- 当在**同一行声明多个变量时**，这些变量必须是相同的类型，否则编译器将会报错，因为编译器实际只对第一个类型进行推导，然后用推导出来的类型定义其他变量
- 用auto声明指针类型时，用auto和auto*没有任何区别，但用auto声明引用类型时则必须加&
### 2.2.2
范围for其实本质也还是迭代器，但是这个写起来更加方便,我们来看代码：

```cpp
	for (auto ch : str1)
	{
		cout << ch;
	}
```
没错几乎就是这么简单，但是注意的是把str每个字符赋值给了ch，你改变ch的值但是不会改变str1的值。如要改变可以加引用，如图：![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/cd3fe716c1dc4dca8a0341432837ffc7.png)
最后的str的结果也是改变了，我们来看：
![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/8345c6a082854e0ebfaac8fbe66a8929.png)
## 2.3 operator[]
我们还可以通过[]来改变和遍历string类，这是通过重载来完成的，废话不多说，我们直接来看吧：

```cpp
	//3. []
	for (int i = 0; i < str1.size(); i++)
	{
		cout << str1[i];
	}
```
也是很简单，我们通过for循环就可以完成遍历。
# 3. string的增删查改：
## 3.1 头插入和尾插入：
在面对string类的插入，我们仅尾插就有pus_back和+=两个接口来完成这件事，我们来尝试完成：
![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/808faafa013d45538e2c49ff0302283a.png)
我们可以看到+=不仅可以加一个char类型的字符传，也可以加const char* 类型的常量字符串，这也是通过重载来完成的，形成了统一的接口。接下来我们来看push_back,这个也是可以完成尾插的。
![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/dd20e78f23e64d15abe48b692ca143e0.png)
注意的是：push_back只能完成对char类型的尾插，我们还可以尝试使用append来追加字符串。简单的介绍了尾插，接下来我们继续看头插insert：
![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/73e4b7aaf7a84811b8de7c9206d115c2.png)
我们可以在特定的位置进行插入，完成头插或者指定位置插入，我们来看一下：

```cpp
#include<iostream>
#include<string>


int main()
{
	std::string s1("hello world");
	s1 += 'x';
	s1 += " baby";
	std::cout << s1 << std::endl;
	std::string s2 = "你好中国";
	s2.push_back('a');
	s2.append("世界美丽");
	std::cout << s2 << std::endl;
	std::string s3("ahut aust");
	s3.insert(s3.begin(), 'x');
	std::cout << s3 << std::endl;
}
```
这里好像注意，当使迭代器的时候，只能插入一个字符。详细见下图：
![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/014900e0f48042229ffbbc488b992670.png)
## 3.2 删:
在这里我们主要来测试一下Erase:
![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/1a6e17cf36eb492dbafe0ec7b119a9d2.png)
在这里我们也可以看到我们可以直接删除指定区间的位置，如果不给删除的长度，会直接从指定位置直接删除
![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/37e77e67cc2343d99bf1159ee27d1ce0.png)
我们可以清楚看到直接进行了删除，如果去掉后面的1，就全部删除没了。

## 3.3string的找
![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/a57233aea467468481ab1912aaf859d7.png)
这里有这么多找，我们都来详细讲一下：先说第一个find：
第一个find会返回找到指定目标的位置
![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/b7a45b70d23a4394a386eba2ee441967.png)
借助循环就可以完成对所有的" "进行查找和替换，对空格这一个进行换成两个#。但是由于一直插入会导致数组往后移导致效率偏低，这时我们需要进行改进：
![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/9caadca399484c41b7bc38deddd064d0.png)
这里减少了大量的移位，时间复杂度大量减少。大大提高了效率。这是第一个我们需要了解。我们继续看后面的：
![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/521871021dd846e49a7af3f406794a0b.png)
这里我们就可以做到分割最后的标识符知道文件的后缀。我们可以尝试文件的读取：
![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/4e99be488a4144619d1e4c14e79d5f7f.png)
我们在介绍一个
![在这里插入图片描述](https://i-blog.csdnimg.cn/direct/2792e5a19e67499e932e2deb8910e613.png)
这里就完成了对所有包含的的字母进行替换，通过这个可以完成游戏中的脏话进行替代。![请添加图片描述](https://i-blog.csdnimg.cn/direct/988c2cb4da904469b597d9c72baf83e5.gif)

