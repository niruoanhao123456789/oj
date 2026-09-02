#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <algorithm>
#include <ctime>
#include "../../oj_server/user/oj_user_model.hpp"
#include "../../oj_server/oj_mysqlmodel.hpp"
#include "test_env.hpp"

// 小组模型(创建/成员关系/删除)依赖 MySQL, 数据库不可用时跳过
class GroupModelTest : public ::testing::Test
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
            GTEST_SKIP() << "MySQL 不可用, 跳过小组相关单元测试";
    }

    void TearDown() override
    {
        if(group_id_ > 0)
        {
            oj_user_model::UserModel m;
            m.DeleteGroup(group_id_);
            group_id_ = 0;
        }
        if(!question_id_.empty())
        {
            oj_mysqlmodel::Model m;
            m.DeleteQuestion(question_id_);
            question_id_.clear();
        }
    }

    int group_id_ = 0;
    std::string question_id_;
};

// 创建小组后删除: 删除生效, 小组查询返回不存在
TEST_F(GroupModelTest, CreateThenDelete)
{
    oj_user_model::UserModel m;
    oj_user_model::Group g;
    std::string name = "gtest_group_" + std::to_string(std::time(nullptr));
    ASSERT_TRUE(m.CreateGroup(name, 1, &g));
    group_id_ = g._id;
    ASSERT_GT(group_id_, 0);
    ASSERT_FALSE(g._invite_code.empty());

    oj_user_model::Group back;
    ASSERT_TRUE(m.GetGroupById(group_id_, &back));
    EXPECT_EQ(back._name, name);

    ASSERT_TRUE(m.DeleteGroup(group_id_));
    group_id_ = 0;
    oj_user_model::Group gone;
    EXPECT_FALSE(m.GetGroupById(g._id, &gone));
}

// 删除小组级联清除成员关系与组内题目(scope=小组id)
TEST_F(GroupModelTest, DeleteRemovesMembersAndGroupQuestions)
{
    oj_user_model::UserModel m;
    oj_user_model::Group g;
    ASSERT_TRUE(m.CreateGroup("gtest_group_" + std::to_string(std::time(nullptr)), 2, &g));
    group_id_ = g._id;

    // 成员关系
    const int member_user = 999999;
    ASSERT_TRUE(m.JoinGroup(member_user, group_id_));
    EXPECT_TRUE(m.IsMember(member_user, group_id_));

    // 组内题目
    oj_mysqlmodel::Model qm;
    oj_mysqlmodel::Question q;
    q._title = "gtest_group_q_" + std::to_string(std::time(nullptr));
    q._rank = oj_mysqlmodel::Question::EASY;
    q._desc = "desc";
    q._header = "h";
    q._answer = "a";
    q._tail = "t";
    q._cpu_limit = 1;
    q._mem_limit = 30;
    q._scope = std::to_string(group_id_);
    ASSERT_TRUE(qm.AddQuestion(&q));
    question_id_ = q._id;

    // 删除小组
    ASSERT_TRUE(m.DeleteGroup(group_id_));
    group_id_ = 0;

    // 小组不存在
    oj_user_model::Group gone;
    EXPECT_FALSE(m.GetGroupById(g._id, &gone));
    // 成员关系清除
    EXPECT_FALSE(m.IsMember(member_user, g._id));
    std::vector<int> gids;
    m.GetUserGroups(member_user, &gids);
    EXPECT_EQ(std::find(gids.begin(), gids.end(), g._id), gids.end());
    // 组内题目清除
    oj_mysqlmodel::Question gone_q;
    qm.GetOneQuestion(q._id, &gone_q);
    EXPECT_TRUE(gone_q._id.empty());
}
