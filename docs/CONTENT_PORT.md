# 内容表迁入进度

## 原则（不变）

- **表现**：打击感、受击、死亡、掉落动画与落点、组队/公会/皮肤/UI/手感节奏 → reference。
- **内容**：花瓣/怪等种类与数值 → 对齐 florrclone。

## 已完成

1. 仓库底座：`d:\ashflor\florrclone-cpp`（自 `reference/florr_clone-web`）。
2. 本地服：`npm run start_nobuild` → http://localhost:3001 （现网未动）。
3. 名称审计：[`content-audit.md`](content-audit.md)。
4. 别名：[`../scripts/content-aliases.js`](../scripts/content-aliases.js)。
5. 花瓣数值同步：`sync-petal-stats.js` + 缺项补齐：`port-missing-content.js`（约 +36 花瓣，现约 159 种）。
6. 共有怪 `damage`/`health` 同步（`speed` 不盲拷）。
7. 新号：5×Common Basic 装备 + 20×Super 随机花瓣（`grantStarterKit`）。
8. WSL 重建：`npm run build:wsl`（工具链见仓库根 `DEPLOY.md` / `/root/emsdk`）。

## 限制

- 新花瓣特殊技能若无对应 C++ 钩子，目前按数值/可装备表项存在，复杂行为需后续逐个接系统。
- 表现层（打击/受击/死亡/掉落动画落点）仍用 reference。
- 花瓣表只保留 florrclone 有的种类：`scripts/prune-reference-petals.js`（已去掉 reference 独有约 41 种）。
