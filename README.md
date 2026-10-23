# 绯雨骑士团 H5（fyqst）WSL 部署 + C++ 化改造

按 [echeverra.cn/fyqst](https://echeverra.cn/fyqst) 教程在 **WSL / Debian** 上部署《绯雨骑士团》H5 服务端，
并把教程里的 **PHP 前端层**、**Node 版本清单服务**、**Node 日志服务**、**高频哈希计算**整体换成 C++。

> 本仓库**不包含游戏本体**（`fyqst.zip`）。请按教程获取游戏包并在本地解压，本仓库只提供
> 部署脚本、C++ 源码/预编译构建、配置与运维文档。

---

## 1. 部署要求

| 项目 | 要求 |
|---|---|
| 系统 | Debian 12/13 或 Ubuntu 22.04+（WSL2 亦可），**需启用 systemd** |
| 权限 | root |
| 内存 | ≥ 4 GB（跑满 5 个 Node 进程约 1.2–1.5 GB，MariaDB 约 130 MB） |
| 磁盘 | ≥ 3 GB（游戏包 560 MB + 运行日志 + 数据库） |
| 端口 | 81、7000、9001、9500、9901、9902、9903、10001、20001、3306 |
| Node | **v6.9.5**（脚本自动安装，勿升级——pomelo 框架只兼容老版本） |
| 数据库 | MariaDB 11.x / MySQL 5.6+，需 `lower_case_table_names=1`、`sql_mode=''` |
| 编译 | g++ ≥ 12、make、libmariadb-dev（脚本安装） |
| 运行库 | 无需 PHP（PHP 层已由 C++ 接管） |

### 依赖清单（脚本会装）

```
curl ca-certificates xz-utils net-tools procps unzip
mariadb-server nginx g++ make libmariadb-dev libmariadb-dev-compat
node-v6.9.5-linux-x64  （从 nodejs.org 下载，或离线包）
```

WSL 需要先启用 systemd（Debian 镜像默认没装）：

```bash
apt-get install -y systemd systemd-sysv
printf '[boot]\nsystemd=true\n' > /etc/wsl.conf
# 然后在 Windows 侧执行： wsl --shutdown
```

---

## 2. 一键部署

```bash
# 1) 取游戏包（自行获取，见教程）
#    解压后应有 Server/ 和 www/ 两个目录

# 2) 克隆本仓库
git clone https://github.com/ycm50/fyqst-wsl-cpp.git
cd fyqst-wsl-cpp

# 3) 一键安装（游戏包目录作为参数）
sudo bash install.sh /path/to/fyqst_src
```

可选环境变量：

```bash
HTTP_PROXY=http://172.25.192.1:10808   # apt / node 下载走代理
SERVER_IP=192.168.1.10                 # 指定对外 IP（默认取 hostname -I）
NODE_TARBALL=/root/node-v6.9.5-linux-x64.tar.xz   # 离线安装
SKIP_APT=1                             # 依赖已装好
```

脚本做的事：装依赖 → 装 Node 6.9.5 → 部署 `Server/`+`www/` → 配 MariaDB 并导入 7 个库
→ 编译 C++ 服务 → 打端口补丁/接原生插件 → 配 nginx + systemd → 刷 IP → 启动 → 自检。

---

## 3. 架构与 C++ 替换范围

| 端口 | 服务 | 实现 | 说明 |
|---|---|---|---|
| 81 | 站点 | nginx | 静态资源；`*.php` 反代到 9500 |
| 9500 | 登录/GM/CDK 后端 | **C++ `fyweb`** | 替代 `game.php` / `pay.php` / `cdkmail.php` / `gm/*` / `cdk/*` |
| 9902 | 版本清单 | **C++ `fyversion`** | 替代 `versionApp.js`，输出与原版**逐字节一致** |
| 7000 | 客户端日志收集 | **C++ `fylog`** | 替代 `logApp.js` |
| 9901 | 主登录/路由 | node **allApp** | 与 9903 合并为一个进程 |
| 9903 | 支付回调/渠道视图 | node **allApp** | 与 9901 同进程，共享 uw-data |
| 10001 | 主城服 | node cityserver | |
| 20001 | 游戏服 | node serverApp | |
| 9001 | 战斗服 | node battleserver | |
| 3306 | MariaDB | | |

> **进程合并**：每个 Node 进程都会把同一份 `uw-data`（游戏配置数据，实测 **315 MB**）加载一遍。
> `mainApp(9901)` 与 `viewApp(9903)` 已合并为单个 `allApp.js` 进程，两者共享一份数据：
> **563.5 MB → 277.6 MB，省 286 MB**（见第 10 节）。

**高频计算 C++ 化**：`native/fynative.cpp` 编译成 `fynative.node`（Node 6 原生插件，
直接用 node 头文件编译，**不需要 node-gyp / python2**），接入：

- `uw-utils/src/md5Utils.js` → md5 / HMAC-MD5（登录、会话、签名）
- `uw-utils/src/hashesUtils.js` → SHA1 hex/raw/b64（支付签名）

实测 md5 10 万次：JS 208 ms → C++ 36 ms（**5.8×**）；819 项与 `node crypto` 交叉校验全通过，且带纯 JS 兜底。

---

## 4. IP 更换

源码里有 22 处硬编码 IP，另有 2 张数据库表。**WSL 每次重启 IP 都会变**，所以由 systemd 单元
`fyqst-ip.service` 在开机时自动刷新：

```bash
# 手动改（文件 + 数据库一起改，幂等）
/Server/setip.sh 172.25.204.81

# 开机自动执行
cat /etc/systemd/system/fyqst-ip.service
```

---

## 5. 生命周期（无常驻保活）

服务全部由 systemd 托管并 `enable`：

* **打开 WSL** → 自动依次启动 mariadb → nginx → fyqst-ip → fyqst-game（5 个 node）→ fyversion/fylog/fyweb
* **关闭 WSL**（`wsl --shutdown` 或关掉终端）→ 全部停止，不留后台进程

Windows 侧双击 `deploy/start_fyqst.cmd` 可一键开 WSL + 等 20 s + 打开浏览器。

实测：`shutdown` 后全部 000；打开后站点 3 s 内 200、20 s 内 10 个端口全开；再关闭全部 000。

---

## 6. 常用运维

```bash
systemctl status  fyqst-game fyweb fyversion fylog
systemctl restart fyqst-game          # 重启 5 个 node
systemctl restart fyweb fyversion fylog
tail -f /Server/startlog/*.log        # 游戏服日志
bash deploy/scripts/status.sh         # 一键体检（进程/端口/接口/内存）
bash deploy/scripts/test_web.sh       # Web 层 29 项接口自测
```

访问地址：

* 游戏 `http://<IP>:81/`（或 `http://localhost:81/`）
* GM 后台 `http://<IP>:81/gm/gm.php`（GM 码 `123456`）
* CDK 平台 `http://<IP>:81/cdk/index.php`（`admin` / `123456`）

---

## 7. 目录

```
├── install.sh                 一键部署
├── Makefile                   构建 fyweb / fyversion / fylog / fynative.node
├── src/                       C++ 源码
│   ├── main.cpp               PHP 层替代（登录/GM/CDK）
│   ├── fyversion.cpp          版本清单服务（含 CORS、静态兜底、请求日志）
│   ├── fylog.cpp              日志收集服务
│   ├── common.cpp/.h          DB / 会话 / md5 / 模板
│   ├── json.h                 自研极简 JSON（保序，用于 manifest 合并）
│   └── httplib.h              cpp-httplib 0.63.0（MIT，vendor）
├── native/                    Node 6 原生插件
├── tpl/                       页面模板（GM/CDK/兑换）
├── deploy/
│   ├── nginx-fyqst.conf
│   ├── mysql/                 MariaDB 兼容配置 + root 初始化
│   ├── systemd/               5 个单元
│   ├── allApp.js              进程合并启动器（mainApp+viewApp）
│   ├── scripts/               setip / s.sh / t.sh / fix_ports / apply_merge / 自测
│   └── start_fyqst.cmd        Windows 一键启动
└── docs/                      部署记录与踩坑
```

---

## 8. 进程合并（省 286 MB）★

### 为什么

实测单个 Node 进程的内存构成：

| 加载内容 | RSS |
|---|---|
| 裸 node 6 | 25.4 MB |
| + express / body-parser | 33.8 MB |
| + uw-db（连库） | 34.0 MB |
| **+ uw-data（游戏配置数据）** | **315.3 MB** |

也就是说每个进程 ~280 MB 里约 **290 MB 是同一份游戏数据**。5 个进程 = 5 份副本。

### 怎么做

`deploy/allApp.js` 只是一个启动器，把两个 app 加载进同一个进程：

```js
require('./mainApp.js');   // 9901 主登录/路由
require('./viewApp.js');   // 9903 支付回调/渠道视图
```

成立的前提（本项目已具备）：

1. 两个 app 各自 `reset.createApp()` 监听自己的端口，互不干扰；
2. `uc` / `s1` 的 `env-cfg/*.js` 已打「端口兜底」补丁，任何一次 reset 写出的
   `config/project.json` 都是完整端口表，`uw-config` 的缓存对两个 app 都正确；
3. 两者同在 `/Server/uc`，`reset.js` 用 `__dirname` 定位，配置目录一致。

### 效果

| 指标 | 合并前 | 合并后 |
|---|---|---|
| Node 进程数 | 5 | **4** |
| mainApp + viewApp 占用 | 283.3 + 280.2 = **563.5 MB** | **277.6 MB**（单进程） |
| Node 总占用 | 1413.8 MB | **1134.7 MB** |

验收：9901 返回 JSON 12900 字节（登录/区服接口正常）、9903 `/pay/index.html` 200、
10 个业务端口全部监听、Web/GM/CDK/manifest 全 200。

### 还能继续合并吗

`serverApp`(20001)、`cityserver`(10001)、`battleserver`(9001) 也可以同样的方式合并，
理论上再省 **约 560 MB**（每个进程约 280 MB），但它们是游戏逻辑本体（定时器、世界 Boss、
排行刷新、DB 连接池、全局单例），需要完整回归测试后再动。

---

## 9. 已知限制

1. `serverApp` / `cityserver` / `battleserver` 仍是独立 Node 进程（各约 280 MB），
   尚未合并；合并方案与风险评估见第 8 节末尾。
2. 3 个游戏进程是 pomelo 游戏逻辑本体，无法用 C++ 替换而不重写游戏。
3. 原 `vp000`（第三方运营后台，整站 PHP）已移出 web 根目录，未做 C++ 化。
4. 每请求的 JSON.parse/stringify 未替换：需要把 C++ JSON 塞进 300 KB 业务代码的每条消息路径，
   风险/收益不划算。

---

## 10. 许可

* 本仓库的 C++ 代码、脚本、文档：MIT
* `src/httplib.h`：cpp-httplib（MIT）
* **游戏本体、素材、服务端 JS 逻辑版权归原厂商**，本仓库不包含，也不分发。
