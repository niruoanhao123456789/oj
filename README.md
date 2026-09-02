# oj — A Load-Balanced Online Judge

An online judge (OJ) platform built around a **load-balanced judging architecture**. Users browse a question bank, write C++ solutions in an in-browser editor, and receive real-time judging results. Every submit is scheduled to the backend **compile-and-run node** with the lowest current load; failed nodes are automatically taken offline and can be recovered on demand.

> **[English](README.md)** · [简体中文](README.zh.md)

---

## Table of Contents

- [Technologies](#technologies)
- [Dependencies](#dependencies)
- [Features](#features)
- [Architecture](#architecture)
- [Directory Layout](#directory-layout)
- [Quick Start](#quick-start)
- [Build & Package](#build--package)
- [Documentation](#documentation)
- [License](#license)

---

## Technologies

1. C++ STL standard library
2. Boost quasi-standard library (string splitting)
3. cpp-httplib third-party open-source networking library
4. ctemplate third-party open-source HTML rendering library
5. jsoncpp third-party open-source serialization / deserialization library
6. Load-balancing design
7. Multi-process, multi-threading
8. MySQL C connector
9. Ace front-end online editor
10. Google Test (gtest) — unit testing

## Dependencies

- A Linux system with a C++20-capable toolchain (recommended `g++` ≥ 10), plus `make` and `g++`.
- A MySQL service (recommended MySQL/MariaDB 8.x) reachable on `127.0.0.1:3306`.

| Dependency | Package (Debian/Ubuntu) | Purpose |
| --- | --- | --- |
| g++ | `g++` | Compiles judged source (`-std=c++20`) and builds this project |
| make | `make` | Builds via the bundled `makefile` of each service |
| jsoncpp | `libjsoncpp-dev` | JSON serialization / deserialization (`-ljsoncpp`) |
| cpp-httplib | `libcpp-httplib-dev` | HTTP networking library (`-lcpp-httplib`) |
| ctemplate | `libctemplate-dev` | Template-driven HTML rendering (`-lctemplate`) |
| MySQL client | `libmysqlclient-dev` | MySQL C API (`-lmysqlclient`) |
| Boost (headers) | `libboost-dev` | String-splitting utilities |
| gtest | `libgtest-dev` | Unit tests (`-lgtest -lgtest_main`) |
| pthread | glibc | Multi-threading (`-lpthread`) |

Example install (Debian/Ubuntu):

```bash
sudo apt update
sudo apt install -y g++ make mysql-server libjsoncpp-dev libcpp-httplib-dev \
    libctemplate-dev libmysqlclient-dev libboost-dev libgtest-dev
```

> For detailed deployment steps (database setup, build, and startup) see [DEPLOY.md](DEPLOY.md).

## Features

- **Load-balanced judging** — the gateway tracks the live load of every compile-and-run backend and dispatches each request to the least-loaded machine.
- **Real-time C++ judging** — submit code in a web editor and get results back in seconds (compiled with `g++ -std=c++20`).
- **Resource limits** — per-question CPU time and memory limits are enforced through `setrlimit` in a forked child process.
- **Fault tolerance** — unreachable compile servers are taken offline automatically; sending `SIGQUIT` to the gateway brings every node back online.
- **Question bank backed by MySQL** — question metadata, hidden test code, and limits are stored in a `questions` table (a file-based model is also included as an alternative).
- **Template-driven HTML pages** — rendered with the ctemplate library from `template_html/`.
- **Structured logging** — an async, time-rolling logger writes logs to `logfiles/`.
- **Roles & groups** — three roles: **admin** (super admin, manages roles, generates the leader-registration invite code, publishes global questions), **leader** (owns groups, invites members via replaceable invite codes, publishes group-only questions), and **user** (joins groups with an invite code, sees global + own-group questions).
- **Question management** — admins/leaders create, edit, and delete questions through HTML+CSS+JS pages (rendered by the `View` class in `oj_server/oj_view.hpp`); the form content is posted as JSON and persisted into MySQL or the file-based `questions/` model.
- **Timestamp-salted password hashing** — passwords are stored as `Hash(password + salt)` with the registration timestamp as the salt; never stored in plaintext.
- **Unit testing with Google Test** — `make test` builds and runs the gtest suite in `tests/unit/` (34 test cases across 10 suites), covering password hashing, both question models (file + MySQL), in-memory sessions, and the user/group model that backs the register/login, admin-invite, role, and group APIs.

## Architecture

The system is split into two services:

```
                    ┌──────────────────────────────────────────────┐
                    │               oj_server (gateway)            │
                    │  • serves HTML pages        (port 8080)      │
                    │  • reads questions from MySQL                │
                    │  • user accounts / roles / groups            │
                    │  • question management                       │
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

**Request flow for a judge request:**

1. The user opens `/question/{id}` and clicks **Submit**.
2. The browser `POST`s `{"code": ...}` to `/judge/{id}` on `oj_server`.
3. `oj_server` loads the question from MySQL and builds the full source: `header.cpp` (hidden) + user code + `tail.cpp` (**hidden test cases**, not visible to users).
4. The load balancer picks the compile server with the **minimum current load** and forwards `{"code", "input", "cpu_limit", "mem_limit"}` to its `/compile_and_run` endpoint.
5. The compile server compiles with `g++`, runs the binary under `setrlimit` CPU/memory limits, and returns a JSON result (including `pass_count`/`total_count` per the PASSRATE protocol).
6. The result is passed back to the browser and rendered **LeetCode-style**: existing error returns plus "Testcases passed: X / Y (percentage)" — individual passing cases are never listed.

## Directory Layout

```
.
├── common/                     # Shared code
│   ├── Util.hpp                # File / path / time / string helpers
│   └── log/                    # Async rolling logging library
├── compile_server/             # Judge backend nodes (one per port)
│   ├── compile_server.cpp      # HTTP entry point, POST /compile_and_run
│   ├── compile_run.hpp         # Compile + run orchestration
│   ├── compiler.hpp            # g++ wrapper
│   ├── runner.hpp              # Runs binary with CPU/mem limits
│   ├── makefile
│   └── temp/                   # Temp source/executable files (git-ignored)
├── oj_server/                  # Web gateway (port 8080)
│   ├── oj_server.cpp           # HTTP routes + static file serving
│   ├── oj_control.hpp          # Load balancer + judge orchestration + auth/perms
│   ├── oj_mysqlmodel.hpp       # MySQL question model (active)
│   ├── oj_filemodel.hpp        # File-based question model (alternative)
│   ├── oj_view.hpp             # ctemplate HTML rendering
│   ├── conf/service_machine.conf   # List of compile-server endpoints
│   ├── database/               # SQL scripts (oj.sql / upgrade.sql)
│   ├── user/                   # Auth / roles / groups (oj_passwd.hpp, oj_user_model.hpp)
│   ├── template_html/          # ctemplate templates (incl. register/login/group_manage/question_manage pages)
│   ├── wwwroot/                # Static assets (landing page, etc.)
│   ├── questions/              # File-model sample questions
│   └── makefile
├── tests/unit/                 # Google Test unit tests (make test)
│   ├── makefile
│   ├── test_env.hpp
│   └── test_*.cpp              # passwd / question / filemodel / mysqlmodel / session
├── makefile                    # Top-level build / test / packaging entry point
├── output/                     # Distributable bundle generated by `make output`
├── README.md                   # This document (EN) / README.zh.md (ZH)
├── SPEC.md                     # Software specification (ZH)
├── API.md                      # HTTP API reference (EN) / API.zh.md (ZH)
├── DEPLOY.md                   # Deployment guide (EN) / DEPLOY.zh.md (ZH)
└── LICENSE
```

## Quick Start

See [DEPLOY.md](DEPLOY.md) (or [DEPLOY.zh.md](DEPLOY.zh.md)) for the full guide. In a nutshell:

```bash
# 1. Create the MySQL database and the oj_client user (see DEPLOY.md)
# 2. Build both services (top-level makefile)
make

# 3. Start compile servers (each in its own working directory)
./compile_server 8081
./compile_server 8082
./compile_server 8083

# 4. Start the gateway from oj_server/
cd ../oj_server && ./oj_server

# 5. Open
```

## Build & Package

The top-level `makefile` drives building and packaging for both services:

| Target | Purpose |
| --- | --- |
| `make` / `make all` | Builds `compile_server` and `oj_server` in their own directories. |
| `make test` | Builds and runs the Google Test unit suite in `tests/unit/` (`./unit_tests`). |
| `make output` | Assembles a complete, self-contained program bundle under `output/`, ready to publish or send elsewhere. |
| `make clean` | Removes the build artifacts of both services and deletes the whole `output/` directory. |

`make output` produces this structure:

```
output/
├── compile_server/
│   ├── compile_server          # Judge node binary
│   └── temp/                   # Runtime temp dir
└── oj_server/
    ├── oj_server               # Gateway binary
    ├── conf/                   # service_machine.conf
    ├── database/               # SQL scripts (oj.sql / upgrade.sql)
    ├── questions/              # File-model sample questions
    ├── template_html/          # ctemplate templates
    └── wwwroot/                # Static assets
```

The bundle is self-contained: after copying `output/` to another host and setting up MySQL, start the nodes and gateway from inside `output/compile_server/` and `output/oj_server/` respectively (see [DEPLOY.md](DEPLOY.md)).

## Documentation

| Document | English | 简体中文 |
| --- | --- | --- |
| Software specification | — | [SPEC.md](SPEC.md) |
| API reference | [API.md](API.md) | [API.zh.md](API.zh.md) |
| Deployment guide | [DEPLOY.md](DEPLOY.md) | [DEPLOY.zh.md](DEPLOY.zh.md) |

## License

This project is licensed under the terms of the [LICENSE](LICENSE) file.
