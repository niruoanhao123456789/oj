#include <iostream>
#include <httplib.h>
#include "../common/log/Log.hpp"

using namespace httplib;

int main()
{
    std::unique_ptr<LogModule::LoggerBuilder> builder = std::make_unique<LogModule::GobalLoggerBuilder>();
    builder->BuildLoggerName("oj_Logger");
    builder->BuildLoggerType(LogModule::LoggerType::LOGGER_ASYNC);
    builder->BUildLoggerSink<LogModule::RollByTimeSink>("./logfiles/",LogModule::TimeGap::GAP_DAY);
    LogModule::Logger::ptr logger = builder->Build();

    // 处理用户路由服务功能
    Server svr;

    // 获取所有的题目列表
    svr.Get("/all_questions",[](const Request& req,Response& resp){
        const std::string html;

        resp.set_content(html,"text/html; charset=utf-8");
    });

    // 用户要根据题目编号，获取题目的内容
    // /question/100 -> 正则匹配
    // R"()", 原始字符串raw string,保持字符串内容的原貌，不用做相关的转义
    svr.Get(R"(/question/(\d+))",[](const Request& req,Response& resp){
        const std::string number = req.matches[1];
        const std::string html;

        resp.set_content(html,"text/html; charset=utf-8");
    });

    svr.set_base_dir("./wwwroot");
    svr.listen("0.0.0.0",8080);

    return 0;
}