# oj — Web UI 自动化测试（tests/autoweb）

针对 [SPEC.md](../../SPEC.md) 与 [API.md](../../API.md) 描述的页面与接口，设计的一套**基于 HTTP 的 Web/UI 功能测试**。
它按真实浏览器会执行的流程去驱动网关：

- 直接访问 HTML 页面并断言页面内容 / 表单 / 关键元素（服务端渲染的部分，如登录表单、题目列表、编辑器预填 `answer`、示例数据 `visible_cases`、编辑页 `qdata` 等）；
- 以页面背后的 JSON 接口完成登录、注册、建组、发布题目、提交评测等 UI 操作（前端本身也是用原生 XHR/fetch 调这些接口）。

> 说明：本套件不依赖浏览器/Selenium（当前无可用浏览器环境），面向“页面 + 接口”两层做黑盒验证。
> 若以后需要真实浏览器回归（校验 ace 编辑器、动态导航等纯 JS 渲染），可在同目录增加基于
> Selenium/Playwright 的用例，复用本目录的账号与数据约定即可。

## 环境与运行

- Python ≥ 3.8，依赖 `requests`（`pip install requests`）。
- 目标实例默认 **`http://1.12.254.247:8080/`**（见 [config.py](config.py)），可用环境变量覆盖：
  - `OJ_BASE_URL`：网关地址（默认即远端）。
  - `OJ_ADMIN_USER` / `OJ_ADMIN_PASS`、`OJ_LEADER_USER` / `OJ_LEADER_PASS`、
    `OJ_USER_USER` / `OJ_USER_PASS`：预置账号（默认与 `tests/example/mysql_test.cpp` 种子一致：admin/leader1/user1/user2/guest）。

运行全部用例：

```bash
cd tests/autoweb
python3 runner.py                       # 使用默认 base url
OJ_BASE_URL=http://127.0.0.1:8080 python3 runner.py   # 本地实例
python3 runner.py --filter judge        # 只运行名字含 judge 的用例
python3 runner.py --base http://127.0.0.1:8080 --filter auth
```

退出码：全部通过为 `0`，任一失败/出错为 `1`。

## 前置条件

- 目标实例已用 `tests/example/mysql_test` 的种子初始化（含演示题 1..5、admin/leader1/leader2/user1/user2/guest 等）。
  种子内容覆盖即可让“依赖演示题”的用例生效；缺失时相应用例会 **跳过（SKIP）** 而不是失败。
- 用例会自动创建少量 **`ui_<时间戳>_*`** 用户用于注册/角色验证，并会清理自己创建的题目与小组；
  创建的测试用户无法通过 API 删除，会在库中保留（不影响业务）。

## 用例与文档映射

| 模块 | 覆盖（对应文档） |
| --- | --- |
| `test_auth_pages.py` | 首页 / 登录页 / 注册页的页面结构；未登录访问受保护页返回“登录引导门”而非真实页（SPEC §1/§4.5、§5.1/§5.2/§5.16） |
| `test_auth_flow.py` | 注册（普通用户/负责人+管理员邀请码）、重复用户名拒绝、登录成功/失败、token 鉴权、登出后 token 失效（SPEC §4.4、API §5.5/§5.6 与 API.md Auth） |
| `test_question_browse.py` | 题目列表、答题页编辑器预填 `answer`、示例卡 `visible_cases`、隐藏项（`header`/`tail`/`hidden_cases`）不泄漏（SPEC §4.1/§5.2） |
| `test_judge_flow.py` | 提交判题：AC/WA/编译错误；函数接口模式与 ACM 模式逐用例判定 `pass_count/total_count`（SPEC §5.3/§6、§9） |
| `test_visibility.py` | 普通用户/负责人/管理员的题目可见性与管理页权限门控（SPEC §4.5/§5.1；管理页需 admin/leader） |
| `test_question_admin.py` | 负责人/管理员发布题目的两种模式（函数接口/ACM）、编辑页预填 `mode+用例`、修改、删除（SPEC §5.13~§5.16、API.md Question Management） |
| `runner.py` | 用例发现与执行、结果汇总、`--filter`/`--base` |

## 目录结构

```
tests/autoweb/
├── README.md
├── config.py          # 默认 base url、超时、预置账号（可用环境变量覆盖）
├── ojui.py            # 轻量 HTTP 客户端 + 页面/JSON 断言工具
├── runner.py          # 发现 test_*.py 中的 test_* 并执行、输出 PASS/FAIL/SKIP/ERROR
├── test_auth_pages.py
├── test_auth_flow.py
├── test_question_browse.py
├── test_judge_flow.py
├── test_visibility.py
└── test_question_admin.py
```
