# oj 接口文档

基于负载均衡的在线判题系统 HTTP 接口参考。系统对外提供两类服务的 HTTP 接口：

- **`oj_server`**（Web 网关，默认端口 `8080`）—— 对外提供页面与判题提交。
- **`compile_server`**（判题节点，端口由命令行参数指定）—— 内部编译运行接口，**不应对终端用户直接开放**。

> [English](API.md) · **[简体中文](API.zh.md)**

---

## 目录

- [通用约定](#通用约定)
- [角色与权限](#角色与权限)
- [GET /all_questions](#get-all_questions)
- [GET /question/{id}](#get-questionid)
- [POST /judge/{id}](#post-judgeid)
- [POST /compile_and_run（内部接口）](#post-compile_and_run内部接口)
- [认证与小组](#认证与小组)
- [题目管理](#题目管理)
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
- **认证：** 受保护接口需要 `POST /api/login` 获取的 token，通过 `Authorization: Bearer <token>` 携带。

---

## 角色与权限

> 角色/小组体系**已接入网关**（实现于 `oj_server/user/` 的 `oj_user_model.hpp` + `oj_passwd.hpp`），完整落地记录见 [SPEC.md 第15节](SPEC.md#15-功能落地记录)。

| 角色 | 权限 |
| --- | --- |
| `admin`（管理员 / 超管） | 最高权限：修改其他用户的角色等级；发布**全局题**（全体可见）；可见并管理**所有**题目 |
| `leader`（负责人） | 拥有自己的小组；通过可更换的邀请码邀请他人加入；在**组内**发布题目（仅本组成员可见） |
| `user`（普通用户） | 凭邀请码加入小组；可见全局题 + 所加入组的题目；可提交评测；无题目/角色管理权限 |

题目可见性：

| 题目范围 | 管理员 | 负责人 | 普通用户 |
| --- | --- | --- | --- |
| 全局题 | ✓ | ✓ | ✓ |
| 组内题（`scope = 小组id`） | ✓（全部） | ✓（本组） | ✓（所在组） |

密码以 `Hash(password + salt)` 存储，盐值为注册时的时间戳，不以明文落库。

## GET /all_questions

返回题目列表页面（HTML）。

**路径：** `GET /all_questions`

**查询参数：** 无

**响应**

- `200 OK`
- `Content-Type: text/html; charset=utf-8`

页面渲染当前登录用户可见题目（全局题 + 所在组题目）的表格，每一行都链接到 `GET /question/{id}`。该接口不返回 JSON 内容。**需登录访问**（未登录返回登录引导页）；登录后管理员见全部题目，负责人与普通用户按所属小组过滤。

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

页面内嵌题目数据与模板代码；在编辑器中提交后，浏览器会向 `/judge/{id}` 发起 `POST`（见下）。**需登录访问**（未登录返回登录引导页）；已登录用户访问不可见的题目（非全局、不在所在组、且非管理员/负责人本组）会返回无权访问提示。

**示例**

```bash
curl http://localhost:8080/question/1
```

---

## POST /judge/{id}

提交用户代码并返回评测结果（JSON）。网关加载题目后拼接完整程序（`header + code + tail`，`header`/`tail` 对学生不可见），并把题目的隐藏判题用例 `hidden_cases`（`[{input, expected}]`）连同任务一起转发给负载最低的编译服务器：编译一次后逐用例写入 stdin 运行，将 stdout 与 `expected` 比对。

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
| `input` | 字符串 | 否 | 传给程序的标准输入（仅题目无隐藏用例时的旧式单次运行回退使用）。 |

**请求示例**

```bash
curl -X POST http://localhost:8080/judge/1 \
  -H "Content-Type: application/json; charset=utf-8" \
  -d '{"code":"#include <iostream>\nint main(){std::cout<<\"ok\";return 0;}"}'
```

**响应**

- `200 OK`
- `Content-Type: application/json; charset=utf-8`

**响应体**

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `status` | 整数 | 评测状态码（见[状态码](#评测状态码)）。 |
| `reason` | 字符串 | 人类可读的结果描述（中文）。 |
| `stdout` | 字符串 | 程序标准输出（批量判题路径返回空串）。 |
| `stderr` | 字符串 | 程序标准错误。仅在 `status == 0` 时返回。 |
| `pass_count` | 整数 | 通过的隐藏用例数。`status == 0` 时返回。 |
| `total_count` | 整数 | 隐藏用例总数。`status == 0` 时返回。 |

> **访问与可见性约束：** 仅登录用户可提交评测（未登录返回提示先登录）；登录后仅允许对当前用户可见的题目提交评测。
>
> **结果呈现（仿 LeetCode）：** `status != 0` 时按现有错误返回展示；`status == 0` 且含 `pass_count`/`total_count` 时，答题页只显示「测试用例通过: X / Y（百分比）」，**不展示具体通过的案例明细**。

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
| `code` | 字符串 | 是 | **完整的**源码（`header + 用户代码 + tail`，`header`/`tail` 可为空）。 |
| `input` | 字符串 | 否 | 程序标准输入（仅无 `cases` 时使用）。 |
| `cases` | 数组 | 否 | **批量判题**：`[{input, expected}...]`。提供时编译一次、逐用例写入 stdin 运行，并将规范化后的 stdout（忽略行尾空白与文末空行）与 `expected` 比对。 |
| `cpu_limit` | 整数 | 是 | CPU 时间限制（秒）。 |
| `mem_limit` | 整数 | 是 | 内存限制（MB）。 |

**请求示例（批量判题）**

```json
{
  "code": "#include <iostream>\nint main(){ int a,b; std::cin >> a >> b; std::cout << a + b; }",
  "cases": [{"input": "1 2", "expected": "3"}, {"input": "10 20", "expected": "30"}],
  "cpu_limit": 1,
  "mem_limit": 30
}
```

**响应**

- `200 OK`（即使被评测程序运行出错也返回 200，结果由 `status` 字段描述）
- `Content-Type: application/json; charset=utf-8`

**响应体** —— 与 [`/judge/{id}`](#post-judgeid) 相同（含 `pass_count`/`total_count`）。无 `cases` 时回退到旧式单次运行：若隐藏驱动在 stdout 末尾输出 `PASSRATE <passed>/<total>` 行，则解析为 `pass_count`/`total_count` 并从用户可见 stdout 中剔除。

---

## 认证与小组

> 以下接口**均已接入网关**（完整落地记录见 [SPEC.md 第15节](SPEC.md#15-功能落地记录)）。JSON 进 / JSON 出，`application/json; charset=utf-8`。对应页面在 `GET /register`、`GET /login`、`GET /group_manage` 提供。

### POST /api/register

**请求体：** `{"username": "<字符串>", "password": "<字符串>", "role": "user" | "leader", "invite_code": "<管理员邀请码>"}`

创建用户。`role` 可选，默认 `user`；选择 `role = "leader"` 时必须提供有效的管理员邀请码 `invite_code`（来自 `POST /api/admin/invite`，仅在注册时校验）。`users` 表为空时首个注册用户自动成为 `admin`。密码以 `Hash(password + salt)` 存储，`salt` 为注册时的时间戳。

**响应：** `200 OK` —— `{"ok": true, "message": "...", "role": "user" | "leader"}`

### POST /api/admin/invite

管理员重新生成用于注册负责人的管理员邀请码（仅管理员）。旧码立即失效。

**响应：** `200 OK` —— `{"ok": true, "invite_code": "新邀请码"}`

### POST /api/login

**请求体：** `{"username": "<字符串>", "password": "<字符串>"}`

**响应：** `200 OK` —— `{"token": "<会话token>", "username": "<...>", "role": "user"}`

返回的 token 需在后续受保护请求中以 `Authorization: Bearer <token>` 携带。

### POST /api/groups

创建小组（负责人或管理员，可创建多个）。

**请求体：** `{"name": "<字符串>"}`

**响应：** `200 OK` —— `{"ok": true, "group_id": 1, "invite_code": "XXXXXX"}`

### POST /api/groups/join

凭邀请码加入小组（普通用户）。

**请求体：** `{"invite_code": "XXXXXX"}`

**响应：** `200 OK` —— `{"ok": true, "group_id": 1}`

### POST /api/groups/{id}/invite

重置该小组的邀请码（仅该组负责人）。旧码失效。

**响应：** `200 OK` —— `{"ok": true, "invite_code": "NEWCODE"}`

### PUT /api/users/{id}/role

修改用户角色等级（仅管理员）。

**请求体：** `{"role": "admin" | "leader" | "user"}`

**响应：** `200 OK` —— `{"ok": true, "message": "..."}`

---

## 题目管理

> 新增/修改/删除题目的接口。前端页面均为纯 **HTML + CSS + JS**，由 `oj_server/oj_view.hpp` 中的 `View` 类（ctemplate）渲染；表单内容由 JS 打包为 JSON 提交到下列接口。网关按当前启用的模型持久化 —— MySQL `questions` 表（含 `scope` 列）或基于文件的 `questions/` 模型（6 列 `questions.list` + `questions/{id}/`）。

### POST /api/questions

发布题目（管理员发布全局题，负责人在自己组内发布）。

**请求体：**

| 字段 | 类型 | 必填 | 说明 |
| --- | --- | --- | --- |
| `title` | 字符串 | 是 | 题目标题 |
| `rank` | 字符串 | 是 | 简单 / 中等 / 困难 |
| `desc` | 字符串 | 否 | 题目描述 |
| `header` | 字符串 | 否 | 隐藏前置代码（头文件/辅助类型，学生不可见；函数接口模式使用） |
| `answer` | 字符串 | 否 | 在线编辑器预填代码：`function` 模式只放待实现函数/类；`acm` 模式放含 `main()` 的完整程序 |
| `tail` | 字符串 | 否 | 隐藏尾部代码（学生不可见）：`function` 模式放读 stdin → 调 answer 函数 → 按要求输出的隐藏 `main()` 驱动；`acm` 模式留空 |
| `mode` | 字符串 | 否 | `function` 或 `acm`（缺省 `acm`），负责人/管理员自选 |
| `visible_cases` | 数组 | 否 | 显式样例 `[{name?, input, expected, explain?}]`，答题页展示、不判题 |
| `hidden_cases` | 数组 | 是 | 隐藏判题用例 `[{input, expected}...]`（至少 1 个，判题唯一依据，学生不可见） |
| `cpu_limit` | 整数 | 是 | 秒 |
| `mem_limit` | 整数 | 是 | MB |
| `scope` | 字符串 | 是 | `global` 或小组 id |

> `hidden_cases`（≥1）在新增与修改时均为**必填**。`mode = "function"` 时 `tail` 需含隐藏 `main()` 驱动。无 `hidden_cases` 的旧题回退到旧式单次运行/PASSRATE 判题。

**响应：** `200 OK` —— `{"ok": true, "id": 3}`

### PUT /api/questions/{id}

修改已有题目（管理员任意；负责人仅本组题目）。请求体与 `POST /api/questions` 相同。

**响应：** `200 OK` —— `{"ok": true, "message": "..."}`

### DELETE /api/questions/{id}

删除题目（管理员任意；负责人仅本组题目）。

**响应：** `200 OK` —— `{"ok": true, "message": "..."}`

### 题目管理页面

| 路径 | 方法 | 说明 |
| --- | --- | --- |
| `GET /question_manage` | GET | 管理列表页（`question_manage.html`）：每行修改/删除、顶部新增 |
| `GET /question_manage/edit` | GET | 新增表单页（`question_edit.html`） |
| `GET /question_manage/edit/{id}` | GET | 编辑表单页（`question_edit.html`，预填） |

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
