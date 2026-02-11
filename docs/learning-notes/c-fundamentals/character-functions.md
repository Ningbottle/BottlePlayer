---
time: 2025-06-13
tags:
  - C语言
---
在学习完成了C语言的的指针这一大难点后，我们将继续学习C语言里面的库函数，其中字符函数也是比较重要的一类。
# 零 . 字符函数：
下面列出了头文件 ctype.h 中定义的函数。
这些函数用于测试字符是否属于某种类型，这些函数接受 **int** 作为参数，它的值必须是 EOF 或表示为一个无符号字符。
如果参数 c 满足描述的条件，则这些函数返回非零（true）。如果参数 c 不满足描述的条件，则这些函数返回零
![[Pasted image 20250613145034.png]]
![[Pasted image 20250613151153.png]]
这些函数所需要的头文件是``ctype.h``,在这里我们只需要练习一个函数就ok了，其他的函数是非常类似的。借助下面的练习完成。
## 1.1. 练习1：将字符串中的小写字母改成大写字母，其他不变
###  方法一：利用ASCII码来改变
代码如下：
```c
#define  _CRT_SECURE_NO_WARNINGS
#include<stdio.h>
#include<ctype.h>
//练习1：将字符串中的小写字母改成大写字母，其他不变
int main()
{
	char str[] = "hElLo WOrld";
	char c;
	int i = 0;
	while (str[i])
	{
		char c = str[i];
		if (islower(c))
				c -= 32;
		putchar(c);
		i++;
	}
	return 0;
}
```
我们来看这段代码：1. 其中islower就是判断字母是小写，如果是小写，那么减去32（小写字母（ASCII 97–122）减去32 → 大写字母（ASCII 65–90），如果是大写就不进去if内部。
2. putchar输出转换后的字符（或原大写字符）到标准输出。
3. **循环条件**​：`str[i]` 检查当前字符是否为非零（即非 `'\0'`），遇到结束符时终止
4. `char c` 声明字符变量，用于存储单个字符，将 `str[i]` 的值（字符）复制到变量 `c` 中，二者内存独立，修改 `c` 不影响原字符串
### 方法二：利用字符转换函数
代码如下：
```c
//练习1：将字符串中的小写字母改成大写字母，其他不变
int main()
{
	char str[] = "hElLo WOrld";
	char c;
	int i = 0;
	while (str[i])
	{
		char c = str[i];
		c = toupper(c);
		putchar(c);
		i++;
	}
	return 0;
}
```
此时就不需要判断了，如果加了判断也是ok的，这样就能把字符进行大写化
``toupper``就是：to upper，往上-大写。
`tolower`就是：to lower，往下-小写。

# 一. 重要的字符函数：
## 1. strlen函数
### 1. 1.strlen 函数的使用：
![[Pasted image 20250613152248.png]]
strlen是库函数，本质是通过计算字符串里面的\0前面的字符的个数，他是可以用来计算字符串，但是不能用来计算字符数组的（一般字符数组里面不含\0）
同时返回值是一个无符号数，需要打印的话使用：%zd；传入字符串的首地址就ok
### 1.2 . strlen函数的模拟：
代码如下：
```c
size_t my_strlen(char* c)
{
	int count = 0;
	while (*c)
	{
		count++;
		c++;
	}
	return count;
}


int main()
{
	char* c = "helso";
	int ret = my_strlen(c);
	printf("%zd", ret);
}
```
还可以做一下稍微的改进：
```c
size_t my_strlen(char* c)
{
	int count = 0;
	while (*c++)
	{
		count++;
	}
	return count;
}


int main()
{
	char* c = "helsole";
	size_t ret = my_strlen(c);
	printf("%zd", ret);
}
```
先来看这个第二个代码的while循环：
- `*c++` 包含两步操作：
    - ​`*c`**​：解引用指针 `c`，获取当前字符的值。
    - ​`c++`​：指针后移（指向下一个字符），​**副作用发生在解引用之后**。
![[Pasted image 20250613153925.png]]
可以详细看这张图标来完成。我们还可以继续尝试递归的方式还有
```c
#include<assert.h>
size_t my_strlen(const char* str)
{
	assert(str);
	if (*str == '\0')
		return 0;
	else
		return 1 + my_strlen(str + 1);
}

int main()
{
	char a[] = "heijos";
	size_t ret = my_strlen(a);
	printf("%zd", ret);
}
```

