#pragma once

#include <jsoncpp/json/json.h>
#include <unistd.h>
#include <cassert>
#include <cctype>
#include <vector>
#include <sstream>
#include "../common/Util.hpp"
#include "compiler.hpp"
#include "runner.hpp"

namespace oj_compile_run
{
    using namespace oj_compiler;
    using namespace oj_runner;
    using namespace LogModule;
    using namespace oj_util;

    class CompileAndRun
    {
    public:
        static void RemoveTempFiles(const std::string& filename)
        {  
            const std::string _src = Path::Src(filename);
            if(File::IsFileExists(_src))
                unlink(_src.c_str());
            
            const std::string _execute = Path::Exe(filename);
            if(File::IsFileExists(_src))
                unlink(_execute.c_str());
            
            const std::string _stderr = Path::Stderr(filename);
            if(File::IsFileExists(_stderr))
                unlink(_stderr.c_str());
            
            const std::string _stdin = Path::Stdin(filename);
            if(File::IsFileExists(_stdin))
                unlink(_stdin.c_str());

            const std::string _stdout = Path::Stdout(filename);
            if(File::IsFileExists(_stdout))
                unlink(_stdout.c_str());

            const std::string _compile_err = Path::CompilerError(filename);
            if(File::IsFileExists(_compile_err))
                unlink(_compile_err.c_str());
        }

        static std::string StatusToDesc(int status, const std::string &filename)
        {
            std::string desc;
            switch(status)
            {
                case 0: desc = "编译成功"; break;
                case -1: desc = "提交代码为空"; break;
                case -2: desc = "内部错误"; break;
                case -3: oj_util::File::ReadFile(&desc,oj_util::Path::CompilerError(filename),true); break;
                case SIGABRT: desc = "内存超过范围"; break;
                case SIGXCPU: desc = "CPU使用超时"; break;
                case SIGFPE: desc = "浮点数溢出"; break;
                default: desc = "未知: " + filename; break;
            }

            return desc;
        }

        // 解析隐藏测试驱动的 PASSRATE 协议: 末尾输出一行 "PASSRATE <passed>/<total>"
        // 成功时从 stdout 中移除该行(不展示给用户), 并回填通过数与总案例数
        static bool ParsePassRate(std::string* stdout_text, int* passed, int* total)
        {
            std::istringstream iss(*stdout_text);
            std::string line;
            std::ostringstream keep;
            bool found = false;
            while(std::getline(iss, line))
            {
                if(line.compare(0, 9, "PASSRATE ") == 0)
                {
                    std::string nums = line.substr(9);
                    size_t slash = nums.find('/');
                    if(slash != std::string::npos)
                    {
                        *passed = std::atoi(nums.substr(0, slash).c_str());
                        *total = std::atoi(nums.substr(slash + 1).c_str());
                        found = true;
                        continue;   // 该行不返回给用户
                    }
                }
                keep << line << "\n";
            }
            if(found)
                *stdout_text = keep.str();
            return found;
        }

        // 输出规范化: 逐行去掉行尾空白, 并忽略末尾的空行(宽松 OJ 比较)
        static std::string NormalizeOutput(const std::string& text)
        {
            std::vector<std::string> lines;
            std::istringstream iss(text);
            std::string line;
            while(std::getline(iss,line))
            {
                size_t e = line.size();
                while(e > 0 && isspace((unsigned char)line[e-1]))
                    --e;
                lines.push_back(line.substr(0,e));
            }
            while(!lines.empty() && lines.back().empty())
                lines.pop_back();

            std::ostringstream os;
            for(size_t i = 0;i < lines.size();++i)
                os << lines[i] << "\n";
            return os.str();
        }

        // 实际输出与期望输出做规范化比对
        static bool OutputEquals(const std::string& actual,const std::string& expected)
        {
            return NormalizeOutput(actual) == NormalizeOutput(expected);
        }

