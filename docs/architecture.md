# 架构阅读入口

这里提供导航；引擎详细说明以[现有架构文档](../cpp/docs/ARCHITECTURE.md)为主，不维护第二套完整描述。

## 运行结构

项目当前实现为C++。权威服务端负责移动、战斗、掉落、账号和存档，客户端负责显示、交互及输入。Web发布中客户端是浏览器WASM，服务端是Node加载的WASM；不能把“服务端C++”误写成线上已运行原生二进制。

协议92起，Node服务端单独编译pthread版本的shared/server库：主线程和3个工作线程协作生成各玩家快照。每批快照期间世界与事件只读，每个任务独占ClientView和输出缓冲，全部完成后由主线程发送；战斗、掉落、账号和存档仍由主线程处理。浏览器及离线构建保持非线程库。`FLIX_SNAPSHOT_WORKERS=0`可在启动时禁用工作线程（0～3），用于诊断或回退；修改线上启动环境仍需维护流程。最近发布目标模拟20 TPS、快照20 Hz，绘制帧率独立，不代表所有负载下都能达到目标。

## 当前数据与功能入口

- 当前发布采用单服数据库：`cpp/server/db.h/.cpp`负责账号和JSON持久化，`game_server.cpp`负责服务端操作校验；生产存档不属于源码或dist发布内容。
- 议会：`cpp/server/council.h`计算资格、权重与议题快照，`cpp/client/web/council.html`提供页面；修改HTML不能替代服务端计算变更。
- 商店：`cpp/shared/game/shop.h`定义商品和价格规则，`cpp/server/shop_lock.h`与`game_server.cpp`校验锁定和购买，`cpp/client/ui/menu_shop.cpp`负责展示。
- 模拟系统：`cpp/server/systems/`；输入、菜单与美术：`cpp/client/ui/`、`cpp/client/render/`。客户端提示与服务端真实效果须配套核对。
- 运行与构建来源以[current-status.md](current-status.md)为准。工作区的未发布实现不能直接当作线上架构。

| 路径 | 职责 |
| --- | --- |
| `cpp/client/` | 客户端、菜单、HUD、输入、渲染 |
| `cpp/server/` | 会话、账号数据库、权威游戏循环及系统 |
| `cpp/shared/core/` | ECS、JSON及基础类型 |
| `cpp/shared/game/` | 内容注册、稀有度、地图、地形及共享规则 |
| `cpp/shared/net/` | 协议、编解码及网络传输 |
| `cpp/third_party/cpp_canvas/` | Canvas/SDL渲染、窗口和浏览器输入桥 |
| `cpp/tests/` | C++及浏览器回归 |
| `data/`、`maps/` | 花瓣、怪物、掉落、Tiled地图及资源 |
| `scripts/` | 构建、验收、维护等待页、迁移及部署 |
| `dist/` | 配套发布产物，受Git管理；不包括线上存档 |

游戏UI主要画在Canvas上。浏览器隐藏DOM输入框承担键盘/IME输入，因此输入问题可能跨 `client/ui/text_input.cpp` 和 `cpp_canvas/window.cpp`，不能只修画出来的输入框。

地图与JSON由构建系统整理到构建目录的data后嵌入WASM；根目录data本身缺少地图，不是完整运行数据目录。协议版本和原始内容哈希共同约束两端兼容。

## 按问题阅读

- 引擎、ECS和网络：[ARCHITECTURE.md](../cpp/docs/ARCHITECTURE.md)。
- 地图制作、碰撞来源：[maps/README.md](../maps/README.md)。
- 画布和平台桥：[cpp_canvas/README.md](../cpp/third_party/cpp_canvas/README.md)。
- 管理工具：[admin-dashboard.md](../cpp/docs/admin-dashboard.md)。
- 游戏约束：[game-canon.md](game-canon.md)；构建与发布：[development-rules.md](development-rules.md)。

根AGENTS.md提供历史TypeScript树的查询方式；引用旧文件前应确认对应提交在当前仓库实际可解析，不重建已删除的TypeScript工具链。
