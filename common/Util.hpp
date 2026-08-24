#pragma once
#include <iostream>
#include <string>
#include <atomic>
#include <cassert>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <boost/algorithm/string.hpp>

namespace oj_util
{
    const std::string tmp_path = "./temp/";

    class Path
    {
    public:
        static const std::string AddSuffix(const std::string& filename,const std::string& suffix)
        {
            return (tmp_path + filename + suffix);           
        }
        
        // 编译时需要有的临时文件
        // 构建源文件路径+后缀的完整文件名
        // 1234 -> ./temp/1234.cpp
        static const std::string Src(const std::string& filename)
        {
            return AddSuffix(filename,".cpp");
        }

        // 构建可执行程序的完整路径+后缀名
        static const std::string Exe(const std::string& filename)
        {
            return AddSuffix(filename,".exe");
        }

        static const std::string CompilerError(const std::string &filename)
        {
            return AddSuffix(filename, ".compile_error");
        }

        // 运行时需要的临时文件
        static const std::string Stdin(const std::string &filename)
        {
            return AddSuffix(filename, ".stdin");
        }
        static const std::string Stdout(const std::string &filename)
        {
            return AddSuffix(filename, ".stdout");
        }
        // 构建该程序对应的标准错误完整的路径+后缀名
        static const std::string Stderr(const std::string &filename)
        {
            return AddSuffix(filename, ".stderr");
        }
    };

    class Time
    {
    public:
        static const std::string GetTimeStamp()
        {
            struct timeval tv;
            gettimeofday(&tv,nullptr);
            return std::to_string(tv.tv_sec);
        }

        static const std::string GetTimeMs()
        {
            struct timeval tv;
            gettimeofday(&tv,nullptr);
            return std::to_string(tv.tv_sec * 1000 + tv.tv_usec / 1000);
        }
    };

    class File
    {
    public:
        static bool IsFileExists(const std::string& filename)
        {
            struct stat st;
            if(!stat(filename.c_str(),&st))
                return true;
            
            return false;
        }

        static const std::string UniqueFileName()
        {
            static std::atomic_uint id(0);
            ++id;
            // 毫秒级时间戳+原子性递增唯一值: 来保证唯一性
            std::string ms = Time::GetTimeMs();
            std::string uniq_id = std::to_string(id);
            return ms + "_" + uniq_id;
        }

        static void WriteFile(const std::string& dest, const std::string& src)
        {
            std::ofstream out(dest);
            assert(out.is_open());
            out.write(src.c_str(),src.size());
            out.close();
        }

        static void ReadFile(std::string* dest, const std::string& src,bool keep = false)
        {
            dest->clear();

            std::ifstream in(src);
            assert(in.is_open());

            std::string line;
            // getline:不保存行分割符,有些时候需要保留\n,
            // getline内部重载了强制类型转化
            while(std::getline(in,line))
            {
                (*dest) += line;
                (*dest) += (keep ? "\n" : "");
            }
            in.close();
        }
    };

    class UtilString
    {
    public:
        static void SplitString(const std::string& str,std::vector<std::string>* target, const std::string& sep = " ")
        {
            assert(target);
            boost::split(*target,str,boost::is_any_of(sep),boost::algorithm::token_compress_on);
        }
    };
}