## 2.strcpy函数
### 2.1 strcpy的使用：
C 库函数 **char *strcpy(char *dest, const char *src)** 把 **src** 所指向的字符串复制到 **dest**。
该函数返回一个指向最终的目标字符串 dest 的指针。
![[Pasted image 20250613155103.png]]
cpy顾名思义就是靠拷贝函数：将原字符串拷进目标字符串（src-->dest）,需要注意的是：
需要注意的是如果目标数组 dest 不够大，而源字符串的长度又太长，可能会造成缓冲溢出的情况，同时原来的字符串也是以‘\0’结尾。（\0也会被复制）
```c
#include<string.h>
int main()
{
	char* c = "copy to d";
	char d[10] = { 0 };
	strcpy(d, c);
	printf("%s", d);
}
```
网址给出的案例![[Pasted image 20250613162112.png]]
最后结果：
![[Pasted image 20250613162148.png]]


### strcpy代码模拟：
我们尝试模拟，代码如下：
```c
char* my_strcpy(char* dest, const char* str)
{
	int tmp = dest;
	while (*str)
	{
		*dest = *str;
		dest++;
		str++;
	}
	return dest;
}

#include<string.h>
int main()
{
	char* c = "copy to d";
	char d[10] = { 0 };
	my_strcpy(d, c);
	printf("%s", d);
}
```
这段代码就完成了复制；我们还可以做一下改进：
```c
char* my_strcpy(char* dest, const char* str)
{
	int tmp = dest;
	while (*dest++ = *str++);
	return dest;
}

#include<string.h>
int main()
{
	char* c = "copy to d";
	char d[10] = { 0 };
	my_strcpy(d, c);
	printf("%s", d);
}
```
以下分步拆解首次循环逻辑（假设 `str` 初始指向字符串 `"copy to d"` 的首字符 `'c'`）：

| ​**步骤**​               | ​**操作**​                                          | ​**指针状态**​      | ​**`dest` 赋值结果**​ |
| ---------------------- | ------------------------------------------------- | --------------- | ----------------- |
| 1. `while (*str++)` 检查 | 先取 `*str` 的值（`'c'`，非零 → 条件为真） → ​**再执行 `str++`**​ | `str` 指向 `'o'`  | ​**未执行赋值**​       |
| 2. 进入循环体               | 执行 `*dest = *str`                                 | `str` 仍指向 `'o'` | `dest[0] = 'o'`   |
| 3. `dest++`            | 目标指针后移                                            | `dest` 指向下一个位置  | -                 |

​**关键问题**​：
- ​**首字符 `'c'` 被完全跳过**​：条件判断时检查了 `*str`（`'c'`），但 `str++` 在进入循环体前已执行，导致循环内 `*str` 实际指向的是**第二个字符 `'o'`**。
- ​**`dest[0]` 未被覆盖**​：因初始化为 `0`（`char d[10] = {0}`），且未被赋值 → 首字节仍是 `'\0'`，导致 `printf` 输出空字符串。
你的问题直指C语言中**后置自增运算符（`*str++`）的核心陷阱**​：它会在判断条件后立刻移动指针，导致循环内操作错位。以下是详细分析：
#### 🔍 ​**问题核心：`while (*str++)` 的执行顺序**​
以下分步拆解第二段代码的首次循环逻辑（假设 `str` 初始指向字符串 `"copy to d"` 的首字符 `'c'`）

|​**步骤**​|​**操作**​|​**指针状态**​|​**`dest` 赋值结果**​|
|---|---|---|---|
|1. `while (*str++)` 检查|先取 `*str` 的值（`'c'`，非零 → 条件为真） → ​**再执行 `str++`**​|`str` 指向 `'o'`|​**未执行赋值**​|
|2. 进入循环体|执行 `*dest = *str`|`str` 仍指向 `'o'`|`dest[0] = 'o'`|
|3. `dest++`|目标指针后移|`dest` 指向下一个位置|-|

​**关键问题**​：
- ​**首字符 `'c'` 被完全跳过**​：条件判断时检查了 `*str`（`'c'`），但 `str++` 在进入循环体前已执行，导致循环内 `*str` 实际指向的是**第二个字符 `'o'`**。
- ​**`dest[0]` 未被覆盖**​：因初始化为 `0`（`char d[10] = {0}`），且未被赋值 → 首字节仍是 `'\0'`，导致 `printf` 输出空字符串。
#### ⚠️ ​**对比：三种自增写法的差异**​

