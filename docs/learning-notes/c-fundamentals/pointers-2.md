---
time: 2025-04-27
tags:
  - C语言
---
# 1.指针数组：
## 1.1 .指针数组的定义：
在上一篇文章：我们已经过二级指针，现在我们来看指针数组，他到底是数组还是指针呢？我们先看名字：整形数字，字符数组，很明显他是把指针放在数组里，到最后还是数组：地址可以用来放在该数组中，那么应该怎么表示呢？我们可以先看指针：

| 整形数组          | 字符数组           | 指针数组            |     |
| ------------- | -------------- | --------------- | --- |
| ``int arr[]`` | ``char arr[]`` | ``int* parr[]`` |     |
## 1.2.指针数组的作用：
我们可以尝试使用指针数组来==模拟==二维数组,代码如下：
```C
#define  _CRT_SECURE_NO_WARNINGS
#include<stdio.h>

int main()
{
	int i = 0;
	int j = 0;
	int arr0[5] = { 0,1,2,3,4 };
	int arr1[5] = { 5,6,7,8,9 };
	int arr2[] = { 10,11,12,13,14 };
	int* parr[3] = { arr0,arr1,arr2 };//arr本身就有地址，直接传入
	for (i = 0; i < 3; i++)
	{
		for (j = 0; j < 5; j++)
		{
			printf("%d ", *(*(parr + i)+j));
			// printf("%d ", parr[i][j]);
		}
		printf("\n");
	}
	return 0;
 }

```
比较在意的是打印的函数：第一次解引用是找到数组中的哪一个数组，第二次解引用是找到数组中哪一个数组

---
# 2.数组指针：
## 2.1.数组指针的定义：
希望不要和上面的搞混淆，我们这是数组指针，我们也可以联想，我们来看整形指针，字符指针，而这样就可以理解数组指针：那么他的表示方式会是什么呢？

