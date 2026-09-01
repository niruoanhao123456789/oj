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
        ctrl.AllQuestions(req.get_header_value("Authorization"),&html);
        resp.set_content(html,"text/html; charset=utf-8");
    });

    // 用户要根据题目编号，获取题目的内容
    // /question/100 -> 正则匹配
    // R"()", 原始字符串raw string,保持字符串内容的原貌，不用做相关的转义
    svr.Get(R"(/question/(\d+))",[&ctrl](const Request& req,Response& resp){
        const std::string id = req.matches[1];
        std::string html;
        ctrl.OneQuestion(req.get_header_value("Authorization"),id,&html);
        resp.set_content(html,"text/html; charset=utf-8");
    });

    // 用户提交代码，使用判题功能(1. 每道题的测试用例 2. compile_and_run)
    svr.Post(R"(/judge/(\d+))",[&ctrl](const Request& req,Response& resp){
        const std::string id = req.matches[1];
        std::string result_json;
        ctrl.Judge(req.get_header_value("Authorization"),id,req.body,&result_json);
        resp.set_content(result_json,"application/json;charset=utf-8");
    });

    // 注册 / 登录 页面
    svr.Get("/register",[&ctrl](const Request& req,Response& resp){
        std::string html;
        ctrl.RegisterPage(&html);
        resp.set_content(html,"text/html; charset=utf-8");
    });
    svr.Get("/login",[&ctrl](const Request& req,Response& resp){
        std::string html;
        ctrl.LoginPage(&html);
        resp.set_content(html,"text/html; charset=utf-8");
    });

    // 小组管理页面
    svr.Get("/group_manage",[&ctrl](const Request& req,Response& resp){
        std::string html;
        ctrl.GroupManage(req.get_header_value("Authorization"),&html);
        resp.set_content(html,"text/html; charset=utf-8");
    });

    // 注册 / 登录
    svr.Post("/api/register",[&ctrl](const Request& req,Response& resp){
        std::string out_json;
        ctrl.Register(req.body,&out_json);
        resp.set_content(out_json,"application/json; charset=utf-8");
    });
    svr.Post("/api/login",[&ctrl](const Request& req,Response& resp){
        std::string out_json;
        ctrl.Login(req.body,&out_json);
        resp.set_content(out_json,"application/json; charset=utf-8");
    });

    // 管理员邀请码(注册负责人用)
    svr.Post("/api/admin/invite",[&ctrl](const Request& req,Response& resp){
        std::string out_json;
        ctrl.ResetAdminInvite(req.get_header_value("Authorization"),&out_json);
        resp.set_content(out_json,"application/json; charset=utf-8");
    });

    // 小组管理
    svr.Post("/api/groups",[&ctrl](const Request& req,Response& resp){
        std::string out_json;
        ctrl.CreateGroup(req.get_header_value("Authorization"),req.body,&out_json);
        resp.set_content(out_json,"application/json; charset=utf-8");
    });
    svr.Post("/api/groups/join",[&ctrl](const Request& req,Response& resp){
        std::string out_json;
        ctrl.JoinGroup(req.get_header_value("Authorization"),req.body,&out_json);
        resp.set_content(out_json,"application/json; charset=utf-8");
    });
    svr.Post(R"(/api/groups/(\d+)/invite)",[&ctrl](const Request& req,Response& resp){
        std::string out_json;
        ctrl.ResetInviteCode(req.get_header_value("Authorization"),req.matches[1],&out_json);
        resp.set_content(out_json,"application/json; charset=utf-8");
    });

    // 角色管理
    svr.Put(R"(/api/users/(\d+)/role)",[&ctrl](const Request& req,Response& resp){
        std::string out_json;
        ctrl.SetUserRole(req.get_header_value("Authorization"),req.matches[1],req.body,&out_json);
        resp.set_content(out_json,"application/json; charset=utf-8");
    });

    // 题目管理 API
    svr.Post("/api/questions",[&ctrl](const Request& req,Response& resp){
        std::string out_json;
        ctrl.AddQuestion(req.get_header_value("Authorization"),req.body,&out_json);
        resp.set_content(out_json,"application/json; charset=utf-8");
    });
    svr.Put(R"(/api/questions/(\d+))",[&ctrl](const Request& req,Response& resp){
        std::string out_json;
        ctrl.UpdateQuestion(req.get_header_value("Authorization"),req.matches[1],req.body,&out_json);
        resp.set_content(out_json,"application/json; charset=utf-8");
    });
    svr.Delete(R"(/api/questions/(\d+))",[&ctrl](const Request& req,Response& resp){
        std::string out_json;
        ctrl.DeleteQuestion(req.get_header_value("Authorization"),req.matches[1],&out_json);
        resp.set_content(out_json,"application/json; charset=utf-8");
    });

    // 题目管理页面
    svr.Get("/question_manage",[&ctrl](const Request& req,Response& resp){
        std::string html;
        ctrl.QuestionManage(req.get_header_value("Authorization"),&html);
        resp.set_content(html,"text/html; charset=utf-8");
    });
    svr.Get("/question_manage/edit",[&ctrl](const Request& req,Response& resp){
        std::string html;
        ctrl.QuestionEdit(req.get_header_value("Authorization"),"",&html);
        resp.set_content(html,"text/html; charset=utf-8");
    });
    svr.Get(R"(/question_manage/edit/(\d+))",[&ctrl](const Request& req,Response& resp){
        std::string html;
        ctrl.QuestionEdit(req.get_header_value("Authorization"),req.matches[1],&html);
        resp.set_content(html,"text/html; charset=utf-8");
    });

    svr.set_base_dir("./wwwroot");
    svr.listen("0.0.0.0",8080);

    return 0;
}