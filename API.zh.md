# oj 接口文档

基于负载均衡的在线判题系统 HTTP 接口参考。系统对外提供两类服务的 HTTP 接口：

- **`oj_server`**（Web 网关，默认端口 `8080`）—— 对外提供页面与判题提交。
- **`compile_server`**（判题节点，端口由命令行参数指定）—— 内部编译运行接口，**不应对终端用户直接开放**。

> [English](API.md) · **[简体中文](API.zh.md)**

---

## 目录

- [通用约定](#通用约定)
- [GET /all_questions](#get-all_questions)
- [GET /question/{id}](#get-questionid)
- [POST /judge/{id}](#post-judgeid)
- [POST /compile_and_run（内部接口）](#post-compile_and_run内部接口)
- [评测状态码](#评测状态码)
- [静态资源](#静态资源)
- [负载均衡相关说明](#负载均衡相关说明)

---

## 通用约定

- **网关基础地址（Base URL）：** `http://<oj-server-host>:8080`
- JSON 请求/响应均为 `application/json; charset=utf-8`。
- 题目页面返回 `text/html; charset=utf-8`。
- 题目编号为数字，网关使用正则 `(\d+)` 进行匹配。
- 除非特别说明，路径中的 `id` 均指**数据库中的题目编号**（如 `1`、`2`）。

---

## GET /all_questions

返回题目列表页面（HTML）。

**路径：** `GET /all_questions`

**查询参数：** 无

**响应**

- `200 OK`
- `Content-Type: text/html; charset=utf-8`

页面渲染所有题目的表格，每一行都链接到 `GET /question/{id}`。该接口不返回 JSON 内容。

**示例**

```bash
curl http://localhost:8080/all_questions
```

---

## GET /question/{id}

返回单道题目的页面，包含题目描述、难度，以及预置了模板代码的代码编辑器。

**路径：** `GET /question/{id}`

| 路径参数 | 类型 | 说明 |
| --- | --- | --- |
| `id` | 整数 | 题目编号。 |

**响应**

- `200 OK`
- `Content-Type: text/html; charset=utf-8`

页面内嵌题目数据与模板代码；在编辑器中提交后，浏览器会向 `/judge/{id}` 发起 `POST`（见下）。

**示例**

```bash
curl http://localhost:8080/question/1
```

---

## POST /judge/{id}

提交用户代码并返回评测结果（JSON）。网关会先拼接完整程序（`header + code + tail`），选择负载最低的编译服务器，并转发评测任务。

**路径：** `POST /judge/{id}`

| 路径参数 | 类型 | 说明 |
| --- | --- | --- |
| `id` | 整数 | 题目编号。 |

**请求头**

- `Content-Type: application/json; charset=utf-8`

**请求体**

| 字段 | 类型 | 必填 | 说明 |
| --- | --- | --- | --- |
| `code` | 字符串 | 是 | 用户提交的 C++ 源码。 |
| `input` | 字符串 | 否 | 传给程序的标准输入（缺省为空）。 |

**请求示例**

```bash
curl -X POST http://localhost:8080/judge/1 \
  -H "Content-Type: application/json; charset=utf-8" \
  -d '{"code":"#include <iostream>\nint main(){std::cout<<\"ok\";return 0;}","input":""}'
```

**响应**

- `200 OK`
- `Content-Type: application/json; charset=utf-8`

**响应体**

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `status` | 整数 | 评测状态码（见[状态码](#评测状态码)）。 |
| `reason` | 字符串 | 人类可读的结果描述（中文）。 |
| `stdout` | 字符串 | 程序标准输出。仅在 `status == 0` 时返回。 |
| `stderr` | 字符串 | 程序标准错误。仅在 `status == 0` 时返回。 |

**成功响应示例**

```json
{
  "status": 0,
  "reason": "编译成功",
  "stdout": "ok",
  "stderr": ""
}
```

**编译失败响应示例**

```json
{
  "status": -3,
  "reason": "error: expected ';' before '}' token..."
}
```

---

## POST /compile_and_run（内部接口）

**`compile_server` 的内部接口。** 由网关调用，终端用户不应直接调用。

**路径：** `POST /compile_and_run`

**请求头**

- `Content-Type: application/json; charset=utf-8`

**请求体**

| 字段 | 类型 | 必填 | 说明 |
| --- | --- | --- | --- |
| `code` | 字符串 | 是 | **完整的**源码（header + 用户代码 + tail）。 |
| `input` | 字符串 | 否 | 程序标准输入（缺省为空）。 |
| `cpu_limit` | 整数 | 是 | CPU 时间限制（秒）。 |
| `mem_limit` | 整数 | 是 | 内存限制（MB）。 |

**请求示例**

```json
{
  "code": "#include <iostream>\nint main(){ int n; std::cin >> n; std::cout << n * 2; }",
  "input": "21",
  "cpu_limit": 1,
  "mem_limit": 30
}
```

**响应**

- `200 OK`（即使被评测程序运行出错也返回 200，结果由 `status` 字段描述）
- `Content-Type: application/json; charset=utf-8`

**响应体** —— 与 [`/judge/{id}`](#post-judgeid) 相同。

---

## 评测状态码

`status` 由 `compile_run.hpp`（`CompileAndRun::StatusToDesc`）设置：

| `status` | 信号 / 含义 | `reason` 示例 |
| --- | --- | --- |
| `0` | 编译成功且运行正常 | 编译成功 |
| `-1` | 提交代码为空 | 提交代码为空 |
| `-2` | 内部错误 | 内部错误 |
| `-3` | 编译失败 | （编译器错误输出） |
| `6` | `SIGABRT` —— 超过内存限制 | 内存超过范围 |
| `24` | `SIGXCPU` —— 超过 CPU 时间限制 | CPU使用超时 |
| `8` | `SIGFPE` —— 浮点异常 | 浮点数溢出 |
| 其他 | 未知终止信号 | 未知: `<filename>` |

> 编译服务器在 fork 出的子进程中运行二进制，并返回导致进程终止的信号编号（`status & 0x7F`）；超时与内存溢出分别以 `SIGXCPU` / `SIGABRT` 体现。

---

## 静态资源

`oj_server` 以 `svr.set_base_dir("./wwwroot")` 的方式托管静态文件：`GET /` 会返回 `wwwroot/index.html`，`wwwroot/` 下的其他文件也可以按相对路径访问。

---

## 负载均衡相关说明

- 编译服务器列表从 `conf/service_machine.conf` 加载（每行一个 `ip:port`，见 [DEPLOY.md](DEPLOY.md)）。
- `POST /judge/{id}` 会选出**当前负载最小**（进行中请求数最少）的编译服务器。
- 若某编译服务器无法连接（连接失败），它会被移入**离线**状态；网关会改用负载次优的机器重试该请求。
- 向 `oj_server` 进程发送 `SIGQUIT` 信号可将**所有**机器恢复上线。
- 若所有机器均离线，`/judge/{id}` 会返回 `200 OK`，且响应体为空字符串（`""`）。
