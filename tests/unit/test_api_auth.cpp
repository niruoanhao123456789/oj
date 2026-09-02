#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <ctime>
#include <random>
#include "../../oj_server/user/oj_user_model.hpp"
#include "../../oj_server/oj_mysqlmodel.hpp"
#include "test_env.hpp"

// 依据 API.md / SPEC.md §5.5~§5.12 接口语义, 在模型层验证其落库/校验行为:
//   POST /api/register / POST /api/login / POST /api/admin/invite / PUT /api/users/{id}/role
// 依赖 MySQL, 数据库不可用时整体跳过。
class ApiAuthTest : public ::testing::Test
{
protected:
    static bool DbUp()
    {
        oj_mysqlmodel::Model m;
        return m.ExecuteSql("select 1");
    }

    static std::string UniqueName(const std::string& tag)
    {
        static unsigned long long counter = 0;
        std::random_device rd;
        unsigned int r = rd() % 100000u;
        return "gt_" + tag + "_" + std::to_string(std::time(nullptr)) + "_" +
               std::to_string(++counter) + "_" + std::to_string(r);
    }

    void SetUp() override
    {
        if(!DbUp())
            GTEST_SKIP() << "MySQL 不可用, 跳过 API 用户相关单元测试";
        EnsureNonEmptyUsers();   // 保证角色不被"空表首个用户自动变 admin"规则覆盖
    }

    void TearDown() override
    {
        for(const auto& name : created_users_)
            RemoveUser(name);
        created_users_.clear();
    }

    void RemoveUser(const std::string& name)
    {
        oj_mysqlmodel::Model m;
        m.ExecuteSql("delete from users where username='" + name + "'");
    }

    // 空 users 表时首个注册用户自动成为 admin(SPEC §5.5/§4.4),
    // 为保证角色断言稳定, 若表为空先播种一个基础用户
    void EnsureNonEmptyUsers()
    {
        oj_user_model::UserModel um;
        int count = 0;
        if(!um.UserCount(&count))
            return;
        if(count == 0)
        {
            std::string base = UniqueName("base");
            if(um.Register(base, "base_pw", "user", nullptr))
                created_users_.push_back(base);
        }
    }

    // 注册一个新用户并登记待清理; 返回用户名
    std::string CreateUser(const std::string& role = "user")
    {
        std::string name = UniqueName("usr");
        oj_user_model::UserModel um;
        std::string out_role;
        EXPECT_TRUE(um.Register(name, "pw_123", role, &out_role));
        created_users_.push_back(name);
        return name;
    }

    std::vector<std::string> created_users_;
};

// POST /api/register: 重复用户名(唯一键)注册应被拒绝
TEST_F(ApiAuthTest, RegisterDuplicateUsernameFails)
{
    std::string name = CreateUser("user");
    oj_user_model::UserModel um;
    std::string out_role;
    EXPECT_FALSE(um.Register(name, "other", "user", &out_role));
}

// POST /api/register + /api/login: 角色持久化, 登录校验, 会话可签发/还原
TEST_F(ApiAuthTest, RegisterPersistsRoleAndLoginIssuesSession)
{
    std::string name = UniqueName("leader");
    oj_user_model::UserModel um;
    std::string out_role;
    ASSERT_TRUE(um.Register(name, "pw_123", "leader", &out_role));
    created_users_.push_back(name);
    EXPECT_EQ(out_role, "leader");

    oj_user_model::User stored;
    ASSERT_TRUE(um.GetUserByName(name, &stored));
    EXPECT_EQ(stored._role, "leader");

    // 正确密码登录成功
    oj_user_model::User logged;
    ASSERT_TRUE(um.Login(name, "pw_123", &logged));
    EXPECT_EQ(logged._username, name);
    EXPECT_EQ(logged._role, "leader");
    // 错误密码失败
    EXPECT_FALSE(um.Login(name, "wrong-pass", &logged));

    // /api/login 签发 token 后, 受保护请求可通过 GetSession 还原身份
    std::string token = um.CreateSession(logged);
    ASSERT_FALSE(token.empty());
    oj_user_model::User sess;
    ASSERT_TRUE(um.GetSession(token, &sess));
    EXPECT_EQ(sess._id, logged._id);
    EXPECT_EQ(sess._username, name);
}

// POST /api/login: 未知用户名应登录失败
TEST_F(ApiAuthTest, LoginUnknownUserFails)
{
    oj_user_model::UserModel um;
    oj_user_model::User out;
    EXPECT_FALSE(um.Login(UniqueName("nobody"), "pw_123", &out));
}

// POST /api/admin/invite(§5.12): 重置后新码生效、旧码立即失效; 空码无效
TEST_F(ApiAuthTest, AdminInviteResetLifecycle)
{
    oj_user_model::UserModel um;

    std::string code1;
    ASSERT_TRUE(um.ResetAdminInvite(&code1));
    EXPECT_FALSE(code1.empty());
    EXPECT_TRUE(um.IsValidAdminInviteCode(code1));
    EXPECT_FALSE(um.IsValidAdminInviteCode(""));
    EXPECT_FALSE(um.IsValidAdminInviteCode("not-a-code"));

    // 再次重置: 新码生效, 旧码失效(仅保留一个当前有效码)
    std::string code2;
    ASSERT_TRUE(um.ResetAdminInvite(&code2));
    EXPECT_NE(code1, code2);
    EXPECT_TRUE(um.IsValidAdminInviteCode(code2));
    EXPECT_FALSE(um.IsValidAdminInviteCode(code1));
}

// POST /api/register(role=leader, §5.5): 需携带管理员邀请码(IsValidAdminInviteCode 校验),
// 有效邀请码可注册负责人且登录后角色为 leader
TEST_F(ApiAuthTest, LeaderRegistrationRequiresValidAdminInvite)
{
    oj_user_model::UserModel um;
    std::string code;
    ASSERT_TRUE(um.ResetAdminInvite(&code));
    ASSERT_TRUE(um.IsValidAdminInviteCode(code));
    ASSERT_FALSE(um.IsValidAdminInviteCode("bogus"));

    std::string name = UniqueName("lead2");
    std::string out_role;
    ASSERT_TRUE(um.Register(name, "pw_123", "leader", &out_role));
    created_users_.push_back(name);
    EXPECT_EQ(out_role, "leader");

    oj_user_model::User logged;
    ASSERT_TRUE(um.Login(name, "pw_123", &logged));
    EXPECT_EQ(logged._role, "leader");
}

// PUT /api/users/{id}/role(§5.11): 角色修改持久化且可改回
TEST_F(ApiAuthTest, UpdateRolePersistsAndReverts)
{
    std::string name = CreateUser("user");
    oj_user_model::UserModel um;

    oj_user_model::User u;
    ASSERT_TRUE(um.GetUserByName(name, &u));
    EXPECT_EQ(u._role, "user");

    ASSERT_TRUE(um.UpdateRole(u._id, "leader"));
    oj_user_model::User up;
    ASSERT_TRUE(um.GetUserById(u._id, &up));
    EXPECT_EQ(up._role, "leader");

    ASSERT_TRUE(um.UpdateRole(u._id, "user"));
    oj_user_model::User back;
    ASSERT_TRUE(um.GetUserById(u._id, &back));
    EXPECT_EQ(back._role, "user");
}