以下对比你提供的三版代码中 `while` 条件的行为：

|​**代码版本**​|​**`while` 条件**​|​**行为**​|​**是否复制首字符**​|
|---|---|---|---|
|​**第一段**​|`while (*str)`|检查当前字符非 `'\0'`，​**不自增指针**​ → 循环内复制当前字符|✅ 是|
|​**第二段**​|`while (*str++)`|检查后**立刻自增**​ → 循环内复制的是**下一个字符**​|❌ 否（跳过首字符）|
|​**第三段**​|`while (*dest++ = *str++)`|先复制当前字符（包括 `'\0'`），​**再自增**​ → 完整复制|✅ 是|

> 💡 ​**核心区别**​：​**后置自增（`i++`）在表达式中“先返回值，再自增”​**，导致条件判断和循环内操作使用的指针位置不同步

## 3.strcat函数
### 3.1 strcat函数的使用：
C 库函数 **char *strcat(char *dest, const char *src)** 把 **src** 所指向的字符串追加到 **dest** 所指向的字符串的结尾
![[Pasted image 20250613162629.png]]
该函数返回一个指向最终的目标字符串 dest 的指针。
两个字符串要求以\0结尾。
正确代码如下
```c
int main()
{
	char c[22] = "i like";
	char* b = " you";
	strcat(c, b);
	printf("%s", c);
}
```
避免出现目标空间不够，会出先错误。

### 3.2 strcat的模拟实现
通过观察我们做出以下代码
```c
char* my_strcat(char* dest, char* str)
{
	char *tmp = dest;
	while (*dest++);
	dest--;
	while ((*dest++ = *str++));

	return tmp;
}

int main()
{
	char c[22] = "i like";
	char* b = " you";
	my_strcat(c, b);
	printf("%s", c);
}
```
#### 为什么有dest--
在 C 语言的字符串操作中，`while (*dest++)` 这种循环是**定位目标字符串末尾**的常见写法，但它的执行逻辑会导致 `dest` 指针最终指向 `'\0'` ​**之后的位置**，而非 `'\0'` 本身。以下是详细分析：
#### 🔍 ​**`while (*dest++)` 的执行机制**​
假设目标字符串 `dest` 为 `"i like"`（存储在数组 `c[22]` 中），其内存布局如下（`'\0'` 在索引 6 处）：

|索引|0|1|2|3|4|5|6|7|...|
|---|---|---|---|---|---|---|---|---|---|
|字符|i||l|i|k|e|\0|?|...|

循环的执行步骤如下：
1. ​**检查条件 `*dest`**​：
    - 若 `*dest` 非 `'\0'`，条件为真，进入循环体（但此循环无显式循环体，仅执行指针自增）。
2. ​**执行自增 `dest++`**​：
    - ​**后置自增**​：先返回 `dest` 的当前值，再将 `dest` 指向下一位置。
3. ​**重复直到遇到 `'\0'`**​：
    - 当 `dest` 指向 `c[6]`（`'\0'`）时：
        - 条件 `*dest` 为 `0`（假），​**但自增仍会执行**​ → `dest` 移动到 `c[7]`。
因此dest--是必须要有的。

## 4.strcmp函数
### 4.1strcmp的使用
顾名思义就是比较两个字符串的大小：
![[Pasted image 20250613170000.png]]
我们先说以下cmp的比叫方式：
字符串通过标准库函数 `strcmp()` 实现比较，核心逻辑为：
- ​**逐字符ASCII值对比**​：从首字符开始，逐个比较字符的ASCII值
- ​**终止条件**​：
    - 遇到不相同的字符 → 返回两字符ASCII差值（正数/负数）。
    - 遇到 `\0` 结束符 → 若长度不同，短字符串较小；否则相等（返回0）
