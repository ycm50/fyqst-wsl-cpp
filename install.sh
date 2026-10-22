#!/bin/bash
# =============================================================================
#  绯雨骑士团 H5 (fyqst) —— WSL / Debian 一键部署脚本
#
#  用法:
#      sudo bash install.sh [游戏包解压目录]
#
#    [游戏包解压目录] 需包含两个子目录:
#        Server/            （服务端，含 s1 uc sql 等）
#        www/wwwroot/www    （网页端）
#      不传则依次尝试 ./game  ./pkg  ../fyqst_src
#
#  可选环境变量:
#      HTTP_PROXY=http://主机:端口   给 apt / node 下载走代理
#      SERVER_IP=x.x.x.x             指定对外 IP（默认取 hostname -I 的第一个）
#      NODE_TARBALL=/path/node-v6.9.5-linux-x64.tar.xz  离线安装包
#      SKIP_APT=1                    跳过 apt（已装好依赖时）
# =============================================================================
set -uo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOG_FILE=/var/log/fyqst-install.log
NODE_DIR=/opt/node6
NODE_VER=v6.9.5
FYWEB_DIR=/opt/fyweb
GAME_DIR=/Server
WWW_DIR=/www/wwwroot/www

C_OK="\033[32m"; C_WARN="\033[33m"; C_ERR="\033[31m"; C_END="\033[0m"
say()  { echo -e "${C_OK}[fyqst]${C_END} $*"; }
warn() { echo -e "${C_WARN}[warn ]${C_END} $*"; }
die()  { echo -e "${C_ERR}[error]${C_END} $*"; exit 1; }

exec > >(tee -a "$LOG_FILE") 2>&1

# ---------------------------------------------------------------- 0. 前置检查
[ "$(id -u)" = "0" ] || die "请用 root 运行：sudo bash install.sh"

if [ -r /etc/os-release ]; then
    . /etc/os-release
    say "系统: $PRETTY_NAME"
else
    warn "无法识别 /etc/os-release，继续尝试"
fi

# 找游戏包目录
PKG="${1:-}"
if [ -z "$PKG" ]; then
    for cand in ./game ./pkg ../fyqst_src ./fyqst_src; do
        if [ -d "$cand/Server" ]; then PKG="$cand"; break; fi
    done
fi
[ -n "$PKG" ] || die "找不到游戏包目录，请把解压后的 Server/ 与 www/ 放在 ./game 下，或作为参数传入"
[ -d "$PKG/Server" ] || die "$PKG/Server 不存在"
[ -d "$PKG/www" ]    || die "$PKG/www 不存在"
PKG="$(cd "$PKG" && pwd)"
say "游戏包目录: $PKG"

# ---------------------------------------------------------------- 1. 系统依赖
if [ "${SKIP_APT:-0}" != "1" ]; then
    say "安装系统依赖 ..."
    apt-get update -qq || warn "apt-get update 失败（可能无网络，可设置 HTTP_PROXY）"
    DEBIAN_FRONTEND=noninteractive apt-get install -y \
        curl ca-certificates xz-utils net-tools procps unzip \
        mariadb-server nginx g++ make libmariadb-dev libmariadb-dev-compat \
        || die "依赖安装失败"
else
    say "SKIP_APT=1，跳过 apt"
fi

for bin in g++ make mysql nginx; do
    command -v "$bin" >/dev/null || die "缺少命令: $bin"
done

# ---------------------------------------------------------------- 2. Node 6.9.5
if [ ! -x "$NODE_DIR/bin/node" ]; then
    say "安装 Node $NODE_VER 到 $NODE_DIR ..."
    mkdir -p "$NODE_DIR"
    TARBALL="${NODE_TARBALL:-}"
    if [ -z "$TARBALL" ]; then
        for cand in "$REPO_DIR/node-$NODE_VER-linux-x64.tar.xz" "$PKG/node-$NODE_VER-linux-x64.tar.xz" \
                    "/tmp/node-$NODE_VER-linux-x64.tar.xz"; do
            [ -f "$cand" ] && { TARBALL="$cand"; break; }
        done
    fi
    if [ -z "$TARBALL" ]; then
        TARBALL=/tmp/node-$NODE_VER-linux-x64.tar.xz
        curl -fL --retry 3 -o "$TARBALL" \
            "https://nodejs.org/dist/$NODE_VER/node-$NODE_VER-linux-x64.tar.xz" \
            || die "Node 下载失败（离线请用 NODE_TARBALL=... 指定本地包）"
    fi
    tar -xJf "$TARBALL" -C "$NODE_DIR" --strip-components=1
