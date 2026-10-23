#!/bin/bash
# 绯雨骑士团 Node 服启动脚本（C++ + 合并进程版）
#
# 说明：
#   1. versionApp(9902) / logApp(7000) 已由 C++ 的 fyversion / fylog 替代，
#      由 systemd 单独管理，这里不再启动。
#   2. mainApp(9901) + viewApp(9903) 合并为单个进程 allApp.js：
#      两者都加载同一份 uw-data（约 290MB），合并后只占一份内存（实测省约 286MB）。
script_dir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
mkdir -p "$script_dir/startlog"

cd /Server/uc
nohup node allApp.js > "$script_dir/startlog/allApp.log" 2>&1 &
echo $! > "$script_dir/startlog/allApp.pid"
echo "allApp (mainApp:9901 + viewApp:9903) started with PID: $(cat "$script_dir/startlog/allApp.pid")"
sleep 3

cd /Server/s1
nohup node serverApp.js > "$script_dir/startlog/serverApp.log" 2>&1 &
echo $! > "$script_dir/startlog/serverApp.pid"
echo "serverApp started with PID: $(cat "$script_dir/startlog/serverApp.pid")"
sleep 1

cd wserver
nohup node cityserver.js > "$script_dir/startlog/cityserver.log" 2>&1 &
echo $! > "$script_dir/startlog/cityserver.pid"
echo "cityserver started with PID: $(cat "$script_dir/startlog/cityserver.pid")"

cd ../battleserver
nohup node battleserver.js > "$script_dir/startlog/battleserver.log" 2>&1 &
echo $! > "$script_dir/startlog/battleserver.pid"
echo "battleserver started with PID: $(cat "$script_dir/startlog/battleserver.pid")"