| 整形指针       | 字符指针            | 浮点指针         | 数组指针               |
| ---------- | --------------- | ------------ | ------------------ |
| ``int* p`` | ``char* p``<br> | ``float* p`` | ``int (* p) [10]`` |
\*号先和p结合，表示为指针变量，后面的int [10]表示为数组修饰指针，这样就是数组指针了，这样的变量我们来装取数组的地址;
## 2.2. 数组指针的初始化：
我们先看一段代码：
```C
#include<stdio.h>

int main()
{
	int arr[10] = { 10 };
	int(*p)[10] = &arr;
}


```
这样我们就把数组的地址放在数组指针中了
## 2.3数组指针的作用：（二维传参）
我们看一段代码：*我们一开始给二维数组传入函数
```C
#define  _CRT_SECURE_NO_WARNINGS
#include<stdio.h>

void test(int a[3][5], int r, int c)
{
	int i = 0; 
	int j = 0;
	for (i = 0; i < 3; i++)
	{
		for (j = 0; j < 5; j++)
		{
			printf("%d ", a[i][j]);
		}
		printf("\n");
	}
}

int main()
{
	int arr[3][5] = { {0,1,2,3,4},
		{1,2,3,4,5},
		{2,3,4,5,6}};
	test(arr,3,5);
	return 0;

}

```
这里我们可以看到我们把数组传给函数后，利用两个for循环来完成打印，我们也可以考虑用数组指针来完成打印，这里我们就必须来思考二维数组的本质：
二维数组可以做数组元素为一维数组，那么我们每一行的一维数组可以拿数组指针来接受。
我们来看一段代码：
```C
#define  _CRT_SECURE_NO_WARNINGS
#include<stdio.h>

void test(int (*p)[5], int r, int c)//用数组指针来接收二维数组中的元素
{
	int i = 0;
	int j = 0;
	for (i = 0; i < r; i++)
	{
		for (j = 0; j < c; j++)
		{
			printf("%d ", *(*(p + i) + j));
		}
		printf("\n");
	}
}

int main()
{
	int arr[3][5] = { {0,1,2,3,4},
		{1,2,3,4,5},
		{2,3,4,5,6}};
	test(arr,3,5);
	return 0;

}

```
二维数组`int arr[3][5]`本质上是连续存储的 ​**3个一维数组**，每个一维数组有 ​**5个元素**。函数参数需要明确数组的列数（即每个一维数组的长度），以便编译器计算行偏移。
`int (*p)[5]`：表示`p`是一个指向 ​**包含5个`int`元素的一维数组** 的指针。这与二维数组`arr`的列数一致，因此能正确接收二维数组的首行地址。
`p + i`：跳过`i`个 ​**长度为5的一维数组**​（即`i`行），指向第`i`行的首地址。
我们给了arr传入给函数，函数通过指针数组来接受，此时的p+1就是跳过一整行，我们再来看解引用的作用：
- `*(p + i)`：对`p + i`解引用后，得到的是第`i`行的一维数组名（即`arr[i]`），类型为`int*`，表示该行的首元素地址。
- `*(p + i) + j`：在第`i`行的基础上偏移`j`个`int`元素，指向`arr[i][j]`的地址
- `*(*(p + i) + j)`等价于`arr[i][j]`，通过两次解引用访问具体的元素值
这样依次解引用我们就打印了二维数组。
```C
int (*p) [10] = &arr;
 |    |    |
 |    |   P指向数组元素的大小
 |   p是数组指针的变量名
 p指向的数组的元素类型
```
- `arr`作为二维数组名，传递给`test`函数时会被转换为`int (*)[5]`类型，与形参`int (*p)[5]`完全匹配。
- ​若将形参改为`int (*p)[3]`，则`p + 1`会跳过3个`int`元素（而非实际需要的5个），导致后续访问越界或数据错乱。
## 2.4.通过两个问题理解二维数组的本质：
### 问题1：为什么要`p+1`会跳过一整行？（当声明一个数组指针 `int (*p)[5]` 时）：
- 二维数组在内存中是以 ​**行优先顺序连续存储** 的，即先存储第一行的所有元素，再存储第二行，依此类推例如，对于 `int arr[3][5]`，其内存布局如下：
```C
arr[0][0], arr[0][1], ..., arr[0][4], 
arr[1][0], arr[1][1], ..., arr[1][4],
arr[2][0], arr[2][1], ..., arr[2][4]
```
- `p` 的类型是 ​**指向包含5个`int`元素的一维数组的指针**。
- 指针的步长由类型决定。每次 `p+1` 操作会使指针 ​**跳过一整行**​（即 `5 * sizeof(int)` 字节），指向下一行的首地址,这一点我们在[[C语言 5：指针及其拓展(1)#5.数组名的本质]]中就讲过
```C
int arr[3][5] = { {0,1,2,3,4}, {5,6,7,8,9}, {10,11,12,13,14} };
int (*p)[5] = arr; // p指向第一行的首地址

printf("p的地址：%p\n", p);   // 输出第0行首地址，如0x1000
printf("p+1的地址：%p\n", p+1); // 输出第1行首地址，如0x1014（假设int为4字节）
```
- 看看与普通指针的对比而数组指针 `int (*p)[5]` 的加减法单位是 ​**行**，而非单个元素，而普通数组就是加减单个元素
```C
int *p = &arr[0][0];
p + 1; // 仅跳过一个int元素（如从arr[0][0]到arr[0][1]）
```
### 问题二：为什么要两次解引用？
- 二维数组名（如 `int a[3][5]`）本质上是一个 ​**行指针**，其类型为 `int (*)[5]`，即指向包含5个`int`元素的一维数组的指针，这里也就可以解释我们为什么要用要用数组指针来接受
- ​**第一层解引用**：`*(a + i)` 表示跳过 `i` 行（每行5个元素），获取第 `i` 行首地址（类型降为列指针 `int*`
- ​**第二层解引用**：`*(*(a + i) + j)` 在第 `i` 行的基础上，跳过 `j` 个`int`元素，最终访问到 `a[i][j]` 的值。
- **行指针**：二维数组名（如 `int arr[3][5]`）本质上是一个 ​**行指针**，其类型为 `int (*)[5]`，表示它指向一个包含5个整型元素的一维数组
- ​**列指针**：对行指针进行解引用操作（如 `*arr`）后，得到的指针类型降级为 `int*`，即 ​**列指针**，指向该行第一个元素的地址
希望还是不要和上面的指针数组搞混淆了，两个实在是太像了，笔者自己在学习时也经常搞混淆
---

# 3. 函数指针数组
## 3.1.函数指针数组的初试
我们在学习指针数组的时候已经知道指针（地址）也是可以放在数组里面的，那么把函数的指针放在数组中，就可以叫函数指针数组，格式如下：
```
返回值类型 (*数组名[数组大小])(参数列表);
```
```C
int (*funcArray[3])(int, int) = {add, subtract, multiply};  // 静态初始化[1,4](@ref)
```
这样就时函数指针数组；与指针数组比较就是多了函数的使用值，本质还是一个数组，将地址给进了数组。
## 3.2.函数指针数组的作用：（连接表）
我们先看我们没学过之前的函数指针数组通过一段简单的代码来实现计算器，代码如下：
```C
#define  _CRT_SECURE_NO_WARNINGS
#include<stdio.h>


void menu()
{
	printf("*****************************\n");
	printf("**********1. add ************\n");
	printf("**********2. sub ************\n");
	printf("**********3. mul ************\n");
	printf("**********4. div ************\n");
	printf("**********0. exit ***********\n");
	printf("*****************************\n");
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
	int input = 1;
	int i = 0;
	int j = 0;
	do
	{
		menu();
		scanf("%d",&input);
		switch (input)
		{
		case 1:
			printf("请输入两个操作数：");
			scanf("%d %d", &i,&j);
			add(i, j);
			break;
		case 2:
			printf("请输入两个操作数：");
			scanf("%d %d", &i,&j);
			sub(i, j);
			break;
		case 3:
			printf("请输入两个操作数：");
			scanf("%d %d", &i,&j);
			mul(i, j);
			break;
		case 4:
			printf("请输入两个操作数：");
			scanf("%d %d", &i,&j);
			div(i, j);
			break;
		case 0:
			printf("已经退出");
			break;
		default:
			printf("输入错误，请输入其他值");
			break;
		}
	} while (input);
	return 0;

}
```
上述代码用于生成一个简单的计算器但是还有很多的问题我们可以进行改造，代码如下：
```C
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>

void menu() {
    printf("*****************************\n");
	printf("**********1. add ************\n");
   	printf("**********2. sub ************\n");
    printf("**********3. mul ************\n");
    printf("**********4. div ************\n");
    printf("**********0. exit ***********\n");
   	printf("*****************************\n");
}

void add(int x, int y) {
    printf("执行的结果为 %d\n", x + y);
}

void sub(int x, int y) {
    printf("执行的结果为 %d\n", x - y);
}

void mul(int x, int y) {
    printf("执行的结果为 %d\n", x * y);
}

void div(int x, int y) {
    if (y == 0) {
        printf("除数不能为零\n");
        return;
    }
    printf("执行的结果为 %d\n", x / y);
}

int main() {
    int input = 1;
    int i = 0, j = 0;

    do {
        menu();
        scanf("%d", &input);

        if (input >= 1 && input <= 4) {
            printf("请输入两个操作数：");
            scanf("%d%d", &i, &j);
        }

        switch (input) {
        case 1: add(i, j); break;
        case 2: sub(i, j); break;
        case 3: mul(i, j); break;
        case 4: div(i, j); break;
        case 0: printf("已经退出\n"); break;
        default:
            printf("输入错误，请输入其他值\n");
            while (getchar() != '\n'); // 清空缓冲区
        }
    } while (input != 0);

    return 0;
```
这段代码优化了缓冲区的问题，并且对除数做了优化。
我们也可以尝试使用函数指针数组来进行优化，比如以下代码：
```C
void menu()
{
	printf("*****************************\n");
	printf("**********1. add ************\n");
	printf("**********2. sub ************\n");
	printf("**********3. mul ************\n");
	printf("**********4. div ************\n");
	printf("**********0. exit ***********\n");
	printf("*****************************\n");
	printf("********************** ******\n输入你的选项 --------->");
	
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
这里就可以看到我们通过连接表来优化了大段的冗余代码，但是我们发现在这段代码在vs2022上的运行速度并不快，这是因为：菜单函数 `menu()` 包含 ​**9次printf调用**，每次输出都会触发系统级I/O操作。合并为单次输出可显著提升效率：
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
---


