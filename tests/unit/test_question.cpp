#include <gtest/gtest.h>
#include "../../oj_server/oj_mysqlmodel.hpp"
#include "../../oj_server/oj_filemodel.hpp"
#include "test_env.hpp"

// 两个模型共用同一套 Question::Rank 转换逻辑, 分别验证
TEST(MysqlQuestionRankTest, StringToRank)
{
    using Q = oj_mysqlmodel::Question;
    EXPECT_EQ(Q::StringToRank("简单"), Q::EASY);
    EXPECT_EQ(Q::StringToRank("中等"), Q::NORMAL);
    EXPECT_EQ(Q::StringToRank("困难"), Q::DIFFICULT);
    EXPECT_EQ(Q::StringToRank("未知等级"), Q::UNKONW);
}

TEST(MysqlQuestionRankTest, RankToString)
{
    oj_mysqlmodel::Question q;
    q._rank = oj_mysqlmodel::Question::EASY;
    EXPECT_EQ(q.RankToString(), "简单");
    q._rank = oj_mysqlmodel::Question::NORMAL;
    EXPECT_EQ(q.RankToString(), "中等");
    q._rank = oj_mysqlmodel::Question::DIFFICULT;
    EXPECT_EQ(q.RankToString(), "困难");
    q._rank = oj_mysqlmodel::Question::UNKONW;
    EXPECT_EQ(q.RankToString(), "未知");
}

TEST(FileQuestionRankTest, StringToRank)
{
    using Q = oj_filemodel::Question;
    EXPECT_EQ(Q::StringToRank("简单"), Q::EASY);
    EXPECT_EQ(Q::StringToRank("中等"), Q::NORMAL);
    EXPECT_EQ(Q::StringToRank("困难"), Q::DIFFICULT);
    EXPECT_EQ(Q::StringToRank("未知等级"), Q::UNKONW);
}

TEST(FileQuestionRankTest, RankToString)
{
    oj_filemodel::Question q;
    q._rank = oj_filemodel::Question::EASY;
    EXPECT_EQ(q.RankToString(), "简单");
    q._rank = oj_filemodel::Question::NORMAL;
    EXPECT_EQ(q.RankToString(), "中等");
    q._rank = oj_filemodel::Question::DIFFICULT;
    EXPECT_EQ(q.RankToString(), "困难");
    q._rank = oj_filemodel::Question::UNKONW;
    EXPECT_EQ(q.RankToString(), "未知");
}

// Question 默认可见范围为 global
TEST(QuestionTest, DefaultScopeIsGlobal)
{
    oj_mysqlmodel::Question mq;
    EXPECT_EQ(mq._scope, "global");
    oj_filemodel::Question fq;
    EXPECT_EQ(fq._scope, "global");
}
