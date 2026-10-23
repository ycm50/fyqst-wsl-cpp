#!/bin/bash
# 应用全部进程合并：allApp(9901+9903) + serverApp(20001) + gameAll(10001+9001)
set -e
REPO=/mnt/a/Downloads/fyqst-wsl-cpp

echo "[1] 安装启动器与启停脚本"
cp "$REPO/deploy/allApp.js"         /Server/uc/allApp.js
cp "$REPO/deploy/gameAll.js"        /Server/s1/gameAll.js
cp "$REPO/deploy/scripts/s.sh"      /Server/s.sh
cp "$REPO/deploy/scripts/t.sh"      /Server/t.sh
chmod +x /Server/s.sh /Server/t.sh

echo "[2] 清理测试遗留进程"
bash /Server/t.sh >/dev/null 2>&1 || true
sleep 3
pgrep -af "node (allApp|gameAll|serverApp|cityserver|battleserver).js" || echo "  已清空"

echo "[3] 启动（systemd）"
systemctl restart fyqst-game.service
for i in $(seq 1 40); do
    sleep 1
    n=$(ss -lnt 2>/dev/null | grep -cE ':(9901|9903|10001|20001|9001) ')
    [ "$n" -ge 5 ] && { echo "  5 个游戏端口全部就绪（${i}s）"; break; }
done

echo
echo "[4] 进程与内存"
ps -C node -o pid=,rss=,args= | awk '{printf "  PID %-6s %7.1f MB  %s\n", $1, $2/1024, $3" "$4}'
echo
printf "  node 合计 : %.1f MB（%s 个进程）\n" "$(ps -C node -o rss= | awk '{s+=$1} END {print s/1024}')" "$(pgrep -c -x node)"
printf "  C++ 合计  : %.1f MB\n" "$(ps -C fyweb,fyversion,fylog -o rss= | awk '{s+=$1} END {print s/1024}')"
printf "  mariadb   : %.1f MB\n" "$(ps -C mariadbd -o rss= | awk '{s+=$1} END {print s/1024}')"
free -m | awk 'NR==2{printf "  系统 used: %s MB\n", $3}'

echo
echo "[5] 接口与端口"
for u in "81/" "81/game.html" "81/gm/gm.php" "81/cdk/index.php" "9902/manifest" "9901/?r=h.s.h0&a=%7B%7D" "9903/pay/index.html"; do
    printf "  %-28s -> %s\n" "$u" "$(curl -s -o /dev/null -w '%{http_code}' -m 6 "http://127.0.0.1:$u")"
done
echo
ss -lnt 2>/dev/null | awk '$1=="LISTEN"{print $4}' | grep -E ':(81|3306|7000|9001|9500|9901|9902|9903|10001|20001)$' | sort -u | tr '\n' ' '; echo
echo
echo "[6] 各服务日志尾部"
for f in /Server/startlog/allApp.log /Server/startlog/serverApp.log /Server/startlog/gameAll.log; do
    echo "  -- $f"
    tail -3 "$f" | sed 's/^/     /'
done
