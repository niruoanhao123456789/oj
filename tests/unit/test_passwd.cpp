#include <gtest/gtest.h>
#include <cctype>
#include "../../oj_server/user/oj_passwd.hpp"

// SHA-256 官方/NIST 测试向量
TEST(Sha256Test, KnownVectors)
{
    EXPECT_EQ(oj_passwd::Sha256::HexDigest(""),
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(oj_passwd::Sha256::HexDigest("abc"),
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    EXPECT_EQ(oj_passwd::Sha256::HexDigest("hello world"),
              "b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9");
    // 超过一个块的输入(>=64 字节)也应正确(与 python hashlib 对照)
    EXPECT_EQ(oj_passwd::Sha256::HexDigest(std::string(65, 'a')),
              "635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0");
}

TEST(Sha256Test, OutputIsHex64)
{
    EXPECT_EQ(oj_passwd::Sha256::HexDigest("x").size(), 64u);
    for(char c : oj_passwd::Sha256::HexDigest("x"))
        EXPECT_TRUE(::isdigit(static_cast<unsigned char>(c)) || (c >= 'a' && c <= 'f'));
}

TEST(HashPasswordTest, DeterministicWithSameSalt)
{
    EXPECT_EQ(oj_passwd::HashPassword("mypass", "1725000000"),
              oj_passwd::HashPassword("mypass", "1725000000"));
}

TEST(HashPasswordTest, SaltChangesHash)
{
    EXPECT_NE(oj_passwd::HashPassword("mypass", "salt_a"),
              oj_passwd::HashPassword("mypass", "salt_b"));
}

TEST(HashPasswordTest, PasswordChangesHash)
{
    EXPECT_NE(oj_passwd::HashPassword("pass_a", "salt"),
              oj_passwd::HashPassword("pass_b", "salt"));
}
