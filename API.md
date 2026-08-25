# oj API Reference

HTTP API reference for the load-balanced online judge. Two services expose HTTP endpoints:

- **`oj_server`** (web gateway, default port `8080`) — public pages and judge submission.
- **`compile_server`** (judge node, port given on the command line) — internal compile-and-run endpoint, **not** meant to be called directly by end users.

> **[English](API.md)** · [简体中文](API.zh.md)

---

## Table of Contents

- [Conventions](#conventions)
- [GET /all_questions](#get-all_questions)
- [GET /question/{id}](#get-questionid)
- [POST /judge/{id}](#post-judgeid)
- [POST /compile_and_run (internal)](#post-compile_and_run-internal)
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

---

## GET /all_questions

Returns the question list page as HTML.

**Path:** `GET /all_questions`

**Query parameters:** none

**Response**

- `200 OK`
- `Content-Type: text/html; charset=utf-8`

The page renders a table of all questions — each row links to `GET /question/{id}`. No JSON body is returned.

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

The page embeds the question data and pre-code; submitting from the editor `POST`s to `/judge/{id}` (see below).

**Example**

```bash
curl http://localhost:8080/question/1
```

---

## POST /judge/{id}

Submits user code for a question and returns the judge result as JSON. The gateway builds the full program from `header + code + tail`, selects the least-loaded compile server, and forwards the job.

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
| `input` | string | no | Optional stdin input passed to the program (defaults to empty). |

**Example request**

```bash
curl -X POST http://localhost:8080/judge/1 \
  -H "Content-Type: application/json; charset=utf-8" \
  -d '{"code":"#include <iostream>\nint main(){std::cout<<\"ok\";return 0;}","input":""}'
```

**Response**

- `200 OK`
- `Content-Type: application/json; charset=utf-8`

**Response body**

| Field | Type | Description |
| --- | --- | --- |
| `status` | integer | Judge status code (see [status codes](#judge-result-status-codes)). |
| `reason` | string | Human-readable result description (Chinese). |
| `stdout` | string | Program standard output. Present only when `status == 0`. |
| `stderr` | string | Program standard error. Present only when `status == 0`. |

**Example response (success)**

```json
{
  "status": 0,
  "reason": "编译成功",
  "stdout": "ok",
  "stderr": ""
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
| `code` | string | yes | The **full** source code (header + user code + tail). |
| `input` | string | no | Program stdin input (defaults to empty). |
| `cpu_limit` | integer | yes | CPU time limit in seconds. |
| `mem_limit` | integer | yes | Memory limit in MB. |

**Example request**

```json
{
  "code": "#include <iostream>\nint main(){ int n; std::cin >> n; std::cout << n * 2; }",
  "input": "21",
  "cpu_limit": 1,
  "mem_limit": 30
}
```

**Response**

- `200 OK` (even when the compiled program fails to run correctly; the outcome is described by `status`)
- `Content-Type: application/json; charset=utf-8`

**Response body** — identical to [`/judge/{id}`](#post-judgeid).

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
