-- ============================================================================
-- oj gateway database upgrade script (existing installs)
-- Adds the scope column to questions, renames legacy columns to the canonical
-- names used by oj_mysqlmodel.hpp, and creates the users/groups/group_members
-- tables. Idempotent: safe to run more than once.
--
--     mysql -u root -p < oj_server/database/upgrade.sql
-- ============================================================================

USE oj;

DELIMITER $$
CREATE PROCEDURE oj_upgrade()
BEGIN
    -- 1) add the scope column at position 9 (after mem_limit) if missing
    IF NOT EXISTS (SELECT 1 FROM information_schema.COLUMNS
                   WHERE TABLE_SCHEMA='oj' AND TABLE_NAME='questions' AND COLUMN_NAME='scope') THEN
        ALTER TABLE questions
            ADD COLUMN scope VARCHAR(16) NOT NULL DEFAULT 'global' COMMENT '可见范围: global 或小组id' AFTER mem_limit;
    END IF;

    -- 1.1) 出题模式 + 两套用例列(统一输入/期望输出判题模型)
    IF NOT EXISTS (SELECT 1 FROM information_schema.COLUMNS
                   WHERE TABLE_SCHEMA='oj' AND TABLE_NAME='questions' AND COLUMN_NAME='mode') THEN
        ALTER TABLE questions
            ADD COLUMN mode VARCHAR(16) NOT NULL DEFAULT 'acm'
            COMMENT '出题模式: function 函数接口 / acm 传统ACM IO' AFTER scope;
    END IF;
    IF NOT EXISTS (SELECT 1 FROM information_schema.COLUMNS
                   WHERE TABLE_SCHEMA='oj' AND TABLE_NAME='questions' AND COLUMN_NAME='visible_cases') THEN
        ALTER TABLE questions
            ADD COLUMN visible_cases TEXT COMMENT '显式样例 JSON 数组, 答题页展示, 不判题' AFTER mode;
    END IF;
    IF NOT EXISTS (SELECT 1 FROM information_schema.COLUMNS
                   WHERE TABLE_SCHEMA='oj' AND TABLE_NAME='questions' AND COLUMN_NAME='hidden_cases') THEN
        ALTER TABLE questions
            ADD COLUMN hidden_cases TEXT COMMENT '隐藏判题用例 JSON 数组({input,expected}), 判题唯一依据' AFTER visible_cases;
    END IF;

    -- 1.2) 兼容旧数据: 依据 main 所在位置推断 mode(function: main 在 tail / acm: main 在 answer)
    IF EXISTS (SELECT 1 FROM information_schema.COLUMNS
               WHERE TABLE_SCHEMA='oj' AND TABLE_NAME='questions' AND COLUMN_NAME='mode') THEN
        UPDATE questions SET mode = 'function'
         WHERE mode = 'acm' AND answer NOT LIKE '%int main(%' AND tail LIKE '%int main(%';
    END IF;

    -- 2) rename legacy column names to the canonical ones read by the gateway
    IF EXISTS (SELECT 1 FROM information_schema.COLUMNS
               WHERE TABLE_SCHEMA='oj' AND TABLE_NAME='questions' AND COLUMN_NAME='star') THEN
        ALTER TABLE questions CHANGE COLUMN star `rank` VARCHAR(16) NOT NULL COMMENT '题目的难度';
    END IF;
    IF EXISTS (SELECT 1 FROM information_schema.COLUMNS
               WHERE TABLE_SCHEMA='oj' AND TABLE_NAME='questions' AND COLUMN_NAME='question_desc') THEN
        ALTER TABLE questions CHANGE COLUMN question_desc desc_text TEXT NOT NULL COMMENT '题目描述';
    END IF;
    IF EXISTS (SELECT 1 FROM information_schema.COLUMNS
               WHERE TABLE_SCHEMA='oj' AND TABLE_NAME='questions' AND COLUMN_NAME='time_limit') THEN
        ALTER TABLE questions CHANGE COLUMN time_limit cpu_limit INT DEFAULT 1 COMMENT '题目的时间限制';
    END IF;
END$$
DELIMITER ;

CALL oj_upgrade();
DROP PROCEDURE oj_upgrade;

-- ----------------------------------------------------------------------------
-- users / roles / groups
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
