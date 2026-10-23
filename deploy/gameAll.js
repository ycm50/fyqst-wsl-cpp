/**
 * 合并启动器：cityserver(10001) + battleserver(9001) 跑在同一个 Node 进程里
 *
 * 原理同 allApp.js：两者各自都会加载一整份 uw-data（约 290MB），合并后只占一份。
 * 实测可省约 280MB。
 *
 * 注意点：
 *   1. 两个 app 都用 fs.walkDirSync('./handlers') 加载各自处理器目录，
 *      而该路径相对 **进程 cwd** —— 所以必须在 require 前切换 cwd；
 *   2. battleserver 依赖 project.battleId=1（cityserver 不需要），
 *      而 uw-config 的 project 对象是缓存的，直接改缓存即可；
 *   3. 端口竞态已由 env-cfg 的端口兜底补丁解决，project.json 总是完整端口表。
 */
'use strict';

var path = require('path');
var S1 = '/Server/s1';

console.error('[gameAll] 启动 cityserver(10001) + battleserver(9001) 合并进程 ...');

// ---- 1) cityserver（cwd 必须在 wserver，才能加载 wserver/handlers）
process.chdir(path.join(S1, 'wserver'));
require(path.join(S1, 'wserver', 'cityserver.js'));
console.error('[gameAll] cityserver 已注册');

// ---- 2) battleserver 需要 battleId=1
var proj = require(path.join(S1, 'node_modules', 'uw-config')).project;
proj.battleId = 1;
proj.battleServerPort = 9001;

// ---- 3) battleserver（cwd 切到 battleserver，才能加载 battleserver/handlers）
process.chdir(path.join(S1, 'battleserver'));
require(path.join(S1, 'battleserver', 'battleserver.js'));
console.error('[gameAll] battleserver 已注册');

console.error('[gameAll] 两个服务已注册');
