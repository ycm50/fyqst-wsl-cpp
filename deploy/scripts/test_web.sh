#!/bin/bash
# fyweb 接口自测
B=http://127.0.0.1:81
pass=0; fail=0
chk() { # chk <desc> <expected> <actual>
  if [ "$2" = "$3" ]; then echo "  OK   $1"; pass=$((pass+1));
  else echo "  FAIL $1  (期望 $2 / 实际 $3)"; fail=$((fail+1)); fi
}
contains() { # contains <desc> <needle> <haystack>
  if echo "$3" | grep -q -- "$2"; then echo "  OK   $1"; pass=$((pass+1));
  else echo "  FAIL $1  (未包含: $2)"; fail=$((fail+1)); fi
}

echo "== 静态页 =="
chk "GET /"                200 "$(curl -s -o /dev/null -w '%{http_code}' $B/)"
chk "GET /game.html"       200 "$(curl -s -o /dev/null -w '%{http_code}' $B/game.html)"

echo "== 登录网关 game.php（C++） =="
code=$(curl -s -o /tmp/gp.out -w '%{http_code}' -c /tmp/ck1 -d 'username=cpptest01&passwd=123456' $B/game.php)
chk "POST /game.php 状态"   302 "$code"
loc=$(grep -i '^location:' -A0 /tmp/gp.out 2>/dev/null)
contains "跳转带 account" "account=cpptest01" "$(curl -s -D - -o /dev/null -b /tmp/ck1 -d 'username=cpptest01&passwd=123456' $B/game.php | grep -i location)"
contains "跳转带 sign"    "sign=" "$(curl -s -D - -o /dev/null -d 'username=cpptest01&passwd=123456' $B/game.php | grep -i location)"
contains "账号已入库"     "cpptest01" "$(mysql -uroot -ppwd123456 -N -e "select user from account.account where user='cpptest01'" 2>/dev/null)"
contains "密码是 md5"     "$(printf 123456 | md5sum | cut -d' ' -f1)" "$(mysql -uroot -ppwd123456 -N -e "select pass from account.account where user='cpptest01'" 2>/dev/null)"
contains "错误密码被拒"   "用户名或密码错误" "$(curl -s -d 'username=cpptest01&passwd=wrong' $B/game.php)"
contains "中文用户名被拒" "用户名不能包含中文" "$(curl -s -d 'username=测试&passwd=1' $B/game.php)"

echo "== GM 后台（C++） =="
contains "GET /gm/gm.php"      "绯雨骑士团H5后台" "$(curl -s $B/gm/gm.php)"
contains "GET /gm/index.php"   "绯雨骑士团H5玩家后台" "$(curl -s $B/gm/index.php)"
contains "区服下拉已渲染"      "绯雨骑士团一区" "$(curl -s $B/gm/gm.php)"
contains "物品下拉已渲染"      "积分" "$(curl -s $B/gm/gm.php)"
chk "gmquery 错误 GM 码" "GM码不对" "$(curl -s -d 'checknum=000&qu=1&uid=x&type=charge&num=1' $B/gm/user/gmquery.php)"
chk "gmquery 角色不存在" "角色不存在" "$(curl -s -d 'checknum=123456&qu=1&uid=nobody&type=charge&num=1' $B/gm/user/gmquery.php)"

echo "== CDK 平台（C++） =="
contains "GET /cdk/index.php" "CDK管理系统" "$(curl -s $B/cdk/index.php)"
chk "登录错误密码" "系统提示：帐号或者密码错误" "$(curl -s -c /tmp/ck2 -d 'user=admin&pass=bad' $B/cdk/check.php)"
code=$(curl -s -o /dev/null -w '%{http_code}' -c /tmp/ck2 -d 'user=admin&pass=123456' $B/cdk/check.php)
chk "登录成功跳转" 302 "$code"
contains "gm.php 需登录态" "欢迎登陆" "$(curl -s -b /tmp/ck2 $B/cdk/gm.php)"
chk "未登录访问 gm.php 被拒" 1 "$(curl -s $B/cdk/gm.php | grep -c '您无此权限')"
contains "生成 CDKEY 页"     "CDKEY生成系统" "$(curl -s -b /tmp/ck2 $B/cdk/gmkey.php)"
gen=$(curl -s -b /tmp/ck2 -d 'submit=generate&c=2' $B/cdk/gmkey.php | grep -oE '[0-9A-F]{8}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{12}' | head -2)
echo "  生成的 CDKEY: $(echo $gen | tr '\n' ' ')"
key=$(echo "$gen" | head -1)
if [ -n "$key" ]; then
  contains "导入 CDKEY 成功" "添加cdkey成功" "$(curl -s -b /tmp/ck2 --data-urlencode "cdkey=$key" -d 'submit=addkey&vip=1' $B/cdk/gmkey.php)"
  contains "CDKEY 已入库" "$key" "$(mysql -uroot -ppwd123456 -N -e "select cdkey from gmdbyzf.cdkey where cdkey='$key'" 2>/dev/null)"
  contains "未使用列表可见" "$key" "$(curl -s -b /tmp/ck2 "$B/cdk/gmkeylist.php?page=1&vip=1")"
  contains "所有列表可见"   "$key" "$(curl -s -b /tmp/ck2 "$B/cdk/gmallkeylist.php?page=1")"
  contains "复制页可见"     "$key" "$(curl -s -b /tmp/ck2 "$B/cdk/gmcopy.php?getkey=$key")"

  echo "== CDK 兑换（C++ 版 pay.php / cdkmail.php） =="
  contains "pay.php 页面"      "绯雨骑士团CDK平台" "$(curl -s "$B/pay.php?orderId=T1&fee=600&feeid=1&rolename=s1.x")"
  checkUrl=$(curl -s -D - -o /dev/null -d "submit=key&cdkey=$key" "$B/pay.php?orderId=T1&fee=600&feeid=1" | grep -i '^location' | tr -d '\r')
  contains "检查激活码跳转" "cdkmail.php" "$checkUrl"
  contains "cdkmail 页面"   "确认领取" "$(curl -s -b /tmp/ck1 "$B/cdkmail.php?rechargeId=1&cdkey=$key")"
fi

echo
echo "通过 $pass 项，失败 $fail 项"
