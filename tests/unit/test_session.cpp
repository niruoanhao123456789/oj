#include <gtest/gtest.h>
#include <string>
#include <algorithm>
#include <cctype>
#include "../../oj_server/user/oj_user_model.hpp"

// 内存 token 会话(CreateSession/GetSession)不依赖数据库
TEST(SessionTest, CreateAndGet)
{
    oj_user_model::UserModel m;
    oj_user_model::User u;
    u._id = 7;
    u._username = "u7";
    u._role = "user";

    std::string token = m.CreateSession(u);
    EXPECT_FALSE(token.empty());

    oj_user_model::User out;
    ASSERT_TRUE(m.GetSession(token, &out));
    EXPECT_EQ(out._id, 7);
    EXPECT_EQ(out._username, "u7");
    EXPECT_EQ(out._role, "user");
}

TEST(SessionTest, TokensAreUnique)
{
    oj_user_model::UserModel m;
    oj_user_model::User u;
    u._id = 1;
    u._username = "a";
    u._role = "admin";
    EXPECT_NE(m.CreateSession(u), m.CreateSession(u));
}

TEST(SessionTest, UnknownTokenFails)
{
    oj_user_model::UserModel m;
    oj_user_model::User out;
    EXPECT_FALSE(m.GetSession("does-not-exist", &out));
}

TEST(SessionTest, GenerateSaltIsNumericTimestamp)
{
    std::string s = oj_user_model::UserModel::GenerateSalt();
    ASSERT_FALSE(s.empty());
    EXPECT_TRUE(std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c); }));
}
