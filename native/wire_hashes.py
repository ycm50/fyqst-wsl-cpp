#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""把 fynative.node 接入 uw-utils 的 hashesUtils（jshashes 的 SHA1 加速）"""
import os
import sys

TAIL = """
// ---- fynative: SHA1 hex/raw 走 C++ 原生实现（失败自动回退 jshashes） ----
try {
  var __fyh = require('fynative');
  if (__fyh && module.exports && module.exports.SHA1) {
    var __p = module.exports.SHA1.prototype;
    __p.hex = function (s) { return __fyh.sha1hex(String(s)); };
    __p.raw = function (s) { return __fyh.sha1raw(String(s)); };
    __p.b64 = function (s) { return __fyh.base64encode(__fyh.sha1raw(String(s))); };
  }
} catch (e) {}
// --------------------------------------------------------------------------
"""


def patch(base):
    target = os.path.join(base, 'node_modules', 'uw-utils', 'src', 'hashesUtils.js')
    if not os.path.exists(target):
        print('  [skip] %s 不存在' % target)
        return
    src = open(target, encoding='utf-8').read()
    if '__fyh' in src:
        print('  [ok]   已接入: %s' % target)
        return
    open(target, 'w', encoding='utf-8').write(src + TAIL)
    print('  [done] 已接入: %s' % target)


if __name__ == '__main__':
    for base in sys.argv[1:]:
        print('处理 %s' % base)
        patch(base)
