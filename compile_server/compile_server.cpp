#include <jsoncpp/json/json.h>
#include "../common/log/Log.hpp"

using namespace LogModule;

// 编译服务存在被多个用户进行请求
int main()
{
    std::unique_ptr<LoggerBuilder> builder = std::make_unique<GobalLoggerBuilder>();
    builder->BuildLoggerName("Async_Loggger");
    builder->BuildLoggerType(LoggerType::LOGGER_ASYNC);
    builder->BUildLoggerSink<RollByTimeSink>("./logfiles/",TimeGap::GAP_DAY);
    Logger::ptr logger = builder->Build();

    

    return 0;
}