fi
ln -sf "$NODE_DIR/bin/node" /usr/local/bin/node
ln -sf "$NODE_DIR/bin/npm"  /usr/local/bin/npm
say "node 版本: $(node -v)"

# ---------------------------------------------------------------- 3. 部署游戏文件
say "部署游戏文件 ..."
mkdir -p "$GAME_DIR" "$WWW_DIR"
if [ ! -f "$GAME_DIR/s1/serverApp.js" ]; then
    cp -a "$PKG/Server/." "$GAME_DIR/"
else
    warn "$GAME_DIR 已存在，跳过复制（如需覆盖请先手动清空）"
fi
if [ ! -f "$WWW_DIR/game.html" ]; then
    cp -a "$PKG/www/wwwroot/www/." "$WWW_DIR/"
else
    warn "$WWW_DIR 已存在，跳过复制"
fi
rm -f "$GAME_DIR/node.exe" "$GAME_DIR/cmd.exe" 2>/dev/null
chmod -R 777 "$GAME_DIR" "$WWW_DIR"

# ---------------------------------------------------------------- 4. 数据库
say "配置 MariaDB ..."
cp "$REPO_DIR/deploy/mysql/99-fyqst.cnf" /etc/mysql/mariadb.conf.d/99-fyqst.cnf
service mariadb restart
for i in $(seq 1 30); do mysqladmin status >/dev/null 2>&1 && break; sleep 1; done
mysql -uroot < "$REPO_DIR/deploy/mysql/01_root.sql" || die "设置 root 密码失败"

LCTN=$(mysql -uroot -ppwd123456 -N -e "select @@lower_case_table_names" 2>/dev/null)
[ "$LCTN" = "1" ] || die "lower_case_table_names 未生效（当前=$LCTN），请检查 /etc/mysql/mariadb.conf.d/99-fyqst.cnf 后重启 mariadb"

if mysql -uroot -ppwd123456 -N -e "show databases" 2>/dev/null | grep -qx "ttf_ly_main"; then
    warn "数据库已存在，跳过导入（如需重导请先 drop 7 个库）"
else
    say "导入 7 个数据库 ..."
    ( cd "$GAME_DIR" && bash ./sk ) || die "数据库导入失败"
fi

# ---------------------------------------------------------------- 5. 构建 C++
say "编译 C++ 服务 ..."
mkdir -p "$FYWEB_DIR"
cp -a "$REPO_DIR/src" "$REPO_DIR/tpl" "$REPO_DIR/native" "$REPO_DIR/Makefile" "$FYWEB_DIR/"
make -C "$FYWEB_DIR" -j"$(nproc)" || die "C++ 构建失败"
install -m 755 "$FYWEB_DIR/fyweb" "$FYWEB_DIR/fyversion" "$FYWEB_DIR/fylog" "$FYWEB_DIR/"

# 配置文件
cat > "$FYWEB_DIR/fyweb.conf" <<'EOF'
db_host=127.0.0.1
db_port=3306
db_user=root
db_pass=pwd123456
db_account=account
db_main=ttf_ly_main
db_login=ttf_ly_login
db_game1=ttf_ly_game1
db_game2=ttf_ly_game2
db_gm=gmdbyzf
gmcode=123456
app_key=QQ436043762
cdk_admin=admin
cdk_admin_pass=123456
www_root=/www/wwwroot/www
tpl_dir=/opt/fyweb/tpl
listen_addr=127.0.0.1
listen_port=9500
timezone=Asia/Shanghai
EOF
cat > "$FYWEB_DIR/fyversion.conf" <<'EOF'
port=9902
listen=0.0.0.0
versions_dir=/Server/uc/versions
log_dir=/Server/startlog
EOF
cat > "$FYWEB_DIR/fylog.conf" <<'EOF'
port=7000
listen=0.0.0.0
log_dir=/Server/startlog
EOF

