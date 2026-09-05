"""tests/autoweb 配置：默认 base url 与预置账号，均可用环境变量覆盖。"""

import os

# 默认目标实例（与题目描述一致）。注意是 http:// 的网关地址。
DEFAULT_BASE_URL = os.environ.get("OJ_BASE_URL", "http://1.12.254.247:8080/")

TIMEOUT = float(os.environ.get("OJ_TIMEOUT", "15"))


def _env(key, default):
    return os.environ.get(key, default)


class Accounts:
    """预置账号（与 tests/example/mysql_test 种子一致，可覆盖）。"""

    ADMIN = (_env("OJ_ADMIN_USER", "admin"), _env("OJ_ADMIN_PASS", "admin123"))
    LEADER1 = (_env("OJ_LEADER1_USER", "leader1"), _env("OJ_LEADER1_PASS", "leader123"))
    LEADER2 = (_env("OJ_LEADER2_USER", "leader2"), _env("OJ_LEADER2_PASS", "leader222"))
    USER1 = (_env("OJ_USER1_USER", "user1"), _env("OJ_USER1_PASS", "user123"))
    USER2 = (_env("OJ_USER2_USER", "user2"), _env("OJ_USER2_PASS", "user223"))
    GUEST = (_env("OJ_GUEST_USER", "guest"), _env("OJ_GUEST_PASS", "guest123"))


# 演示题标题（tests/example/mysql_test 种子）——依赖它们的用例在缺失时 SKIP
DEMO_GLOBAL = ["判断回文数", "求最大值", "两数之和"]   # scope=global
DEMO_GROUP_LEADER1 = ["字符串反转"]                     # leader1 的组内题
DEMO_GROUP_LEADER2 = ["链表反转"]                       # leader2 的组内题