        /*
         * 输入:
         * code: 完整源码(header + 用户代码 + tail, header/tail 可为空)
         * input: 程序标准输入(仅无 cases 时使用)
         * cases: [可选] [{input, expected}...] 批量判题: 编译一次, 逐案例写入 stdin 运行并比对 stdout
         * cpu_limit: 时间要求
         * mem_limit: 空间要求
         *
         * 输出:
         * 必填
         * status: 状态码
         * reason: 请求结果
         * 选填：
         * stdout: 我的程序运行完的结果(单次运行路径)
         * stderr: 我的程序运行完的错误结果
         * pass_count / total_count: 运行成功时上报的通过数/总案例数
         *    - 批量路径: 逐案例 stdout 规范化比对累计
         *    - 单次路径: 隐藏驱动按 PASSRATE 协议上报
         *
         * 参数：
         * in_json: {"code": "...", "cases":[{"input":"","expected":""}], "cpu_limit":1, "mem_limit":10240}
         *          或 {"code": "...", "input": "", "cpu_limit":1, "mem_limit":10240}
         * out_json: {"status":"0", "reason":"","stdout":"","stderr":"","pass_count":3,"total_count":5}
         */
        static void Start(const std::string &in_json, std::string *out_json)
        {
            assert(out_json);
            Json::Value in_value;
            Json::Reader reader;
            reader.parse(in_json, in_value);

            std::string code = in_value["code"].asString();
            std::string input = in_value["input"].asString();
            size_t cpu_limit = in_value["cpu_limit"].asLargestUInt();
            size_t mem_limit = in_value["mem_limit"].asLargestUInt();
            const bool batch = in_value["cases"].isArray() && in_value["cases"].size() > 0;

            int status = 0;
            int run_retsult = 0;
            Json::Value out_value;
            std::string filename;
            int pass_count = 0, total_count = 0;

            if(code.empty())
            {
                status = -1; // 代码为空
                out_value["status"] = status;
                out_value["reason"] = StatusToDesc(status,filename);
                Json::StyledWriter writer;
                *out_json = writer.write(out_value);
                return;
            }

            // 形成的文件名只具有唯一性，没有目录没有后缀
            // 毫秒级时间戳+原子性递增唯一值: 来保证唯一性
            filename = oj_util::File::UniqueFileName();

            // 形成临时src文件并编译一次
            oj_util::File::WriteFile(oj_util::Path::Src(filename), code);
            if(!Compiler::Compile(filename))
            {
                status = -3; // 编译失败
                out_value["status"] = status;
                out_value["reason"] = StatusToDesc(status,filename);
                Json::StyledWriter writer;
                *out_json = writer.write(out_value);
                RemoveTempFiles(filename);
                return;
            }

            if(batch)
            {
                // ---- 批量判题: 逐案例写入 stdin 运行, 规范化比对 stdout 与期望输出 ----
                const Json::Value& cases = in_value["cases"];
                for(Json::ArrayIndex i = 0;i < cases.size();++i)
                {
                    std::string case_input = cases[i]["input"].asString();
                    std::string expected   = cases[i]["expected"].asString();

                    oj_util::File::WriteFile(oj_util::Path::Stdin(filename), case_input);
                    // 清空上次运行的输出文件
                    ::unlink(oj_util::Path::Stdout(filename).c_str());
                    ::unlink(oj_util::Path::Stderr(filename).c_str());

                    run_retsult = Runner::Run(filename, cpu_limit, mem_limit);
                    if(run_retsult < 0)
                    {
                        status = -2; // 内部错误
                        break;
                    }
                    if(run_retsult > 0)
                    {
                        // 该案例运行异常(超时/超内存/信号): 以该信号作为整体状态返回
                        status = run_retsult;
                        break;
                    }

                    std::string actual;
                    oj_util::File::ReadFile(&actual,oj_util::Path::Stdout(filename),true);
                    ++total_count;
                    if(OutputEquals(actual, expected))
                        ++pass_count;
                }
                // 编译成功且所有案例均正常运行 -> status 保持 0
            }
            else
            {
                // ---- 旧式单次运行(兼容): 可选 input 作为标准输入 ----
                if(!input.empty())
                    oj_util::File::WriteFile(oj_util::Path::Stdin(filename), input);
                run_retsult = Runner::Run(filename, cpu_limit, mem_limit);
                if(run_retsult < 0)
                    status = -2; // 内部错误
                else if(run_retsult > 0)
                    status = run_retsult; // 程序运行异常(信号)
                else
                    status = 0; // 程序运行成功
            }

            out_value["status"] = status;
            out_value["reason"] = StatusToDesc(status,filename);
            if(status == 0)
            {
                if(batch)
                {
                    // 批量判题: 只以 pass_count/total_count 表达通过情况, 不返回案例输出
                    out_value["pass_count"] = pass_count;
                    out_value["total_count"] = total_count;
                    out_value["stdout"] = "";
                    out_value["stderr"] = "";
                }
                else
                {
                    std::string _stdout;
                    oj_util::File::ReadFile(&_stdout,oj_util::Path::Stdout(filename),true);
                    int passed = 0, total = 0;
                    if(ParsePassRate(&_stdout,&passed,&total))
                    {
                        out_value["pass_count"] = passed;
                        out_value["total_count"] = total;
                    }
                    out_value["stdout"] = _stdout;

                    std::string _stderr;
                    oj_util::File::ReadFile(&_stderr,oj_util::Path::Stderr(filename),true);
                    out_value["stderr"] = _stderr;
                }
            }

            Json::StyledWriter writer;
            *out_json = writer.write(out_value);

            RemoveTempFiles(filename);
        }
    };
}