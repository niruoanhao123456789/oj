-- ============================================================================
-- oj gateway database init script (canonical schema, see SPEC.md §4)
-- Run once on a fresh install:
--     mysql -u root -p < oj_server/database/oj.sql
-- For an existing install, run upgrade.sql instead.
-- ============================================================================

CREATE DATABASE IF NOT EXISTS oj DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_general_ci;
USE oj;

-- ----------------------------------------------------------------------------
-- questions (columns are read positionally via SELECT *, order matters!)
-- 0 id | 1 title | 2 rank | 3 desc_text | 4 header | 5 answer |
-- 6 tail | 7 cpu_limit | 8 mem_limit | 9 scope | 10 mode |
-- 11 visible_cases | 12 hidden_cases
-- ----------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS questions (
    id            INT PRIMARY KEY AUTO_INCREMENT,
    title         VARCHAR(255) NOT NULL,
    rank          VARCHAR(16)  NOT NULL,   -- 简单 / 中等 / 困难
    desc_text     TEXT,
    header        TEXT,                    -- 隐藏头文件(函数接口模式), 拼接在用户代码之前
    answer        TEXT,                    -- 在线编辑器预填代码(函数模式只放函数; ACM 模式含 main)
    tail          TEXT,                    -- 隐藏附加代码(函数模式放读 stdin 调 answer 的 main 驱动; ACM 留空)
    cpu_limit     INT DEFAULT 1,           -- seconds
    mem_limit     INT DEFAULT 30,          -- MB
    scope         VARCHAR(16) NOT NULL DEFAULT 'global',  -- 'global' or a group id
    mode          VARCHAR(16) NOT NULL DEFAULT 'acm',     -- 出题模式: 'function' 函数接口 / 'acm' 传统ACM IO
    visible_cases TEXT,                    -- 显式样例 JSON 数组, 答题页展示, 不判题
    hidden_cases  TEXT                     -- 隐藏判题用例 JSON 数组({input,expected}), 判题唯一依据
);

-- ----------------------------------------------------------------------------
-- users / roles / groups (planned feature, see SPEC.md §14)
-- ----------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS users (
    id            INT PRIMARY KEY AUTO_INCREMENT,
    username      VARCHAR(64) NOT NULL UNIQUE,
    password_hash VARCHAR(64) NOT NULL,             -- Hash(password + salt)
    salt          VARCHAR(20) NOT NULL,             -- registration timestamp
    role          VARCHAR(16) NOT NULL DEFAULT 'user',  -- admin / leader / user
    created_at    DATETIME
);

CREATE TABLE IF NOT EXISTS `groups` (
    id          INT PRIMARY KEY AUTO_INCREMENT,
    name        VARCHAR(64) NOT NULL,
    owner_id    INT NOT NULL,                       -- leader user id
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

-- ----------------------------------------------------------------------------
-- application user used by the gateway (credentials in oj_mysqlmodel.hpp)
-- ----------------------------------------------------------------------------
CREATE USER IF NOT EXISTS 'oj_client'@'localhost' IDENTIFIED BY '1234';
CREATE USER IF NOT EXISTS 'oj_client'@'127.0.0.1' IDENTIFIED BY '1234';
GRANT ALL PRIVILEGES ON oj.* TO 'oj_client'@'localhost';
GRANT ALL PRIVILEGES ON oj.* TO 'oj_client'@'127.0.0.1';
FLUSH PRIVILEGES;
