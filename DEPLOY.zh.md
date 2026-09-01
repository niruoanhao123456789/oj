# oj 部署文档

本文档将引导你完成基于负载均衡的在线判题系统的部署，包括环境准备、数据库初始化、编译构建、启动与验证两个服务。

> [English](DEPLOY.md) · **[简体中文](DEPLOY.zh.md)**

---

## 目录

- [1. 架构概览](#1-架构概览)
- [2. 环境依赖](#2-环境依赖)
- [3. 数据库初始化](#3-数据库初始化)
- [4. 编译构建](#4-编译构建)
- [5. 配置编译服务器](#5-配置编译服务器)
- [6. 启动编译服务器](#6-启动编译服务器)
- [7. 启动 OJ 网关](#7-启动-oj-网关)
- [8. 验证部署](#8-验证部署)
- [9. 新增题目](#9-新增题目)
- [10. 运维与维护](#10-运维与维护)
- [11. 故障排查](#11-故障排查)
- [12. 角色、小组与题目管理（规划中）](#12-角色小组与题目管理规划中)

---

## 1. 架构概览

需要运行两类进程：

- **编译服务器（判题节点）** —— 每个节点一个 `compile_server` 进程，各自监听独立端口。它们通过 `POST /compile_and_run` 接收判题任务，使用 `g++` 编译，在 CPU/内存限制下运行二进制，并返回 JSON 结果。
- **OJ 网关** —— 一个 `oj_server` 进程（端口 `8080`），负责提供页面，并把 `/judge/{id}` 请求负载均衡到各编译服务器。

> 网关在启动时从 `oj_server/conf/service_machine.conf` 读取编译服务器列表。在处理判题请求前，**所有**编译服务器必须已启动。

## 2. 环境依赖

- 支持 C++20 的工具链的 Linux 系统（推荐 `g++` ≥ 10）。
- MySQL 服务（推荐 MySQL/MariaDB 8.x），且可在 `127.0.0.1:3306` 访问。
- 以下开发库：

| 库 | 包名（Debian/Ubuntu） | 链接参数 |
| --- | --- | --- |
| jsoncpp | `libjsoncpp-dev` | `-ljsoncpp` |
| cpp-httplib | `libcpp-httplib-dev` | `-lcpp-httplib` |
| ctemplate | `libctemplate-dev` | `-lctemplate` |
| MySQL 客户端 | `libmysqlclient-dev` | `-lmysqlclient` |
| Boost（头文件） | `libboost-dev` | — |
| pthread | glibc | `-lpthread` |

安装示例（Debian/Ubuntu）：

```bash
sudo apt update
sudo apt install -y g++ make mysql-server libjsoncpp-dev libcpp-httplib-dev \
    libctemplate-dev libmysqlclient-dev libboost-dev
```

## 3. 数据库初始化

启动 MySQL 后，执行以下 SQL 创建数据库、数据表以及网关使用的应用用户（`oj_client` / `1234`，主机 `127.0.0.1:3306`）：

```sql
CREATE DATABASE IF NOT EXISTS oj DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_general_ci;
USE oj;

CREATE TABLE IF NOT EXISTS questions (
    id         INT PRIMARY KEY AUTO_INCREMENT,
    title      VARCHAR(255) NOT NULL,
    rank       VARCHAR(16)  NOT NULL,   -- 简单 / 中等 / 困难
    desc_text  TEXT,
    header     TEXT,                    -- 隐藏头，拼接在用户代码之前
    answer     TEXT,                    -- 编辑器中展示的模板代码
    tail       TEXT,                    -- 测试用例，拼接在用户代码之后
    cpu_limit  INT DEFAULT 1,           -- 秒
    mem_limit  INT DEFAULT 30,          -- MB
    scope      VARCHAR(16) NOT NULL DEFAULT 'global'   -- global 或小组 id
);

-- 用户 / 角色 / 小组（规划中的特性，见 SPEC.md 第14节）
CREATE TABLE IF NOT EXISTS users (
    id            INT PRIMARY KEY AUTO_INCREMENT,
    username      VARCHAR(64) NOT NULL UNIQUE,
    password_hash VARCHAR(64) NOT NULL,           -- Hash(password + salt)
    salt          VARCHAR(20) NOT NULL,           -- 注册时的时间戳
    role          VARCHAR(16) NOT NULL DEFAULT 'user',  -- admin / leader / user
    created_at    DATETIME
);

CREATE TABLE IF NOT EXISTS groups (
    id          INT PRIMARY KEY AUTO_INCREMENT,
    name        VARCHAR(64) NOT NULL,
    owner_id    INT NOT NULL,                     -- 负责人用户 id
    invite_code VARCHAR(32) NOT NULL UNIQUE,
    created_at  DATETIME
);

CREATE TABLE IF NOT EXISTS group_members (
    group_id INT NOT NULL,
    user_id  INT NOT NULL,
    PRIMARY KEY (group_id, user_id)
);

-- 种子管理员（hash / salt 由应用运行时生成）：
-- INSERT INTO users (username, password_hash, salt, role) VALUES ('admin', '<hash>', '<时间戳>', 'admin');

CREATE USER IF NOT EXISTS 'oj_client'@'localhost' IDENTIFIED BY '1234';
CREATE USER IF NOT EXISTS 'oj_client'@'127.0.0.1' IDENTIFIED BY '1234';
GRANT ALL PRIVILEGES ON oj.* TO 'oj_client'@'localhost';
GRANT ALL PRIVILEGES ON oj.* TO 'oj_client'@'127.0.0.1';
FLUSH PRIVILEGES;
```

> **升级说明：** 已有安装可执行
> `ALTER TABLE questions ADD COLUMN scope VARCHAR(16) NOT NULL DEFAULT 'global' AFTER mem_limit;` 追加该列。

> **列顺序说明：** 网关通过 `SELECT *` 按位置读取字段，顺序为：
> `id, title, rank, desc, header, answer, tail, cpu_limit, mem_limit, scope`。
> 若希望使用不同的列顺序或名称，请调整表结构以匹配 `oj_server/oj_mysqlmodel.hpp` 中的读取顺序。

插入一道示例题目（判断回文数）：

```sql
INSERT INTO questions (title, rank, desc_text, header, answer, tail, cpu_limit, mem_limit)
VALUES (
  '判断回文数',
  '简单',
  '判断一个整数是否是回文数。',
  '#include <iostream>\n#include <string>\nusing namespace std;\n',
  'bool solve(int x){ return false; }\n',
  'int main(){ int n; cin >> n; cout << (solve(n) ? "true" : "false") << endl; return 0; }\n',
  1,
  30
);
```

## 4. 编译构建

各服务使用自带的 `makefile` 构建。在项目根目录下，顶层 `makefile` 可以一次编译两个服务：

```bash
# 在项目根目录 —— 一次编译两个服务
make
```

也可以逐个构建：

```bash
# 编译服务器
cd compile_server
make

# OJ 网关
cd ../oj_server
make
```

构建产物为 `compile_server/compile_server` 与 `oj_server/oj_server` 两个可执行文件。顶层 `make clean` 可清理它们（对 `compile_server` 还会清理 `temp/` 下的临时文件）。

### 打包发布

顶层 `make output` 目标会将完整的、可独立运行的程序打包到 `output/` 目录下 —— 包含在其他主机上运行该系统所需的全部内容，可用于发布或发送：

```bash
make output
```

生成的结构如下：

```
output/
├── compile_server/
│   ├── compile_server          # 判题节点二进制
│   └── temp/                   # 运行时临时目录
└── oj_server/
    ├── oj_server               # 网关二进制
    ├── conf/                   # service_machine.conf
    ├── questions/              # 文件模型的示例题目
    ├── template_html/          # ctemplate 模板
    └── wwwroot/                # 静态资源
```

将 `output/` 拷贝到目标主机后，按本指南后续步骤操作，并分别在 `output/compile_server/` 与 `output/oj_server/` 目录下启动节点和网关即可。`make clean` 也会删除整个 `output/` 目录。

## 5. 配置编译服务器

编辑 `oj_server/conf/service_machine.conf` —— 每行一个 `ip:port`，以 `:` 分隔，不要包含空格：

```
127.0.0.1:8081
127.0.0.1:8082
127.0.0.1:8083
```

这是网关做负载均衡时使用的节点列表，可自由增删。网关仅在启动时读取该文件，因此修改后需要重启网关。

## 6. 启动编译服务器

每个 `compile_server` 进程将端口作为第一个参数：

```bash
./compile_server 8081
```

**注意：** 临时文件目录（`./temp/`）与日志目录（`./logfiles/`）均为**相对于工作目录**的路径。若要在同一台主机上运行多个节点，请为每个节点准备独立的工作目录：

```bash
# 节点 1
mkdir -p node1/temp node1/logfiles
cp compile_server node1/
cd node1 && ./compile_server 8081 &

# 节点 2
mkdir -p node2/temp node2/logfiles
cp compile_server node2/
cd node2 && ./compile_server 8082 &

# 节点 3
mkdir -p node3/temp node3/logfiles
cp compile_server node3/
cd node3 && ./compile_server 8083 &
```

或者将节点部署到不同主机，并把 `service_machine.conf` 指向这些主机。

> 编译通过 `execlp("g++", ...)` 完成，因此节点进程的 `PATH` 中必须能找到 `g++`。

## 7. 启动 OJ 网关

网关使用多个**相对**路径，因此必须从 `oj_server/` 目录内启动：

```bash
cd oj_server
mkdir -p logfiles          # 如缺失则创建
./oj_server
```

网关监听 `0.0.0.0:8080`：
- `GET /`、`GET /all_questions`、`GET /question/{id}` —— HTML 页面
- `POST /judge/{id}` —— 判题提交（JSON）

## 8. 验证部署

```bash
# 1. 题目列表页面
curl http://localhost:8080/all_questions

# 2. 单题页面
curl http://localhost:8080/question/1

# 3. 提交题解
curl -X POST http://localhost:8080/judge/1 \
  -H "Content-Type: application/json; charset=utf-8" \
  -d '{"code":"#include <iostream>\nint main(){ std::cout<<\"ok\"; return 0; }"}'
```

一次成功的判题会返回类似如下的 JSON：

```json
{
  "status": 0,
  "reason": "编译成功",
  "stdout": "ok",
  "stderr": ""
}
```

同时可以查看 `compile_server/*/logfiles/` 与 `oj_server/logfiles/` 下的日志，确认每个请求的处理情况。

## 9. 新增题目

1. 向 `questions` 表插入一行（见[数据库初始化](#3-数据库初始化)）。
2. 设计题目时注意：
   - `header` 为隐藏头（如 `#include` 行与辅助函数），
   - `answer` 为在线编辑器中展示的模板代码，
   - `tail` 为测试驱动（包含 `main` 与测试用例），与用户实现的 `solve(...)` 链接。
3. `scope` 设为 `global`（全体可见）或**小组 id**（仅该组组长与成员可见 —— 规划中的特性）。
4. 网关每次请求都会查询 MySQL，因此 `/all_questions` 能立即看到新题目，无需重启。

> 仓库中还附带一套基于文件的替代模型（`oj_filemodel.hpp`），从 `oj_server/questions/` + `questions.list` 读取题目。它**并非**当前启用模型；如需切换，请修改 `oj_control.hpp` 与 `oj_view.hpp` 中的 `using namespace` 行。`questions.list` 每行为 `id title rank cpu_limit mem_limit scope`（6 列；缺省 `scope` 视为 `global`），每题目录 `questions/{id}/` 内含 `desc.txt`、`header.cpp`、`answer.cpp`、`tail.cpp`。

> **规划中：** 未来将通过 `POST /api/questions`、`PUT /api/questions/{id}`、`DELETE /api/questions/{id}` 接口（配合 `template_html/` 下、由 `oj_server/oj_view.hpp` 的 `View` 类渲染的 HTML+CSS+JS 管理页面）在界面中新增/修改/删除题目，持久化到 MySQL 或文件模型。

## 10. 运维与维护

- **故障恢复：** 当编译服务器无法连接时，网关会将其移入离线状态。向网关发送 `SIGQUIT` 信号即可将所有节点恢复上线：

  ```bash
  kill -QUIT <oj_server_pid>
  ```

  也可以通过重启网关恢复 —— 网关启动时会重新读取 `service_machine.conf`。

- **日志：** 两个服务均使用异步、按时间滚动的日志器，写入各自工作目录下的 `logfiles/`。
- **资源限制：** 每道题的 `cpu_limit`（秒）与 `mem_limit`（MB）通过 fork 出的子进程中的 `setrlimit`（`RLIMIT_CPU`、`RLIMIT_AS`）强制执行。
- **临时文件：** 编译产物存放在 `compile_server/temp/`，每次判题结束后由编译服务器清理。
- **横向扩展：** 在 `service_machine.conf` 中追加 `ip:port` 并启动更多 `compile_server` 进程即可扩容；网关会自动调度到负载最低的节点。

## 11. 故障排查

| 现象 | 可能原因 / 解决办法 |
| --- | --- |
| `oj_server` 启动失败 | 缺少相对路径下的 `conf/`、`template_html/`、`wwwroot/` 或 `logfiles/` —— 请从 `oj_server/` 目录启动。 |
| `compile_server` 启动失败 | 缺少 `./temp/` 或 `./logfiles/` —— 请创建。端口被占用 —— 换一个端口。 |
| `oj_server/logfiles/*.log` 中出现 MySQL 连接错误 | MySQL 未启动、数据库/用户未创建，或凭据与 `oj_mysqlmodel.hpp` 不一致（`oj` / `oj_client` / `1234` / `127.0.0.1:3306`）。 |
| `judge` 返回空响应体 | 所有编译服务器离线或不可达 —— 检查节点，然后 `kill -QUIT <oj_server_pid>` 恢复，或修正 `service_machine.conf`。 |
| 编译报错提示找不到 `g++` | 编译服务器进程的 `PATH` 中没有 `g++` —— 请安装。 |
| 每次判题都返回 `status == -2` | 编译服务器内部错误 —— 查看其 `logfiles/` 下的日志。 |
| 端口 `8080` 被占用 | 修改 `oj_server.cpp` 中的监听端口并重新编译。 |

## 12. 角色、小组与题目管理（规划中）

> 该特性集为**规划中**（见 [SPEC.md 第14节](SPEC.md#14-待办清单todo)），尚未接入网关。数据库表已在[数据库初始化](#3-数据库初始化)中定义；工作目录 `oj_server/user/` 与 `oj_server/question_manage/` 目前为占位。所有前端页面均使用纯 HTML + CSS + JS。

**角色**

| 角色 | 权限 |
| --- | --- |
| `admin`（管理员） | 修改其他用户角色等级；发布全局题；可见并管理所有题目 |
| `leader`（负责人） | 拥有小组；通过可更换的邀请码拉人；在组内发布题目 |
| `user`（普通用户） | 凭邀请码加入小组；可见全局题 + 所在组题目；提交评测 |

**实现后的典型操作**

1. 注册/种子化第一个 `admin`（见[数据库初始化](#3-数据库初始化)中被注释的 `INSERT`）。
2. `POST /api/register` 创建普通用户；`POST /api/login` 返回 token（受保护调用携带 `Authorization: Bearer <token>`）。
3. 管理员提升用户：`PUT /api/users/{id}/role`。
4. 负责人创建小组：`POST /api/groups` → 返回 `invite_code`；可随时用 `POST /api/groups/{id}/invite` 重置。
5. 用户凭 `POST /api/groups/join` `{"invite_code": "..."}` 加入小组。
6. 管理员/负责人通过 `GET /question_manage` 界面（或直接 `POST /api/questions`）发布题目，`scope = global` 或小组 id；用 `PUT`/`DELETE /api/questions/{id}` 修改/删除。
7. `GET /all_questions`、`GET /question/{id}`、`POST /judge/{id}` 按当前用户的角色与所属小组过滤。

**密码** 以 `Hash(password + salt)` 存储，盐值为注册时的时间戳 —— 不以明文落库。不引入第三方加密库。
