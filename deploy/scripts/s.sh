#!/bin/bash
# 绯雨骑士团 Node 服启动脚本（C++ 化版本）
# 说明：versionApp(9902) 与 logApp(7000) 已由 C++ 的 fyversion / fylog 替代，
#       由 systemd 单独管理，这里不再启动它们。
script_dir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
mkdir -p "$script_dir/startlog"

cd /Server/uc
nohup node mainApp.js > "$script_dir/startlog/mainApp.log" 2>&1 &
echo $! > "$script_dir/startlog/mainApp.pid"
echo "mainApp started with PID: $(cat "$script_dir/startlog/mainApp.pid")"
sleep 1

nohup node viewApp.js > "$script_dir/startlog/viewApp.log" 2>&1 &
echo $! > "$script_dir/startlog/viewApp.pid"
echo "viewApp started with PID: $(cat "$script_dir/startlog/viewApp.pid")"
sleep 1

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
