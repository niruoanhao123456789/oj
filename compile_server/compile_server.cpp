#include "compiler.hpp"
#include "runner.hpp"
#include "../log/Log.hpp"

using namespace LogModule;
using namespace oj_compiler;
using namespace oj_runner;

int main()
{
    std::unique_ptr<LoggerBuilder> builder = std::make_unique<GobalLoggerBuilder>();
    builder->BuildLoggerName("Async_Loggger");
    builder->BuildLoggerType(LoggerType::LOGGER_ASYNC);
    builder->BUildLoggerSink<RollByTimeSink>("./logfiles/",TimeGap::GAP_DAY);
    Logger::ptr logger = builder->Build();

    std::string file = "code";
    Compiler::Compile(file);
    Runner::Run(file);

    return 0;
}