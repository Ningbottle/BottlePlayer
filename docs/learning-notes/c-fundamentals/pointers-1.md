---
time: 2025-04-27
tags:
  - C语言
---
# 1.野指针：
## 1.1.野指针介绍：
经过上次的介绍我们已经初步认识了一点野指针[[C语言 5：指针及其拓展(0)]]  这次我们来详细认识一下野指针：
野指针指的是指向到未知区域的指针，常常具有未知性。

## 1.2野指针的成因：
### 1.2.1.指针没有初始化：
先看一段代码：
```C
#define  _CRT_SECURE_NO_WARNINGS
#include<stdio.h>


int main()
{
	int* p;
	*p = 20;
	printf("%d", *p);
}
```

这段代码有明显的错误，由于指针没有初始化：指针指向的区域是未知的，如果指向系统区域后面再解引用进行改变会导致系统出现问题：为了防止此类问题，在vs2022中编译器会无法运行。
### 1.2.2.函数调用空间释放：
如果在函数创建一个变量，而返回他的地址，通过指针变量来接受这个变量，也会导致野指针。代码如下：
```C 
#define  _CRT_SECURE_NO_WARNINGS
#include<stdio.h>

int* add(int x, int y)
{
	int z = x + y;
	return &z;
}

int main()
{
	
	int* ret = add(2,7);
	printf("%d", *ret);
	

}
```
即使修正返回值类型，`ret` 指向的地址在 `add` 函数退出后已失效，`printf("%d", *ret)` 访问的是无效内存。这也叫**悬空指针访问**。
### 1.2.3. 数组跳跃过大：
在[[C语言 5：指针及其拓展(0)]]我们已经讲过数组的部分野指针，这里我们还有回顾一下
先给出正常代码：
```C

int main()
{
	int i = 0;
	char arr[10] = { 1,2,3,4,5,6,7,8,9,0 };
	char* pa = &arr[0];
	for (i = 0; i < 10; i++)
	{
		printf("%d", *(pa+i));
	}

}
```
给出此段代码会正常显示出数组的1234567890；如果将char换成int会咋样呢，代码如下：
```C
#define  _CRT_SECURE_NO_WARNINGS
#include<stdio.h>



int main()
{
	int i = 0;
	char arr[10] = { 1,2,3,4,5,6,7,8,9,0 };
	int* pa = &arr[0];
	for (i = 0; i < 10; i++)
	{
		printf("%d", *(pa+i));
	}

}
```
![[Pasted image 20250426145557.png]]
此时也是野指针的问题，出现了大数，原因是指针已经跳出了指针的地址范围。

# 2.assert断言：
为了预防野指针，我们除了在以上三个成因方面来操作，还可以使用assert来预防,assert的使用需要头文件：`#include<assert.h>`  
``` C
assert( p != NULL )
```
该代码是用来判断：如果* p指针指向是否为空，如果真的为空，则停止运行；
如果不是空，则继续运行，这样就能防止野指针的出现：
```C
#include <assert.h>
void copy_data(void *dest, const void *src, size_t len) {
    assert(dest != NULL && src != NULL);  // 验证指针有效性
    // 数据拷贝逻辑...
}
```
- **宏定义**：通过 `assert(expression)` 宏实现，属于标准库 `<assert.h>` 的一部分。若条件为假，会向 `stderr` 输出错误信息（含文件名、行号及表达式），并调用 `abort()` 终止程序 。
- ​**启用与禁用**：默认在调试模式（Debug）启用，发布模式（Release）可通过定义 `NDEBUG` 宏禁用断言以减少性能开销
# 3. 模拟实现strlen()函数：
库函数strlen()是查询字符串串的长度，是统计字符串里面\0前的字符个数：
```c
#define  _CRT_SECURE_NO_WARNINGS
#include<stdio.h>
#include<assert.h>

 unsigned int my_strlen(char* str)
{
	int count = 0;
	assert(*str);
	while (*str)
	{
		count++;
		str++;
	}
	return count;
}


int main()
{
	int len = my_strlen("abcdef");
	printf("%d", len);
}
```
1. ​**断言条件 `assert(*str)`**  
    断言检查的是 `*str` 是否非零（即字符串的第一个字符是否为非 `\0`）。若传入空字符串（如 `my_strlen("")`），断言失败，程序终止。
    
