# 1. 前言：让 AI 的记忆成为永恒，DataManager 的诞生
在前面七篇文章中，我们建立起了强大的大模型通信矩阵（DeepSeek、GLM、Ollama），并用 `LLMManager` 进行了统一路由。但现在有一个致命的问题：所有的聊天记录（`std::vector<Message>`）都只存活在内存里。只要程序一关，你和 AI 彻夜长谈的心血就会瞬间灰飞烟灭。
为了让 AI 拥有“长期记忆”，我们必须引入**数据持久化（Data Persistence）**。在 C++ 桌面端开发中，最轻量、最强悍的选择莫过于 [SQLite3](https://www.sqlite.org/)。今天，我们将手撸一个 `DataManager` 数据管家，不仅要带你彻底打通 SQLite 的底层操作，还要教你如何防范多线程开发中最可怕的**死锁（Deadlock）**和**SQL注入（SQL Injection）**。
![互斥锁](file:///C:/Users/w1521/.gemini/antigravity/brain/8b0fcc1f-c673-43ec-b7cc-9311a41c4355/data_01_mutex_1782287592804.png)
# 2. 头文件设计：线程安全与内部通道
打开 `DataManager.h`，你会发现这里多了一把沉甸甸的锁（`std::mutex`），而且在私有成员里，出现了一批带有 `NoLock` 后缀的神秘函数。
## 2.1 类的核心机理
- **底层工具**：我们使用了 `sqlite3*` 这个纯 C 语言的指针作为数据库的操作句柄。
- **线程安全机制**：AI 聊天往往是**并发（Concurrent）**的。比如你在和大模型流式对话的同时，后台可能正在拉取历史记录。如果两个线程同时对同一个文件（数据库）进行读写，必然会导致崩溃或数据损坏。因此，我们引入了 `std::mutex _mutex` 进行**独占访问**保护。
```cpp
// include/DataManager.h
#pragma once
#include "common.h"
#include <cstddef>
#include <sqlite3.h>
#include <vector>
#include <string>
#include <mutex>
#include <memory>

namespace AI_Chat_SDK
{
    class DataManager
    {
    private:
        sqlite3* _db = nullptr;     // 数据库指针句柄
        std::string _dbName;        // 数据库名字（文件路径）
        mutable std::mutex _mutex;  // 互斥锁（mutable 保证 const 函数也能上锁）
    private:
        bool InitDB();
        bool executeSQL(const std::string& sql);
        // 专门为防止死锁设计的内部免锁（NoLock）调用通道
        std::shared_ptr<Session> GetSessionByIdNoLock(const std::string& sessionId);
        bool updateSessionStateNoLock(const std::shared_ptr<Session>& session, std::time_t TimeStamp);
        std::vector<Message> GetMessagesBySessionIdNolock(const std::string& sessionId);

    public:
        DataManager(const std::string& dbName);
        ~DataManager();
        // 会话（Session）相关接口
        std::vector<std::string> GetAllSessionId();
        std::shared_ptr<Session> GetSessionById(const std::string& sessionId);
        bool insertSession(const std::shared_ptr<Session>& session);
        bool updateSessionState(const std::shared_ptr<Session>& session, std::time_t TimeStamp);
        bool deleteSession(const std::string& sessionId);
        std::size_t GetSessionCount();
        std::vector<std::shared_ptr<Session>> GetAllSessions();
        bool clearAllSessions();
        // 消息（Message）相关接口
        bool insertMessage(const std::string& sessionId, const Message& message, std::time_t timestamp); 
        std::vector<Message> GetMessagesBySessionId(const std::string& sessionId); 
        bool deleteSessionMessage(const std::string& sessionId); 
    };
}
```
# 3. 基础信息：数据库初始化与外键级联
## 3.1 函数介绍与机理
- **怎么来的**：在主程序启动时，通过实例化 `DataManager("chat.db")` 触发构造函数。
- **底层的设计机理**：我们在 `InitDB` 里使用原生的 `sqlite3_exec` 一次性建表。最精彩的机理在于建表语句中的**外键约束（Foreign Key）**。消息表（`messages`）是依附于会话表（`sessions`）的，如果你删除了一个会话，里面的消息怎么办？通过声明 `ON DELETE CASCADE`，SQLite 引擎会在底层帮你自动触发**级联删除**，瞬间清理掉所有挂钩的历史消息，干脆利落。
![级联删除](file:///C:/Users/w1521/.gemini/antigravity/brain/8b0fcc1f-c673-43ec-b7cc-9311a41c4355/data_02_foreign_key_1782287607802.png)
```cpp
#include "../include/DataManager.h"
#include "../include/util/myLog.h"
#include <cstdint>
#include <mutex>

namespace AI_Chat_SDK {
    // 第一步：构造函数中打开数据库句柄
    DataManager::DataManager(const std::string& dbName)
        :_db(nullptr)
        ,_dbName(dbName)
    {
        int ret = sqlite3_open(_dbName.c_str(), &_db);
        if (ret != SQLITE_OK) {
            ERROR("Failed to open database: {}", sqlite3_errmsg(_db));
            sqlite3_close(_db);
            _db = nullptr;
            throw std::runtime_error("Failed to open database");
        }
        INFO("open success");
        if(!InitDB())
        {
            ERROR("Failed to init database");
            sqlite3_close(_db);
            _db = nullptr;
            throw std::runtime_error("Failed to init database");
        }
        INFO("init success");
    }

    DataManager::~DataManager()
    {
        if(_db)
        {
            sqlite3_close(_db);
            _db = nullptr;
        }
    }

    // 第二步：封装最基础的无返回语句执行器
    bool DataManager::executeSQL(const std::string& sql)
    {
        if(!_db) return false;
        char* error = nullptr;
        int ret = sqlite3_exec(_db, sql.c_str(), nullptr, nullptr, &error);
        if(ret != SQLITE_OK)
        {
            ERROR("Failed to execute SQL: {}", error);
            sqlite3_free(error);
            return false;
        }
        return true;
    }

    // 第三步：建表并建立强大的级联外键
    bool DataManager::InitDB()
    {
        std::string createSessionTable = R"(
            CREATE TABLE IF NOT EXISTS sessions (
                session_id TEXT PRIMARY KEY,
                model_name TEXT NOT NULL,
                created_time INTEGER NOT NULL,
                updated_time INTEGER NOT NULL
            );
        )";
        if(!executeSQL(createSessionTable)) return false;
        
        std::string createMessageTable = R"(
            CREATE TABLE IF NOT EXISTS messages (
                message_id TEXT PRIMARY KEY,
                session_id TEXT NOT NULL,
                role TEXT NOT NULL,
                content TEXT NOT NULL,
                timestamp INTEGER NOT NULL,
                FOREIGN KEY (session_id) REFERENCES sessions(session_id) ON DELETE CASCADE
            );
        )";
        if(!executeSQL(createMessageTable)) return false;
        return true;
    }
```
# 4. 攻克硬核难点：预编译机制与防死锁设计
在业务增删改查时，最忌讳的就是直接把变量拼接到 SQL 字符串里执行，这会导致毁灭性的 **SQL注入攻击**。
## 4.1 预编译防注入机理（Prepared Statement）
SQLite 提供的防注入大招叫**预编译（Prepared Statement）**。就像压铸零件一样：
- 第一步，你提供一个带有问号 `?` 占位符的模具，用 `sqlite3_prepare_v2` 让数据库编译它。
- 第二步，你用 `sqlite3_bind` 把具体变量像倒铁水一样倒进问号里。哪怕变量里含有恶意代码（比如 `' OR 1=1;`），数据库也只会把它当成普通的字符串数据，绝对不会当作代码执行。
![预编译绑定](file:///C:/Users/w1521/.gemini/antigravity/brain/8b0fcc1f-c673-43ec-b7cc-9311a41c4355/data_03_prepare_1782287620019.png)
## 4.2 死锁（Deadlock）的机理与 NoLock 解法
看下面代码中的 `insertMessage` 函数，由于它是一个写操作，进来第一件事就是 `std::lock_guard<std::mutex> lock(_mutex);` 把大门锁死。
但是，插入一条消息后，我们需要顺带去更新会话表的时间戳。如果你直接调用对外的 `updateSessionState`，那个函数一进去又要申请一把锁。此时你手里拿着锁没放，又去跟系统要同一把锁，这就形成了死结，程序瞬间**死锁卡死**！
这就是为什么我们设计了以 `NoLock` 结尾的内部函数通道：**外层大函数负责拿钥匙开大门，进门之后，内部函数的相互调用必须全部走 `NoLock` 免锁通道！**
![免锁机制](file:///C:/Users/w1521/.gemini/antigravity/brain/8b0fcc1f-c673-43ec-b7cc-9311a41c4355/data_04_nolock_1782287634284.png)
```cpp
    // --------------------------- 内部免锁核心通道（NoLock）---------------------------
    std::shared_ptr<Session> DataManager::GetSessionByIdNoLock(const std::string& sessionId)
    {
        std::string sql = R"( SELECT * FROM sessions WHERE session_id = ? )";
        sqlite3_stmt* stmt;
        // 1. 编译模具
        int rc = sqlite3_prepare_v2(_db, sql.c_str(), -1, &stmt, nullptr);
        if(rc != SQLITE_OK) return nullptr;
        
        // 2. 绑定变量到问号占位符
        sqlite3_bind_text(stmt, 1, sessionId.c_str(), -1, SQLITE_TRANSIENT);
        
        // 3. 执行单步查询
        if(sqlite3_step(stmt) != SQLITE_ROW)
        {
            sqlite3_finalize(stmt);
            return nullptr;
        }
        
        // 4. 利用底层指针偏移提取数据，并用 reinterpret_cast 强转回字符串
        const unsigned char* ModelNamePtr = sqlite3_column_text(stmt, 1);
        std::string ModelName = ModelNamePtr ? reinterpret_cast<const char*>(ModelNamePtr) : "";
        time_t CreatedAt = sqlite3_column_int64(stmt, 2);
        time_t UpdatedAt = sqlite3_column_int64(stmt, 3);
        std::shared_ptr<Session> session = std::make_shared<Session>(ModelName);
        session->_createAt = CreatedAt;
        session->_updateAt = UpdatedAt;
        session->_messages = GetMessagesBySessionIdNolock(sessionId); // 内部调用继续免锁
        
        sqlite3_finalize(stmt);
        return session;
    }

    bool DataManager::updateSessionStateNoLock(const std::shared_ptr<Session>& session, std::time_t TimeStamp)
    {
        std::string sql = R"( UPDATE sessions SET updated_time = ? WHERE session_id = ? )";
        sqlite3_stmt* stmt;
        int rc = sqlite3_prepare_v2(_db, sql.c_str(), -1, &stmt, nullptr);
        if(rc != SQLITE_OK) return false;
        
        sqlite3_bind_int64(stmt, 1, static_cast<uint64_t>(TimeStamp));
        sqlite3_bind_text(stmt, 2, session->_session_id.c_str(), -1, SQLITE_TRANSIENT);
        rc = sqlite3_step(stmt); // 增删改操作使用 sqlite3_step 即可完成
        if(rc != SQLITE_DONE) return false;
        sqlite3_finalize(stmt); // 释放模具内存
        return true;
    }

    std::vector<Message> DataManager::GetMessagesBySessionIdNolock(const std::string& sessionId)
    {
        std::string sql = R"( SELECT * FROM messages WHERE session_id = ? )";
        sqlite3_stmt* stmt;
        int rc = sqlite3_prepare_v2(_db, sql.c_str(), -1, &stmt, nullptr);
        if(rc != SQLITE_OK) return {};
        
        sqlite3_bind_text(stmt, 1, sessionId.c_str(), -1, SQLITE_TRANSIENT);
        std::vector<Message> messages;
        messages.reserve(32); // 极度严谨：预分配 32 个空间，防止由于元素增多导致底层数组频繁拷贝扩容
        
        // 循环提取多行数据
        while(sqlite3_step(stmt) == SQLITE_ROW)
        {
            Message msg( reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)),
                reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)));
            msg._message_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            msg._timestamp = sqlite3_column_int64(stmt, 4);
            messages.push_back(msg);
        }
        sqlite3_finalize(stmt);
        return messages;
    }

    // --------------------------- 暴露给外部的带锁安全接口 ---------------------------
    
    // 核心难点：插入消息时连带更新状态的死锁防范
    bool DataManager::insertMessage(const std::string& sessionId, const Message& message, std::time_t timestamp)
    {
        // 上锁！这是最外层保护伞
        std::lock_guard<std::mutex> lock(_mutex);
        std::string sql = R"( INSERT INTO messages (message_id,session_id,role,content,timestamp) VALUES (?,?,?,?,?) )";
        sqlite3_stmt* stmt;
        int rc = sqlite3_prepare_v2(_db, sql.c_str(),-1,&stmt,nullptr);
        if(rc != SQLITE_OK) return false;
        
        sqlite3_bind_text(stmt,1, message._message_id.c_str(),-1,SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt,2, sessionId.c_str(),-1,SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt,3, message._role.c_str(),-1,SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt,4, message._content.c_str(),-1,SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt,5, static_cast<sqlite3_int64>(timestamp));
        
        rc = sqlite3_step(stmt);
        if(rc != SQLITE_DONE)
        {
            sqlite3_finalize(stmt);
            return false;
        }
        sqlite3_finalize(stmt);
        
        // 注意看！这里绝不能调用带锁的 GetSessionById 和 updateSessionState
        // 而是走专属的内部免锁通道，完美避开死锁
        std::shared_ptr<Session> session = GetSessionByIdNoLock(sessionId);
        bool ret = updateSessionStateNoLock(session, timestamp);
        return ret;
    }

    // 其他增删改查外层接口，机理完全一致（先上锁，再预编译，最后执行清理）
    std::vector<std::string> DataManager::GetAllSessionId()
    {
        std::lock_guard<std::mutex> lock(_mutex);
        std::string sql = R"( SELECT session_id FROM sessions ORDER BY updated_time DESC; )";
        sqlite3_stmt* stmt; 
        int rc = sqlite3_prepare_v2(_db, sql.c_str(), -1, &stmt, nullptr); 
        if(rc != SQLITE_OK) return {};
        
        std::vector<std::string> sessionIds;
        sessionIds.reserve(64); 
        if(sqlite3_step(stmt) != SQLITE_ROW)
        {
            sqlite3_finalize(stmt);
            return sessionIds;
        }
        do
        {
            const unsigned char* raw_text = sqlite3_column_text(stmt, 0);
            if (raw_text != nullptr)
                sessionIds.push_back(reinterpret_cast<const char*>(raw_text)); 
            else
                sessionIds.push_back(""); 
        } while(sqlite3_step(stmt) == SQLITE_ROW);
        sqlite3_finalize(stmt);
        return sessionIds;
    }

    std::shared_ptr<Session> DataManager::GetSessionById(const std::string& sessionId)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        std::string sql = R"( SELECT * FROM sessions WHERE session_id = ? )";
        sqlite3_stmt* stmt;
        int rc = sqlite3_prepare_v2(_db, sql.c_str(), -1, &stmt, nullptr);
        if(rc != SQLITE_OK) return nullptr;
        
        sqlite3_bind_text(stmt, 1, sessionId.c_str(), -1, SQLITE_TRANSIENT);
        if(sqlite3_step(stmt) != SQLITE_ROW)
        {
            sqlite3_finalize(stmt);
            return nullptr;
        }
        const unsigned char* ModelNamePtr = sqlite3_column_text(stmt, 1);
        std::string ModelName = ModelNamePtr ? reinterpret_cast<const char*>(ModelNamePtr) : "";
        time_t CreatedAt = sqlite3_column_int64(stmt, 2);
        time_t UpdatedAt = sqlite3_column_int64(stmt, 3);
        std::shared_ptr<Session> session = std::make_shared<Session>(ModelName);
        session->_createAt = CreatedAt;
        session->_updateAt = UpdatedAt;
        session->_messages = GetMessagesBySessionIdNolock(sessionId);
        sqlite3_finalize(stmt);
        return session;
    }

    bool DataManager::insertSession(const std::shared_ptr<Session>& session)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        std::string sql = R"( INSERT INTO sessions (session_id,model_name,created_time,updated_time) VALUES (?,?,?,?) )";
        sqlite3_stmt* stmt;
        int rc = sqlite3_prepare_v2(_db, sql.c_str(), -1, &stmt, nullptr);
        if(rc != SQLITE_OK) return false;
        
        sqlite3_bind_text(stmt, 1, session->_session_id.c_str(),-1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, session->_model_name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 3, static_cast<uint64_t>(session->_createAt));
        sqlite3_bind_int64(stmt, 4, static_cast<uint64_t>(session->_updateAt));
        rc = sqlite3_step(stmt);
        if(rc != SQLITE_DONE) return false;
        sqlite3_finalize(stmt);
        return true;
    }

    bool DataManager::updateSessionState(const std::shared_ptr<Session>& session, std::time_t TimeStamp)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        std::string sql = R"( UPDATE sessions SET updated_time = ? WHERE session_id = ? )";
        sqlite3_stmt* stmt;
        int rc = sqlite3_prepare_v2(_db, sql.c_str(), -1, &stmt, nullptr);
        if(rc != SQLITE_OK) return false;
        
        sqlite3_bind_int64(stmt, 1, static_cast<uint64_t>(TimeStamp));
        sqlite3_bind_text(stmt, 2, session->_session_id.c_str(), -1, SQLITE_TRANSIENT);
        rc = sqlite3_step(stmt);
        if(rc != SQLITE_DONE) return false;
        sqlite3_finalize(stmt);
        return true;
    }

    bool DataManager::deleteSession(const std::string& sessionId)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        std::string sql = R"( DELETE FROM sessions WHERE session_id = ? )";
        sqlite3_stmt* stmt;
        int rc = sqlite3_prepare_v2(_db, sql.c_str(), -1, &stmt, nullptr);
        if(rc != SQLITE_OK) return false;
        
        sqlite3_bind_text(stmt, 1, sessionId.c_str(), -1, SQLITE_TRANSIENT);
        rc = sqlite3_step(stmt);
        if(rc != SQLITE_DONE)
        {
            sqlite3_finalize(stmt);
            return false;
        }
        sqlite3_finalize(stmt);
        return true;
    }

    std::size_t DataManager::GetSessionCount()
    {
        std::lock_guard<std::mutex> lock(_mutex);
        std::string sql = R"( SELECT COUNT(*) FROM sessions )";
        sqlite3_stmt* stmt;
        int rc = sqlite3_prepare_v2(_db, sql.c_str(), -1, &stmt, nullptr);
        if(rc != SQLITE_OK) return 0;
        
        rc = sqlite3_step(stmt);
        if(rc != SQLITE_ROW) return 0;
        
        std::size_t count = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
        return count;
    }

    std::vector<std::shared_ptr<Session>> DataManager::GetAllSessions()
    {
        std::lock_guard<std::mutex> lock(_mutex);
        std::string sql = R"( SELECT * FROM sessions ORDER BY updated_time DESC )";
        sqlite3_stmt* stmt;
        int rc = sqlite3_prepare_v2(_db, sql.c_str(), -1, &stmt, nullptr);
        if(rc != SQLITE_OK) return {};
        
        std::vector<std::shared_ptr<Session>> sessions;
        sessions.reserve(64);
        while((rc = sqlite3_step(stmt)) == SQLITE_ROW)
        {
            std::string sessionId = reinterpret_cast<const char* >(sqlite3_column_text(stmt, 0));
            std::string modelName = reinterpret_cast<const char* >(sqlite3_column_text(stmt, 1));
            std::time_t createdTime = sqlite3_column_int64(stmt, 2);
            std::time_t updatedTime = sqlite3_column_int64(stmt, 3);
            std::shared_ptr<Session> session = std::make_shared<Session>(modelName);
            session->_session_id = sessionId;
            session->_createAt = createdTime;
            session->_updateAt = updatedTime;
            sessions.push_back(session);
        }
        sqlite3_finalize(stmt);
        return sessions;
    }

    bool DataManager::clearAllSessions()
    {
        std::lock_guard<std::mutex> lock(_mutex);
        std::string sql = R"( DELETE FROM sessions )";
        sqlite3_stmt* stmt;
        int rc = sqlite3_prepare_v2(_db, sql.c_str(), -1, &stmt, nullptr);
        if(rc != SQLITE_OK) return false;
        
        rc = sqlite3_step(stmt);
        if(rc != SQLITE_DONE)
        {
            sqlite3_finalize(stmt);
            return false;
        }
        sqlite3_finalize(stmt);
        return true;
    }

    std::vector<Message> DataManager::GetMessagesBySessionId(const std::string& sessionId)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        std::string sql = R"( SELECT * FROM messages WHERE session_id = ? )";
        sqlite3_stmt* stmt;
        int rc = sqlite3_prepare_v2(_db, sql.c_str(), -1, &stmt, nullptr);
        if(rc != SQLITE_OK) return {};
        
        sqlite3_bind_text(stmt, 1, sessionId.c_str(), -1, SQLITE_TRANSIENT);
        std::vector<Message> messages;
        messages.reserve(32); 
        while(sqlite3_step(stmt) == SQLITE_ROW)
        {
            Message msg( reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)),
                reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)));
            msg._message_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            msg._timestamp = sqlite3_column_int64(stmt, 4);
            messages.push_back(msg);
        }
        sqlite3_finalize(stmt);
        return messages;
    }

    bool DataManager::deleteSessionMessage(const std::string& sessionId)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        std::string sql = R"( DELETE FROM messages WHERE session_id = ? )";
        sqlite3_stmt* stmt;
        int rc = sqlite3_prepare_v2(_db, sql.c_str(), -1, &stmt, nullptr);
        if(rc != SQLITE_OK) return false;
        
        sqlite3_bind_text(stmt, 1, sessionId.c_str(), -1, SQLITE_TRANSIENT);
        rc = sqlite3_step(stmt);
        if(rc != SQLITE_DONE)
        {
            sqlite3_finalize(stmt);
            return false;
        }
        sqlite3_finalize(stmt);
        return true;
    }
}
```
# 5. 结语
历经将近 500 行的原生 SQLite 接口封装，我们终于把极度危险的内存数据，稳稳当当地锁进了硬盘的保险箱里。
回看整个 `DataManager`，最核心的精华并不在于学会写那两句 `SELECT` 或 `INSERT`，而在于**基于 `std::mutex` 的并发防御机制**，以及由此延伸出的**内外双通道（加锁接口与 NoLock 免锁接口）防死锁设计**。再加上利用 `sqlite3_prepare_v2` 预编译机制构建出的**防注入城墙**，这个模块已经是一个可以直接拿上生产环境打硬仗的模块了。
至此，通信基类、模型调用、数据存储我们都已经搞定。下一篇，距离彻底掌控整个 AI_SDK，只差最后一个指挥大营了！
