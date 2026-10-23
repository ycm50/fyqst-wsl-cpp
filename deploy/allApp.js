/**
 * 合并启动器：mainApp(9901) + viewApp(9903) 跑在同一个 Node 进程里
 *
 * 为什么要合并：
 *   每个 Node 进程都会把同一份 uw-data（游戏配置数据，约 290MB）加载一遍。
 *   合并后两个服务共享同一份数据，实测可省约 200~280MB。
 *
 * 安全性：
 *   - 两个 app 各自用 reset.createApp() 监听自己的端口，互不干扰；
 *   - config/project.json 的端口竞态已由 env-cfg 的端口兜底补丁解决，
 *     任何一次 reset 写出的都是完整端口表，uw-config 缓存的结果对两者都正确。
 */
'use strict';

console.error('[allApp] 启动 mainApp(9901) + viewApp(9903) 合并进程 ...');

require('./mainApp.js');
require('./viewApp.js');

console.error('[allApp] 两个服务已注册');
