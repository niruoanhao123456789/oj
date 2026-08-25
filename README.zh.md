# oj — 基于负载均衡的在线判题系统（Online Judge）

一个以**负载均衡判题架构**为核心的在线判题（OJ）平台。用户可以在线浏览题库、在网页编辑器中编写 C++ 题解，并实时获得评测结果。每次提交都会被调度到当前**负载最低**的编译运行节点；故障节点会被自动下线，并可按需一键恢复。

> [English](README.md) · **[简体中文](README.zh.md)**

---

## 目录

- [所用技术](#所用技术)
- [环境依赖](#环境依赖)
- [功能特性](#功能特性)
- [系统架构](#系统架构)
- [目录结构](#目录结构)
- [快速开始](#快速开始)
- [构建与打包](#构建与打包)
- [文档索引](#文档索引)
- [许可证](#许可证)

---

## 所用技术: 
1. C++ STL 标准库
2. Boost 准标准库(字符串切割) 
3. cpp-httplib 第三方开源网络库
4. ctemplate 第三方开源前端网页渲染库
5. jsoncpp 第三方开源序列化、反序列化库
6. 负载均衡设计
7. 多进程、多线程
8. MySQL C connect   
9. Ace前端在线编辑器

## 环境依赖

- 支持 C++20 的 Linux 系统（推荐 `g++` ≥ 10），需要 `make` 与 `g++`。
- MySQL 服务（推荐 MySQL/MariaDB 8.x），可于 `127.0.0.1:3306` 访问。

| 依赖 | 包名（Debian/Ubuntu） | 用途 |
| --- | --- | --- |
| g++ | `g++` | 编译判题源码（`-std=c++20`）与构建本项目 |
| make | `make` | 通过各服务自带 `makefile` 构建 |
| jsoncpp | `libjsoncpp-dev` | JSON 序列化 / 反序列化（`-ljsoncpp`） |
| cpp-httplib | `libcpp-httplib-dev` | HTTP 网络库（`-lcpp-httplib`） |
| ctemplate | `libctemplate-dev` | 模板化 HTML 渲染（`-lctemplate`） |
| MySQL 客户端 | `libmysqlclient-dev` | MySQL C API（`-lmysqlclient`） |
| Boost（头文件） | `libboost-dev` | 字符串切割工具 |
| pthread | glibc | 多线程（`-lpthread`） |

> 更详细的部署步骤（数据库初始化、构建与启动）请参考 [DEPLOY.zh.md](DEPLOY.zh.md)。

## 功能特性

- **负载均衡判题** —— 网关实时跟踪每个编译运行后端节点的负载，将请求调度到负载最低的机器。
- **C++ 实时评测** —— 在网页编辑器提交代码，数秒内返回评测结果（使用 `g++ -std=c++20` 编译）。
- **资源限制** —— 通过 `setrlimit` 在 fork 出的子进程中强制限制每道题的 CPU 时间与内存上限。
- **故障容错** —— 不可达的编译服务器会被自动下线；向网关发送 `SIGQUIT` 信号即可将所有节点恢复上线。
- **MySQL 题库** —— 题目元数据、隐藏测试代码与限制均存储在 `questions` 表中（同时附带一套基于文件的模型作为替代方案）。
- **模板化 HTML 渲染** —— 使用 ctemplate 库根据 `template_html/` 下的模板渲染页面。
- **结构化日志** —— 异步、按时间滚动的日志器，日志写入 `logfiles/`。

## 系统架构

系统分为两个服务：

```
                    ┌──────────────────────────────────────────────┐
                    │               oj_server（网关）               │
                    │  • 提供 HTML 页面          （端口 8080）       │
                    │  • 从 MySQL 读取题目信息                      │
                    │  • 对 /judge/{id} 请求做负载均衡              │
                    └───────┬──────────────┬──────────────┬────────┘
                            │              │              │
                HTTP POST /compile_and_run │              │
                            ▼              ▼              ▼
              ┌──────────────────┐  ┌──────────────┐  ┌──────────────┐
              │ compile_server   │  │ compile_server│  │ compile_server│
              │ （负载最低）      │  │   （节点 2）  │  │   （节点 3）  │
              │ 编译 + 运行      │  │              │  │              │
              └──────────────────┘  └──────────────┘  └──────────────┘
```

**一次判题请求的完整流程：**

1. 用户打开 `/question/{id}` 并点击「提交评测」。
2. 浏览器向 `oj_server` 的 `/judge/{id}` 发起 `POST`，请求体为 `{"code": ...}`。
3. `oj_server` 从 MySQL 加载题目，并拼接出完整源码：`header.cpp`（隐藏头）+ 用户代码 + `tail.cpp`（测试用例）。
4. 负载均衡器选择**当前负载最小**的编译服务器，将 `{"code", "input", "cpu_limit", "mem_limit"}` 转发到其 `/compile_and_run` 接口。
5. 编译服务器用 `g++` 编译，并在 `setrlimit` 的 CPU/内存限制下运行二进制，返回 JSON 评测结果。
6. 结果回传给浏览器，渲染为 `AC / WA / TLE` 等反馈。

## 目录结构

```
.
├── common/                     # 公共代码
│   ├── Util.hpp                # 文件 / 路径 / 时间 / 字符串工具
│   └── log/                    # 异步滚动日志库
├── compile_server/             # 判题后端节点（每个端口一个）
│   ├── compile_server.cpp      # HTTP 入口，POST /compile_and_run
│   ├── compile_run.hpp         # 编译 + 运行编排
│   ├── compiler.hpp            # g++ 封装
│   ├── runner.hpp              # 在 CPU/内存限制下运行二进制
│   ├── makefile
│   └── temp/                   # 临时源文件 / 可执行文件（已 git 忽略）
├── oj_server/                  # Web 网关（端口 8080）
│   ├── oj_server.cpp           # HTTP 路由 + 静态文件服务
│   ├── oj_control.hpp          # 负载均衡 + 判题编排
│   ├── oj_mysqlmodel.hpp       # MySQL 题目模型（当前使用）
│   ├── oj_filemodel.hpp        # 文件题目模型（替代方案）
│   ├── oj_view.hpp             # ctemplate HTML 渲染
│   ├── conf/service_machine.conf   # 编译服务器端点列表
│   ├── template_html/          # ctemplate 模板
│   ├── wwwroot/                # 静态资源（落地页等）
│   ├── questions/              # 文件模型的示例题目
│   └── makefile
├── makefile                    # 顶层构建 / 打包入口
├── output/                     # `make output` 生成的发布包
├── README.md                   # 本文档（中文）/ README.md（英文）
├── API.md                      # HTTP 接口文档（中文）/ API.zh.md（英文）
├── DEPLOY.md                   # 部署文档（中文）/ DEPLOY.zh.md（英文）
└── LICENSE
```

## 快速开始

完整步骤请参考 [DEPLOY.zh.md](DEPLOY.zh.md)（或 [DEPLOY.md](DEPLOY.md)）。简而言之：

```bash
# 1. 创建 MySQL 数据库与 oj_client 用户（详见 DEPLOY.zh.md）
# 2. 编译两个服务（顶层 makefile）
make

# 3. 启动编译服务器（每个节点需在各自的工作目录下运行）
./compile_server 8081
./compile_server 8082
./compile_server 8083

# 4. 在 oj_server/ 目录下启动网关
cd ../oj_server && ./oj_server

# 5. 浏览器打开
```

## 构建与打包

顶层 `makefile` 统一管理两个服务的构建与打包：

| 目标 | 用途 |
| --- | --- |
| `make` / `make all` | 在各自目录下编译 `compile_server` 与 `oj_server`。 |
| `make output` | 将完整的、可独立运行的程序打包到 `output/` 目录，用于发布或发送给他人。 |
| `make clean` | 清理两个服务的构建产物，并删除整个 `output/` 目录。 |

`make output` 生成的结构如下：

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

该发布包可独立运行：将 `output/` 拷贝到其他主机并配置好 MySQL 后，分别在 `output/compile_server/` 与 `output/oj_server/` 目录下启动节点和网关即可（详见 [DEPLOY.zh.md](DEPLOY.zh.md)）。

## 文档索引

| 文档 | English | 简体中文 |
| --- | --- | --- |
| 接口文档 | [API.md](API.md) | [API.zh.md](API.zh.md) |
| 部署文档 | [DEPLOY.md](DEPLOY.md) | [DEPLOY.zh.md](DEPLOY.zh.md) |

## 许可证

本项目遵循 [LICENSE](LICENSE) 文件中的许可条款。
