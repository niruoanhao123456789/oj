#include "complier.hpp"
#include "../log/Log.hpp"

using namespace LogModule;

int main()
{
    std::unique_ptr<LoggerBuilder> builder = std::make_unique<GobalLoggerBuilder>();
    builder->BuildLoggerName("Async_Loggger");
    builder->BuildLoggerType(LoggerType::LOGGER_ASYNC);
    builder->BUildLoggerSink<RollByTimeSink>("../logfiles/complie_logs/",TimeGap::GAP_DAY);
    Logger::ptr logger = builder->Build();

    return 0;
}