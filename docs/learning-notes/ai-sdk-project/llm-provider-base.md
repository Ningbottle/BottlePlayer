# 1. 基类需要的成员：

要想先实现什么是LLMProvider，我们需要的基类需要封装什么。我们知道我们给大模型的提供商发消息，我们需要

1. apiKey ： `std::string _ApiKey; // Api key 用于模型通话`
2. 还需要服务器的URL ： `*std::string _Endpoint; // URL of 模型通讯连接地址*`
3. 为了便于检查初始化是否完成，默认`false`：`bool _IsAvailable = false;`

## 1.1 API Key的获取：

我们简单介绍一下什么是API Key吧？以我们熟悉的DeepSeek官网为例，我们打开DeepSeek官网，发现有：
![image.png](https://cdn.jsdelivr.net/gh/Ningbottle/blog-images@main/img/20260511150742393.png)


我们打开api 开放平台，自己创建属于自己的APIkey，这个APIkey是属于保密的（需要自己保管好，是我们和大模型服务商对话的密钥）。
![image.png](https://cdn.jsdelivr.net/gh/Ningbottle/blog-images@main/img/20260511150834814.png)

点击这里的创建自己的APIKey，需要复制好。这里我们一般帮助我填入环境变量。注意保证隐私。

## 1.2 EndPoint的获取

那么第二个参数应该是什么呢，endpoint指的是接口，我们可以从DeepSeek（这里以DeepSeek官网为例）的接口文档中获取：
![](https://cdn.jsdelivr.net/gh/Ningbottle/blog-images@main/img/20260511150834814.png)
打开之后，就可以发现两种URL：前一种是Open AI的，后面则是最近很火的Anthropic的URL。这是为了兼容这两家的工具。2.
![image.png](https://cdn.jsdelivr.net/gh/Ningbottle/blog-images@main/img/20260511150707354.png)

# 2. 补充的知识：

## 2.1一个完整的请求大概包含什么：

1. 请求行：`POST <https://api.openai.com/v1/chat/completions`>
    
2. 请求头：告诉服务器发送的是Json，第二个告诉服务器密钥，进行身份认证
    
    `Content-Type: application/json Authorization: Bearer sk-xxxxxxxxxxxxxxxx`
    
3. 请求体：
    
    ```jsx
    {
      "model": "gpt-4",
      "messages": [
        {
          "role": "user",
          "content": "你好，请介绍一下自己"
        }
      ],
      "temperature": 0.7
    }
    
    ```
    

这里面的关键词我们后面来详细讲讲。

## 2.2 什么是Json格式：

Json是一种轻量级的数据结构，便于人类进行阅读，同时也利于机器的解析和生成。

1. 对象：通过`{}` 来包裹，也是键值对组成
2. 数组：通过`[]` 来包裹，是有序的值的集合

通常两者一起结合，构成大模型的请求体，或者网络传送的载体。

```json
{
  "model": "gpt-4",
  "messages": [
    {
      "role": "user",
      "content": "你好，请介绍一下自己"
    }
  ],
  "temperature": 0.7
}
```

## 2.3 为什么都使用Json

- **通用性强**：几乎所有编程语言都支持解析 JSON
- **简洁易读**：比 XML 更轻量
- **网络传输标准**：HTTP API（包括你正在学的 LLM API）几乎都用 JSON 传递数据

# 3. 需要的接口函数：

```cpp
virtual ~LLMProvider() = default;
// 1. 初始化模型提供者，使用配置参数
virtual bool Init(const std::map<std::string, std::string>& config) = 0;
// 2. 是否初始化完成
virtual bool IsAvailable() const = 0;
// 3. 获取模型名称
virtual std::string GetModeName() const = 0;
// 4. 发送消息，非流式，就是非增量 第一个参数是消息，第二个参数是温度，最大tokens，之类的
virtual std::string SendMessage(std::vector<Message> messages,
                    std::map<std::string, std::string> requestParam) = 0;
// 5. 发送消息，流式: 第一个参数是消息，第二个参数是温度，最大tokens，之类的，第三个参数是回调函数
// 回到函数里面是第一个是增量，第二个是否还有增量
virtual std::string SendMessageStream(std::vector<Message> messages,
                            std::map<std::string, std::string> requestParam,
                            std::function<void(std::string, bool)>) = 0;
virtual std::string ModelDesc() const = 0;
```

1. 析构函数必须是虚函数。原因：当子类析构析构的时候会自动调用父类进行析构，如果析构不是虚函数，会导致内存泄漏，父类存贮的堆上的变量无法释放。
2. 初始化是否完成和获取模型的名字，还是比较简单的。
3. 剩下的就是非增量的发送消息，就是发送消息的时候是一次性吐过来了
4. 还一个是增量的发送消息。

上述的几个函数都是纯虚函数，要求子类必须实现。

# 4. 关键知识介绍：

## 4.1 大模型LLM 是什么？

**大模型（LLM，Large Language Model）** 是一种基于深度学习训练的人工智能模型，通过在海量文本数据上进行预训练，学会理解和生成自然语言。它的核心能力是根据输入的上下文（prompt）预测并输出合理的文字续写，从而实现对话、问答、代码生成、文本摘要等各类任务。我们熟知的 GPT、DeepSeek、Claude 都属于大模型。

## 4.2 LLM 的发展历史

大模型的发展可以粗略分为以下几个阶段：

1. **早期语言模型（2000年代）**：最早的语言模型基于统计方法（如 n-gram），只能做简单的词频预测，能力十分有限。
2. **词向量与神经网络（2013年前后）**：Word2Vec 的提出让机器开始用向量表示词语的语义，神经网络开始被引入 NLP 领域。
3. **Transformer 架构诞生（2017年）**：Google 发表论文《Attention is All You Need》，提出了 Transformer 架构，彻底改变了 NLP 的发展方向，成为现代大模型的基石。
4. **预训练模型爆发（2018–2020年）**：BERT（Google）、GPT-2（OpenAI）相继出现，证明了「大规模预训练 + 微调」的路线可以在几乎所有语言任务上取得顶尖效果。
5. **大模型时代（2020年至今）**：GPT-3（1750亿参数）横空出世，展现出惊人的「涌现能力」；随后 ChatGPT、GPT-4、Claude、Gemini、DeepSeek 等一系列模型不断刷新边界，大模型正式进入大众视野。

## 4.3 LLM 与大众认为的「人工智能」有什么区别？

很多人对「人工智能」的印象来自科幻电影——有自我意识、能独立思考、甚至有情感的机器人。但 LLM 与此有本质区别：

|对比维度|大众认知的「AI」|LLM 实际是什么|
|---|---|---|
|是否有意识|有自我意识、情感|没有，只是统计模式的匹配与预测|
|工作原理|像人一样「理解」和「思考」|根据上下文预测下一个最可能的词|
|知识来源|主动学习、持续更新|固定的训练数据，有知识截止日期|
|能力边界|万能、无所不能|擅长语言任务，但会「幻觉」（胡说）|
|目标|模拟人类智能|完成语言生成任务，服务于具体场景|

简单说：**LLM 是一个极其强大的「语言预测器」，而不是科幻电影里那种有意识的智能体。** 它的强大来自于海量数据的训练，而非真正的「理解」。

## 4.4 temperature 和 Top-K 是什么意思？

### 4.4.1 temperature介绍:

我们的大模型和人们认为的数值还是有很大的差别的。在我们使用DeepSeek的时候，我们可以设置他的温度值。就是在Json的段落中加上temperature的字段。

一般对于确定的事件我们尽量把温度给调低，对于另一个方面对于需要创造力的工作，我会适当把温度给提高。具体的机理我们不再这里进行讲解。

用官方话语来说就是：In large language models,**temperature**is a hyperparameter that controls randomness in text generation.即temperature（温度）是控制大模型生成文本随机性的超参数。

可以把它想象成对下一个词的概率分布进行“软化”：高温度让罕见词出现的概率增大，低温度则让最可能的词更突出。

### 4.4.2 Top-K介绍

Top‑K（前K采样）是一种控制生成随机性的策略，只考虑概率最高的 K 个下一个 token（词/子词）。

- Example: If K=50, the model selects the next token only from the top 50 candidates by probability, ignoring the rest. 示例：K=50 时，模型只会从概率前 50 的候选词中选取下一个词，其余的候选词被忽略。
- Purpose: Reduces the chance of extremely unlikely words while maintaining some diversity. 目的：降低生成极不可能词的风险，同时保留一定的多样性。

### 4.4.3 两者进行对比:
![image.png](https://cdn.jsdelivr.net/gh/Ningbottle/blog-images@main/img/20260511150928388.png)

对于我们来说，一般调整温度就可以了，这里只是短暂的介绍一下，这两个关系