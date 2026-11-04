# Project rules

## 项目上下文入口与任务边界

- 新会话先读本文件和 `docs/current-status.md`；随后只读与任务相关的 `docs/architecture.md`、`docs/game-canon.md`、`docs/development-rules.md` 及实现文件，不要默认通读全部历史日志。
- 通常一个独立功能对应一个会话、一个 `codex/` 分支及一个PR。同一Bug的诊断、修复和验收保持在原任务内；不要为每个小修改拆会话。仅在用户明确要求时创建新的用户对话。
- 开始前核对Git状态和任务范围，保留其他任务修改；并行工作须明确文件归属、协议/数据变更和统一部署负责人，禁止并行操作同一构建目录或同时部署。
- 未经当前任务明确授权，不提交、推送、合并或部署。功能完成后记录修改位置、实际测试与结果、未完成事项、风险和未提交修改；更新现有状态文档并给出简短交接说明。
- 文档不是完成证据：未测试的功能不能标记为已验收，历史部署结果不能冒充当前线上健康检查。具体规则冲突时先检查当前代码、数据和用户最新明确要求，不凭旧聊天静默改动游戏平衡。

## The TypeScript version is gone — recover it from git history

The TypeScript implementation was deleted on 2026-10-08: the browser client
(`src/`, `src/graphics/`, `src/title_screen/`, ...), the Node server
(`src/server/`, `src/ecs/`), the tsc/webpack toolchain, and the compiled
JavaScript that used to sit in `dist/`. `cpp/` is the only implementation:

- client — rendering, UI, menus, input, effects: `cpp/client/`
- server: `cpp/server/`
- shared code: `cpp/shared/`; tests: `cpp/tests/`; tools: `cpp/tools/`;
  the single-file offline build: `cpp/offline/`

Do not re-add TypeScript sources, tsconfig/webpack configs or compiled TS
output, and do not port a change "back" to TypeScript.

To see how the TypeScript version did something, read it out of history. The
last commit with the full TypeScript tree is **`d47055a7`**:

```sh
git ls-tree -r --name-only d47055a7 -- src/          # what existed
git show d47055a7:src/server/enemySpawner.ts         # read one file
git grep -n getEnemySizeScale d47055a7 -- src/       # search it
git log --oneline d47055a7 -- src/database.ts        # its history
```

Many comments in `cpp/` cite a TypeScript file as the reference a port was
taken from (`src/mobs.ts getEnemySizeScale`, `playerState.ts:2751`, "the
reference", "the browser build"). Those paths resolve in that history. A cited
line number is from whatever `src/` looked like when the comment was written:
`git blame` the C++ line, then `git show <that commit>:src/<file>`, or search
by symbol. "The browser build" in older comments usually means the TypeScript
version; in `cpp/client/web/`, the text and markup code and
`cpp/third_party/cpp_canvas/` it can mean the emscripten web client.

## Game data

- `data/mobs.json`, `data/petals.json`, `data/mob_drops.json` and `maps/`
  (Tiled maps — see `maps/README.md`) are the source of truth.
  `cpp/CMakeLists.txt` stages them flat into `<build>/data`, which the wasm
  builds embed as `/data`. The repo-root `data/` is not a valid `--data`
  directory by itself (it has no maps); run binaries from their build dir.
- The handshake compares a content hash taken over the raw bytes of the three
  JSON files, `maps.json`, every map it lists and their tilesets, so any edit
  to them — even reformatting — means client and server are rebuilt and
  shipped together.

## dist/

- `dist/` is committed: the in-game `/admin update` installs the `web`
  branch's zipball `dist/`, and prod runs `dist/server.js` + `dist/server.wasm`.
- What is committed there is exactly what `npm run build` stages
  (`scripts/build-web.js`): `index.html`, `bundle.js`, `bundle.js.bin`,
  `bundle.wasm`, `bundle.wasm.bin`, `server.js`, `server.wasm`,
  `favicon.ico`. `dist/inventory.json` (the live database) and the offline
  pages (`npm run build:offline`) are gitignored; never commit them.
- Rebuild it with `npm run build` from a clean tree and commit it with the
  source change. A `.bin` always moves together with its plain file.
- The C++ server takes no npm runtime dependency (the update never runs
  `npm install`); the only packages are the optional WebTransport pair.

## Protocol

Bump `kProtocolVersion` in `cpp/shared/net/protocol.h` whenever any message
layout changes, wherever it is written (`cpp/shared/net/`, `writeMapGrid`, the
skin format), and ship client and server together.

## Admin rights

Admin rights come only from server-held state: the account's `admin` flag in
the database, or a one-life `/admin grant_admin`. Never grant, check or target
a privilege by username, display name or any other string a player chooses.

## Reference clones

- `~/rysteria_gardn` — the visual/style reference (C++ client). Petal, mob and
  UI rendering should follow it.
- `~/gardn` — the movement/physics feel reference.

## Docs

`README.md` (build, run, test), `cpp/docs/ARCHITECTURE.md` (engine),
`maps/README.md` (map authoring), `cpp/docs/admin-dashboard.md` (admin tools),
`cpp/third_party/cpp_canvas/README.md` (the canvas library).

Do not edit README.md

## Production deployment

- 每次部署前先开启维护等待页，再正常停服保存；将停服后的最新存档下载到用户本地电脑 `D:/ashflor/backups/florrclone/<UTC时间>/`，校验本地与服务器 SHA256 一致后才允许覆盖正式文件。不要提交任何存档或备份到 Git。
- 使用 `scripts/deploy-with-backup.cjs` 执行部署，维护页提供小游戏；后端和协议校验通过后才恢复公网入口。更新期间应显示等待页，不能直接向玩家显示 502。
