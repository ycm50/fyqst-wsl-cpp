#!/bin/bash
# 应用合并进程方案：安装 allApp.js + 新 s.sh/t.sh，重启并验收
set -e
REPO=/mnt/a/Downloads/fyqst-wsl-cpp

echo "[1] 安装合并启动器与启停脚本"
cp "$REPO/deploy/allApp.js"        /Server/uc/allApp.js
cp "$REPO/deploy/scripts/s.sh"     /Server/s.sh
cp "$REPO/deploy/scripts/t.sh"     /Server/t.sh
chmod +x /Server/s.sh /Server/t.sh

echo "[2] 重启游戏服"
systemctl restart fyqst-game.service
for i in $(seq 1 30); do
    sleep 1
    n=$(ss -lnt 2>/dev/null | grep -cE ':(9901|9903|10001|20001|9001) ')
    [ "$n" -ge 5 ] && { echo "  5 个端口全部就绪（${i}s）"; break; }
done

echo
echo "[3] 进程与内存"
ps -C node -o pid=,rss=,args= | awk '{printf "  PID %-6s %7.1f MB  %s\n", $1, $2/1024, $3" "$4}'
echo
printf "  node 合计: %.1f MB (%s 个进程)\n" "$(ps -C node -o rss= | awk '{s+=$1} END {print s/1024}')" "$(pgrep -c -x node)"
printf "  C++ 合计 : %.1f MB\n" "$(ps -C fyweb,fyversion,fylog -o rss= | awk '{s+=$1} END {print s/1024}')"

echo
echo "[4] 接口自检"
for u in "81/" "81/game.html" "81/gm/gm.php" "81/cdk/index.php" "9902/manifest"; do
    printf "  %-18s -> %s\n" "$u" "$(curl -s -o /dev/null -w '%{http_code}' -m 5 "http://127.0.0.1:$u")"
done
echo
echo "[5] 端口"
ss -lnt 2>/dev/null | awk '$1=="LISTEN"{print $4}' | grep -E ':(81|3306|7000|9001|9500|9901|9902|9903|10001|20001)$' | sort -u | tr '\n' ' '; echo
