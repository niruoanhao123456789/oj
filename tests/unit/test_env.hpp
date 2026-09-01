#pragma once
#include <gtest/gtest.h>
#include <memory>
#include "../../common/log/Log.hpp"

// 构建与网关同名的日志器(输出到标准输出), 供各模型内部 LOG_* 使用
inline void BuildTestLogger()
{
    static bool built = false;
    if(built)
        return;
    std::unique_ptr<LogModule::LoggerBuilder> builder = std::make_unique<LogModule::GobalLoggerBuilder>();
    builder->BuildLoggerName("oj_Logger");
    builder->BuildLoggerType(LogModule::LoggerType::LOGGER_ASYNC);
    builder->BUildLoggerSink<LogModule::StdOutSink>();
    builder->Build();
    built = true;
}

struct OjLoggerEnv : public ::testing::Environment
{
    void SetUp() override
    {
        BuildTestLogger();
    }
};

// inline 变量: 多翻译单元仅注册一次
inline ::testing::Environment* const oj_logger_env = ::testing::AddGlobalTestEnvironment(new OjLoggerEnv);