2. ​**循环条件 `while (*str)`**
    - ​**当 `str` 指向 `\0` 时**：`*str` 的值为 ASCII 码 0，逻辑判断为 `false`，循环停止。
    - ​**正常字符串场景**：例如 `"abcdef"`，当 `str` 逐次后移，最终指向字符串末尾的 `\0` 时，`while (*str)` 条件失效，循环结束。此时 `count` 值为 6，正确计算了字符串长.
# 4.函数传址调用：
我们来看函数的的特殊情况：交换函数，错误示例代码如下：
```C
#define  _CRT_SECURE_NO_WARNINGS
#include<stdio.h>

int swap(int x,int y)
{
	int z = 0;
	z = x;
	x = y;
	y = z;
}

int main()
{
	int a = 5;
	int b = 3;
	swap(a,b);
	printf("%d,%d", a, b);
}
```
结果发现a和b的值并没有换过来；
可以进行改变如下：
```C
#define  _CRT_SECURE_NO_WARNINGS
#include<stdio.h>

int swap(int* x,int* y)
{
	int z = 0;
	z = *x;
	*x = *y;
	*y = z;
}

int main()
{
	int a = 5;
	int b = 3;
	swap(&a,&b);
	printf("%d,%d", a, b);
}


```
可以看到a和b的的值已经换了。
# 5.数组名的本质
在之前的代码中我们多次出现了arr，有时取数组的地址，用的是&arr[0],有时候却用的是arr，这些到底有什么区别呢？
我们先来看一段代码：
```C
#define  _CRT_SECURE_NO_WARNINGS
#include<stdio.h>

int main()
{
	int arr[10] = { 0,1,2,3,4,5,6,7,8,9 };
	int* p1 = arr;
	int* p2 = &arr[0];
	int* p3 = arr;
	printf("%p\n", p1);
	printf("%p\n", p2);
	printf("%p\n", p2);

}

```
我们来看结果：
![[Pasted image 20250426163337.png]]
这是不是代表他们是一样的呢，其实并不是一样的，虽然他们有​**相同地址值**  即：
数组名 `arr` 在大多数表达式中会被隐式转换为指向首元素的指针，即 `arr` 等价于 `&arr[0]`。两者的地址值均指向数组首元素的内存位置。但是还是有点差别：

| ​**`arr` (数组名)**           | **`&arr[0]`**              | ​**`&arr`**                   |
| -------------------------- | -------------------------- | ----------------------------- |
| `int*` (指向元素的指针)           | `int*` (指向元素的指针)           | `int(*)[5]`指向的是数组的指针          |
| 隐式退化为首元素地址                 | 首地址                        | 整个数组的起始地址                     |
| 按元素大小移动（如 `arr+1` 移动 4 字节） | 按元素大小移动（如 `arr+1` 移动 4 字节） | 按整个数组大小移动（如 `&arr+1` 移动 40 字节 |
这里就详细的解释了他们的区别，这样我们也就知道了`arr`也就是地址。
## 总结

- ​**地址值相同**：`arr` 和 `&arr[0]` 指向同一内存位置。
- ​**类型决定行为**：`arr` 隐式退化为指针，`&arr` 保留数组类型信息，`&arr[0]` 显式操作元素。
- ​**操作差异**：指针运算、函数传参、`sizeof` 等场景需注意类型匹配。

实际编程中，优先使用 `arr` 简化代码，在需要明确语义或处理数组整体时使用 `&arr[0]` 或 `&arr`