![[Pasted image 20250613170755.png]]
使用说明，代码如下：
```c
int main()
{
	char c[22] = "i like";
	char* b = " you";
	int ret = strcpmy(c, b);
	if (ret = 0)
		printf("一样大");
	if (ret < 0)
		printf("c < b");
	else {
		printf("c > b");
	}
}
```
### 4.2.strcmp的模拟实现：
第一次尝试而写的错误代码，没有考虑到``\0``,就是str的结束
```c
int my_strcmp(const char *str1,const char * str2 )
{
	while (*str1 - *str2 == 0)
	{
		str1++;
		str2++;
	}
	return *str1 - *str2;
}

int main()
{
	char c[22] = "i like";
	char* b = " you";
	int ret = my_strcmp(c, b);
	if (ret == 0)
		printf("一样大");
	if (ret < 0)
		printf("c < b");
	else {
		printf("c > b");
	}
}
```
改进过后的代码就有：
```c
int my_strcmp(const char *str1,const char * str2 )
{
	while (*str1 != 0 && *str1 - *str2 == 0)
	{
		//只有未到达/0时才继续
		str1++;
		str2++;
	}
	return *str1 - *str2;
	//当到达0时一定比没到到达的小，这样比较出来了
}

int main()
{
	char c[22] = "i like";
	char* b = " you";
	int ret = my_strcmp(b,c);
	if (ret == 0)
		printf("一样大");
	if (ret < 0)
		printf("c < b");
	else {
		printf("c > b");
	}
}
```

## 5. strncpy函数：
### 5.1tsrncpy的使用：
![[Pasted image 20250616142538.png]]
``strncpy``函数和``strcpy``一样都是用于字符的的复制，不同的是：有一个无符号数字来控制复制的数量，最多复制n个数字。

```c
#define  _CRT_SECURE_NO_WARNINGS
#include<stdio.h>
#include<string.h>


int main()
{
	char arr[20] = "hello world";
	char arr2[20] = { 0 };
	strncpy(arr2, arr, 5);
	printf("%s", arr2);
}

```
这段代码就可以控制字符串的的复制，需要注意的是：- ​源字符串长度 < `n`：  
    `strncpy` 会复制全部源字符（包括其自带的 `\0`），并在目标缓冲区**剩余空间填充 `\0`直到写满 `n` 个字符
    _示例_：复制 `"Hi"`（长度2）到 `n=8` 的目标缓冲区，结果如 `"Hi\0\0\0\0\0\0"`。
- ​**源字符串长度 ≥ `n`**​：  
    仅复制前 `n` 个字符，​**不会自动添加 `\0`**。若未手动添加终止符，目标字符串可能因缺少 `\0` 导致后续操作（如 `printf`）出现未定义行为（如乱码或崩溃）


### 5.2strncpy的模拟使用：

我们来尝试使用代码来复原：
```c
char* my_strncpy(char *dest, const char *str, size_t num)
{
	for (int i = 0; i < num; i++)
	{
		if (*(str + i) != 0)
		{
			*(dest + i) = *(str + i);
		}
	}
	return dest;
}

int main()
{
	char arr[20] = "hello world";
	char arr2[20] = { 0 };
	my_strncpy(arr2, arr, 12);
	printf("%s", arr2);
}
```
这段函数就完成了对strncpy的模仿：


## 6. strncat函数：
### 6.1 strncat的使用：
![[Pasted image 20250617135721.png]]
``strncat``和``strcat``一样都是追加字符到原来的目标字符串；要求目标的字符串需要足够的空间，并返回目标字符串的开头的地址。我们来尝试以下如何使用：
```c
int main()
{
	char* a = "world";
	char arr[40] = "学习c，hello ";
	strncat(arr, a, 5);
	printf("%s", arr);
}

```
这一小段代码就可以完成对arr这个字符串的追加，我们这次来深究以下arr字符串：
####  ​`strncat()` 的工作原理​
1. ​**定位目标字符串的结尾**​
    - 函数首先找到目标字符串 `dest` 的结尾（即 `dest` 的 `\0` 位置）。
    - 例如：若 `dest = "Hello"`，则从 `'o'` 后的 `\0` 处开始追加。
2. ​**追加字符并处理终止符**​
    - ​**情况 1：`n` ≤ 源字符串长度**​  
        复制源字符串 `src` 的前 `n` 个字符到 `dest` 末尾，然后**自动追加一个 `\0`**。  
        _示例_：
        ```c
        char dest[10] = "Hi";
        strncat(dest, "World", 3); // 结果："HiWor\0"
        ```
        - 复制 `'W'`、`'o'`、`'r'` 后，函数自动添加 `\0`。
    - ​**情况 2：`n` > 源字符串长度**​  
        复制整个 `src`（包括其自身的 `\0`），并**用 `\0` 填充剩余空间**直到总追加字符数达到 `n`。  
        _示例_：
        ```c
        char dest[10] = "A";
        strncat(dest, "BC", 5);    // 结果："ABC\0\0\0"
        ```
        - 源字符串 `"BC"` 长度=2（含 `\0`），追加后补充 3 个 `\0` 至总长度 5。
