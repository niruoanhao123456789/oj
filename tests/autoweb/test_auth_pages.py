"""认证相关页面的 Web UI 用例：首页 / 登录页 / 注册页 / 未登录引导门。"""

from ojui import require, require_in, require_not_in, SkipTest

PUBLIC_PAGES = ["/", "/login", "/register"]
GATED_PATHS = [
    "/all_questions",
    "/question/1",
    "/group_manage",
    "/question_manage",
    "/question_manage/edit",
    "/question_manage/edit/1",
]


def test_home_index_is_public_and_has_nav_entries(ui):
    r = ui.get("/")
    require(r.status == 200, "首页 HTTP %d" % r.status)
    html = r.text
    require_in('<header class="navbar"', html, "悬浮导航")
    require_in('id="navLinks"', html, "导航容器")
    # 未登录：登录/注册按钮；已登录：用户 chip 与退出 —— 这两套客户端渲染代码都在首页 JS 中
    require_in('class="pill" href="/login">登录', html, "登录按钮(未登录态)")
    require_in('class="pill reg" href="/register">注册', html, "注册按钮(未登录态)")
    require_in('class="nav-user"', html, "登录态用户 chip")
    require_in('js-enter', html, "刷题入口")


def test_login_page_form(ui):
    r = ui.get("/login")
    require(r.status == 200, "登录页 HTTP %d" % r.status)
    html = r.text
    require_not_in('id="oj-msg-page"', html, "登录页不应是引导门")
    require_in('<form id="loginform"', html, "登录表单")
    require_in('id="username"', html, "用户名输入框")
    require_in('id="password"', html, "密码输入框")
    require_in('id="submitBtn"', html, "登录按钮")
    require_in('href="/register"', html, "去注册链接")


def test_register_page_form(ui):
    r = ui.get("/register")
    require(r.status == 200, "注册页 HTTP %d" % r.status)
    html = r.text
    require_not_in('id="oj-msg-page"', html, "注册页不应是引导门")
    require_in('<form id="regform"', html, "注册表单")
    require_in('id="username"', html, "用户名输入框")
    require_in('id="password"', html, "密码输入框")
    require_in('data-role="user"', html, "普通用户选择")
    require_in('data-role="leader"', html, "负责人选择")
    require_in('id="invite_code"', html, "管理员邀请码输入框(负责人)")
    require_in('href="/login"', html, "去登录链接")


def test_anonymous_visits_are_gated_to_login(ui):
    for path in GATED_PATHS:
        r = ui.get(path)
        require(r.status == 200, "%s HTTP %d" % (path, r.status))
        html = r.text
        # 未登录访问受保护页只得到“登录引导门”，而不是真实页面/额外登录页
        require_in('id="oj-msg-page"', html, "%s 应是登录引导门" % path)
        require_in("location.replace('/login')", html, "%s 应跳转到 /login" % path)
        require(len(html) < 4000, "%s 引导门页面过大(%d)，疑似渲染了真实页面" % (path, len(html)))


def test_public_pages_are_accessible(ui):
    for path in PUBLIC_PAGES:
        r = ui.get(path)
        require(r.status == 200, "%s HTTP %d" % (path, r.status))
        require_not_in('id="oj-msg-page"', r.text, "%s 是公开页，不应是引导门" % path)
