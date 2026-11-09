# florrclone ![花瓣图标](img/splitter_small.svg)

一款浏览器多人动作游戏。配装花瓣，在丛林、下水道、侏罗纪等地图打怪拾取掉落，合成和升级装备。可以和队友打 Wave，挑战首领与奖励波，也可以在超级木桩测试 10 秒伤害并保存配装成绩。

## 在线游玩

| 服务器 | 游戏入口 |
| --- | --- |
| 一服 | [154.219.105.158:8088](http://154.219.105.158:8088/) |
| 二服 | [154.219.105.161:10000](http://154.219.105.161:10000/) |
| 三服 | [154.219.105.164:10000](http://154.219.105.164:10000/) |
| 四服 | [1.14.76.55](http://1.14.76.55/) |

四服各自保存账号进度、花瓣与装备。可以先以游客进入，再在「账户」设置用户名和密码。每次发布的具体内容可在游戏内「更新日志」查看。

玩家交流和问题反馈见 [QQ 交流群 227990150](https://qm.qq.com/q/E0OG9CLIg)。

## 代码与架构

后端基于 [flowrix-io/florr_clone](https://github.com/flowrix-io/florr_clone)。浏览器客户端和服务端均由 C++ 编译为 WebAssembly。浏览器负责输入、绘制与界面，Node.js 承载服务端 WASM，通过 WebSocket 与客户端同步。战斗、掉落、账号和存档由服务端处理。

| 路径 | 内容 |
| --- | --- |
| [`cpp/client/`](cpp/client/) | 浏览器客户端、Canvas 渲染、界面和输入 |
| [`cpp/server/`](cpp/server/) | 游戏模拟、联机同步、账号与存档 |
| [`cpp/shared/`](cpp/shared/) | 双端共享规则、内容读取和网络协议 |
| [`data/`](data/) 与 [`maps/`](maps/) | 花瓣、怪物、掉落表与 Tiled 地图 |
| [`dist/`](dist/) | 配套构建的浏览器与服务端产物 |

## 本地运行

安装 Node.js 22 后，可以直接用仓库里的配套构建启动。

```sh
git clone https://github.com/aqiangei/florrclone.git
cd florrclone
node -r ./scripts/deploy-runtime.cjs dist/server.js --port 3001 --db dist/inventory.json --data /data --web-root dist
```

浏览器打开 [localhost:3001](http://localhost:3001/)。上面的启动命令默认监听本机，使用 WASM 内嵌的游戏数据。`dist/inventory.json` 是本地存档，已由 Git 忽略。对公网提供服务时，可在前面配置反向代理，将游戏页面、资源和 `/ws` WebSocket 转发给后端。更新前备份自己的存档。

## 从源码构建

在装好 Node.js、CMake、构建工具和 Emscripten SDK 的 Linux 环境或 WSL 中执行以下命令。先加载 emsdk 的环境配置。

```sh
source /path/to/emsdk/emsdk_env.sh
npm run build
```

这会重建浏览器客户端和服务端，并将配套文件写入 `dist/`。修改共享协议、游戏数据或地图后，客户端与服务端应一起构建和更新。开发入口见[架构导航](docs/architecture.md)和[开发与发布规则](docs/development-rules.md)。

## 许可

AGPL-3.0-or-later，见 [LICENSE](LICENSE)。Copyright (c) 2026 sussybite8888。含改编自 [trigonal-bacon/gardn](https://github.com/trigonal-bacon/gardn) 的代码。