# 6 一位数组传参本质：
## 6.1 问题的抛出：
我们之前讲过一般函数？传参并没有把地址传给函数；那么数组呢？我们来看一下代码：
```C
void my_strlen(int* arr)//int arr[]
{
	int sz2 = sizeof(arr) / sizeof(arr[0]);
	printf("sz2 = %d \n", sz2);
}
int main()
{
	int arr[5] = { 0,1,2,3,4 };
	my_strlen(arr);
	int sz1 = sizeof(arr) / sizeof(arr[0]);
	printf("sz1 = %d \n", sz1);
}

```
![[Pasted image 20250426224536.png]]
运算结果很是奇怪，毕竟在我们的猜想里，两个结果都是5，而在函数里面出现的是2，这是应为在函数中我们只是将arr的首地址传了过去，而也只用了一个arr指针来计算，此时没有进行解引用。在函数内部，`arr`本质是`int*`类型的指针，而非完整的数组。 `sizeof(arr)`此时计算的是**指针变量的大小**，而非整个数组的大小。而`int`类型在32位系统中占用4个字节
```C
// main函数中的sizeof(arr)
int sz1 = sizeof(arr) / sizeof(arr[0]); 
// arr是完整的数组，sizeof(arr) = 5 * sizeof(int) = 20（假设int为4字节）
//20/4 = 5

// my_strlen函数中的sizeof(arr)
int sz2 = sizeof(arr) / sizeof(arr[0]); 
// arr退化为指针，sizeof(arr) = sizeof(int*) = 8（64位系统）或4（32位系统）
//此时8/4 = 2；
//如果是32为：4/4 = 1
```
而在32位系统有如下结果：
![[Pasted image 20250426231550.png]]
## 6.2问题的解决：
在C语言中，当代码运行在**x86架构（32位系统）​**时，若出现`sz2=1`和`sz1=5`的结果，核心原因在于**数组作为函数参数传递时的退化行为**和`sizeof`运算符在不同上下文中的计算规则。以下是详细分析：
```c
void my_strlen(int* arr) {
    int sz2 = sizeof(arr) / sizeof(arr[0]);  // sz2计算结果为1
    printf("sz2 = %d \n", sz2);
}

int main() {
    int arr[5] = {0,1,2,3,4};
    my_strlen(arr);  // 传递数组首地址
    int sz1 = sizeof(arr) / sizeof(arr[0]);  // sz1计算结果为5
    printf("sz1 = %d \n", sz1);
}
```

输出结果为：

```markdown
sz2 = 1
sz1 = 5
```
### 6.2.1 ​关键机制分析:

#### （1）​**数组退化为指针**

- ​**函数参数传递**：当数组`arr`作为参数传递给`my_strlen(int* arr)`时，C语言会将数组退化为指向其首元素的指针。
    - 在函数内部，`arr`不再是完整的数组，而是`int*`类型的指针。
    - 因此，`sizeof(arr)`计算的是**指针变量的大小**，而非数组总大小。

#### （2）​`sizeof`的计算规则

| ​**上下文**          | ​**计算逻辑**                           | ​**结果（32位系统）​**    |
| ----------------- | ----------------------------------- | ------------------ |
| ​**main函数中**      | `sizeof(arr)`：数组总大小（`5 * 4 = 20`字节） | `sz1 = 20 / 4 = 5` |
| ​**my_strlen函数中** | `sizeof(arr)`：指针大小（4字节）             | `sz2 = 4 / 4 = 1`  |

- ​**指针大小**：在32位系统下，所有指针类型（如`int*`）的大小为**4字节**​（x86架构特性）。
- ​**元素大小**：`arr[0]`是`int`类型，通常占4字节（由编译器决定)

