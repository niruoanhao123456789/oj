#pragma once
#include <iostream>
#include <string>
#include <vector>
#include <cassert>
#include <fstream>
#include <algorithm>
#include <mutex>
#include <cstdio>
#include <unordered_map>
#include <sys/stat.h>
#include <unistd.h>
#include "../common/log/Log.hpp"
#include "../common/Util.hpp"

namespace oj_filemodel
{
    using namespace LogModule;
    using namespace oj_util;

    struct Question
    {
        enum Rank
        {
            EASY,
            NORMAL,
            DIFFICULT,
            UNKONW
        };

        static Rank StringToRank(const std::string& rank)
        {
            if(rank == "简单")
                return Rank::EASY;
            else if(rank == "中等")
                return Rank::NORMAL;
            else if(rank == "困难")
                return Rank::DIFFICULT;
            else
            {
                LOG_WARNNING(GetLogger("oj_Logger"),"%s","the rank string is undefined!");
                return Rank::UNKONW;
            }
        }

        const std::string RankToString() const
        {
            switch(_rank)
            {
                case EASY:      return "简单";
                case NORMAL:    return "中等";
                case DIFFICULT: return "困难";
                default:        return "未知";
            }
        }

        std::string _id;        // 题目编号，唯一
        std::string _title;     // 题目的标题
        Rank _rank;             // 难度: 简单 中等 困难
        size_t _cpu_limit;      // 题目的时间要求(S)
        size_t _mem_limit;      // 题目的空间要去(MB)
        std::string _desc;      // 题目的描述
        std::string _header;    // 题目隐藏的头文件，只有接口模式下有，IO模式为空
        std::string _answer;    // 题目预设给用户在线编辑器的代码
        std::string _tail;      // 题目的测试用例，需要和header拼接，形成完整代码
        std::string _scope = "global";  // 可见范围: "global" 表示全局题, 否则为小组id
    };

    const std::string questions_list = "./questions/questions.list";
    const std::string questions_path = "./questions/";

    class Model
    {
    public:
        Model()
        {
            LoadQuestionsList(questions_list);
        }

        void LoadQuestionsList(const std::string& questions_list)
        {
            std::ifstream in(questions_list);
            assert(in.is_open());

            std::string line;
            while(std::getline(in,line))
            {
                std::vector<std::string> tokens;
                UtilString::SplitString(line,&tokens);

                // 兼容旧5列格式, 缺省 scope=global
                if(tokens.size() != 5 && tokens.size() != 6)
                {
                    LOG_WARNNING(GetLogger("oj_Logger"),"%s","Some questions load failed! Please check the format in questions.list.");
                    continue;
                }

                oj_filemodel::Question q;
                q._id = tokens[0];
                q._title = tokens[1];
                q._rank = oj_filemodel::Question::StringToRank(tokens[2]);
                q._cpu_limit = std::atoll(tokens[3].c_str());
                q._mem_limit = std::atoll(tokens[4].c_str());
                q._scope = (tokens.size() == 6) ? tokens[5] : "global";

                std::string path = questions_path + q._id + "/";
                File::ReadFile(&(q._desc),path+"desc.txt",true);
                File::ReadFile(&(q._header),path+"header.cpp",true);
                File::ReadFile(&(q._answer),path+"answer.cpp",true);
                File::ReadFile(&(q._tail),path+"tail.cpp",true);

                _questions[q._id] = q;
            }
            LOG_INFOR(GetLogger("oj_Logger"),"%s","adding question succeed");
            in.close();
        }

        void GetAllQuestions(std::vector<oj_filemodel::Question>* out)
        {
            assert(out);
            std::lock_guard<std::mutex> lock(_mutex);
            if(_questions.empty())
            {
                LOG_ERROR(GetLogger("oj_Logger"),"%s","GetAllQuestions failed!");
                return;
            }

            for(const auto& q:_questions)
            {
                out->emplace_back(q.second);
            }
        }

        void GetOneQuestion(const std::string& id,oj_filemodel::Question* q)
        {
            assert(q);
            std::lock_guard<std::mutex> lock(_mutex);
            auto iter = _questions.find(id);
            if(iter == _questions.end())
            {
                LOG_ERROR(GetLogger("oj_Logger"),"%s%d","GetOneQuestion failed! question id: ",id);
                return;
            }
            (*q) = iter->second;
        }

