---
time: 2025-04-25
tags:
  - C语言
---
# 1.递归的定义：

本质是函数自己调用自己，如下是一个简单的递归代码：

``` C
 #include<stdio.h>  
 ​  
 int main()  
 {  
     printf("hehe");  
     main(); //在mian函数中自己调用自己  
     return 0;  
 }
```

这个代码会由于没有截至条件，会一直调用下去导致栈溢出（stack overflow,同时[www.stack overflow.com](www.stack%20overflower.com)是一个类似与知乎的问答网站）导致进入死循环；

# 2.用递归求n的阶层

第一可以考虑大事化小的想法，一步一步抽丝化简

第二想到递归的本质要递出去也要收回来，可以暂时考虑成(n-1)*n，后面的(n-1)也可以这么考虑

``` C
#define  _CRT_SECURE_NO_WARNINGS

#include<stdio.h>

int fact(int n)
{
	if (n == 0)
		return 1;
	else		
		return n * fact(n - 1);
}

int main()
{
	int n = 0;
	scanf("%d", &n);
	int ret = 0;
	ret = fact(n);
	printf("n的阶乘是%d", ret);
	return 0;`
}


```
 
 ​

## 2.1.题目一：顺序打印数的每一位数字

比如一个数字123，按照顺序打印下来这个结果为1，2，3；

此时也可以考虑为递归，可以有以下代码：

``` C
 void Print(int n)  
 {  
     if (n < 9)  
         printf("%d ", n);  
     else  
     {  
         Print(n / 10);  
         printf("%d ", n % 10);//注意这里一定要有括号，刚刚调试了好久才发现要搞这个  
     }  
 }  
 ​  
 int main()  
 {  
     int n = 0;  
     scanf("%d", &n);  
     Print(n);  
 ​  
 }  
```
 ​  
 ​

修正后的递归流程以 `n=1234` 为例：

1. `Print(1234)` → `1234 >= 9` → 递归调用 `Print(123)`。
    
2. `Print(123)` → `123 >= 9` → 递归调用 `Print(12)`。
    
3. `Print(12)` → `12 >= 9` → 递归调用 `Print(1)`。
    
4. `Print(1)` → `1 < 9` → 打印 `1`，返回上层。
    
5. 逐层返回并打印余数：`12%10=2` → `123%10=3` → `1234%10=4`，最终输出 `1 2 3`
    

## 2.2.题目二：斐波拉契数列问题（递归和迭代）

斐波拉契数列为1，1，2，3，5，8，13，21，34，55......，其主要时前两项之和得到第三项;

通过递归可以将第n项改成(n-1)+(n-2)相加，同时n-1和n-2可以继续往后传递，当遇到第一位和第二位时候可以往前返回；

``` C
 int fib(int n)  
 {  
     if (n <= 2)  
         return 1;  
     else  
         return fib(n - 1) + fib(n - 2);  
 }  
 ​  
 int main()  
 {  
     int n = 0;  
     scanf("%d", &n);  
     int ret = fib(n);  
     printf("%d", ret);  
 }  
 ​  
```
 ​

但是此处递归明显有问题，并不是栈溢出，而是多次重复：

$$  
fib(5)= fib(4)+fib(3); fib(4) = fib(3) +fib(2); fib(3) = fib(2)+fib(1); fib(2)=1;  
$$

  

``` C
 int fib(int n)  
 {     
       
     if (n == 3)  
         count++;//计算3号斐波拉契数列的计算次数  
     if (n <= 2)  
         return 1;  
     else  
         return fib(n - 1) + fib(n - 2);  
 }  
 ​  
 int main()  
 {  
     int n = 0;  
     scanf("%d", &n);  
     int ret = fib(n);  
     printf("%d\n", ret);  
     printf("%d", count);  
 }  
 ​  
 //  
```
 ​  
 ​

运行时可以发现此时计算过度重复；此时可以看到多处重复：可以尝试使用迭代来实现该问题：

此时我们可以考虑使用迭代来写代码：既然从后面往前比较麻烦，那么可以改变从前面往后走：

``` C
 int main()  
 {  
     int ret = 1;  
     int n = 0;  
     scanf("%d", &n);  
     int a = 1;  
     int b = 0;  
     while (n)  
         {     
             ret = a + b;  
             a = b;  
             b = ret;  
             n--;  
         }  
     printf("%d", ret);  
 }
```

通过水杯一样的传递，将数字传到后面得到结果；注意由于int为32位所以结果过大可能溢出；

==本质递归和迭代还是些许不同，迭代更讲究数据之间的变化==