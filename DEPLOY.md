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
| pthread | glibc | `-lpthread` |

Example install (Debian/Ubuntu):

```bash
sudo apt update
sudo apt install -y g++ make mysql-server libjsoncpp-dev libcpp-httplib-dev \
    libctemplate-dev libmysqlclient-dev libboost-dev
```

## 3. Database Setup

Start MySQL and run the following SQL to create the database, table, and the application user used by the gateway (`oj_client` / `1234`, host `127.0.0.1:3306`):

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
    tail       TEXT,                    -- test cases, appended after user code
    cpu_limit  INT DEFAULT 1,           -- seconds
    mem_limit  INT DEFAULT 30           -- MB
);

CREATE USER IF NOT EXISTS 'oj_client'@'localhost' IDENTIFIED BY '1234';
CREATE USER IF NOT EXISTS 'oj_client'@'127.0.0.1' IDENTIFIED BY '1234';
GRANT ALL PRIVILEGES ON oj.* TO 'oj_client'@'localhost';
GRANT ALL PRIVILEGES ON oj.* TO 'oj_client'@'127.0.0.1';
FLUSH PRIVILEGES;
```

> **Column mapping note:** the gateway reads columns positionally via `SELECT *`:
> `id, title, rank, desc, header, answer, tail, cpu_limit, mem_limit`.
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
- `POST /judge/{id}` — judge submission (JSON)

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
   - `tail` contains the test driver (`main` with the test cases) that links with `solve(...)`.
3. Restart the gateway so it re-reads the database. (The gateway queries MySQL on every request, so new questions appear immediately for `/all_questions`; no restart is strictly required.)

> A file-based alternative model (`oj_filemodel.hpp`) also ships in the repo — it reads questions from `oj_server/questions/` + `questions.list`. It is **not** the active model; switch by editing the `using namespace` lines in `oj_control.hpp` and `oj_view.hpp`.

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
