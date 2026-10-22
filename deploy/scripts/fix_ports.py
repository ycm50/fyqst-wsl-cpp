#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
修复上一版补丁的语法问题（追加的 IIFE 前缺少分号），并重新打补丁。
可重复执行：先剥离旧的 fyqst-port-merge 块，再以正确形式追加。
"""
import os
import sys

MARK = '// ---- fyqst-port-merge'

TAIL = """;

// ---- fyqst-port-merge: 统一端口，避免多进程共享 config/project.json 时互相覆盖成空 ----
(function () {
    var ports = {
        mainHttpPort: 9901,
        versionHttpPort: 9902,
        viewHttpPort: 9903,
        serverHttpPort: 20001,
        cityServerPort: 10001,
        battleServerPort: 9001,
        logPort: 7000,
        saveDBInterval: 5,
        stopInterval: 25
    };
    for (var k in ports) {
        var v = module.exports[k];
        if (v === undefined || v === null || v === "") module.exports[k] = ports[k];
    }
})();
"""

FILES = ['main_cfg.js', 'server_cfg.js', 'cityserver_cfg.js',
         'battleserver_cfg.js', 'version_cfg.js', 'view_cfg.js']


def patch(base):
    d = os.path.join(base, 'env-cfg')
    if not os.path.isdir(d):
        return
    for name in FILES:
        p = os.path.join(d, name)
        if not os.path.exists(p):
            continue
        src = open(p, encoding='utf-8').read()
        i = src.find(MARK)
        if i != -1:
            src = src[:i].rstrip() + '\n'
        open(p, 'w', encoding='utf-8').write(src + TAIL)
        print('  [fixed] %s' % p)


if __name__ == '__main__':
    for base in sys.argv[1:]:
        print('处理 %s' % base)
        patch(base)
