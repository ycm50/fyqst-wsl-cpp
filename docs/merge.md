
## 10. 进程合并：mainApp + viewApp（省 286 MB）

### 动机（实测数据）

逐个模块测 RSS：

| 加载内容 | RSS |
|---|---|
| 裸 node 6 | 25.4 MB |
| express + body-parser | 33.8 MB |
| uw-db | 34.0 MB |
| **uw-data（游戏配置数据）** | **315.3 MB** |
| config + log + utils + route + db | 353.8 MB |

结论：每个 Node 进程 ~280 MB 里，约 290 MB 是**同一份 uw-data**。5 个进程 = 5 份副本，
仅这一项就浪费了约 1.1 GB。

### 做法

`deploy/allApp.js`：

```js
require('./mainApp.js');   // 9901
require('./viewApp.js');   // 9903
```

前提条件（本项目均已满足）：

1. 两个 app 各自 `reset.createApp(serverType, port)`，监听各自端口；
2. 端口竞态已由 `fix_ports.py` 修复——任何一次 reset 写出的 `project.json` 都含完整端口表，
   所以 `uw-config` 的缓存对象对两个 app 都是正确的；
3. 两者同在 `/Server/uc`，`reset.js` 用 `__dirname` 定位，配置目录一致。

### 实测结果

| 指标 | 合并前 | 合并后 |
|---|---|---|
| Node 进程数 | 5 | 4 |
| mainApp + viewApp | 283.3 + 280.2 = 563.5 MB | **277.6 MB** |
| 启动耗时 | — | 两个端口 2 秒内就绪 |
| 9901 登录接口 | 200 | 200（JSON 12900 字节） |
| 9903 /pay/index.html | 200 | 200 |
| Node 总占用 | 1413.8 MB | **1134.7 MB** |

### 下一步（可选）

`serverApp`(20001) / `cityserver`(10001) / `battleserver`(9001) 也可同样合并，理论上再省
约 560 MB，但需回归测试：主城切换、战斗结算、世界 Boss、排行刷新、跨服/公会战定时器。
