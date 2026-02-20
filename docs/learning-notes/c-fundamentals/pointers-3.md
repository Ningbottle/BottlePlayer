---
time: 2025-05-20
tags:
  - C语言
---
# 1.回调函数？
## 1.1 回调函数的定义：
我们在上一篇文章中讲述了转移表，但是并没有讲完全，这里我们将借助回调函数来完成和完善这个回调函数。我们先看回调函数的定义：回调函数是**通过函数指针或参数形式传递给另一个函数，并在特定事件或条件触发时被调用执行的函数**​。它并非由开发者直接调用，而是由接收它的函数在需要时激活，常用于实现灵活的事件响应和异步逻辑。这种设计充分体现了c语言的灵活性。

```C
void menu() {
	printf("****************************​\n"
		"​**********1. add ​************\n"
		"​**********2. sub ************\n"
		"​**********3. mul ************\n"
		"​**********4. div ************\n"
		"​**********0. exit ***********\n"
		"输入你的选项 --------->");
}
void add(int x, int y)
{
	int ret = x + y;
	printf("执行的结果为 %d ", ret);
}

void sub(int x, int y)
{
	int ret = x - y;
	printf("执行的结果为 %d ", ret);
}

void mul(int x, int y)
{
	int ret = x * y;
	printf("执行的结果为 %d ", ret);
}

void div(int x, int y)
{
	int ret = x / y;
	printf("执行的结果为 %d ", ret);
}

int main()
{
	int i = 0, j = 0;
	int input = 1;
	void (*p[5])(int a, int b) = {0,add,sub,mul,div};
	do
	{
		menu();
		scanf("%d",&input);
		if (input <= 4 && input >= 1)
		{
			printf("请输入两个数字：");
			scanf("%d %d", &i, &j);
			(*p[input])(i, j);//函数的使用
		}
		else if (input == 0)
		{
			printf("退出计算器\n");
		}
		else
			printf("输入错误，请重新输入\n");
	} while (input);
	return 0;
}
```
我们来看这段代码这段代码有多段的函数，我们是否能通过回调函数来实现改进，有代码如下：
```C
#define  _CRT_SECURE_NO_WARNINGS
#include<stdio.h>
void menu()
{
	printf(
		"**************欢迎使用计算器 ************\n"
		"**************1.加法  2.减法 ************\n"
		"**************3.除法  4.乘法 ************\n"
		"**************  0. 退出      ************\n"
		"*****************************************\n"
		"****************请输入你需要功能的序号—>\n"
	);
} // 菜单的函数，来完成整个程序的菜单

int add(int a,int b)
{
	return a + b;
}

int sub(int a, int b)
{
	return a - b;
}

int div(int a, int b)
{
	return a / b;
}

int mul(int a, int b)
{
	return a * b;
}
// 通过函数来完成加减乘除小功能

int calc(int(*pf)(int,int))//通过函数指针传进去
{
	int ret = 0; //定义 ret 来接受结果
	int x, y = 0;
	printf("请输入你要计算的两个值：\n");
	scanf("%d %d", &x,&y);
	ret = pf(x, y);
	printf("ret = %d\n", ret);
}

int main()
{
	int input = 0;
	do
	{
		menu();//每一次都应该输出菜单
		scanf("%d",&input);
		switch (input) {
		case 1: calc(add); break;
		case 2: calc(sub); break;
		case 3: calc(div); break;
		case 4: calc(mul); break;
		//函数名也是地址，将函数的地址传入函数指针 int（*pf）(int int)
		case 0: printf("已经退出\n"); break;
		default:
			printf("输入错误，请输入其他值\n");
			while (getchar() != '\n'); // 清空缓冲区
		}
	} while (input);
}

```
这段代码就是用来函数调用来实现的：
**函数回调（Callback）​**​ 的核心实现体现在`calc`函数和`switch`分支的动态绑定上
1. 函数指针的定义
```C
int calc(int(*pf)(int, int)) { // pf是函数指针参数
    int ret = pf(x, y); // 通过指针调用具体函数
}
```
- ​**`int(*pf)(int, int)`**​：定义了一个函数指针`pf`，该指针指向一个接受两个`int`参数并返回`int`的函数。
- ​**动态绑定**​：通过将不同函数（如`add`、`sub`）的地址传递给`calc`，实现了同一接口处理多种操作。
 2. 回调函数的触发和注册
