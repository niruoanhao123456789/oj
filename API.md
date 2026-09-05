# oj API Reference

HTTP API reference for the load-balanced online judge. Two services expose HTTP endpoints:

- **`oj_server`** (web gateway, default port `8080`) — public pages and judge submission.
- **`compile_server`** (judge node, port given on the command line) — internal compile-and-run endpoint, **not** meant to be called directly by end users.

> **[English](API.md)** · [简体中文](API.zh.md)

---

## Table of Contents

- [Conventions](#conventions)
- [Roles & Permissions](#roles--permissions)
- [GET /all_questions](#get-all_questions)
- [GET /question/{id}](#get-questionid)
- [POST /judge/{id}](#post-judgeid)
- [POST /compile_and_run (internal)](#post-compile_and_run-internal)
- [Auth & Groups](#auth--groups)
- [Question Management](#question-management)
- [Judge Result Status Codes](#judge-result-status-codes)
- [Static Assets](#static-assets)
- [Notes on Load Balancing](#notes-on-load-balancing)

---

## Conventions

- **Base URL (gateway):** `http://<oj-server-host>:8080`
- **Content-Type for JSON requests/responses:** `application/json; charset=utf-8`
- Question pages return `text/html; charset=utf-8`.
- All question IDs are numeric and are matched by the gateway via the regular expression `(\d+)`.
- Unless stated otherwise, an `id` in a path is the **question id** stored in the database (e.g. `1`, `2`).
- **Authentication:** protected endpoints require a token obtained from `POST /api/login`, sent as `Authorization: Bearer <token>`.

---

## Roles & Permissions

> The role/group system is **wired into the gateway** via the JSON APIs in `oj_server/user/` (`oj_user_model.hpp` + `oj_passwd.hpp`); see [SPEC.md §15](SPEC.md#15-功能落地记录) for the full implementation record.

| Role | Permissions |
| --- | --- |
| `admin` (super admin) | Highest permission: change other users' roles, generate/reset the **leader-registration invite code**, publish **global** questions (visible to everyone), view/manage **all** questions, and create **multiple** groups like a leader. |
| `leader` | Registered as a leader using the admin invite code (registration-time only); creates **multiple** groups, invites others via each group's replaceable invite code, publishes questions **within the group** (visible only to group members). |
| `user` | Joins groups with an invite code; sees global questions + questions of groups they joined; submits code. No question/role management. |

Visibility of questions:

| Question scope | admin | leader | user |
| --- | --- | --- | --- |
| global | ✓ | ✓ | ✓ |
| group (`scope = group_id`) | ✓ (all) | ✓ (own group) | ✓ (joined groups) |

Passwords are stored as `Hash(password + salt)` where the salt is the registration timestamp; never plaintext.

## GET /all_questions

Returns the question list page as HTML.

**Path:** `GET /all_questions`

**Query parameters:** none

**Response**

- `200 OK`
- `Content-Type: text/html; charset=utf-8`

The page renders a table of all questions visible to the current user (global + own groups) — each row links to `GET /question/{id}`. No JSON body is returned. **Login is required** (anonymous visitors are guided to log in); after login, admins see all questions while leaders and regular users are filtered by their groups.

**Example**

```bash
curl http://localhost:8080/all_questions
```

---

## GET /question/{id}

Returns the page for a single question, including its description, difficulty, and a code editor pre-filled with the starter code.

**Path:** `GET /question/{id}`

| Path parameter | Type | Description |
| --- | --- | --- |
| `id` | integer | The numeric question id. |

**Response**

- `200 OK`
- `Content-Type: text/html; charset=utf-8`

The page embeds the question data and pre-code (`answer`) and, when the question has `visible_cases`, renders them as sample cards; submitting from the editor `POST`s to `/judge/{id}` (see below). `header`, `tail` and `hidden_cases` are never exposed to students. **Login is required** (anonymous visitors are guided to log in). Questions the logged-in user cannot see (non-global, not in their groups, and not admin/leader-own-group) return an access-denied notice.

**Example**

```bash
curl http://localhost:8080/question/1
```

---

## POST /judge/{id}

Submits user code for a question and returns the judge result as JSON. The gateway loads the question, builds the full program from `header + code + tail`, and forwards a job to the least-loaded compile server together with the question's hidden cases (`hidden_cases`, `[{input, expected}]`, invisible to students). The compile server compiles once and runs one process per hidden case, feeding each case's `input` to stdin and comparing stdout against `expected`.

**Path:** `POST /judge/{id}`

| Path parameter | Type | Description |
| --- | --- | --- |
| `id` | integer | The numeric question id. |

**Request headers**

- `Content-Type: application/json; charset=utf-8`

**Request body**

| Field | Type | Required | Description |
| --- | --- | --- | --- |
| `code` | string | yes | The user-submitted C++ source code. |
| `input` | string | no | Optional stdin input (only used in the legacy single-run fallback when a question has no hidden cases). |

**Example request**

```bash
curl -X POST http://localhost:8080/judge/1 \
  -H "Content-Type: application/json; charset=utf-8" \
  -d '{"code":"#include <iostream>\nint main(){std::cout<<\"ok\";return 0;}"}'
```

**Response**

- `200 OK`
- `Content-Type: application/json; charset=utf-8`

**Response body**

| Field | Type | Description |
| --- | --- | --- |
| `status` | integer | Judge status code (see [status codes](#judge-result-status-codes)). |
| `reason` | string | Human-readable result description (Chinese). |
| `stdout` | string | Program standard output (batch path returns `""`). |
| `stderr` | string | Program standard error. Present only when `status == 0`. |
| `pass_count` | integer | Passed hidden cases. Present when `status == 0`. |
| `total_count` | integer | Total hidden cases. Present when `status == 0`. |

> **Access & visibility:** only logged-in users may submit a judge (anonymous requests are told to log in first); among logged-in users, only questions visible to them may be judged.
>
> **Result presentation (LeetCode style):** errors are returned as before (`status != 0`). When `status == 0` with `pass_count`/`total_count`, the answer page shows only "Testcases passed: X / Y (percentage)" — it never lists which individual cases passed.

**Example response (success)**

```json
{
  "status": 0,
  "reason": "编译成功",
  "stdout": "",
  "stderr": "",
  "pass_count": 3,
  "total_count": 3
}
```

**Example response (compile error)**

```json
{
  "status": -3,
  "reason": "error: expected ';' before '}' token..."
}
```

---

## POST /compile_and_run (internal)

**Internal endpoint of `compile_server`.** The gateway calls this endpoint; end users should not call it directly.

**Path:** `POST /compile_and_run`

**Request headers**

- `Content-Type: application/json; charset=utf-8`

**Request body**

| Field | Type | Required | Description |
| --- | --- | --- | --- |
| `code` | string | yes | The **full** source code (`header + user code + tail`, `header`/`tail` may be empty). |
| `input` | string | no | Program stdin input (used only when `cases` is absent). |
| `cases` | array | no | Batch judging: `[{input, expected}...]`. When present the code is compiled once and then run once per case; stdout is normalized (trailing whitespace per line and trailing blank lines ignored) and compared with `expected`. |
| `cpu_limit` | integer | yes | CPU time limit in seconds. |
| `mem_limit` | integer | yes | Memory limit in MB. |

**Example request (batch)**

```json
{
  "code": "#include <iostream>\nint main(){ int a,b; std::cin >> a >> b; std::cout << a + b; }",
  "cases": [{"input": "1 2", "expected": "3"}, {"input": "10 20", "expected": "30"}],
  "cpu_limit": 1,
  "mem_limit": 30
}
```

**Response**

- `200 OK` (even when the compiled program fails to run correctly; the outcome is described by `status`)
- `Content-Type: application/json; charset=utf-8`

**Response body** — identical to [`/judge/{id}`](#post-judgeid) (including `pass_count`/`total_count`). When `cases` is absent the legacy single-run fallback applies and a `PASSRATE <passed>/<total>` line printed by the hidden driver is parsed into `pass_count`/`total_count` and stripped from `stdout`.

---

## Auth & Groups

> All endpoints below are **wired into the gateway** (see [SPEC.md §15](SPEC.md#15-功能落地记录)). JSON in / JSON out, `application/json; charset=utf-8`. The corresponding pages are served at `GET /register`, `GET /login`, and `GET /group_manage`.

### POST /api/register

**Request body:** `{"username": "<string>", "password": "<string>", "role": "user" | "leader", "invite_code": "<admin-invite>"}`

Creates a user. `role` is optional and defaults to `user`. Choosing `role = "leader"` requires a valid admin `invite_code` (from `POST /api/admin/invite`); it is validated at registration time only. The first registered user (empty `users` table) automatically becomes `admin`. The password is stored as `Hash(password + salt)` where `salt` is the registration timestamp.

**Response:** `200 OK` — `{"ok": true, "message": "...", "role": "user" | "leader"}`

### POST /api/admin/invite

Regenerates the admin invite code used to register leaders (admin only). The old code is invalidated immediately.

**Response:** `200 OK` — `{"ok": true, "invite_code": "NEWCODE"}`

### POST /api/login

**Request body:** `{"username": "<string>", "password": "<string>"}`

**Response:** `200 OK` — `{"token": "<session-token>", "username": "<...>", "role": "user"}`

The returned token is carried on subsequent protected requests as `Authorization: Bearer <token>`.

### POST /api/groups

Creates a group (leader or admin). Multiple groups are allowed per user.

**Request body:** `{"name": "<string>"}`

**Response:** `200 OK` — `{"ok": true, "group_id": 1, "invite_code": "XXXXXX"}`

### POST /api/groups/join

Joins a group with an invite code (regular user).

**Request body:** `{"invite_code": "XXXXXX"}`

**Response:** `200 OK` — `{"ok": true, "group_id": 1}`

### POST /api/groups/{id}/invite

Regenerates the group's invite code (that group's leader only). The old code is invalidated.

**Response:** `200 OK` — `{"ok": true, "invite_code": "NEWCODE"}`

### PUT /api/users/{id}/role

Changes a user's role (admin only).

**Request body:** `{"role": "admin" | "leader" | "user"}`

**Response:** `200 OK` — `{"ok": true, "message": "..."}`

---

## Question Management

> Endpoints for creating, editing, and deleting questions. The frontend pages are plain **HTML + CSS + JS**, rendered by the `View` class in `oj_server/oj_view.hpp` (ctemplate); the form content is packed into JSON and submitted to the APIs below. The gateway persists into the active model — MySQL `questions` table (with the `scope` column) or the file-based `questions/` model (6-column `questions.list` + `questions/{id}/`).

### POST /api/questions

Creates a question (admin publishes global questions, leader publishes within their own group).

**Request body:**

| Field | Type | Required | Description |
| --- | --- | --- | --- |
| `title` | string | yes | Question title |
| `rank` | string | yes | 简单 / 中等 / 困难 |
| `desc` | string | no | Description |
| `header` | string | no | Hidden prologue code (headers/helpers; used by `function`-mode questions, invisible to students) |
| `answer` | string | no | Starter code in the editor: `function` mode → the functions/class to implement (no `main`); `acm` mode → a complete program with `main()` |
| `tail` | string | no | Hidden trailing code (invisible to students): `function` mode → the hidden `main()` driver that reads input, calls the student function and prints per the output format; `acm` mode usually empty |
| `mode` | string | no | `function` or `acm` (defaults to `acm`) |
| `visible_cases` | array | no | Visible sample cases `[{name?, input, expected, explain?}]` shown to students on the answer page (not judged) |
| `hidden_cases` | array | yes | Hidden judging cases `[{input, expected}...]` (≥1; sole judging basis, never shown to students) |
| `cpu_limit` | integer | yes | Seconds |
| `mem_limit` | integer | yes | MB |
| `scope` | string | yes | `global` or a group id |

> `hidden_cases` (≥1) is required on both create and update. For `mode = "function"`, `tail` must contain the hidden `main()` driver. Questions without `hidden_cases` fall back to the legacy single-run/PASSRATE judging path.

**Response:** `200 OK` — `{"ok": true, "id": 3}`

### PUT /api/questions/{id}

Updates an existing question (admin any; leader only their own group's questions). Body identical to `POST /api/questions`.

**Response:** `200 OK` — `{"ok": true, "message": "..."}`

### DELETE /api/questions/{id}

Deletes a question (admin any; leader only their own group's questions).

**Response:** `200 OK` — `{"ok": true, "message": "..."}`

### Question management pages

| Path | Method | Description |
| --- | --- | --- |
| `GET /question_manage` | GET | Management list page (`question_manage.html`): edit/delete per row, create button on top |
| `GET /question_manage/edit` | GET | Create form page (`question_edit.html`) |
| `GET /question_manage/edit/{id}` | GET | Edit form page (`question_edit.html`, pre-filled) |

---

## Judge Result Status Codes

`status` is set by `compile_run.hpp` (`CompileAndRun::StatusToDesc`):

| `status` | Signal / meaning | `reason` example |
| --- | --- | --- |
| `0` | Compiled and ran successfully | 编译成功 |
| `-1` | Submitted code is empty | 提交代码为空 |
| `-2` | Internal error | 内部错误 |
| `-3` | Compilation failed | (compiler error output) |
| `6` | `SIGABRT` — memory limit exceeded | 内存超过范围 |
| `24` | `SIGXCPU` — CPU time limit exceeded | CPU使用超时 |
| `8` | `SIGFPE` — floating point exception | 浮点数溢出 |
| other | Unknown termination signal | 未知: `<filename>` |

> The compile server runs the binary in a forked child process and returns the terminating signal number (`status & 0x7F`); timeouts and memory overflows surface as `SIGXCPU`/`SIGABRT` respectively.

---

## Static Assets

`oj_server` serves static files from `wwwroot/` (set via `svr.set_base_dir("./wwwroot")`), so `GET /` returns `wwwroot/index.html` and any other files placed under `wwwroot/` are reachable at their relative paths.

---

## Notes on Load Balancing

- The list of compile servers is loaded from `conf/service_machine.conf` (one `ip:port` per line, see [DEPLOY.md](DEPLOY.md)).
- `POST /judge/{id}` picks the compile server with the **lowest current load** (least in-flight requests).
- If a compile server cannot be reached (connection failure), it is moved **offline**; the gateway retries the request with the next-best machine.
- Sending `SIGQUIT` to the `oj_server` process brings **all** machines back online.
- If every machine is offline, `/judge/{id}` returns `200 OK` with an empty JSON body (`""`).
