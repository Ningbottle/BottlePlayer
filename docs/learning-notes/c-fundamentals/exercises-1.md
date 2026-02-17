---
time: 2025-06-11
tags:
  - C语言
---
# 题目一：不允许创建临时变量，交换两个整数的内容
## 法一：
```c
#define  _CRT_SECURE_NO_WARNINGS
#include<stdio.h>

// 不允许创建临时变量，交换两个整数的内容
int main()
{
	int a = 1;
	int b = 2;
	a = a + b;
	b = a - b;
	a = a - b;
	printf("%d %d", a, b);
}
```
通过将两个数的和传递给a，再减去b的值赋给b，此时b更新了数值为a；变量a再减去变量b得到新的值传递给a，就完成了交换；
## 法二：
```c
int main()
{
	int a = 1;
	int b = 4;
	scanf("%d %d", &a, &b);
	a = a ^ b;
	b = a ^ b;//两次异或得到将a的值赋值给了b
	a = a ^ b;
	printf("%d %d", a, b);
}
```
与上面的方法一致也是两次更新完成交换。
# 题目二：输入一个整数 n ，输出该数32位二进制表示中1的个数。其中负数用补码表示。
数据范围：−231<=n<=231−1−231<=n<=231−1
即范围为:−2147483648<=n<=2147483647−2147483648<=n<=2147483647
在我之前的文章中[[C语言 4：操作符及其应用]]已经提到过三种方法：
在这里我们，代码如下：
```c
int NumberOf1(int n ) {

    // write code here

    unsigned int un = (unsigned int)n;

    int count = 0;

    int j = 0;

    for(int i = 0;i < 32;i++)

    {

        j = n ^ 1;

        if(j == n -1)

            count++;

        n = n >> 1;

    }

    return count;

}
```
先将他强制转换为无符号整数，这样才能进行移位计算，移位时不会乱，依次检查是否为1.是1就强制加1.
但是有错误：### 🔍 问题核心：条件 `j == un - 1` 的局限性

1. ​**巧合正确性**​：
    - 当最低位为 ​**1**​ 时：  
        `un ^ 1 = un - 1` 成立（例如 `un=3`（二进制 `11`）: `3^1=2`, `3-1=2` → 计数 ✅）。
    - 当最低位为 ​**0**​ 时：  
        `un ^ 1 = un + 1`（例如 `un=4`（二进制 `100`）: `4^1=5`, `4-1=3` → `5≠3` → 不计数 ✅）。  
        ​**表面看似乎正确，但问题在于高位干扰**。
2. ​**高位干扰导致误判**​：
    - 右移后高位补0，但 `un` 的原始值被破坏：  
        以 `un=6`（二进制 `110`）为例：
        - ​**第一次迭代**​（`un=6`）:  
            最低位为 `0` → `6^1=7`, `6-1=5` → `7≠5` → 不计数 ✅。
        - ​**第二次迭代**​（`un=3` 右移后）:  
            此时 `un=3`（二进制 `11`），最低位为 `1` → `3^1=2`, `3-1=2` → `2=2` → 计数 ✅。
        - ​**第三次迭代**​（`un=1` 右移后）:  
            此时 `un=1`（二进制 `1`），最低位为 `1` → `1^1=0`, `1-1=0` → `0=0` → 计数 
            ❌（错误！实际只有一个1）。  
            ​**结果返回 `count=2`（正确），但第三次迭代错误计数**。
    
    > 原因：右移后 `un=1` 是**移位后的新值**，而非原始数据的有效位。你的条件误判了已处理过的位。
```c
     int get_one_of_n(unsigned int m )  
 {  
     int count = 0;  
     while (m)     
     {     
         count++;//进来就加一  
         m = m & (m - 1);//比如0000，0011和00000010来按位与则会将末尾变成0000，0010，此时m>0循环继续，继续按位与；  
         //与之前的移位操作符号来比，不需要循环那么多次  
     }  
     return count;  
 }  
 int main()  
 {  
     int n = 0;  
     scanf("%d", &n);  
     int ret = get_one_of_n(n);  
     printf("%d", ret);  
 }
```
```
# 题目三：获取一个整数二进制序列中所有的偶数位和奇数位，分别打印出二进制序列
我的代码如下：
```c
void print_odd(int n)
{
	unsigned un = (unsigned int)n;
	for (int i = 0; i < 16; i++)
	{
		printf("%d ", un & 1);
		un = un >> 2;
	}
	printf("奇数列结束\n");
}
void print_even(int n)
{
	unsigned un = (unsigned int)n;
	un = un >> 1;
	for (int i = 0; i < 16; i++)
	{	
		printf("%d ", un & 1);
		un = un >> 2;
	}
	printf("偶数列结束\n");
}


int main()
{
	int n = 0;
	scanf("%d", &n);
	print_odd(n);
	print_even(n);
	return 0; 
}
```
我的代码还是不够好，当前代码从**最低位**开始打印（LSB first），但通常二进制展示应从**最高位**开始（MSB first）
​**示例**​：`n=5` (二进制 `...0101`)：
- 奇数位应为：`0 0 ... 0 1` (从高到低)
- 您的输出：`1 0 1 0 ...` (从低到高)
```c
#include <stdio.h>

// 打印奇数位（从最高位开始）
void print_odd(int n) {
    unsigned un = (unsigned)n;
    printf("奇数位：");
    // 从30位开始（最高奇数位），步长-2
    for (int i = 30; i >= 0; i -= 2) {
        printf("%d ", (un >> i) & 1);  // 直接移位检测特定位[1,3](@ref)
    }
    printf("\n");
}

// 打印偶数位（从最高位开始）
void print_even(int n) {
    unsigned un = (unsigned)n;
    printf("偶数位：");
    // 从31位开始（最高偶数位），步长-2
    for (int i = 31; i >= 0; i -= 2) {
        printf("%d ", (un >> i) & 1);  // 标准位检测法[2,4](@ref)
    }
    printf("\n");
}

int main() {
    int n = 0;
    scanf("%d", &n);
    print_odd(n);
    print_even(n);
    return 0; 
}
```
# 题目四：编程实现：两个int（32位）整数m和n的二进制表达中，有多少个位(bit)不同？ 

```
#include <stdio.h>

  

int main() {

    int a, b;

    while (scanf("%d %d", &a, &b) != EOF) { // 注意 while 处理多个 case

        // 64 位输出请用 printf("%lld") to

        int c = a ^ b;

        int count = 0;

        unsigned int uc = (unsigned int) c;

        for(int i = 0;i < 32;i++)

        {

            if(uc>> i & 1)

                count++;

        }

        printf("%d",count);

    }

    return 0;

}
```