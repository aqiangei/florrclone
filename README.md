# florrclone ![florr](img/splitter_small.svg)

Ashflor 的 C++/WASM 整端（服务端 + 浏览器客户端）。权威联机、账号存档、地图与内容表均在本仓库；`dist/` 已含预构建产物，克隆后可直接跑。

更多边界与内容对齐说明见 [ASHFLOR.md](ASHFLOR.md)。

## 本地运行

需要 **Node.js 18+**。

```powershell
git clone https://github.com/aqiangei/florrclone.git
cd florrclone
npm install
npm run start_nobuild
```

浏览器打开 <http://localhost:3001> 。

默认端口 `3001`，存档写在 `dist/inventory.json`（已 gitignore，不会提交）。

本地默认可用管理员账号进入（开发向）；公网部署请自行改掉。

### 改代码后重建

改 C++ 或 `data/*.json` 后需要重编 WASM。本机用 **WSL Ubuntu + emsdk**（默认 `/root/emsdk/emsdk_env.sh`）：

```powershell
npm run build:wsl
npm run start_nobuild
```

只改前端壳或已有 `dist/`、不改引擎时，继续 `npm run start_nobuild` 即可。

| 命令 | 作用 |
|---|---|
| `npm run start_nobuild` | 直接跑已有 `dist/` |
| `npm run build:wsl` | 在 WSL 里编 client/server WASM 并写入 `dist/` |
| `npm start` | 本机直接 `build-web`（需本机已配 emsdk）后再启动 |

## License

Copyright (c) 2026 sussybite8888

AGPL-3.0-or-later，见 [LICENSE](LICENSE)。含改编自 [trigonal-bacon/gardn](https://github.com/trigonal-bacon/gardn) 的代码。
