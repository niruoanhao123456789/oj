#include <jsoncpp/json/json.h>
#include <httplib.h>
#include "../common/log/Log.hpp"
#include "compile_run.hpp"

using namespace LogModule;
using namespace oj_compile_run;

// 编译服务存在被多个用户进行请求
int main()
{
    std::unique_ptr<LoggerBuilder> builder = std::make_unique<GobalLoggerBuilder>();
    builder->BuildLoggerName("Async_Loggger");
    builder->BuildLoggerType(LoggerType::LOGGER_ASYNC);
    builder->BUildLoggerSink<RollByTimeSink>("./logfiles/",TimeGap::GAP_DAY);
    Logger::ptr logger = builder->Build();

    // std::string in_json;
    // Json::Value in_value;
    // in_value["code"] = R"(#include<iostream>
    // int main()
    // {
    //     while(1);
    //     return 0;
    // })";
    
    // in_value["input"] = "";
    // in_value["cpu_limit"] = 1; // s
    // in_value["mem_limit"] = 30;

    // Json::FastWriter writer;
    // in_json = writer.write(in_value);

    // std::string out_json;
    // CompileAndRun::Start(in_json,&out_json);

    // std::cout<<out_json<<std::endl;

    return 0;
}