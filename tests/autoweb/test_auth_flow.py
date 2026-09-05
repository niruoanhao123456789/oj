"""注册 / 登录 / 会话 流程用例（对应 API.md Auth、SPEC §4.4/§5.5/§5.6）。"""

from ojui import require, require_in, require_not_in, SkipTest, unique_name, admin_token


def _register_user(ui, role="user"):
    name = unique_name("usr" if role == "user" else "lead")
    pw = "pw_" + ("user123" if role == "user" else "lead123")
    return name, pw


def test_register_and_login_user(ui):
    username, password = _register_user(ui)
    r = ui.register(username, password, "user")
    data = r.json or {}
    require(data.get("ok"), "注册普通用户失败: %s" % data.get("message"))
    require(data.get("role") == "user", "注册角色应为 user，实际 %s" % data.get("role"))

    # 重复用户名注册被拒
    dup = ui.register(username, password, "user").json or {}
    require(dup.get("ok") is False, "重复用户名注册应被拒绝")

    # 密码错误登录失败
    bad = ui.login(username, "wrong-password").json or {}
    require(bad.get("ok") is False, "错误密码应登录失败")

    # 正确登录成功并签发 token
    ok = ui.login(username, password).json or {}
    require(ok.get("ok") and ok.get("token"), "正确密码应登录成功并签发 token")
    require(ok.get("username") == username, "登录返回的用户名应一致")

    # 用 token 访问受保护页面
    page = ui.get("/all_questions", token=ok["token"])
    require(page.status == 200, "带 token 访问题目列表 HTTP %d" % page.status)
    require_not_in('id="oj-msg-page"', page.text, "登录态不应看到引导门")


def test_leader_register_requires_admin_invite(ui):
    admin_tok = admin_token(ui)
    if not admin_tok:
        raise SkipTest("admin 账号不可用，跳过负责人注册用例")

    inv = ui.reset_admin_invite(admin_tok).json or {}
    require(inv.get("ok") and inv.get("invite_code"), "管理员重置邀请码失败")

    # 正确邀请码注册负责人
    name, pw = _register_user(ui, "leader")
    r = ui.register(name, pw, "leader", invite_code=inv["invite_code"]).json or {}
    require(r.get("ok"), "带管理员邀请码注册负责人失败: %s" % r.get("message"))
    require(r.get("role") == "leader", "应注册为 leader")

    # 错误邀请码被拒
    bad = ui.register(unique_name("lead"), "pw_lead123", "leader", invite_code="WRONG").json or {}
    require(bad.get("ok") is False, "错误管理员邀请码应拒绝注册负责人")


def test_unknown_token_is_rejected(ui):
    # 服务器会话为登录签发后有效：伪造/无效 token 不应访问受保护页
    page = ui.get("/all_questions", token="0" * 64)
    require_in('id="oj-msg-page"', page.text, "无效 token 应被拒(回到登录引导门)")
    require_in("location.replace('/login')", page.text, "无效 token 应引导至 /login")


def test_anonymous_judge_is_rejected(ui):
    # 与前端一致：未登录提交评测返回 status=-2（不执行判题）
    r = ui.post("/judge/1", {"code": "int main(){return 0;}"})
    data = r.json or {}
    require(data.get("status") == -2, "匿名提交应返回 status=-2，实际 %s" % data)
