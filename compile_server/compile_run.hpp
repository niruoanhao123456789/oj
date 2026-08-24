#pragma once

#include <jsoncpp/json/json.h>
#include <unistd.h>
#include <cassert>
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

        /*
         * 输入:
         * code： 用户提交的代码
         * input: 用户给自己提交的代码对应的输入，不做处理
         * cpu_limit: 时间要求
         * mem_limit: 空间要求
         *
         * 输出:
         * 必填
         * status: 状态码
         * reason: 请求结果
         * 选填：
         * stdout: 我的程序运行完的结果
         * stderr: 我的程序运行完的错误结果
         *
         * 参数：
         * in_json: {"code": "#include...", "input": "","cpu_limit":1, "mem_limit":10240}
         * out_json: {"status":"0", "reason":"","stdout":"","stderr":"",}
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

            int status = 0;
            int run_retsult = 0;
            Json::Value out_value;
            std::string filename;

            int cnt = 10;
            while (1)
            {
                if (!code.size())
                {
                    status = -1; // 代码为空
                    break;
                }

                // 形成的文件名只具有唯一性，没有目录没有后缀
                // 毫秒级时间戳+原子性递增唯一值: 来保证唯一性
                filename = oj_util::File::UniqueFileName();

                // 形成临时src文件
                oj_util::File::WriteFile(oj_util::Path::Src(filename), code);

                if (!Compiler::Compile(filename))
                {
                    status = -3; // 编译失败
                    break;
                }

                run_retsult = Runner::Run(filename, cpu_limit, mem_limit);
                if (run_retsult < 0)
                {
                    status = -2; // 内部错误
                    break;
                }
                else if (run_retsult > 0)
                {
                    // 程序运行异常
                    status = run_retsult;
                    break;
                }
                else
                {
                    // 程序运行成功
                    status = 0;
                    break;
                }

                break;
            }

            out_value["status"] = status;
            out_value["reason"] = StatusToDesc(status,filename);
            if(status == 0)
            {
                std::string _stdout;
                oj_util::File::ReadFile(&_stdout,oj_util::Path::Stdout(filename),true);
                out_value["stdout"] = _stdout;

                std::string _stderr;
                oj_util::File::ReadFile(&_stderr,oj_util::Path::Stderr(filename),true);
                out_value["stderr"] = _stderr;
            }

            Json::StyledWriter writer;
            *out_json = writer.write(out_value);

            RemoveTempFiles(filename);
        }
    };
}