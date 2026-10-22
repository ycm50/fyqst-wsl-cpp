#!/bin/bash
echo "======================== 服务状态 ========================"
for u in mariadb nginx fyqst-ip fyqst-game fyversion fylog fyweb; do
    st=$(systemctl is-active $u 2>/dev/null)
    en=$(systemctl is-enabled $u 2>/dev/null)
    up=$(systemctl show -p ActiveEnterTimestamp --value $u 2>/dev/null)
    printf "  %-12s %-10s %-10s %s\n" "$u" "$st" "$en" "$up"
done

echo
echo "======================== 进程 ========================"
for p in $(pgrep -f 'node (mainApp|viewApp|serverApp|cityserver|battleserver).js' | sort -n); do
    cmd=$(tr '\0' ' ' < /proc/$p/cmdline | awk '{print $2}')
    rss=$(awk '/VmRSS/{printf "%.0f", $2/1024}' /proc/$p/status)
    printf "  PID %-6s %-20s %6s MB\n" "$p" "$cmd" "$rss"
done
for s in fyweb fyversion fylog; do
    p=$(pgrep -x $s | head -1)
    if [ -n "$p" ]; then
        rss=$(awk '/VmRSS/{printf "%.0f", $2/1024}' /proc/$p/status)
        printf "  PID %-6s %-20s %6s MB\n" "$p" "$s (C++)" "$rss"
    fi
done
p=$(pgrep -x mariadbd | head -1); [ -n "$p" ] && printf "  PID %-6s %-20s %6s MB\n" "$p" "mariadbd" "$(awk '/VmRSS/{printf "%.0f", $2/1024}' /proc/$p/status)"

echo
echo "======================== 端口 ========================"
ss -lntp 2>/dev/null | awk '$1=="LISTEN"{
    split($4,a,":"); port=a[length(a)];
    if (port=="81"||port=="3306"||port=="7000"||port=="9001"||port=="9500"||port=="9901"||port=="9902"||port=="9903"||port=="10001"||port=="20001")
        printf "  %-20s %s\n", $4, $6
}' | sort -t: -k2 -n

echo
echo "======================== 接口自检 ========================"
IP=$(hostname -I | awk '{print $1}')
printf "  当前 WSL IP     : %s\n" "$IP"
printf "  站点首页 :81    : %s\n" "$(curl -s -o /dev/null -w '%{http_code}' http://127.0.0.1:81/)"
printf "  game.html :81   : %s\n" "$(curl -s -o /dev/null -w '%{http_code}' http://127.0.0.1:81/game.html)"
printf "  GM 后台  :81    : %s\n" "$(curl -s -o /dev/null -w '%{http_code}' http://127.0.0.1:81/gm/gm.php)"
printf "  CDK 平台 :81    : %s\n" "$(curl -s -o /dev/null -w '%{http_code}' http://127.0.0.1:81/cdk/index.php)"
printf "  manifest :9902  : %s (%s 字节)\n" \
    "$(curl -s -o /tmp/_m.json -w '%{http_code}' http://127.0.0.1:9902/manifest)" "$(wc -c < /tmp/_m.json)"
printf "  fylog    :7000  : %s\n" "$(curl -s -o /dev/null -w '%{http_code}' 'http://127.0.0.1:7000/log?x=1')"
printf "  游戏服   :20001 : %s\n" "$( (exec 3<>/dev/tcp/127.0.0.1/20001) 2>/dev/null && echo OPEN || echo CLOSED )"
printf "  主城服   :10001 : %s\n" "$( (exec 3<>/dev/tcp/127.0.0.1/10001) 2>/dev/null && echo OPEN || echo CLOSED )"
printf "  主登录   :9901  : %s\n" "$( (exec 3<>/dev/tcp/127.0.0.1/9901) 2>/dev/null && echo OPEN || echo CLOSED )"

echo
echo "======================== 资源 ========================"
printf "  node 合计     : %7.1f MB (%s 个)\n" "$(ps -C node -o rss= | awk '{s+=$1} END {print s/1024}')" "$(pgrep -c -x node)"
printf "  C++ 服务合计  : %7.1f MB\n" "$(ps -C fyweb,fyversion,fylog -o rss= | awk '{s+=$1} END {print s/1024}')"
free -m | awk 'NR==2{printf "  系统内存      : %s MB / %s MB used\n", $3, $2}'
uptime | sed 's/^/  /'
