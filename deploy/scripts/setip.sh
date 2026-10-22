#!/bin/bash
# 修改绯雨骑士团服务端对外 IP
# 用法: /Server/setip.sh 172.25.204.81
set -e
NEWIP="$1"
if [ -z "$NEWIP" ]; then
    echo "用法: $0 <新的服务器IP>"
    exit 1
fi

IPFILE=/Server/.current_ip
DEFAULT="101.43.194.154"
if [ -f "$IPFILE" ]; then
    CURIP="$(cat "$IPFILE")"
else
    CURIP="$DEFAULT"
fi

echo "当前 IP: $CURIP  ->  新 IP: $NEWIP"

if [ "$CURIP" != "$NEWIP" ]; then
    sed -i "s/$CURIP/$NEWIP/g" /Server/uc/env-cfg/*.js
    sed -i "s/$CURIP/$NEWIP/g" /Server/uc/versions/*.json
    sed -i "s/$CURIP/$NEWIP/g" /Server/s1/wserver/logic/guildbattle/appUtil.js
    sed -i "s/$CURIP/$NEWIP/g" /www/wwwroot/www/game.html
    echo "$NEWIP" > "$IPFILE"
fi

# 数据库里的外网地址
mysql -uroot -ppwd123456 2>/dev/null <<SQL
UPDATE ttf_ly_login.uw_server_info SET host='$NEWIP', host2='$NEWIP';
UPDATE ttf_ly_main.uw_battles_group   SET battleHost='$NEWIP';
SQL

LEFT=$(grep -rl "$DEFAULT" /Server/uc/env-cfg /Server/uc/versions /Server/s1/wserver /www/wwwroot/www/game.html 2>/dev/null || true)
if [ -n "$LEFT" ]; then
    echo "!!! 以下文件仍含旧 IP $DEFAULT:"
    echo "$LEFT"
else
    echo "文件内 IP 已全部为 $NEWIP"
fi

mysql -uroot -ppwd123456 -N -e "select concat('uw_server_info.host = ', host) from ttf_ly_login.uw_server_info;" 2>/dev/null
