# oj — A Load-Balanced Online Judge

An online judge (OJ) platform built around a **load-balanced judging architecture**. Users browse a question bank, write C++ solutions in an in-browser editor, and receive real-time judging results. Every submit is scheduled to the backend **compile-and-run node** with the lowest current load; failed nodes are automatically taken offline and can be recovered on demand.

> **[English](README.md)** · [简体中文](README.zh.md)

---

## Table of Contents

- [Features](#features)
- [Architecture](#architecture)
- [Directory Layout](#directory-layout)
- [Quick Start](#quick-start)
- [Documentation](#documentation)
- [License](#license)

---

## Features

- **Load-balanced judging** — the gateway tracks the live load of every compile-and-run backend and dispatches each request to the least-loaded machine.
- **Real-time C++ judging** — submit code in a web editor and get results back in seconds (compiled with `g++ -std=c++20`).
- **Resource limits** — per-question CPU time and memory limits are enforced through `setrlimit` in a forked child process.
- **Fault tolerance** — unreachable compile servers are taken offline automatically; sending `SIGQUIT` to the gateway brings every node back online.
- **Question bank backed by MySQL** — question metadata, hidden test code, and limits are stored in a `questions` table (a file-based model is also included as an alternative).
- **Template-driven HTML pages** — rendered with the ctemplate library from `template_html/`.
- **Structured logging** — an async, time-rolling logger writes logs to `logfiles/`.

## Architecture

The system is split into two services:

```
                    ┌──────────────────────────────────────────────┐
                    │               oj_server (gateway)            │
                    │  • serves HTML pages        (port 8080)      │
                    │  • reads questions from MySQL                │
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
3. `oj_server` loads the question from MySQL and builds the full source: `header.cpp` (hidden) + user code + `tail.cpp` (test cases).
4. The load balancer picks the compile server with the **minimum current load** and forwards `{"code", "input", "cpu_limit", "mem_limit"}` to its `/compile_and_run` endpoint.
5. The compile server compiles with `g++`, runs the binary under `setrlimit` CPU/memory limits, and returns a JSON result.
6. The result is passed back to the browser and rendered as `AC / WA / TLE`-style feedback.

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
│   ├── oj_control.hpp          # Load balancer + judge orchestration
│   ├── oj_mysqlmodel.hpp       # MySQL question model (active)
│   ├── oj_filemodel.hpp        # File-based question model (alternative)
│   ├── oj_view.hpp             # ctemplate HTML rendering
│   ├── conf/service_machine.conf   # List of compile-server endpoints
│   ├── template_html/          # ctemplate templates
│   ├── wwwroot/                # Static assets (landing page, etc.)
│   ├── questions/              # File-model sample questions
│   └── makefile
├── README.md                   # This document (EN) / README.zh.md (ZH)
├── API.md                      # HTTP API reference (EN) / API.zh.md (ZH)
├── DEPLOY.md                   # Deployment guide (EN) / DEPLOY.zh.md (ZH)
└── LICENSE
```

## Quick Start

See [DEPLOY.md](DEPLOY.md) (or [DEPLOY.zh.md](DEPLOY.zh.md)) for the full guide. In a nutshell:

```bash
# 1. Create the MySQL database and the oj_client user (see DEPLOY.md)
# 2. Build both services
cd compile_server && make
cd ../oj_server && make

# 3. Start compile servers (each in its own working directory)
./compile_server 8081
./compile_server 8082
./compile_server 8083

# 4. Start the gateway from oj_server/
cd ../oj_server && ./oj_server

# 5. Open
```

## Documentation

| Document | English | 简体中文 |
| --- | --- | --- |
| API reference | [API.md](API.md) | [API.zh.md](API.zh.md) |
| Deployment guide | [DEPLOY.md](DEPLOY.md) | [DEPLOY.zh.md](DEPLOY.zh.md) |

## License

This project is licensed under the terms of the [LICENSE](LICENSE) file.
