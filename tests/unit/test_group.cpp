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
        for(int gid : created_groups_)
        {
            oj_user_model::UserModel m;
            m.DeleteGroup(gid);
        }
        created_groups_.clear();
        if(!question_id_.empty())
        {
            oj_mysqlmodel::Model m;
            m.DeleteQuestion(question_id_);
            question_id_.clear();
        }
    }

    // 登记待清理的小组(TearDown 统一删除)
    void TrackGroup(int gid)
    {
        created_groups_.push_back(gid);
    }

    int group_id_ = 0;
    std::vector<int> created_groups_;
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

// POST /api/groups(§5.7): 同一负责人可创建多个小组, 邀请码互不相同
TEST_F(GroupModelTest, LeaderCanCreateMultipleGroups)
{
    oj_user_model::UserModel m;
    oj_user_model::Group g1, g2;
    ASSERT_TRUE(m.CreateGroup("gtest_multi_" + std::to_string(std::time(nullptr)), 2, &g1));
    TrackGroup(g1._id);
    ASSERT_TRUE(m.CreateGroup("gtest_multi2_" + std::to_string(std::time(nullptr)), 2, &g2));
    TrackGroup(g2._id);

    EXPECT_GT(g1._id, 0);
    EXPECT_GT(g2._id, 0);
    EXPECT_NE(g1._id, g2._id);
    EXPECT_FALSE(g1._invite_code.empty());
    EXPECT_NE(g1._invite_code, g2._invite_code);   // 邀请码唯一

    // GetGroupsByOwner 能查到该负责人全部小组
    std::vector<oj_user_model::Group> owned;
    ASSERT_TRUE(m.GetGroupsByOwner(2, &owned));
    bool has1 = false, has2 = false;
    for(const auto& g : owned)
    {
        has1 = has1 || (g._id == g1._id);
        has2 = has2 || (g._id == g2._id);
    }
    EXPECT_TRUE(has1 && has2);
}

// POST /api/groups/join(§5.8): 凭小组邀请码加入, 重复加入失败
TEST_F(GroupModelTest, JoinByInviteCodeThenDuplicateJoinFails)
{
    oj_user_model::UserModel m;
    oj_user_model::Group g;
    ASSERT_TRUE(m.CreateGroup("gtest_join_" + std::to_string(std::time(nullptr)), 2, &g));
    TrackGroup(g._id);

    // 无效邀请码查不到小组
    oj_user_model::Group not_found;
    EXPECT_FALSE(m.GetGroupByInvite("no-such-invite", &not_found));

    // 有效邀请码可查到小组
    oj_user_model::Group found;
    ASSERT_TRUE(m.GetGroupByInvite(g._invite_code, &found));
    EXPECT_EQ(found._id, g._id);

    // 加入成功, 可查询到成员关系
    const int member_user = 888888;
    ASSERT_TRUE(m.JoinGroup(member_user, g._id));
    EXPECT_TRUE(m.IsMember(member_user, g._id));

    // group_members 联合主键: 重复加入失败
    EXPECT_FALSE(m.JoinGroup(member_user, g._id));
    EXPECT_TRUE(m.IsMember(member_user, g._id));
}

// POST /api/groups/{id}/invite(§5.9): 重置邀请码后新码生效、旧码失效
TEST_F(GroupModelTest, ResetInviteCodeInvalidatesOldCode)
{
    oj_user_model::UserModel m;
    oj_user_model::Group g;
    ASSERT_TRUE(m.CreateGroup("gtest_inv_" + std::to_string(std::time(nullptr)), 2, &g));
    TrackGroup(g._id);
    std::string old_code = g._invite_code;

    std::string new_code;
    ASSERT_TRUE(m.ResetInviteCode(g._id, &new_code));
    EXPECT_FALSE(new_code.empty());
    EXPECT_NE(new_code, old_code);

    // 旧码失效, 新码可定位到同一小组
    oj_user_model::Group gone;
    EXPECT_FALSE(m.GetGroupByInvite(old_code, &gone));
    oj_user_model::Group found;
    ASSERT_TRUE(m.GetGroupByInvite(new_code, &found));
    EXPECT_EQ(found._id, g._id);
}

// 组间可见性数据: 小组 owner 记录正确, 非成员查询成员关系为空
TEST_F(GroupModelTest, GroupVisibilityDataIsolation)
{
    oj_user_model::UserModel m;
    oj_user_model::Group g;
    ASSERT_TRUE(m.CreateGroup("gtest_vis_" + std::to_string(std::time(nullptr)), 3, &g));
    TrackGroup(g._id);

    // GetGroupsByOwner 能看到 owner 拥有的该小组
    std::vector<oj_user_model::Group> owned;
    ASSERT_TRUE(m.GetGroupsByOwner(3, &owned));
    bool owner_found = false;
    for(const auto& grp : owned)
        owner_found = owner_found || (grp._id == g._id);
    EXPECT_TRUE(owner_found);

    // 非成员查询成员关系为空
    EXPECT_FALSE(m.IsMember(555555, g._id));
    std::vector<int> gids;
    ASSERT_TRUE(m.GetUserGroups(555555, &gids));
    EXPECT_EQ(std::find(gids.begin(), gids.end(), g._id), gids.end());
}
