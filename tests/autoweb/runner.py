#!/usr/bin/env python3
"""tests/autoweb 用例执行器。

用法:
    python3 runner.py
    python3 runner.py --base http://127.0.0.1:8080
    python3 runner.py --filter judge
"""

import argparse
import importlib
import os
import sys
import traceback

import config
import ojui

MODULES = [
    "test_auth_pages",
    "test_auth_flow",
    "test_question_browse",
    "test_judge_flow",
    "test_visibility",
    "test_question_admin",
]


def discover_tests(module):
    tests = []
    for name in dir(module):
        obj = getattr(module, name)
        if callable(obj) and name.startswith("test_"):
            if getattr(obj, "__module__", None) == module.__name__:
                tests.append(name)
    return tests


def main():
    ap = argparse.ArgumentParser(description="OJ Web/UI functional tests")
    ap.add_argument("--base", default=config.DEFAULT_BASE_URL, help="gateway base url")
    ap.add_argument("--filter", default="", help="run only tests whose name contains this substring")
    args = ap.parse_args()

    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    ui = ojui.UI(base_url=args.base)

    passed = failed = skipped = errors = 0
    print("== OJ Web UI tests  base = %s ==" % ui.base)

    for mod_name in MODULES:
        try:
            module = importlib.import_module(mod_name)
        except Exception as exc:  # 模块自身导入失败
            errors += 1
            print("[ERROR] import %s: %s" % (mod_name, exc))
            continue
        for test_name in discover_tests(module):
            label = "%s.%s" % (mod_name, test_name)
            if args.filter and args.filter not in label:
                continue
            try:
                getattr(module, test_name)(ui)
                passed += 1
                print("[PASS] %s" % label)
            except ojui.SkipTest as exc:
                skipped += 1
                print("[SKIP] %s  %s" % (label, exc))
            except AssertionError as exc:
                failed += 1
                print("[FAIL] %s" % label)
                print("       %s" % exc)
            except Exception as exc:
                errors += 1
                print("[ERROR] %s  %s" % (label, exc))
                traceback.print_exc(limit=3)

    print("\n==== 汇总: %d 通过, %d 失败, %d 跳过, %d 错误 ====" % (passed, failed, skipped, errors))
    return 0 if failed == 0 and errors == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
