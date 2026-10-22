// 原生插件正确性校验：与 uw-utils 里的 JS 实现逐条对齐
var path = '/opt/fyweb/native/fynative.node';
var nat = require(path);
var md5Utils = require('uw-utils').md5Utils;

var fails = 0, total = 0;
function eq(desc, a, b) {
    total++;
    if (a === b) return;
    fails++;
    console.log('FAIL ' + desc + '\n  native: ' + a + '\n  js    : ' + b);
}

// 1. 已知向量
eq('md5("abc")', nat.md5hex('abc'), '900150983cd24fb0d6963f7d28e17f72');
eq('sha1("abc")', nat.sha1hex('abc'), 'a9993e364706816aba3e25717850c26c9cd0d89d');
eq('base64("hello")', nat.base64encode('hello'), 'aGVsbG8=');

// 2. 与 JS 逐个对比（含中文、长串、空串）
var cases = ['', 'a', 'abc', 'kshh1d23hsdas', '中文测试', 's1.一阵风', 
             new Array(300).join('x'), 'mimixiaoxia'];
var i;
for (i = 0; i < cases.length; i++) {
    eq('md5hex  ' + JSON.stringify(cases[i]), nat.md5hex(cases[i]), md5Utils.md5(cases[i]));
    // 两参数 = HMAC-MD5
    eq('hmacmd5 ' + JSON.stringify(cases[i]), nat.hmacMd5hex(cases[i], 'secretkey'),
       md5Utils.md5(cases[i], 'secretkey'));
}

// 3. 与 node 自带 crypto 对比
var crypto = require('crypto');
for (i = 0; i < 200; i++) {
    var s = Math.random().toString(36).slice(2) + i + '中文' + (i % 7);
    eq('crypto md5 #' + i, nat.md5hex(s), crypto.createHash('md5').update(s, 'utf8').digest('hex'));
    eq('crypto sha1 #' + i, nat.sha1hex(s), crypto.createHash('sha1').update(s, 'utf8').digest('hex'));
    eq('crypto hmac #' + i,
       nat.hmacSha1hex(s, 'k' + i),
       crypto.createHmac('sha1', 'k' + i).update(s, 'utf8').digest('hex'));
    eq('base64 #' + i, nat.base64decode(nat.base64encode(s)), s);
}

// 4. 性能对比（md5 10 万次）
var t0 = Date.now();
for (i = 0; i < 100000; i++) md5Utils.md5('account_' + i + '|salt');
var jsMs = Date.now() - t0;
t0 = Date.now();
for (i = 0; i < 100000; i++) nat.md5hex('account_' + i + '|salt');
var natMs = Date.now() - t0;

console.log('\n共 ' + total + ' 项，失败 ' + fails + ' 项');
console.log('md5 x100000  JS: ' + jsMs + ' ms   C++: ' + natMs + ' ms   加速 ' +
            (jsMs / Math.max(natMs, 1)).toFixed(1) + 'x');
process.exit(fails === 0 ? 0 : 1);