| ​**场景**     | ​**32位系统（x86）​** | ​**64位系统（x64）​** |
| ----------- | ---------------- | ---------------- |
| ​**指针大小**   | 4字节              | 8字节              |
| ​**sz2的计算** | `4 / 4 = 1`      | `8 / 4 = 2`      |
| ​**sz1的计算** | `20 / 4 = 5`     | 不变（仍为5）          |
# 7.冒泡排序：
先来看看冒泡排序的定义：将两个数进行一一比较，在进行排序，这是比较基础的算法：
我们要将一段数字进行排序，排成升序：
```C
#define  _CRT_SECURE_NO_WARNINGS
#include<stdio.h>

void bubble_sort(int arr[],int sz)
{
	int i = 0;
	int j = 0;
	int z = 0;
	for (i = 0; i < sz-1; i++)
	{
		for (j = 0; j <sz - i -1 ; j++)
		{
			if (arr[j] > arr[j + 1])
			{
				z = arr[j];
				arr[j] = arr[j + 1];
				arr[j + 1] = z;
			}
		}
	}
	
	
}

int main()
{
	int arr[10] = { 9,8,7,6,5,4,3,2,1,0};
	int sz = sizeof(arr) / sizeof(arr[0]);
	bubble_sort(arr,sz);
	int i = 0;
	for (i = 0; i < sz; i++)
	{
		printf("%d", arr[i]);
	}
```
这就是冒泡排序：第一层for循环来完成第一次排序，确定要排几次，后面的第二次循环来交换两个之间的数：这次将第一个数排序成功，则下一次循环会减一.但是此次代码还是做了太多重复次。
我们可以进行以下优化：代码如下：
```C
#define  _CRT_SECURE_NO_WARNINGS
#include<stdio.h>

 void bubble_sort(int arr[],int sz)
{
	int i = 0;
	int z = 0;
	for (i = 0; i < sz - 1; i++)
	{
		int j = 0;
		while (arr[j] - arr[j + 1] > 0 && j < sz-i-1)
		{
			z = arr[j];
			arr[j] = arr[j + 1];
			arr[j + 1] = z;
			j++;
		}
	}
	
}

int main()
{
	int arr[10] = { 9,8,7,6,5,4,3,2,1,0};
	int sz = sizeof(arr) / sizeof(arr[0]);
	bubble_sort(arr,sz);
	int i = 0;
	for (i = 0; i < sz; i++)
	{
		printf("%d", arr[i]);
	}

}

```
在这里我们可以看到，只有满足前面的数大于后面的数才会进行交换。但是此次优化还不彻底我们还可以继续优化，可以尝试加入一个标志符号：如果有一次符号没有变化就已经变成有序就可以之间break。
```C
#define  _CRT_SECURE_NO_WARNINGS
#include<stdio.h>

 void bubble_sort(int arr[],int sz)
{
	int i = 0;
	int z = 0;
	for (i = 0; i < sz - 1; i++)
	{	
		int flag = 1;//假设直接是有序的；
		int j = 0;
		while (arr[j] - arr[j + 1] > 0 && j < sz-i-1)
		{
			z = arr[j];
			arr[j] = arr[j + 1];
			arr[j + 1] = z;
			j++;
			flag = 0;//发生交换就为0；
		}
		if (flag == 1)
			break;
	}
	
}

int main()
{
	int arr[10] = {1,2,4,5,6,3,7,8,9};
	int sz = sizeof(arr) / sizeof(arr[0]);
	bubble_sort(arr,sz);
	int i = 0;
	for (i = 0; i < sz; i++)
	{
		printf("%d", arr[i]);
	}

}
```
# 8. 二级指针：
我们常在想可不可以把指针(地址)传进给指针，在C语言中我们把这个叫做二级指针：
我们先看以下的代码:
```C
#define  _CRT_SECURE_NO_WARNINGS
#include<stdio.h>

int main()
{
	int a = 10;
	int* pa = &a;//pa指向的是a的地址；
	int** ppa = &pa;//将pa的地址给ppa；
	printf("%p\n", &a);
	printf("%p\n", pa);
	printf("%p\n", ppa);
	printf("%p\n", *ppa);
	printf("%d\n", *pa);
	printf("%d\n", **ppa);
}
```
![[Pasted image 20250427151419.png]]
- - -
完
