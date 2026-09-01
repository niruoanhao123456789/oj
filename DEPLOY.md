# oj Deployment Guide

This guide walks through deploying the load-balanced online judge, from prerequisites and database setup through building, running, and verifying both services.

> **[English](DEPLOY.md)** · [简体中文](DEPLOY.zh.md)

---

## Table of Contents

- [1. Architecture Overview](#1-architecture-overview)
- [2. Prerequisites](#2-prerequisites)
- [3. Database Setup](#3-database-setup)
- [4. Build the Services](#4-build-the-services)
- [5. Configure the Compile Servers](#5-configure-the-compile-servers)
- [6. Start the Compile Servers](#6-start-the-compile-servers)
- [7. Start the OJ Gateway](#7-start-the-oj-gateway)
- [8. Verify the Deployment](#8-verify-the-deployment)
- [9. Adding New Questions](#9-adding-new-questions)
- [10. Operations & Maintenance](#10-operations--maintenance)
- [11. Troubleshooting](#11-troubleshooting)
- [12. Roles, Groups & Question Management](#12-roles-groups--question-management)

---

## 1. Architecture Overview

Two kinds of processes must be running:

- **Compile servers (judge nodes)** — one `compile_server` process per node, each listening on its own port. They receive judge jobs via `POST /compile_and_run`, compile with `g++`, run the binary under CPU/memory limits, and return JSON results.
- **OJ gateway** — one `oj_server` process (port `8080`) that serves pages and load-balances `/judge/{id}` requests across the compile servers.

> The gateway reads the compile-server list from `oj_server/conf/service_machine.conf` at startup. All compile servers **must** be running before the gateway handles judge requests.

## 2. Prerequisites

- A Linux system with a C++20-capable toolchain (`g++` ≥ 10 recommended).
- MySQL Server (tested with MySQL/MariaDB, 8.x recommended) running and reachable on `127.0.0.1:3306`.
- The following development libraries:

| Library | Package (Debian/Ubuntu) | Linked with |
| --- | --- | --- |
| jsoncpp | `libjsoncpp-dev` | `-ljsoncpp` |
| cpp-httplib | `libcpp-httplib-dev` | `-lcpp-httplib` |
| ctemplate | `libctemplate-dev` | `-lctemplate` |
| MySQL client | `libmysqlclient-dev` | `-lmysqlclient` |
| Boost (headers) | `libboost-dev` | — |
| gtest | `libgtest-dev` | `-lgtest -lgtest_main` (unit tests only) |
| pthread | glibc | `-lpthread` |

Example install (Debian/Ubuntu):

```bash
sudo apt update
sudo apt install -y g++ make mysql-server libjsoncpp-dev libcpp-httplib-dev \
    libctemplate-dev libmysqlclient-dev libboost-dev libgtest-dev
```

## 3. Database Setup

Start MySQL and run the following SQL to create the database, table, and the application user used by the gateway (`oj_client` / `1234`, host `127.0.0.1:3306`):

> The same statements are shipped as runnable scripts in the repo: `oj_server/database/oj.sql` (fresh install) and `oj_server/database/upgrade.sql` (adds the `scope` column and the `users`/`groups`/`group_members` tables to an existing install).

```sql
CREATE DATABASE IF NOT EXISTS oj DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_general_ci;
USE oj;

CREATE TABLE IF NOT EXISTS questions (
    id         INT PRIMARY KEY AUTO_INCREMENT,
    title      VARCHAR(255) NOT NULL,
    rank       VARCHAR(16)  NOT NULL,   -- 简单 / 中等 / 困难 (Easy / Normal / Hard)
    desc_text  TEXT,
    header     TEXT,                    -- hidden prologue, appended before user code
    answer     TEXT,                    -- starter code shown in the editor
    tail       TEXT,                    -- hidden test cases (sole judging basis), appended after user code; invisible to users
    cpu_limit  INT DEFAULT 1,           -- seconds
    mem_limit  INT DEFAULT 30,          -- MB
    scope      VARCHAR(16) NOT NULL DEFAULT 'global'   -- global or a group id
);

-- Users / roles / groups (wired into the gateway, see SPEC.md §15)
CREATE TABLE IF NOT EXISTS users (
    id            INT PRIMARY KEY AUTO_INCREMENT,
    username      VARCHAR(64) NOT NULL UNIQUE,
    password_hash VARCHAR(64) NOT NULL,           -- Hash(password + salt)
    salt          VARCHAR(20) NOT NULL,           -- registration timestamp
    role          VARCHAR(16) NOT NULL DEFAULT 'user',  -- admin / leader / user
    created_at    DATETIME
);

CREATE TABLE IF NOT EXISTS groups (
    id          INT PRIMARY KEY AUTO_INCREMENT,
    name        VARCHAR(64) NOT NULL,
    owner_id    INT NOT NULL,                     -- leader user id
    invite_code VARCHAR(32) NOT NULL UNIQUE,
    created_at  DATETIME
);

CREATE TABLE IF NOT EXISTS group_members (
    group_id INT NOT NULL,
    user_id  INT NOT NULL,
    PRIMARY KEY (group_id, user_id)
);

-- Admin invite code used to register leaders (single current code, id = 1).
-- The code is generated/reset at runtime by admins via POST /api/admin/invite.
CREATE TABLE IF NOT EXISTS admin_invite (
    id         INT PRIMARY KEY,
    code       VARCHAR(32) NOT NULL,
    created_at DATETIME
);

-- No manual admin seed needed: the first user registered via POST /api/register
-- automatically becomes admin (empty users table bootstrap).

CREATE USER IF NOT EXISTS 'oj_client'@'localhost' IDENTIFIED BY '1234';
CREATE USER IF NOT EXISTS 'oj_client'@'127.0.0.1' IDENTIFIED BY '1234';
GRANT ALL PRIVILEGES ON oj.* TO 'oj_client'@'localhost';
GRANT ALL PRIVILEGES ON oj.* TO 'oj_client'@'127.0.0.1';
FLUSH PRIVILEGES;
```

> **Upgrade note:** for an existing install, add the scope column with:
> `ALTER TABLE questions ADD COLUMN scope VARCHAR(16) NOT NULL DEFAULT 'global' AFTER mem_limit;`

> **Column mapping note:** the gateway reads columns positionally via `SELECT *`:
> `id, title, rank, desc, header, answer, tail, cpu_limit, mem_limit, scope`.
> If you prefer a different column order or names, adjust the table to match the order used by `oj_server/oj_mysqlmodel.hpp`.

Insert a sample question (palindrome number):

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

## 4. Build the Services

Each service builds with its bundled `makefile`. From the project root, the top-level `makefile` builds both at once:

```bash
# From the project root — builds both services
make
```

Or build each service individually:

```bash
# Compile servers
cd compile_server
make

# OJ gateway
cd ../oj_server
make
```

This produces `compile_server/compile_server` and `oj_server/oj_server` executables. `make clean` (top level) removes them and, for `compile_server`, the temporary files under `temp/`.

### Unit Tests

`make test` builds and runs the **Google Test** suite in `tests/unit/` (22 test cases across 8 suites) and prints a summary:

```bash
make test
```

The suite covers password hashing (`test_passwd.cpp`), the shared `Question::Rank` logic (`test_question.cpp`), the file-based model (`test_filemodel.cpp`), the MySQL model — including escape-against-injection — (`test_mysqlmodel.cpp`, skipped automatically when MySQL is unreachable), and the in-memory session store (`test_session.cpp`). Requires `libgtest-dev`.

### Packaging for Distribution

The top-level `make output` target assembles a complete, self-contained program bundle under `output/` — everything needed to run the system on another host — ready to publish or send:

```bash
make output
```

It produces:

```
output/
├── compile_server/
│   ├── compile_server          # Judge node binary
│   └── temp/                   # Runtime temp dir
└── oj_server/
    ├── oj_server               # Gateway binary
    ├── conf/                   # service_machine.conf
    ├── questions/              # File-model sample questions
    ├── template_html/          # ctemplate templates
    └── wwwroot/                # Static assets
```

After copying `output/` to the target host, follow the remaining steps of this guide, running the nodes and gateway from inside `output/compile_server/` and `output/oj_server/` respectively. `make clean` also deletes the whole `output/` directory.

## 5. Configure the Compile Servers

Edit `oj_server/conf/service_machine.conf` — one `ip:port` per line, `:` separated, no spaces:

```
127.0.0.1:8081
127.0.0.1:8082
127.0.0.1:8083
```

This is the list the gateway load-balances across. Add or remove lines freely. The gateway reads this file only at startup, so restart the gateway after changes.

## 6. Start the Compile Servers

Each `compile_server` process takes its port as the first argument:

```bash
./compile_server 8081
```

**Important:** both the temp file directory (`./temp/`) and the log directory (`./logfiles/`) are **relative to the working directory**. To run several nodes on one host, give each node its own working directory:

```bash
# Node 1
mkdir -p node1/temp node1/logfiles
cp compile_server node1/
cd node1 && ./compile_server 8081 &

# Node 2
mkdir -p node2/temp node2/logfiles
cp compile_server node2/
cd node2 && ./compile_server 8082 &

# Node 3
mkdir -p node3/temp node3/logfiles
cp compile_server node3/
cd node3 && ./compile_server 8083 &
```

Or run them on separate hosts and point `service_machine.conf` at those hosts.

> The node needs `g++` available on `PATH` — compilation is performed via `execlp("g++", ...)`.

## 7. Start the OJ Gateway

The gateway uses several **relative** paths, so it must be started from inside the `oj_server/` directory:

```bash
cd oj_server
mkdir -p logfiles          # if missing
./oj_server
```

The gateway serves on `0.0.0.0:8080`:
- `GET /` and `GET /all_questions`, `GET /question/{id}` — HTML pages
- `GET /register`, `GET /login`, `GET /group_manage`, `GET /question_manage` (+ `/question_manage/edit[/{id}]`) — account / group / question-management pages
- `POST /judge/{id}` — judge submission (JSON)
- `POST/PUT/DELETE /api/...` — register / login / groups / roles / question-management JSON APIs

## 8. Verify the Deployment

```bash
# 1. Question list page
curl http://localhost:8080/all_questions

# 2. Single question page
curl http://localhost:8080/question/1

# 3. Submit a solution
curl -X POST http://localhost:8080/judge/1 \
  -H "Content-Type: application/json; charset=utf-8" \
  -d '{"code":"#include <iostream>\nint main(){ std::cout<<\"ok\"; return 0; }"}'
```

A successful judge returns JSON similar to:

```json
{
  "status": 0,
  "reason": "编译成功",
  "stdout": "ok",
  "stderr": ""
}
```

Also check the logs under `compile_server/*/logfiles/` and `oj_server/logfiles/` for per-request information.

## 9. Adding New Questions

1. Insert a row into the `questions` table (see [Database Setup](#3-database-setup)).
2. Design the question so that:
   - `header` contains the hidden prologue (e.g. `#include` lines and helpers),
   - `answer` is the starter code shown in the online editor,
   - `tail` contains the **hidden test driver** (the sole judging basis) — it accumulates passes via `RUN_TEST(name, cond)` and prints exactly one final line `PASSRATE <passed>/<total>`; it is never shown to users on the question page.
3. Set `scope` to `global` (visible to everyone) or to a **group id** (visible only to that group's leader and members).
4. The gateway queries MySQL on every request, so new questions appear immediately for `/all_questions`; no restart is required.

> `tail` is **required** when creating/editing a question (the API returns `必须设置不可见测试案例(tail)` if missing).

> A file-based alternative model (`oj_filemodel.hpp`) also ships in the repo — it reads questions from `oj_server/questions/` + `questions.list`. It is **not** the active model; switch by editing the `using namespace` lines in `oj_control.hpp` and `oj_view.hpp`. Each line of `questions.list` is `id title rank cpu_limit mem_limit scope` (6 columns; a missing `scope` defaults to `global`), and each `questions/{id}/` directory holds `desc.txt`, `header.cpp`, `answer.cpp`, `tail.cpp`.

> **Alternative — via the web UI:** admins and group leaders can also create/edit/delete questions through `GET /question_manage` and the `GET /question_manage/edit[/{id}]` pages (plain HTML + CSS + JS, rendered by the `View` class in `oj_server/oj_view.hpp`), which submit to `POST /api/questions`, `PUT /api/questions/{id}`, and `DELETE /api/questions/{id}`. These persist into the active model (MySQL or the file model).

## 10. Operations & Maintenance

- **Fault recovery:** when a compile server cannot be reached, the gateway moves it offline. Bring all nodes back online by sending `SIGQUIT` to the gateway:

  ```bash
  kill -QUIT <oj_server_pid>
  ```

  You can also restart the gateway — it re-reads `service_machine.conf` at startup.

- **Logs:** both services use an async, time-rolling logger writing into `logfiles/` relative to their working directories.
- **Resource limits:** per-question `cpu_limit` (seconds) and `mem_limit` (MB) are enforced via `setrlimit` (`RLIMIT_CPU`, `RLIMIT_AS`) in a forked child.
- **Temp files:** compiled artifacts live under `compile_server/temp/` and are removed after each judge by the compile server.
- **Scaling:** add more nodes by appending `ip:port` entries to `service_machine.conf` and starting more `compile_server` processes; the gateway automatically schedules to the least-loaded node.

## 11. Troubleshooting

| Symptom | Likely cause / fix |
| --- | --- |
| `oj_server` fails to start | Missing `conf/`, `template_html/`, `wwwroot/`, or `logfiles/` relative to the working directory — start from `oj_server/`. |
| `compile_server` fails to start | Missing `./temp/` or `./logfiles/` — create them. Port already in use — pick another port. |
| MySQL connection errors in `oj_server/logfiles/*.log` | MySQL not running, database/user not created, or credentials differ from `oj_mysqlmodel.hpp` (`oj` / `oj_client` / `1234` / `127.0.0.1:3306`). |
| `judge` returns empty body | All compile servers are offline or unreachable — check nodes, then `kill -QUIT <oj_server_pid>` to recover, or fix `service_machine.conf`. |
| Compile errors mention `g++` not found | `g++` is not on `PATH` of the compile-server process — install it. |
| `status == -2` on every judge | Internal error on the compile server — check its logs under `logfiles/`. |
| Port `8080` already in use | Change the listen port in `oj_server.cpp` and rebuild. |

## 12. Roles, Groups & Question Management

> User accounts, roles and groups are **wired into the gateway** via the JSON API in `oj_server/user/` (`oj_user_model.hpp` + `oj_passwd.hpp`); question management (`/api/questions` and the `GET /question_manage` pages) and visibility filtering are also fully implemented. See [SPEC.md §15](SPEC.md#15-功能落地记录) for the full implementation record. All frontend pages use plain HTML + CSS + JS.

**Roles**

| Role | Permissions |
| --- | --- |
| `admin` | Changes other users' roles, generates/resets the leader-registration invite code, publishes global questions, sees/manages all questions, and can create multiple groups like a leader. |
| `leader` | Registered as a leader using the admin invite code; can create **multiple** groups, invites members via each group's replaceable invite code, publishes questions inside the group. |
| `user` | Joins groups with an invite code, sees global + joined-group questions, submits code. |

**Typical workflow**

1. Register the first account — it automatically becomes `admin` (empty `users` table bootstrap).
2. An admin generates the leader-registration invite code: `POST /api/admin/invite` (admin auth).
3. Register a regular user, or register a **leader** with `POST /api/register` `{"username": "...", "password": "...", "role": "leader", "invite_code": "<admin-invite>"}`.
4. `POST /api/login` returns `{token, username, role}`; send `Authorization: Bearer <token>` on protected calls.
5. A leader or admin creates groups: `POST /api/groups` `{"name": "..."}` → returns `group_id` + `invite_code` (multiple groups allowed); reset a group's code anytime with `POST /api/groups/{id}/invite`.
6. Users join with `POST /api/groups/join` `{"invite_code": "..."}`.
7. Admin promotes/demotes users with `PUT /api/users/{id}/role`.
8. Admin/leader creates questions through the `GET /question_manage` UI (or `POST /api/questions` directly); `scope = global` or a group id. Edit/delete via `PUT`/`DELETE /api/questions/{id}`.
9. `GET /all_questions`, `GET /question/{id}`, `POST /judge/{id}` filter by the current user's role and groups.

**Passwords** are stored as `Hash(password + salt)` with the registration timestamp as the salt — never in plaintext. No third-party crypto library is used.