```C
   switch (input) {
    case 1: calc(add); break; // 传递add函数地址
    case 2: calc(sub); break; // 传递sub函数地址
    ...
}
```
- **注册阶段**​：用户选择操作（如加法）时，将`add`函数地址作为参数传递给`calc`。
- ​**触发阶段**​：在`calc`内部通过`pf(x, y)`调用实际绑定的函数（如`add`），完成计算。
---
# 2. qsort函数的使用
## 2.1 qsort函数的定义：
以下是关于C语言中`qsort`函数的详细解析与应用指南：
`qsort`是C标准库（`<stdlib.h>`）提供的通用快速排序函数，支持对任意数据类型数组的排序。其核心原理基于**分治法**，通过递归将数组划分为子序列并独立排序，最终合并结果
​**函数原型**​：
`qsort` 是 C 标准库中提供的一个函数，用于对数组进行快速排序。它在 `<stdlib.h>` 头文件中定义。`qsort` 使用的是快速排序算法（quicksort），这是一种高效的排序算法，平均时间复杂度为 O(n log n)。
C 库函数 **void qsort(void *base, size_t nitems, size_t size, int (*compar)(const void *, const void*))** 对数组进行排序。
![[Pasted image 20250520160500.png]]  
- `base`: 指向待排序数组的第一个元素的指针。
- `nitems`: 数组中的元素数量。
- `size`: 数组中每个元素的大小（以字节为单位）。
- `compar`: 比较函数的指针，该函数用于比较两个元素。比较函数应当返回一个整数，表示比较结果：
    - 小于零：表示第一个元素小于第二个元素。
    - 等于零：表示两个元素相等。
    - 大于零：表示第一个元素大于第二个元素。
这个比较和排序很想我们在[[C语言 5：指针及其拓展(1)#7.冒泡排序：]]所写的冒泡排序，不过我们用的冒泡排序只能使用给int整形来进行排序，此时qsort排序可以进行多种排序，他正是用了函数回调，可以做到排序结构体和浮点数等其他的数。
`qsort`通过**通用化设计**和**回调函数机制**，实现了对任意数据类型的灵活排序。
## 2.2qsort函数的使用
### 2.2.1 qsort 函数排序int型数组：
我们按照定义来尝试去使用排序函数去排序int型，代码如下：
```C
int_compare(const void *p1,const void *p2)
{
	return (*(int*)p1 - *(int*)p2);
	//强制转换为int型，然后相减
}

int main()
{
	int arr[10] = {9,7,4,3,5,6,8,1,0,2};
	int sz = sizeof(arr) / sizeof(arr[0]);
	qsort(arr,sz,sizeof(arr[0]),int_compare);
	for (int i = 0; i < sz; i++)
	{
		printf("%d ", arr[i]);
	}
}
```
在C语言的`qsort`函数中，使用`const void*`作为参数类型是为了实现**通用性**和**安全性**的双重目标。以下是详细解释：
`void*`的作用：通用指针类型
1. ​**支持任意数据类型**​  
    `qsort`是一个通用排序函数，需支持对**任意类型数据**​（如`int`、结构体、字符串等）的排序。
    - `void*`是“无类型指针”，可接受任何类型的指针（如`int*`、`char**`），避免为每种数据类型单独编写排序函数。
    - ​**在比较函数中**​：通过强制类型转换（如`*(int*)p1`）将其还原为具体类型
2. ​**内存操作灵活性**​  
    `qsort`通过`void*`和`size`参数（元素大小）计算偏移，实现对不同大小元素的统一内存访问：
    ```c
    void *element = (char*)base + i * size; // 通用地址计算
    ```

 `const`的作用：数据保护
1. ​**防止意外修改**​  
    `const void*`表示指针指向的内容是只读的，禁止在比较函数中修改数据。例如，若误写为`*(int*)p1 = 0`，编译器会报错
    ```c
    error: assignment of read-only location ‘*(int*)p1’
    ```
2. ​**语义明确性**​
    - 对排序函数而言，比较操作**不应改变数据内容**，`const`明确告知调用者数据不会被修改。
    - 若比较函数可能修改数据，排序结果将不可预测。
 **为何不直接使用`int*`作为参数类型？​**​
- 若比较函数声明为`int(*)(int*, int*)`，则`qsort`只能用于`int`数组，失去通用性。
- 使用`void*`允许同一函数处理多种数据类型（如结构体、字符串）。
**为何不省略`const`？​**​
- 从语法上可以省略，但会降低代码安全性。例如，以下代码能编译通过但可能导致错误：
 
    ```c
    int unsafe_compare(void *p1, void *p2) {
        *(int*)p1 = 0; // 危险操作：修改原数据
        return *(int*)p2 - *(int*)p1;
    }
    ```
    
- `const`强制开发者遵守“只读”约定，减少潜在错误
总结：

| 特性         | `void*`的作用              | `const`的作用    |
| ---------- | ----------------------- | ------------- |
| ​**通用性**​  | 支持任意数据类型，适配`qsort`的通用设计 | 不涉及           |
| ​**安全性**​  | 不直接涉及                   | 防止比较函数意外修改原数据 |
| ​**类型操作**​ | 需强制类型转换以访问具体数据          | 不涉及           |
通过`void*`和`const`的配合，`qsort`实现了类型安全、高复用性的排序功能，是C语言通用库设计的经典范例。
### 2.2.1 使用qsort排序结构体：
