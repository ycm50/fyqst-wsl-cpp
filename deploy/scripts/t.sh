#!/bin/bash

# 停止Node.js应用程序
stop_node_app() {
    app_name="$1"
    app_pid=$(pgrep -f "node $app_name.js")

    if [ -n "$app_pid" ]; then
        kill $app_pid
        echo "Stopped $app_name (PID: $app_pid)"
    else
        echo "$app_name is not running"
    fi
}

# 停止mainApp
stop_node_app "mainApp"

# 停止versionApp
stop_node_app "versionApp"

# 停止viewApp
stop_node_app "viewApp"

# 切换到s1目录
cd /Server/s1

# 停止serverApp
stop_node_app "serverApp"

# 停止logApp
stop_node_app "logApp"

# 切换到wserver目录
cd /Server/uc/wserver

# 停止cityserver
stop_node_app "cityserver"

# 切换到battleserver目录
cd /Server/uc/battleserver

# 停止battleserver
stop_node_app "battleserver"

# 可以继续添加其他Node.js应用程序的停止命令，按照相同的方式

# 输出操作完成消息
echo "All Node.js services stopped"
