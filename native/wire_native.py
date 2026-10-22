#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""把 fynative.node 接入 uw-utils 的 md5Utils（保留纯 JS 兜底）"""
import os
import shutil
import sys

NODE_BIN = '/opt/fyweb/native/fynative.node'

LOADER = """// ---- fynative: C++ 原生加速（md5/hmac-md5/sha1/base64），失败自动回退纯 JS ----
var __fynative = null;
try { __fynative = require('fynative'); } catch (e) { __fynative = null; }
// ---------------------------------------------------------------------------
"""

ORIG_BODY = """    function md5(string, key, raw) {
        if (!key) {
            if (!raw) {
                return hex_md5(string);
            }
            return raw_md5(string);
        }
        if (!raw) {
            return hex_hmac_md5(key, string);
        }
        return raw_hmac_md5(key, string);
    }"""

NEW_BODY = """    function md5(string, key, raw) {
        // 高频路径交给 C++（结果与下面纯 JS 实现逐字节一致）
        if (__fynative && typeof string === 'string' &&
            (key === undefined || key === null || typeof key === 'string')) {
            if (!key) {
                return raw ? __fynative.md5raw(string) : __fynative.md5hex(string);
            }
            return raw ? __fynative.hmacMd5raw(string, key) : __fynative.hmacMd5hex(string, key);
        }
        if (!key) {
            if (!raw) {
                return hex_md5(string);
            }
            return raw_md5(string);
        }
        if (!raw) {
            return hex_hmac_md5(key, string);
        }
        return raw_hmac_md5(key, string);
    }"""


def patch(base):
    mod_dir = os.path.join(base, 'node_modules', 'fynative')
    os.makedirs(mod_dir, exist_ok=True)
    shutil.copy2(NODE_BIN, os.path.join(mod_dir, 'fynative.node'))
    with open(os.path.join(mod_dir, 'index.js'), 'w') as f:
        f.write("module.exports = require('./fynative.node');\n")

    target = os.path.join(base, 'node_modules', 'uw-utils', 'src', 'md5Utils.js')
    if not os.path.exists(target):
        print('  [skip] %s 不存在' % target)
        return
    src = open(target, encoding='utf-8').read()
    if '__fynative' in src:
        print('  [ok]   已接入: %s' % target)
        return
    if ORIG_BODY not in src:
        print('  [warn] %s 中未找到原始 md5() 体，跳过替换（仅装插件）' % target)
        return
    src = LOADER + src.replace(ORIG_BODY, NEW_BODY, 1)
    open(target, 'w', encoding='utf-8').write(src)
    print('  [done] 已接入: %s' % target)


if __name__ == '__main__':
    for base in sys.argv[1:]:
        print('处理 %s' % base)
        patch(base)
