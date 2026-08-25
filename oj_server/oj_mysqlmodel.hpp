#pragma once
#include <iostream>
#include <string>
#include <vector>
#include <cassert>
#include <fstream>
#include <unordered_map>
#include <mysql/mysql.h>
#include "../common/log/Log.hpp"
#include "../common/Util.hpp"

namespace oj_mysqlmodel
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
    };

    const std::string database = "oj";
    const std::string table = "questions";
    const std::string host = "127.0.0.1";
    const std::string user = "oj_client";
    const std::string password = "1234";
    const size_t port = 3306;
    

    class Model
    {
    public:
        void QueryMysql(const std::string& sql, std::vector<oj_mysqlmodel::Question>* out)
        {
            assert(out);
            // 创建mysql句柄
            MYSQL* my = mysql_init(nullptr);

            // 连接数据库
            if(mysql_real_connect(my,host.c_str(),user.c_str(),password.c_str(),database.c_str(),port,nullptr,0) == nullptr)
            {
                LOG_FATAL(GetLogger("oj_Logger"),"%s","connect database failed!");
                return;
            }

            // 设置该链接的编码格式, 避免出现乱码问题
            mysql_set_character_set(my,"utf-8");

            LOG_INFOR(GetLogger("oj_Logger"),"%s","connect database succeed");

            // 执行sql语句
            if(mysql_query(my,sql.c_str()))
            {
                LOG_WARNNING(GetLogger("oj_Logger"),"%s","execute error!");
                return;
            }

            // 提取结果
            MYSQL_RES* res = mysql_store_result(my);

            // 分析结果
            int rows = mysql_num_rows(res);
            int cols = mysql_num_fields(res);

            oj_mysqlmodel::Question q;
            for(int i=0;i<rows;i++)
            {
                MYSQL_ROW row = mysql_fetch_row(res);
                q._id = row[0];
                q._title = row[1];
                q._rank = oj_mysqlmodel::Question::StringToRank(row[2]);
                q._desc = row[3];
                q._header = row[4];
                q._answer = row[5];
                q._tail = row[6];
                q._cpu_limit = std::atoll(row[7]);
                q._mem_limit = std::atoll(row[8]);

                out->emplace_back(q);
            }
            // 释放结果空间
            mysql_free_result(res);
            // 关闭mysql连接
            mysql_close(my);
        }

        void GetAllQuestions(std::vector<oj_mysqlmodel::Question>* out)
        {
            assert(out);
            std::string sql = "select * from ";
            sql += table;
            QueryMysql(sql,out);
        }

        void GetOneQuestion(const std::string& id,oj_mysqlmodel::Question* q)
        {
            assert(q);
            std::string sql = "select * from " + table + " where id=" + id;
            std::vector<oj_mysqlmodel::Question> ret;
            QueryMysql(sql,&ret);
            if(ret.size()==1)
            {
                *q = ret[0];
            }
        }
    };
}