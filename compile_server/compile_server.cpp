#include <jsoncpp/json/json.h>
#include <httplib.h>
#include "../common/log/Log.hpp"
#include "compile_run.hpp"

using namespace LogModule;
using namespace oj_compile_run;
using namespace httplib;

void Usage(std::string proc)
{
    std::cerr << "Usage: " << "\n\t" << proc << " port" << std::endl;
}

// 编译服务存在被多个用户进行请求
int main(int argc, char *argv[])
{
    if(argc != 2){
        Usage(argv[0]);
        return 1;
    }

    std::unique_ptr<LoggerBuilder> builder = std::make_unique<GobalLoggerBuilder>();
    builder->BuildLoggerName("CompileRun_Loggger");
    builder->BuildLoggerType(LoggerType::LOGGER_ASYNC);
    builder->BUildLoggerSink<RollByTimeSink>("./logfiles/",TimeGap::GAP_DAY);
    LogModule::Logger::ptr logger = builder->Build();

    httplib::Server svr;

    svr.Post("/compile_and_run",[](const Request& req,Response& res){
        // 用户请求的服务正文是我们想要的json string
        std::string in_json = req.body;
        std::string out_json;
        if(!in_json.empty())
        {
            CompileAndRun::Start(in_json,&out_json);
            res.set_content(out_json,"application/json;charset=utf-8");
        }
    });

    svr.listen("0.0.0.0",atoi(argv[1]));

    return 0;
}