3. ​**返回值**​  
    返回目标字符串 `dest` 的指针，便于链式调用。​

| ​**特性**​        | `strncat()`       | `strncpy()`                |
| --------------- | ----------------- | -------------------------- |
| ​**终止符处理**​     | 总是追加 `\0`         | 仅在 `src` 长度 < `n` 时填充 `\0` |
| ​**填充行为**​      | 仅填充至总追加字符数为 `n`   | 用 `\0` 填满整个 `n` 位          |
| ​**目标缓冲区修改起点**​ | `dest` 的结尾 `\0` 处 | `dest` 的起始位置               |
| ​**安全性**​       | 更高（强制终止）          | 需手动添加 `\0`，否则可能出错          |
### 6.2strncat的模拟使用：
```c
char* my_strncat(char* dest,const char* str, size_t num)//通过指针来完成对地址的接收
{
	char *tmp = dest;
	while (*dest++);
	*dest--;//当完成了*str == 0 时，str还会在加一次，我们减一次，完成对\0的赋值；
	for (int i = 0; i < num; i++)
	{
		*(dest + i) = *(str + i);
	}
	*(dest + num) = '\0';
	return tmp ;
}


int main()
{
	char* a = "world";
	char arr[40] = "学习c，hello ";
	my_strncat(arr, a, 5);
	printf("%s", arr);
}
```
我的第一版代码是有错误的；没有想到​**未处理源字符串提前结束**，这会导致会越界访问，还有逻辑上的麻烦处理了，因此我们需要做出以下的改变：
```c
#include <stdio.h>
#include <assert.h>

char* my_strncat(char* dest, const char* src, size_t num) 
{
    assert(dest && src);  // 确保指针有效
    char* ret = dest;     // 保存目标字符串起始地址

    // 1. 定位到目标字符串的结束符
    while (*dest != '\0') 
    {
        dest++;
    }

    // 2. 复制最多num个字符（遇'\0'则提前停止）
    while (num-- && *src != '\0')
    {
        *dest++ = *src++;
    }

    // 3. 在末尾添加终止符（关键修正！）
    *dest = '\0';

    return ret;
}

int main() 
{
    const char* a = "world";  // 使用const避免警告
    char arr[40] = "学习c，hello ";

    // 测试1：正常追加
    my_strncat(arr, a, 5);
    printf("追加后: %s\n", arr);  // 输出：学习c，hello world

    // 测试2：源字符串短于num
    char arr2[20] = "test";
    my_strncat(arr2, "xy", 5);   // 实际只追加"xy"
    printf("短源测试: %s\n", arr2);  // 输出：testxy

    return 0;
}
```
这样就完成了对strncat的模拟使用。

