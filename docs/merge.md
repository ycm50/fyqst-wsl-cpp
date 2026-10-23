
## 10. 进程合并（Node 进程 5 → 3，省 567 MB）

### 动机（实测数据）

逐个模块测 RSS：

| 加载内容 | RSS |
|---|---|
| 裸 node 6 | 25.4 MB |
| express + body-parser | 33.8 MB |
| uw-db | 34.0 MB |
| **uw-data（游戏配置数据）** | **315.3 MB** |
| config + log + utils + route + db | 353.8 MB |

结论：每个 Node 进程 ~280 MB 里，约 290 MB 是**同一份 uw-data**。
5 个进程 = 5 份副本，仅此一项就浪费约 1.1 GB。

### 做法

两个启动器，把「同职责」的服务塞进同一个进程，共享那一份 uw-data：

`deploy/allApp.js`（在 `/Server/uc` 下运行）

```js
require('./mainApp.js');   // 9901 主登录/路由
require('./viewApp.js');   // 9903 支付回调/渠道视图
```

`deploy/gameAll.js`（在 `/Server/s1` 下运行）

```js
process.chdir('/Server/s1/wserver');
require('/Server/s1/wserver/cityserver.js');    // 10001 主城

// battleserver 依赖 battleId=1，而 uw-config.project 是缓存对象，直接改
var proj = require('/Server/s1/node_modules/uw-config').project;
proj.battleId = 1;
proj.battleServerPort = 9001;

process.chdir('/Server/s1/battleserver');
require('/Server/s1/battleserver/battleserver.js');  // 9001 战斗
```

成立的前提（本项目均已满足）：

1. 各 app 用 `reset.createApp(serverType, port)` 监听自己的端口，互不干扰；
2. `uc` / `s1` 的 `env-cfg/*.js` 已打「端口兜底」补丁，任何一次 reset 写出的
   `project.json` 都含完整端口表，`uw-config` 的缓存对同进程内所有 app 都正确；
3. **city/battle 两个服务用 `fs.walkDirSync('./handlers')` 加载处理器，路径相对进程 cwd**，
   所以必须在各自 `require` 前切换 cwd（上面代码里的两处 `chdir` 就是为此）。

### 效果（实测）

| 指标 | 合并前 | 合并后 |
|---|---|---|
| Node 进程数 | **5** | **3** |
| mainApp + viewApp | 283.3 + 280.2 = 563.5 MB | **279.9 MB**（allApp） |
| cityserver + battleserver | 282.1 + 282.0 = 564.1 MB | **282.9 MB**（gameAll） |
| serverApp | 286.2 MB | 284.2 MB |
| **node 合计** | **1413.8 MB** | **847.0 MB（−567 MB，−40%）** |
| 系统 used | ~2000 MB | **1531 MB** |

验收：

* 10 个业务端口全部监听：81 / 3306 / 7000 / 9001 / 9500 / 9901 / 9902 / 9903 / 10001 / 20001
* 接口全 200：首页、game.html、GM、CDK、manifest、9901 登录接口（JSON 12900 字节）、9903 `/pay/index.html`
* 日志正常：`city listening on *:10001`、`server listening on *:9001`、`start server finish! listen 20001`

### 还能继续吗

把 `serverApp`(20001) 也合进 `allApp` 或 `gameAll`，还能再省 **约 283 MB**（2 个进程 ≈ 566 MB）。
但它同时涉及：`cluster` 判断、`sessionManager` 初始化、`g_common.appIsHttp` 全局开关、
`SIGINT/SIGUSR` 停服钩子，且它是玩家请求的主入口（与后台计算同进程会互相抢占事件循环），
**需要完整回归测试（战斗/世界 Boss/排行/停服重启）后再动**。

### 回滚

```bash
# 恢复成"每服务一个进程"的旧启动方式
cp /Server/s.sh.orig /Server/s.sh   # 若没有备份，用仓库里 docs 记录的原始顺序手工恢复
systemctl restart fyqst-game
```
