#!/bin/bash
# 停止全部 Node 服（与 s.sh 配套）

stop_node_app() {
    app_name="$1"
    app_pid=$(pgrep -f "node $app_name.js")
    if [ -n "$app_pid" ]; then
        kill $app_pid 2>/dev/null
        echo "Stopped $app_name (PID: $app_pid)"
    else
        echo "$app_name is not running"
    fi
}

# 合并进程
stop_node_app "allApp"      # mainApp(9901) + viewApp(9903)
stop_node_app "gameAll"     # cityserver(10001) + battleserver(9001)

# 兼容旧版单进程启动的情况
stop_node_app "mainApp"
stop_node_app "viewApp"
stop_node_app "serverApp"
stop_node_app "cityserver"
stop_node_app "battleserver"
stop_node_app "versionApp"
stop_node_app "logApp"

echo "All Node.js services stopped"
