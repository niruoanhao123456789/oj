#include <iostream>
#include <string>
#include <vector>
#include <cassert>
#include <fstream>
#include <unordered_map>
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
        std::string _header;    // 题目预设给用户在线编辑器的代码
        std::string _tail;    // 题目的测试用例，需要和header拼接，形成完整代码
    };

    const std::string questions_list = "./questions/questions.list";
    const std::string questions_path = "./questions/";

    class FileModel
    {
    public:
        FileModel()
        {
            LoadQuestionsList(questions_list);
        }

        void LoadQuestionsList(const std::string& questions_list)
        {
            std::ifstream in(questions_list);
            assert(!in.is_open());

            std::string line;
            while(std::getline(in,line))
            {
                std::vector<std::string> tokens;
                UtilString::SplitString(line,&tokens);

                if(tokens.size() != 5)
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

                std::string path = questions_path + q._id + "/";
                File::ReadFile(&(q._desc),path+"desc.txt",true);
                File::ReadFile(&(q._header),path+"header.cpp",true);
                File::ReadFile(&(q._tail),path+"tail.cpp",true);

                _questions[q._id] = q;
            }
            LOG_INFOR(GetLogger("oj_Logger"),"%s","adding question succeed");
            in.close();
        }

        void GetAllQuestions(std::vector<oj_filemodel::Question>* out)
        {
            assert(out);
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
            auto iter = _questions.find(id);
            if(iter == _questions.end())
            {
                LOG_ERROR(GetLogger("oj_Logger"),"%s%d","GetOneQuestion failed! question id: ",id);
                return;
            }
            (*q) = iter->second;
        }
        
    private:
        std::unordered_map<std::string,oj_filemodel::Question> _questions;
    };
}