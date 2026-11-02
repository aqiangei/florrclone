# florrclone-cpp（Ashflor）

C++/WASM 整端重写线。与现网 JS 服 [`../florrclone`](../florrclone) **并存**，本目录**暂不替换现网部署**。

## 来源分工

| 来源 | 采用什么 |
|---|---|
| [`../reference/florr_clone-web`](../reference/florr_clone-web)（本树底座） | 框架、Canvas UI、操作与移动端、组队/公会/皮肤、手感节奏、打击/受击/死亡、花瓣掉落动画与落点 |
| [`../florrclone`](../florrclone) | **内容表**一比一（花瓣/怪/掉落）+ **地图几何/传送门/刷怪名单**（`maps/*.tmj` 由 `scripts/port-florrclone-maps.js` 从 florrclone 生成；旧 reference 图在 `maps_ref_backup/`） |

手感对齐 `florrclone/server/feel.js`：**750 移速**、**半装填**（reload ×0.5）、**20 判**（花瓣接触 50ms）、掉落 **10× / Super+ 30×**。其余操作/UI/打击表现仍用 reference。

进度见 [`docs/CONTENT_PORT.md`](docs/CONTENT_PORT.md)。内容表已迁入（约 160 种花瓣、共有怪数值、Rock 掉落补齐）；新号另赠 20 Super。用 `npm run build:wsl` 重建后生效。

## 许可

底座为 **AGPL-3.0-or-later**（见 [LICENSE](LICENSE)），源自 flowrix / florr_clone-web。

## 本地运行

```powershell
cd d:\ashflor\florrclone-cpp
npm run start_nobuild
```

浏览器打开 http://localhost:3001 （与现网 JS 服端口错开）。

改 C++ / `data/*.json` 后需重建嵌入式 WASM。工具链在 **WSL Ubuntu**（与仓库根 [`DEPLOY.md`](../DEPLOY.md) 相同：`/root/emsdk/emsdk_env.sh`）：

```powershell
cd d:\ashflor\florrclone-cpp
npm run build:wsl
npm run start_nobuild
```

仅跑已有 `dist/`、不改内容时：`npm run start_nobuild` 即可。