# ---------------------------------------------------------------- 6. 打补丁
say "打补丁：端口竞态 / 原生插件接入 ..."
python3 "$REPO_DIR/deploy/scripts/fix_ports.py" "$GAME_DIR/uc" "$GAME_DIR/s1" || warn "端口补丁失败"
python3 "$REPO_DIR/native/wire_native.py" "$GAME_DIR/uc" "$GAME_DIR/s1" || warn "md5 插件接入失败"
python3 "$REPO_DIR/native/wire_hashes.py" "$GAME_DIR/uc" "$GAME_DIR/s1" || warn "sha1 插件接入失败"

# 原始 PHP 源码移出 web 根目录（C++ 已接管），避免源码暴露
mkdir -p /www/_php_orig
( cd "$WWW_DIR" && find . -name '*.php' -print0 2>/dev/null | while IFS= read -r -d '' f; do
      mkdir -p "/www/_php_orig/$(dirname "$f")"; mv "$f" "/www/_php_orig/$f" 2>/dev/null
  done )
rm -rf "$WWW_DIR/vp000" 2>/dev/null && mv "$WWW_DIR/vp000" /www/_php_orig/ 2>/dev/null

# ---------------------------------------------------------------- 7. 启动脚本 / IP
say "安装 IP 刷新与启动脚本 ..."
cp "$REPO_DIR/deploy/scripts/setip.sh" "$GAME_DIR/setip.sh"
cp "$REPO_DIR/deploy/scripts/s.sh"     "$GAME_DIR/s.sh"
[ -f "$GAME_DIR/t.sh" ] || cp "$REPO_DIR/deploy/scripts/t.sh" "$GAME_DIR/t.sh"
chmod +x "$GAME_DIR/setip.sh" "$GAME_DIR/s.sh" "$GAME_DIR/t.sh" 2>/dev/null
[ -f "$GAME_DIR/s.sh.orig" ] || cp "$GAME_DIR/s.sh" "$GAME_DIR/s.sh.bak" 2>/dev/null

# ---------------------------------------------------------------- 8. nginx + systemd
say "配置 nginx 与 systemd ..."
cp "$REPO_DIR/deploy/nginx-fyqst.conf" /etc/nginx/sites-available/fyqst
ln -sf /etc/nginx/sites-available/fyqst /etc/nginx/sites-enabled/fyqst
rm -f /etc/nginx/sites-enabled/default
nginx -t || die "nginx 配置有误"

cp "$REPO_DIR"/deploy/systemd/*.service /etc/systemd/system/
systemctl daemon-reload
systemctl enable mariadb nginx fyqst-ip fyqst-game fyversion fylog fyweb >/dev/null 2>&1

# ---------------------------------------------------------------- 9. 启动 + 自检
say "启动服务 ..."
systemctl restart mariadb nginx
IP="${SERVER_IP:-$(hostname -I | awk '{print $1}')}"
"$GAME_DIR/setip.sh" "$IP" || warn "setip 失败"
systemctl restart fyversion fylog fyweb
systemctl restart fyqst-game
sleep 20

echo
say "==================== 自检 ===================="
systemctl is-active mariadb nginx fyqst-ip fyqst-game fyversion fylog fyweb | paste -sd' ' - | sed 's/^/  单元: /'
ss -lnt 2>/dev/null | awk '$1=="LISTEN"{print $4}' | grep -E ':(81|3306|7000|9001|9500|9901|9902|9903|10001|20001)$' \
    | sort -u | tr '\n' ' ' | sed 's/^/  端口: /'; echo
for u in / game.html gm/gm.php cdk/index.php; do
    printf "  %-16s -> %s\n" "/$u" "$(curl -s -o /dev/null -w '%{http_code}' "http://127.0.0.1:81/$u")"
done
printf "  %-16s -> %s\n" "manifest" "$(curl -s -o /dev/null -w '%{http_code}' http://127.0.0.1:9902/manifest)"

echo
say "完成。访问地址:"
echo "    游戏     : http://$IP:81/    （或 http://localhost:81/）"
echo "    GM 后台  : http://$IP:81/gm/gm.php      GM 码 123456"
echo "    CDK 平台 : http://$IP:81/cdk/index.php  admin / 123456"
echo
echo "  服务管理 : systemctl {status|restart|stop} fyqst-game fyweb fyversion fylog"
echo "  日志     : /Server/startlog/*.log"
echo "  改 IP    : /Server/setip.sh <新IP>"
