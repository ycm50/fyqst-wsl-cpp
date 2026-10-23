#!/bin/bash
# 绯雨骑士团 Node 服启动脚本（C++ + 进程合并版）
#
# 进程布局（优化后只剩 3 个 node 进程）：
#   allApp.js   → mainApp(9901) + viewApp(9903)      共享一份 uw-data
#   serverApp.js→ 游戏服(20001)
#   gameAll.js  → cityserver(10001) + battleserver(9001)  共享一份 uw-data
#
# 优化掉的：
#   versionApp(9902) / logApp(7000)  → C++ 的 fyversion / fylog（systemd 管理，本脚本不管）
#   mainApp + viewApp                → 合并（省 286MB）
#   cityserver + battleserver        → 合并（省 370MB）
script_dir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
mkdir -p "$script_dir/startlog"

# 1) 主登录(9901) + 视图(9903)
cd /Server/uc
nohup node allApp.js > "$script_dir/startlog/allApp.log" 2>&1 &
echo $! > "$script_dir/startlog/allApp.pid"
echo "allApp (9901+9903) started with PID: $(cat "$script_dir/startlog/allApp.pid")"
sleep 3

# 2) 游戏服(20001)
cd /Server/s1
nohup node serverApp.js > "$script_dir/startlog/serverApp.log" 2>&1 &
echo $! > "$script_dir/startlog/serverApp.pid"
echo "serverApp (20001) started with PID: $(cat "$script_dir/startlog/serverApp.pid")"
sleep 2

# 3) 主城(10001) + 战斗(9001)
nohup node gameAll.js > "$script_dir/startlog/gameAll.log" 2>&1 &
echo $! > "$script_dir/startlog/gameAll.pid"
echo "gameAll (10001+9001) started with PID: $(cat "$script_dir/startlog/gameAll.pid")"
