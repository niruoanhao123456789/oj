#include <gtest/gtest.h>
#include <string>
#include <ctime>
#include "../../oj_server/oj_mysqlmodel.hpp"
#include "test_env.hpp"

class MysqlModelTest : public ::testing::Test
{
protected:
    static bool DbUp()
    {
        oj_mysqlmodel::Model m;
        return m.ExecuteSql("select 1");
    }

    void SetUp() override
    {
        if(!DbUp())
            GTEST_SKIP() << "MySQL 不可用, 跳过 MySQL 相关单元测试";
    }

    void TearDown() override
    {
        if(!created_id_.empty())
        {
            oj_mysqlmodel::Model m;
            m.DeleteQuestion(created_id_);
            created_id_.clear();
        }
    }

    std::string created_id_;
};

// Add/Update/Delete 往返: id 回填、字段与 scope 持久化、删除生效
TEST_F(MysqlModelTest, AddUpdateDeleteRoundTrip)
{
    oj_mysqlmodel::Model m;
    oj_mysqlmodel::Question q;
    q._title = "gtest_roundtrip_" + std::to_string(std::time(nullptr));
    q._rank = oj_mysqlmodel::Question::EASY;
    q._desc = "描述";
    q._header = "// header";
    q._answer = "// answer";
    q._tail = "// tail";
    q._cpu_limit = 1;
    q._mem_limit = 30;
    q._scope = "global";
    ASSERT_TRUE(m.AddQuestion(&q));
    created_id_ = q._id;
    ASSERT_FALSE(q._id.empty());

    oj_mysqlmodel::Question back;
    m.GetOneQuestion(q._id, &back);
    ASSERT_EQ(back._id, q._id);
    EXPECT_EQ(back._title, q._title);
    EXPECT_EQ(back._rank, oj_mysqlmodel::Question::EASY);
    EXPECT_EQ(back._desc, "描述");
    EXPECT_EQ(back._scope, "global");

    // 更新 title 与 scope
    back._title = q._title + "_改";
    back._scope = "42";
    ASSERT_TRUE(m.UpdateQuestion(back));
    oj_mysqlmodel::Question back2;
    m.GetOneQuestion(q._id, &back2);
    EXPECT_EQ(back2._title, q._title + "_改");
    EXPECT_EQ(back2._scope, "42");

    ASSERT_TRUE(m.DeleteQuestion(q._id));
    created_id_.clear();
    oj_mysqlmodel::Question gone;
    m.GetOneQuestion(q._id, &gone);
    EXPECT_TRUE(gone._id.empty());
}

// 转义防注入: 含引号/反斜杠/分号的标题可原样存取
TEST_F(MysqlModelTest, EscapePreventsInjection)
{
    oj_mysqlmodel::Model m;
    oj_mysqlmodel::Question q;
    q._title = "it's a \"tricky\" \\ title; DROP TABLE questions; --";
    q._rank = oj_mysqlmodel::Question::NORMAL;
    q._desc = "desc";
    q._header = "h";
    q._answer = "a";
    q._tail = "t";
    q._cpu_limit = 1;
    q._mem_limit = 30;
    q._scope = "global";
    ASSERT_TRUE(m.AddQuestion(&q));
    created_id_ = q._id;

    oj_mysqlmodel::Question back;
    m.GetOneQuestion(q._id, &back);
    EXPECT_EQ(back._title, q._title);

    ASSERT_TRUE(m.DeleteQuestion(q._id));
    created_id_.clear();
}