        // 新增题目: 分配 max(id)+1, 创建目录, 写入四个文件, 追加列表行
        bool AddQuestion(oj_filemodel::Question* q)
        {
            assert(q);
            std::lock_guard<std::mutex> lock(_mutex);

            size_t max_id = 0;
            for(const auto& kv : _questions)
            {
                max_id = std::max(max_id,(size_t)std::atoll(kv.first.c_str()));
            }
            q->_id = std::to_string(max_id + 1);

            std::string path = questions_path + q->_id + "/";
            if(mkdir(path.c_str(),0755) != 0)
            {
                LOG_ERROR(GetLogger("oj_Logger"),"%s%s","add question failed! mkdir error: ",path.c_str());
                return false;
            }
            File::WriteFile(path+"desc.txt",q->_desc);
            File::WriteFile(path+"header.cpp",q->_header);
            File::WriteFile(path+"answer.cpp",q->_answer);
            File::WriteFile(path+"tail.cpp",q->_tail);

            _questions[q->_id] = *q;
            AppendQuestionsListLine(*q);
            LOG_INFOR(GetLogger("oj_Logger"),"%s%s","add question succeed! id: ",q->_id.c_str());
            return true;
        }

        // 修改题目: 覆盖四个文件并重建列表
        bool UpdateQuestion(const oj_filemodel::Question& q)
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if(_questions.find(q._id) == _questions.end())
            {
                LOG_ERROR(GetLogger("oj_Logger"),"%s%s","update question failed! question id: ",q._id.c_str());
                return false;
            }

            std::string path = questions_path + q._id + "/";
            File::WriteFile(path+"desc.txt",q._desc);
            File::WriteFile(path+"header.cpp",q._header);
            File::WriteFile(path+"answer.cpp",q._answer);
            File::WriteFile(path+"tail.cpp",q._tail);

            _questions[q._id] = q;
            WriteQuestionsList();
            LOG_INFOR(GetLogger("oj_Logger"),"%s%s","update question succeed! id: ",q._id.c_str());
            return true;
        }

        // 删除题目: 删除文件与目录并重建列表
        bool DeleteQuestion(const std::string& id)
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if(_questions.find(id) == _questions.end())
            {
                LOG_ERROR(GetLogger("oj_Logger"),"%s%s","delete question failed! question id: ",id.c_str());
                return false;
            }

            std::string path = questions_path + id + "/";
            ::remove((path+"desc.txt").c_str());
            ::remove((path+"header.cpp").c_str());
            ::remove((path+"answer.cpp").c_str());
            ::remove((path+"tail.cpp").c_str());
            ::rmdir(path.c_str());

            _questions.erase(id);
            WriteQuestionsList();
            LOG_INFOR(GetLogger("oj_Logger"),"%s%s","delete question succeed! id: ",id.c_str());
            return true;
        }

    private:
        // 按数字id升序重写 questions.list(6列)
        void WriteQuestionsList()
        {
            std::vector<std::string> ids;
            for(const auto& kv : _questions)
            {
                ids.push_back(kv.first);
            }
            std::sort(ids.begin(),ids.end(),[](const std::string& a,const std::string& b){
                return std::atoll(a.c_str()) < std::atoll(b.c_str());
            });

            std::ofstream out(questions_list);
            assert(out.is_open());
            for(const auto& id : ids)
            {
                const oj_filemodel::Question& q = _questions[id];
                out << q._id << " " << q._title << " " << q.RankToString() << " "
                    << q._cpu_limit << " " << q._mem_limit << " " << q._scope << "\n";
            }
            out.close();
        }

        // 向 questions.list 追加一行
        void AppendQuestionsListLine(const oj_filemodel::Question& q)
        {
            std::ofstream out(questions_list,std::ios::app);
            assert(out.is_open());
            out << q._id << " " << q._title << " " << q.RankToString() << " "
                << q._cpu_limit << " " << q._mem_limit << " " << q._scope << "\n";
            out.close();
        }

        std::unordered_map<std::string,oj_filemodel::Question> _questions;
        std::mutex _mutex;
    };
}