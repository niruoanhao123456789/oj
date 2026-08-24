#include <iostream>
#include <httplib.h>
#include <signal.h>
#include "../common/log/Log.hpp"
#include "oj_control.hpp"

using namespace httplib;
using namespace oj_control;

static oj_control::Control* ctrl_ptr = nullptr;

void Recovery(int signo)
{
    ctrl_ptr->RecoveryMachine();
}

int main()
{
    std::unique_ptr<LogModule::LoggerBuilder> builder = std::make_unique<LogModule::GobalLoggerBuilder>();
    builder->BuildLoggerName("oj_Logger");
    builder->BuildLoggerType(LogModule::LoggerType::LOGGER_ASYNC);
    builder->BUildLoggerSink<LogModule::RollByTimeSink>("./logfiles/",LogModule::TimeGap::GAP_DAY);
    LogModule::Logger::ptr logger = builder->Build();

    signal(SIGQUIT, Recovery);

    // 处理用户路由服务功能
    Server svr;

    Control ctrl;
    ctrl_ptr = &ctrl;

    // 获取所有的题目列表
    svr.Get("/all_questions",[&ctrl](const Request& req,Response& resp){
        std::string html;
        ctrl.AllQuestions(&html);
        resp.set_content(html,"text/html; charset=utf-8");
    });

    // 用户要根据题目编号，获取题目的内容
    // /question/100 -> 正则匹配
    // R"()", 原始字符串raw string,保持字符串内容的原貌，不用做相关的转义
    svr.Get(R"(/question/(\d+))",[&ctrl](const Request& req,Response& resp){
        const std::string id = req.matches[1];
        std::string html;
        ctrl.OneQuestion(id,&html);
        resp.set_content(html,"text/html; charset=utf-8");
    });

    // 用户提交代码，使用判题功能(1. 每道题的测试用例 2. compile_and_run)
    svr.Post(R"(/judge/(\d+))",[&ctrl](const Request& req,Response& resp){
        const std::string id = req.matches[1];
        std::string result_json;
        ctrl.Judge(id,req.body,&result_json);
        resp.set_content(result_json,"application/json;charset=utf-8");
    });

    svr.set_base_dir("./wwwroot");
    svr.listen("0.0.0.0",8080);

    return 0;
}