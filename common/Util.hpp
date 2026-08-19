#pragma once
#include <iostream>
#include <string>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

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

        static const std::string CompilerError(const std::string &file_name)
        {
            return AddSuffix(file_name, ".compile_error");
        }

        static const std::string Stderror(const std::string &file_name)
        {
            return AddSuffix(file_name, ".stderr");
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
    };
}