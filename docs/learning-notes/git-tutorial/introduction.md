# 1. 初步认识git：
git简单来说就是一个仓库版本管理器。当我们写文档的时候，很有可能需要不同版本类型的文档，或者代码，这个就需要git来进行回退和撤销。

我们先来看看，Linux下的git吧，输入：`git --version`：
出现这种情况，就说明安装了git，如果没有安装则需要安装git
![image.png](https://cdn.jsdelivr.net/gh/Ningbottle/blog-images@main/img/20260330170422518.png)
对于不同的版本的系统：
1. 对于Ubuntu系统，我们一般使用：`sudo apt install git -y`安装git
2. 对于Centos系统，我们一般使用：`sudo yum install git -y`

安装之后，我们可以通过`git --version`是否安装好了。如果出现上图，则表明没有出现问题。

# 2.设立一个本地仓库：
接下来，我们开始设立一个本地仓库。先创建一个

