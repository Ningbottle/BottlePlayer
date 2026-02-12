---
time: 2025-06-18
tags:
  - C语言
---
# 内存函数：
## 1. memcpy函数：
### 1.1memcpy函数的使用
注意``memcpy``函数和``strcpy``函数的区别，与上面的区别就是``memcpy``不仅仅能复制str字符串还能过完成对其他的变量进行复制。
先看函数的声明：
```c
void* memcpy(void* dest, const void* str, size_t num)
```
其中dest是需要复制进去的指针，将str是被复制的地方，num是**字节**（一定要注意是字节）
- **str1** -- 指向用于存储复制内容的目标数组，类型强制转换为 void* 指针。
- **str2** -- 指向要复制的数据源，类型强制转换为 void* 指针。
- **n** -- 要被复制的字节数。
最后的返回值是指向dest的开头地址。
我们尝试进行使用：
```c
int main()
{
	int arr1[6] = { 1,2,3,4,5,6 };
	int arr2[20] = { 0 };
	memcpy(arr2, arr1, 5 * sizeof(arr1[1]));
	for (int i = 0; i < 5; i++)
	{
		printf("%d", arr2[i]);
	}
}
```
这样就可以把arr1数组前五个数字的内容复制给arr2，其中``sizeof``来计算int类型的字节数，再看一段例子：
```c
int main()
{
	const char src[50] = "http://www.runoob.com";
	char dest[50];

	memcpy(dest, src, strlen(src) + 1);
	printf("dest = %s\n", dest);

	return(0);
}
```
这段代码就是将str里面的内容复制进dest里面，为啥strlen后面需要加一个1，可以存放一个\0；来完成字符串的使用。
再来看一段代码：将 s 中第 11 个字符开始的 6个连续字符复制到 d 中:
```c
#include <stdio.h>
#include<string.h>
 
int main()
 
{
  char *s="http://www.runoob.com";
  char d[20];
  memcpy(d, s+11, 6);// 从第 11 个字符(r)开始复制，连续复制 6 个字符(runoob)
  // 或者 memcpy(d, s+11*sizeof(char), 6*sizeof(char));
  d[6]='\0';
  printf("%s", d);
  return 0;
}
```
### 1.2memcpy的模拟：
我们在考虑memcpy函数的使用时，会想到他是一个一个字节来使用的，此时我们可以考虑到使用char来控制指针的的加减大小
```c
void* my_memcpy(void* dest, const void* str, size_t num)
{
	assert(dest && str);//进行断言处理
	char* str1 = (char *)str;
	char* str2 = (char*)dest;//全部转换成为char *类型的字符指针，可以完成字节的控制
	for (int i = 0; i < num; i++)
	{
		*(str2 + i) = *(str1 + i);
	}
	return dest;
}

int main()
{
	int arr1[5] = { 1,2,3,4,5 };
	int arr2[20] = { 0 };
	my_memcpy(arr2, arr1, 20);
	for (int i = 0; i < 5; i++)
	{
		printf("%d",arr2[i]);
	}
}
```
此时就有了mencpy函数的模拟。但是面对地址重合的区域是否可以复制。此时要用memmove
## 2.memmove函数
## 2.1memmove函数的使用
如果地址没有重复，即没有内存块重叠，``memmove``函数时和``memcpy``函数是一样的
如果地址重复则优先考虑使用``memmove``函数。
声明如下
```c
void *memmove(void dest,const void* str,size_t n)
```
- **dest** -- 指向用于存储复制内容的目标数组，类型强制转换为 void* 指针。
- **str** -- 指向要复制的数据源，类型强制转换为 void* 指针。
- **n** -- 要被复制的字节数。
我们先来看一段代码：
```c
int main()
{
	char *str = "woshiwwh";
	char str2[10] = { 0 };
	memmove(str2, str, 9);
	printf("%s", str2);
}
```
这段代码和之前的memcpy函数一样。
我们现在讨论就是地址重复的位置：
```c
int main()
{
	int arr[5] = { 1,2,4,6,7 };
	memmove(arr + 2, arr, 3 * sizeof(arr[0]));
	for (int i = 0; i < 5; i++)
	{
		printf("%d", arr[i]);
	}
}

```
如果使用上次我写的模拟函数最后的结果为：12121；而正确的应该为：12124；
## 2.2memmove函数的模拟：
代码如下：
```c
void* my_memmove(void* dest, const void* str, size_t num)
{
	char* p1 = (char*)dest;
	const char* p2 = (const char*)str;
	if (p1 > p2)
	{
		p1 = p1 + num - 1;
		p2 = p2 + num - 1;
		for (int i = 0; i < num; i++)
		{
			*(p1 - i) = *(p2 - i);
		}
		return dest;
	}
	else {
		for (int i = 0; i < num; i++)
		{
			*(p1 + i) = *(p2 + i);
		}
		return dest;
	}
}


int main()
{
	int arr[5] = { 1,2,7,4,5 };
	my_memmove(arr + 2, arr, 3 * sizeof(arr[0]));
	for (int i = 0; i < 5; i++)
	{
		printf("%d", arr[i]);
	}
}

```
我这里基本完成了memmove函数的实现，通过判断str和str的位置不同来完成是从前到后还是从后到前.