## 7. strstr函数：
![[Pasted image 20250617143437.png]]
图片讲的很清楚，是找寻haystack中第一次出现needle的位置，如果还是看的不清楚，我们给出两个实例：
```c
int main()
{
	char *str1 = "i am a student,i want be a good student";
	char *str2 = "student";
	char* pch = strstr(str1, str2);
	printf("%s", pch);
}
```
![[Pasted image 20250617144558.png]]
从图上可以看出是从第一处student开始打印。
```c
int main() {
    const char* haystack = "Hello, world! This is a test string.";
    const char* needle = "world";

    // 在 haystack 中查找 needle
    char* result = strstr(haystack, needle);

    if (result != NULL) {
        printf("Substring found: %s\n", result);
    }
    else {
        printf("Substring not found.\n");
    }

    return 0;
}
```
- `strstr()` 函数从 `haystack` 的开头开始查找 `needle`，直到找到匹配的子字符串或到达 `haystack` 的末尾。
- 如果 `needle` 是空字符串（`""`），`strstr()` 会返回 `haystack` 的起始地址。
- `strstr()` 是区分大小写的。如果需要不区分大小写的查找，可以使用 `strcasestr()`（非标准函数，可能需要特定库支持）。
![[Pasted image 20250617145121.png]]
## 8.strtok函数：
![[Pasted image 20250617145718.png]]
将字符串 str 按分隔符集合 delim 分割成多个子串（标记），每次调用返回一个子串的指针。
参数​：
str：首次调用传入待分割字符串，后续调用需传入 NULL（函数通过内部静态指针记录位置）
delim：分隔符集合（如 ",; "），任意字符匹配均触发分割
```c
#include <string.h>

int main() {
    char str[] = "apple,orange;banana";
    const char *delim = ",;";
    char *token;

    // 1. 首次调用传入待分割字符串
    token = strtok(str, delim);

    // 2. 后续调用传入 NULL，循环获取子串
    while (token != NULL) {
        printf("%s\n", token);
        token = strtok(NULL, delim);  // 继续分割
    }

    return 0;
}
```
###  ​**代码逐行解析**​
```c
#include <string.h>
#include <stdio.h>  // 补充了printf所需的头文件

int main() {
    char str[] = "apple,orange;banana";  // 原始字符串（存储在可修改的数组中）
    const char *delim = ",;";             // 分隔符集合（逗号和分号）
    char *token;                          // 用于接收分割后的子串指针

    // 首次调用：传入原始字符串
    token = strtok(str, delim);            // 找到第一个子串 "apple"

    // 循环获取所有子串
    while (token != NULL) {
        printf("%s\n", token);             // 打印当前子串（如 "apple"）
        token = strtok(NULL, delim);       // 后续调用传入 NULL，继续分割
    }
    return 0;
}
```
###  ​**`strtok()` 的工作原理**​

#### 1. ​**首次调用：传入原始字符串**​

- ​**行为**​：
    - 函数从 `str` 开头扫描，跳过开头的分隔符（本例无）
    - 找到第一个非分隔符字符（`'a'`），将其作为子串起始地址。
    - 继续扫描直到遇到分隔符（`,` 或 `;`），​**将该分隔符替换为 `\0`**，返回子串指针（`"apple"`）。
- ​**字符串变化**​
```diff
原始： "apple,orange;banana"
修改后： "apple\0orange;banana"
	     ↑ token 指向此处
```

#### 2. ​**后续调用：传入 `NULL`**​

- ​**为什么必须传 `NULL`？​**​
    - `strtok()` 内部通过 ​**静态指针**​ 记录上次分割结束的位置（即 `\0` 后的字符）
    - 传入 `NULL` 告知函数继续从静态指针位置扫描（而非重新从头开始）。
- ​**第二次调用**​：
    - 从 `orange;banana` 开始扫描，找到 `;` 替换为 `\0`，返回 `"orange"`。
        ```diff
        修改后： "apple\0orange\0banana"
                        ↑ token 指向此处
```
        
- ​**第三次调用**​：
    - 从 `banana` 开始扫描，无更多分隔符，直接返回 `"banana"`
#### 3. ​**终止条件**​

- 当扫描到字符串末尾时，返回 `NULL`，循环结束
为何不会打印整个字符串？​**​

- ​**`strtok()` 修改了原始字符串**​：  
    它在每个分隔符位置插入 `\0`，将原字符串**分割为多个独立子串**。例如：
    ```c
    "apple,orange;banana" 
    → 被修改为： "apple\0orange\0banana"
    ```
- ​**`printf("%s\n", token)` 的机制**​：  
    `%s` 从 `token` 指向的地址开始打印，​**遇到 `\0` 立即停止**。因此每次只打印一个子串（如 `"apple"` 遇到第一个 `\0` 结束）
 ​**关键点**​：`strtok()` 仅替换作为分割点的分隔符（如 `,` 和 `;`），而非所有分隔符。连续分隔符（如 `"a,,b"`）会被跳过，不产生空子串
###  ​**内部状态与字符串变化示例**​

| ​**调用次数**​ | ​**参数**​ | ​**操作**​      | ​**字符串状态**​               | ​**返回值**​  |
| ---------- | -------- | ------------- | ------------------------- | ---------- |
| 第一次        | `str`    | 替换 `,` 为 `\0` | `"apple\0orange;banana"`  | `"apple"`  |
| 第二次        | `NULL`   | 替换 `;` 为 `\0` | `"apple\0orange\0banana"` | `"orange"` |
| 第三次        | `NULL`   | 无分隔符，返回剩余部分   | 保持不变                      | `"banana"` |
| 第四次        | `NULL`   | 已扫描完毕         | 保持不变                      | `NULL`     |
