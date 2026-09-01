# oj — 软件规格说明书

面向负载均衡在线评测（OJ）平台的软件规格说明书。本文档描述系统组件、数据模型、HTTP 协议、负载均衡与容错行为、资源限制的强制方式以及非功能需求，均以源码树中的实际实现为准。

> 配套文档：[README.md](README.md)（概述）· [API.md](API.md)（HTTP 接口）· [DEPLOY.md](DEPLOY.md)（运维部署）

---

## 目录

- [1. 范围](#1-范围)
- [2. 系统概述](#2-系统概述)
- [3. 组件规格](#3-组件规格)
- [4. 数据模型](#4-数据模型)
- [5. HTTP 协议](#5-http-协议)
- [6. 评测流程](#6-评测流程)
- [7. 负载均衡与容错](#7-负载均衡与容错)
- [8. 资源限制与执行沙箱](#8-资源限制与执行沙箱)
- [9. 结果状态码](#9-结果状态码)
- [10. 日志](#10-日志)
- [11. 配置](#11-配置)
- [12. 非功能需求](#12-非功能需求)
- [13. 不在范围内](#13-不在范围内)
- [14. 待办清单（TODO）](#14-待办清单todo)

---

## 1. 范围

本规格涵盖两个协作服务：

- **`oj_server`** —— 网页网关。负责提供 HTML 页面、从 MySQL 读取题目、管理用户账号与角色、小组与题目管理，并将评测提交负载均衡地分发到各编译服务器。
- **`compile_server`** —— 判题节点（编译运行服务）。每个节点一个进程，各自绑定一个 TCP 端口。通过 HTTP 接收评测任务，使用 `g++` 编译用户代码，在 CPU/内存限制下运行程序，并返回 JSON 结果。

从用户视角：注册/登录后浏览题库（全局题 + 自己所在小组的题目），查看带浏览器内代码编辑器的题目，提交 C++ 题解，实时获取评测结果（AC 类 / WA 类 / 超时 / 内存超限反馈）。管理员可发布全局题并管理角色等级；负责人可创建小组、通过邀请码拉人并在组内发布题目；普通用户凭邀请码加入小组。上述用户认证、角色与小组、题目管理能力为**规划中的待办特性**（见 [第 14 节](#14-待办清单todo)），尚未接入代码实现。

---

## 2. 系统概述

```
                     ┌──────────────────────────────────────────────┐
                     │               oj_server (gateway)            │
                     │  • serves HTML pages        (port 8080)      │
                     │  • reads questions from MySQL                │
                     │  • user accounts / roles / groups   (TODO)   │
                     │  • question manage (create/update/delete)    │
                     │  • load-balances /judge/{id} requests        │
                     └───────┬──────────────┬──────────────┬────────┘
                             │              │              │
                HTTP POST /compile_and_run  │              │
                             ▼              ▼              ▼
               ┌──────────────────┐  ┌──────────────┐  ┌──────────────┐
               │ compile_server   │  │ compile_server│  │ compile_server│
               │ (least load)     │  │  (node 2)    │  │  (node 3)    │
               │ compile + run    │  │              │  │              │
               └──────────────────┘  └──────────────┘  └──────────────┘
```

- 网关与所有节点是相互独立的进程，仅通过 HTTP 通信。
- 网关通过静态配置文件 `conf/service_machine.conf` 获知节点列表（仅在启动时读取一次）。
- 每次评测请求被分发到当前 **负载最低的在线** 节点。
- 不可达的节点会被自动 **下线**；向网关发送 `SIGQUIT` 可使所有节点重新 **上线**。

---

## 3. 组件规格

### 3.1 `oj_server`（网关）

| 项目 | 值 |
| --- | --- |
| 入口点 | `oj_server/oj_server.cpp` |
| 监听地址 | `0.0.0.0:8080`（硬编码） |
| HTTP 库 | cpp-httplib |
| 题库存储 | MySQL `oj.questions`（当前启用）；另附基于文件的模型 |
| HTML 渲染 | `oj_server/oj_view.hpp`（`View` 类）基于 `template_html/` 模板用 ctemplate 渲染（新增页面均为 HTML + CSS + JS） |
| 静态资源 | 从 `./wwwroot` 提供（相对网关工作目录） |

**`oj_server.cpp` 中注册的路由：**

| 路由 | 方法 | 处理函数 |
| --- | --- | --- |
| `/all_questions` | GET | `Control::AllQuestions` —— 题目列表页 |
| `/question/{id}` | GET | `Control::OneQuestion` —— 单题页面（含编辑器），`{id}` 通过 `(\d+)` 匹配 |
| `/judge/{id}` | GET/POST | `Control::Judge` —— 评测提交，返回 JSON |
| `*`（文件） | GET | cpp-httplib 静态目录 `./wwwroot` |

**规划中的路由（TODO，见 [第 14 节](#14-待办清单todo)）：**

| 路由 | 方法 | 说明 |
| --- | --- | --- |
| `/api/register` | POST | 注册普通用户 |
| `/api/login` | POST | 登录，返回 token |
| `/api/groups` | POST | 负责人创建小组 |
| `/api/groups/join` | POST | 普通用户凭邀请码加入小组 |
| `/api/groups/{id}/invite` | POST | 负责人重置小组邀请码 |
| `/api/users/{id}/role` | PUT | 管理员修改用户角色等级 |
| `/api/questions` | POST | 发布题目（管理员全局 / 负责人组内） |
| `/api/questions/{id}` | PUT | 修改题目 |
| `/api/questions/{id}` | DELETE | 删除题目 |
| `/question_manage` | GET | 题目管理列表页 |
| `/question_manage/edit` | GET | 新增题目表单页 |
| `/question_manage/edit/{id}` | GET | 编辑题目表单页（预填） |

**功能目录划分（`oj_server/` 下）：**

- `oj_server/user/` —— 用户管理内容（注册/登录、角色、小组），当前仅占位（`.gitkeep`）。
- `oj_server/question_manage/` —— 题目管理内容（新增/修改/删除），当前仅占位（`.gitkeep`）。
- 新增前端页面统一采用 **HTML + CSS + JS**（无前端框架），置于 `template_html/`，由 `oj_server/oj_view.hpp` 中的 `View` 类（ctemplate）渲染、原生 XHR/fetch 调用 JSON 接口：
  - `question_manage.html` —— 题目管理列表页（对应新增的 `View::QuestionManageExpandHtml`，TODO）；
  - `question_edit.html` —— 新增/编辑共用表单页（对应新增的 `View::QuestionEditExpandHtml`，TODO）。

### 3.2 `compile_server`（判题节点）

| 项目 | 值 |
| --- | --- |
| 入口点 | `compile_server/compile_server.cpp` |
| 用法 | `./compile_server <port>` |
| HTTP 库 | cpp-httplib |
| 路由 | `POST /compile_and_run` —— JSON 进，JSON 出 |

### 3.3 公共代码（`common/`）

| 模块 | 职责 |
| --- | --- |
| `common/Util.hpp` | `Path`（临时文件路径拼接）、`Time`（时间戳）、`File`（读写/唯一文件名）、`UtilString`（基于 boost 的字符串切分） |
| `common/log/` | `Log.hpp` 入口 + 异步、按时间滚动的日志库（`Buffer`、`Formatter`、`Level`、`Looper`、`Sink`、`LogMessage` 等） |

---

## 4. 数据模型

### 4.1 内存中的题目对象（`Question`）

两个模型（`oj_mysqlmodel.hpp` / `oj_filemodel.hpp`）共用：

| 字段 | 类型 | 描述 |
| --- | --- | --- |
| `_id` | `string` | 唯一数字型题目编号 |
| `_title` | `string` | 题目标题 |
| `_rank` | `enum {EASY, NORMAL, DIFFICULT, UNKNOWN}` | 难度（简单/中等/困难） |
| `_cpu_limit` | `size_t` | CPU 时间限制，秒 |
| `_mem_limit` | `size_t` | 内存限制，MB |
| `_desc` | `string` | 题目描述 |
| `_header` | `string` | 隐藏的前置代码，拼接在 **用户代码之前** |
| `_answer` | `string` | 在线编辑器中预填的初始代码 |
| `_tail` | `string` | 测试用例驱动代码，拼接在 **用户代码之后** |
| `_scope` | `string` | 可见范围：`"global"` 表示全局题（全体可见），否则为小组 id（组内题，仅该组成员可见）（TODO） |

### 4.2 MySQL 表结构（`oj.questions`）

通过 `SELECT *` 按位置读取 —— **列顺序很关键**：

| 位置 | 列 | 映射字段 |
| --- | --- | --- |
| 0 | `id INT PK AUTO_INCREMENT` | `_id` |
| 1 | `title VARCHAR(255)` | `_title` |
| 2 | `rank VARCHAR(16)` | `_rank` |
| 3 | `desc_text TEXT` | `_desc` |
| 4 | `header TEXT` | `_header` |
| 5 | `answer TEXT` | `_answer` |
| 6 | `tail TEXT` | `_tail` |
| 7 | `cpu_limit INT`（默认 1） | `_cpu_limit` |
| 8 | `mem_limit INT`（默认 30） | `_mem_limit` |
| 9 | `scope VARCHAR(16) DEFAULT 'global'`（TODO） | `_scope` —— `global` 或小组 id |

连接参数（硬编码于 `oj_mysqlmodel.hpp`）：主机 `127.0.0.1`，端口 `3306`，数据库 `oj`，用户 `oj_client`，密码 `1234`。

### 4.3 基于文件的模型（备选）

- 索引：`questions/questions.list` —— 每行一道题，以空白切分，**6 个字段**（TODO，兼容旧的 5 列行，缺省 `scope=global`）：`id title rank cpu_limit mem_limit scope`。
- 每题一个目录 `questions/{id}/`，内含 `desc.txt`、`header.cpp`、`answer.cpp`、`tail.cpp`。
- 非当前启用模型；通过切换 `oj_control.hpp` 和 `oj_view.hpp` 中的 `using namespace` 行来启用。
- 规划中的题目管理（新增/修改/删除）将写入该文件模型：`AddQuestion`（分配 `max(id)+1`、`mkdir`、写四文件、追加列表行）、`UpdateQuestion`（覆盖四文件并重建列表）、`DeleteQuestion`（删除文件与目录并重建列表）（TODO）。

### 4.4 用户、角色与小组（TODO）

**角色等级：**

| 角色 | 说明 |
| --- | --- |
| `admin`（管理员 / 超管） | 最高权限：可修改其他用户的角色等级；可发布全局题（全体可见）；可见并管理**所有**题目 |
| `leader`（负责人） | 拥有自己的小组：可通过邀请码邀请他人加入；在**组内**发布/修改/删除题目（仅本组成员可见） |
| `user`（普通用户） | 凭邀请码加入小组；可见全局题 + 自己所在组的题目；可提交评测；无题目管理与角色管理权限 |

**MySQL 表结构（规划，待建）：**

`users`：

| 列 | 类型 | 说明 |
| --- | --- | --- |
| `id` | `INT PK AUTO_INCREMENT` | 用户 id |
| `username` | `VARCHAR(64) UNIQUE NOT NULL` | 用户名 |
| `password_hash` | `VARCHAR(64) NOT NULL` | 密码加盐哈希值 |
| `salt` | `VARCHAR(20) NOT NULL` | 加盐值 = 注册时的时间戳 |
| `role` | `VARCHAR(16) NOT NULL DEFAULT 'user'` | `admin` / `leader` / `user` |
| `created_at` | `DATETIME` | 注册时间 |

`groups`：

| 列 | 类型 | 说明 |
| --- | --- | --- |
| `id` | `INT PK AUTO_INCREMENT` | 小组 id |
| `name` | `VARCHAR(64) NOT NULL` | 小组名称 |
| `owner_id` | `INT NOT NULL` | 负责人用户 id |
| `invite_code` | `VARCHAR(32) NOT NULL UNIQUE` | 邀请码，每组一个，可由负责人重置 |
| `created_at` | `DATETIME` | 创建时间 |

`group_members`（多对多）：

| 列 | 类型 | 说明 |
| --- | --- | --- |
| `group_id` | `INT NOT NULL` | 小组 id |
| `user_id` | `INT NOT NULL` | 用户 id |
| 主键 | `PRIMARY KEY(group_id, user_id)` | — |

**密码存储：** 密码不以明文存储。注册时以**当前时间戳**作为盐（`salt`），将 `password + salt` 做哈希后存入 `password_hash`；登录时用相同的盐重算比对。哈希函数仅以 std/Boost 实现，不引入第三方加密库。

### 4.5 可见性规则（TODO）

| 题目范围 | 管理员 | 负责人 | 普通用户 |
| --- | --- | --- | --- |
| 全局题（`scope = global`） | ✓ | ✓ | ✓ |
| 组内题（`scope = 小组id`） | ✓（全部） | ✓（仅本组） | ✓（仅所在组） |

- `GET /all_questions`、`GET /question/{id}`、`POST /judge/{id}` 均需按当前登录用户的角色与所属小组过滤可见性。

---

## 5. HTTP 协议

两个服务对 JSON 使用 `application/json; charset=utf-8`，对页面使用 `text/html; charset=utf-8`。

### 5.1 `GET /all_questions`（网关）

- 响应：`200`，`text/html; charset=utf-8` —— 当前用户可见题目（全局题 + 所在组题目）的表格，每行链接到 `/question/{id}`。
- 题目按数字 `id` 升序排序。
- 可见性过滤（TODO）：按登录用户的角色与所属小组返回题目。

### 5.2 `GET /question/{id}`（网关）

- 路径参数 `id`：数字型题目编号（`(\d+)`）。
- 响应：`200`，`text/html; charset=utf-8` —— 题目描述、难度以及预填代码的编辑器。
- 可见性校验（TODO）：用户不可见的题目（非全局、非所在组、且非管理员/负责人本组）返回不可访问提示。

### 5.3 `POST /judge/{id}`（网关）

**请求体：**

| 字段 | 类型 | 必填 | 描述 |
| --- | --- | --- | --- |
| `code` | string | 是 | 用户 C++ 源码 |
| `input` | string | 否 | 可选的标准输入（默认为空） |

**响应体：**

| 字段 | 类型 | 描述 |
| --- | --- | --- |
| `status` | integer | 评测状态码（见 [第 9 节](#9-结果状态码)） |
| `reason` | string | 人类可读的结果描述（中文） |
| `stdout` | string | 仅当 `status == 0` 时返回 |
| `stderr` | string | 仅当 `status == 0` 时返回 |

- 可见性校验（TODO）：仅允许对当前用户可见的题目提交评测。

### 5.4 `POST /compile_and_run`（编译服务器，内部接口）

**请求体：**

| 字段 | 类型 | 必填 | 描述 |
| --- | --- | --- | --- |
| `code` | string | 是 | **完整源码**：`header + "\n" + 用户代码 + "\n" + tail` |
| `input` | string | 否 | 程序标准输入 |
| `cpu_limit` | integer | 是 | 秒 |
| `mem_limit` | integer | 是 | MB |

**响应体：** 与 `/judge/{id}` 相同。HTTP 层始终返回 `200`；结果通过 `status` 表达。

### 5.5 `POST /api/register`（网关，TODO）

- 请求体：`{username, password}`。
- 行为：以当前时间戳为盐计算 `password_hash = Hash(password + salt)`，插入 `users` 表（角色默认为 `user`）。
- 响应：`200` + JSON `{ok, message}`。

### 5.6 `POST /api/login`（网关，TODO）

- 请求体：`{username, password}`。
- 行为：用 `users.salt` 重算哈希并比对；成功则签发 token（内存会话），返回 `{token, username, role}`。
- 后续受保护请求在 `Authorization: Bearer <token>` 中携带 token。

### 5.7 `POST /api/groups`（网关，TODO）

- 请求体：`{name}`（需负责人身份）。
- 行为：创建小组（`owner_id = 当前用户`），生成唯一 `invite_code`。
- 响应：`200` + JSON `{ok, group_id, invite_code}`。

### 5.8 `POST /api/groups/join`（网关，TODO）

- 请求体：`{invite_code}`（需普通用户身份）。
- 行为：校验邀请码，向 `group_members` 插入当前用户。
- 响应：`200` + JSON `{ok, group_id}`。

### 5.9 `POST /api/groups/{id}/invite`（网关，TODO）

- 路径参数 `id`：小组 id（需该组负责人身份）。
- 行为：为该组生成新的 `invite_code`（旧码失效）。
- 响应：`200` + JSON `{ok, invite_code}`。

### 5.10 `PUT /api/users/{id}/role`（网关，TODO）

- 路径参数 `id`：用户 id；请求体：`{role}`（需管理员身份）。
- 行为：修改该用户的角色等级（`admin` / `leader` / `user`）。
- 响应：`200` + JSON `{ok, message}`。

### 5.11 `POST /api/questions`（网关，TODO）

- 请求体（字段：`title rank desc header answer tail cpu_limit mem_limit scope`）；需管理员或负责人身份。
- 权限：管理员可发布全局题（`scope=global`）；负责人仅可发布本组题（`scope=小组id`）。
- 行为：按当前启用的模型存储 ——
  - MySQL 模型：`INSERT INTO questions(...)`（含 `scope` 列）；
  - 文件模型：分配 `max(id)+1`，创建 `questions/{id}/`，写入 `desc.txt`、`header.cpp`、`answer.cpp`、`tail.cpp`，并在 `questions.list` 追加一行（6 列）。
- 响应：`200` + JSON `{ok, id}`。

### 5.12 `PUT /api/questions/{id}`（网关，TODO）

- 路径参数 `id`：题目 id；请求体同 5.11；权限同上（负责人仅能修改本组题目）。
- 行为：MySQL 模型 `UPDATE ... WHERE id=?`；文件模型覆盖 `questions/{id}/` 四文件并重建 `questions.list`。
- 响应：`200` + JSON `{ok, message}`。

### 5.13 `DELETE /api/questions/{id}`（网关，TODO）

- 路径参数 `id`：题目 id；权限同上（负责人仅能删除本组题目）。
- 行为：MySQL 模型 `DELETE FROM questions WHERE id=?`；文件模型删除 `questions/{id}/` 目录及 `questions.list` 中对应行。
- 响应：`200` + JSON `{ok, message}`。

### 5.14 题目管理页面（网关，TODO）

| 路由 | 方法 | 说明 |
| --- | --- | --- |
| `GET /question_manage` | GET | 题目管理列表页（`question_manage.html`）：行内修改/删除、顶部新增；管理员见全部题，负责人仅见本组题 |
| `GET /question_manage/edit` | GET | 新增题目表单页（`question_edit.html`） |
| `GET /question_manage/edit/{id}` | GET | 编辑题目表单页（`question_edit.html`，预填数据） |

- 前端页面均为 **HTML + CSS + JS**（由 `oj_view.hpp` 的 `View` 类基于 `template_html/` 模板渲染，原生 XHR/fetch 调 JSON 接口）；表单填写后由 JS 打包为 JSON，经 `POST/PUT /api/questions` 提交。

---

## 6. 评测流程

实现在 `oj_control.hpp`（`Control::Judge`）与 `compile_run.hpp`（`CompileAndRun::Start`）中。

1. **加载题目** —— 网关按编号从 MySQL 查询题目。
2. **可见性校验**（TODO）—— 校验当前登录用户是否可访问该题（全局题 / 所在组题目 / 管理员或本组负责人），不可见则直接返回权限错误。
3. **反序列化** 请求 `{code, input}`（使用 jsoncpp）。
4. **拼接完整源码** —— `code = header + "\n" + 用户代码 + "\n" + tail`；将 `{code, input, cpu_limit, mem_limit}` 打包为 JSON。
5. **选择一个节点** —— `LoadBlance::SmartChoice` 选择当前负载（`_load`）**最小**的在线机器。若无在线机器，循环退出，网关返回空响应体。
6. **转发** —— 网关向 `/{ip}:{port}/compile_and_run` 发起 `POST`；发送前对节点的 `_load` 加一，收到任何 HTTP 响应后减一。
7. **判题** —— 节点将源码写入唯一命名的临时文件，编译，在资源限制下运行，返回 JSON。
8. **返回** —— 网关将节点的响应体原样透传给浏览器。

选择循环内的失败处理：

| 情况 | 动作 |
| --- | --- |
| HTTP 请求失败（节点不可达） | 记录错误日志，`OfflineMachine(id)`，循环选择次优节点 |
| 返回 HTTP `200` | 负载减一，透传响应体，退出循环 |
| 无在线机器 | `SmartChoice` 返回 `false`；请求以空 JSON 响应体结束 |

---

## 7. 负载均衡与容错

### 7.1 负载指标

每台 `Machine` 维护一个 `std::atomic<size_t> _load` —— **在途评测请求数**。发起 HTTP 调用前加一，调用完成（成功或返回 `200`）后减一。读取负载时受每台机器的互斥锁保护。

### 7.2 选择算法

`SmartChoice` 扫描 `_online` 列表，选出 `GetLoad()` 最小的机器；负载相同时选择在线列表中最靠前的机器。网关循环执行：先尝试最优机器；若不可达，则将其下线并重试次优机器，直到成功或在线列表为空。

### 7.3 上线 / 下线簿记

- `_online` / `_offline` 为机器索引向量，受 `_lock` 保护。
- `OfflineMachine(id)`：将负载清零，从 `_online` 移到 `_offline`。
- `OnlineAllMachines()`：将所有下线机器重新上线（由 `SIGQUIT` 信号处理器调用）。
- `ShowMachines()`：调试辅助函数，打印两个列表。

### 7.4 恢复

向 `oj_server` 发送 `SIGQUIT` 触发 `Recovery` → `Control::RecoveryMachine` → `LoadBlance::OnlineAllMachines`。重启网关同样可行（启动时通过 `LoadBlance` 构造函数中的 `assert` 重新读取 `service_machine.conf`）。

---

## 8. 资源限制与执行沙箱

实现在 `runner.hpp`（`Runner`）中。

1. `Runner::Run` 打开 `stdin`/`stdout`/`stderr` 临时文件（`O_CREAT`），然后 `fork()`。
2. 子进程将三个文件描述符通过 `dup2` 重定向到 0/1/2，调用 `SetResoureLimit`，再对编译产物执行 `execl`。
3. `SetResoureLimit` 施加以下限制：
   - `RLIMIT_CPU` → `cpu_limit` 秒（`rlim_max = RLIM_INFINITY`）
   - `RLIMIT_AS` → `mem_limit * 1024 * 1024` 字节（MB）
4. 父进程 `waitpid` 并返回 `status & 0x7F` —— 即导致进程终止的 **信号编号**（若有）。
   - 返回值约定：`>0` 异常退出（收到信号），`==0` 正常退出，`<0` 内部错误（fd 打开失败返回 `-1`，`fork` 失败返回 `-2`）。

编译（`compiler.hpp`）：fork 一个子进程，将 stderr 重定向到 `.compile_error` 文件，通过 `execlp` 执行 `g++ -o <exe> <src> -D COMPILER_ONLINE -std=c++20`。以可执行文件是否存在判断编译是否成功。

临时文件生命周期：每次评测在 `./temp/` 下生成唯一命名的文件（`<毫秒时间戳>_<原子递增计数>` 前缀），后缀为 `.cpp`、`.exe`、`.compile_error`、`.stdin`、`.stdout`、`.stderr`；`CompileAndRun::RemoveTempFiles` 在每次请求结束后将其全部删除。

---

## 9. 结果状态码

定义于 `compile_run.hpp` 的 `CompileAndRun::StatusToDesc`：

| `status` | 信号 / 含义 | `reason` |
| --- | --- | --- |
| `0` | 编译并运行成功 | 编译成功 |
| `-1` | 提交代码为空 | 提交代码为空 |
| `-2` | 内部错误 | 内部错误 |
| `-3` | 编译失败 | `.compile_error` 文件的内容 |
| `6` | `SIGABRT` | 内存超过范围（内存超限） |
| `24` | `SIGXCPU` | CPU使用超时（CPU 时间超限） |
| `8` | `SIGFPE` | 浮点数溢出（浮点异常） |
| 其他 | 未知终止信号 | 未知: `<filename>` |

> 运行期状态码取子进程的终止信号（`status & 0x7F`）。仅当 `status == 0` 时才填充 `stdout`/`stderr`。

---

## 10. 日志

两个服务均使用公共的 `common/log/` 模块，在启动时配置：

| 服务 | 日志名 | 类型 | Sink | 目录 |
| --- | --- | --- | --- | --- |
| `oj_server` | `oj_Logger` | `LOGGER_ASYNC` | `RollByTimeSink`，`GAP_DAY` | `./logfiles/` |
| `compile_server` | `CompileRun_Loggger` | `LOGGER_ASYNC` | `RollByTimeSink`，`GAP_DAY` | `./logfiles/` |

- 异步、按时间滚动的日志器；日志目录 **相对各进程的工作目录**（因此在单机多节点时需为每个节点分配独立目录）。
- 日志事件包括题目模型查询、节点选择、HTTP 请求结果、节点下线、编译成败以及运行期信号。
- 规划中的日志事件（TODO）：注册/登录成败、角色变更、建组/加入小组/重置邀请码、题目新增/修改/删除、可见性拒绝。

---

## 11. 配置

| 项 | 位置 | 说明 |
| --- | --- | --- |
| 编译服务器列表 | `oj_server/conf/service_machine.conf` | 每行一个 `ip:port`，以 `:` 分隔，不含空格。仅在网关启动时读取；修改后需重启网关。 |
| MySQL 凭据 | 硬编码于 `oj_mysqlmodel.hpp` | `oj` / `oj_client` / `1234` / `127.0.0.1:3306` |
| 网关监听端口 | 硬编码于 `oj_server.cpp` | `8080` |
| 节点监听端口 | 命令行参数 | `./compile_server <port>` |
| 临时 / 日志目录 | 相对工作目录 | `./temp/`、`./logfiles/` |
| 数据模型切换 | `oj_control.hpp`、`oj_view.hpp` 中的 `using namespace` | 当前启用 `oj_mysqlmodel`；备选 `oj_filemodel` |

---

## 12. 非功能需求

- **语言 / 工具链：** C++20（`-std=c++20`）；编译服务器 PATH 上需存在 `g++` ≥ 10。
- **可移植性：** 仅 Linux（依赖 `fork`/`exec`、`waitpid`、`setrlimit`、`dup2`、POSIX 信号）。
- **并发：** HTTP 服务器多线程；节点选择状态由互斥锁保护；各节点负载为原子量。
- **容错：** 节点故障不会丢失请求 —— 网关会向次优节点重试；可通过 `SIGQUIT` 完全恢复。
- **资源隔离：** 通过 fork 出的子进程施加每题 CPU（`RLIMIT_CPU`）与内存（`RLIMIT_AS`）限制，防止失控程序拖垮节点。
- **确定性：** 每次评测相互独立（唯一临时文件名、无共享状态）；负载指标为计数型，而非真实的瞬时测量。
- **可观测性：** 对请求、节点选择、故障及评测结果进行结构化异步日志记录。
- **认证与安全（TODO）：** 密码以"时间戳盐 + 哈希"存储，不以明文落库；登录签发 token，受保护接口需校验身份与角色；题目管理、角色管理、小组操作均做权限校验；MySQL 写入使用转义防注入。
- **前端技术约束：** 所有新增页面统一采用 HTML + CSS + JS（由 `oj_view.hpp` 的 `View` 类基于 ctemplate 渲染，原生 XHR/fetch），不引入前端框架。

---

## 13. 不在范围内

- 用户提交历史、按用户统计与排行榜。
- 评测结果的持久化存储（结果仅返回给浏览器并记入日志，不落库）。
- 真正的进程/容器沙箱（如命名空间、cgroups）；隔离仅依赖 `setrlimit`。
- HTTPS/TLS、限流，以及多区域或加权负载均衡策略。
- 除 C++ 之外的语言。
- 前端框架（Vue/React 等）；新增页面仅用 HTML + CSS + JS。
- 第三方加密库（如 OpenSSL）；密码哈希仅以 std/Boost 实现。

---

## 14. 待办清单（TODO）

> 以下特性已在本文档中完成规格设计，尚未接入源码实现，按顺序逐步落地。代码实现须复用仓库内已有的第三方库（cpp-httplib / ctemplate / jsoncpp / libmysqlclient / Boost / std），不新增依赖。

### 数据库与数据模型

- [ ] 为 `questions` 表追加列 `scope VARCHAR(16) NOT NULL DEFAULT 'global'`（位置 9）。
- [ ] 新建 `users` 表（`id username password_hash salt role created_at`）。
- [ ] 新建 `groups` 表（`id name owner_id invite_code created_at`）。
- [ ] 新建 `group_members` 关联表（`group_id user_id` 联合主键）。
- [ ] 将 `questions/questions.list` 扩展为 6 列格式：`id title rank cpu_limit mem_limit scope`（并兼容旧 5 列，缺省 `global`）。

### 数据模型层（`oj_mysqlmodel.hpp` / `oj_filemodel.hpp`）

- [ ] `Question` 结构体新增字段 `_scope`（`"global"` 或小组 id）。
- [ ] `oj_mysqlmodel.hpp`：`QueryMysql` 读取 `row[9]` → `_scope`。
- [ ] `oj_mysqlmodel.hpp`：新增 `ExecuteSql` 辅助方法与 `AddQuestion`（转义 + INSERT + `mysql_insert_id` 回填 id）、`UpdateQuestion`、`DeleteQuestion`。
- [ ] `oj_filemodel.hpp`：`LoadQuestionsList` 解析 6 列；新增 `AddQuestion`、`UpdateQuestion`、`DeleteQuestion` 与辅助 `WriteQuestionsList`（按数字 id 升序重写）。
- [ ] 文件模型 CRUD 需对 `_questions` 加互斥锁，保证并发读写安全。

### 用户、角色与小组

- [ ] `POST /api/register`：注册普通用户（时间戳盐 + 哈希存储密码）。
- [ ] `POST /api/login`：登录校验并签发 token（内存会话）；受保护接口校验 `Authorization: Bearer <token>`。
- [ ] `PUT /api/users/{id}/role`：管理员修改其他用户角色等级。
- [ ] `POST /api/groups`：负责人创建小组并生成唯一邀请码。
- [ ] `POST /api/groups/join`：普通用户凭邀请码加入小组。
- [ ] `POST /api/groups/{id}/invite`：负责人重置小组邀请码（旧码失效）。

### 题目管理（新增 / 修改 / 删除）

- [ ] `POST /api/questions`：发布题目（管理员全局 / 负责人组内），按启用模型写入 MySQL 或 `questions/` 文件模型。
- [ ] `PUT /api/questions/{id}`：修改题目（负责人仅限本组）。
- [ ] `DELETE /api/questions/{id}`：删除题目（负责人仅限本组）。
- [ ] 页面：`GET /question_manage`、`GET /question_manage/edit`、`GET /question_manage/edit/{id}`。

### 前端页面（HTML + CSS + JS，`oj_view.hpp` 的 `View` 类渲染）

- [ ] `template_html/question_manage.html` —— 题目管理列表页。
- [ ] `oj_view.hpp` 新增 `View::QuestionManageExpandHtml`（渲染 `question_manage.html`）。
- [ ] `template_html/question_edit.html` —— 新增/编辑共用表单页（表单由 JS 打包 JSON 提交）。
- [ ] `oj_view.hpp` 新增 `View::QuestionEditExpandHtml`（渲染 `question_edit.html`）。
- [ ] 用户相关页面：注册页、登录页、小组管理页（对应 `View` 类新增的渲染方法）。
- [ ] 现有页面（`all_questions.html`、`one_question.html`）接入登录态与可见性过滤。

### 可见性与权限

- [ ] `GET /all_questions`、`GET /question/{id}`、`POST /judge/{id}` 按角色与小组过滤可见性。
- [ ] 题目管理、角色管理、小组操作接口的权限校验。
- [ ] 相关日志事件（登录/注册/角色变更/建组/邀请码/题目 CRUD/可见性拒绝）。

### 收尾

- [ ] `user/`、`question_manage/` 中的实际内容实现完成后，删除其中的占位文件 `.gitkeep`。
