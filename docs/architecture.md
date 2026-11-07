# 架构阅读入口

这里提供导航；引擎详细说明以[现有架构文档](../cpp/docs/ARCHITECTURE.md)为主，不维护第二套完整描述。

## 运行结构

项目当前实现为C++。权威服务端负责移动、战斗、掉落、账号和存档，客户端负责显示、交互及输入。Web发布中客户端是浏览器WASM，服务端是Node加载的WASM；不能把“服务端C++”误写成线上已运行原生二进制。

Node服务端单独编译pthread版本的shared/server库。主线程和最多15个工作线程共用一组线程，分别处理掉落维护、怪物移动与分离、玩家快照。分离读冻结的网格并按原顺序提交，掉落领取和试算、战斗、账号变更由主线程处理。单服存档由主线程交接变更，后台编码并刷盘。每批快照期间世界与事件只读，每个任务独占ClientView和输出缓冲，全部完成后由主线程发送。浏览器及离线构建保持非线程库。`FLIX_SIMULATION_WORKERS`、`FLIX_SNAPSHOT_WORKERS`分别控制两个阶段的工作线程上限（0～15，0走串行），默认随可用核心数调整。修改线上启动环境须经维护。最近发布目标模拟20 TPS、快照20 Hz，实际吞吐随场景变化。

单线模拟候选在野怪和召唤物各自的意图阶段前，将索敌与视线查询分批交给工作线程。存在沙暴拉人的地图保留野怪的实时索敌。召唤物在回撤、失主或换了查询输入时使用实时查询。原顺序仍负责写回目标、推进 AI、抽随机数及生成指令。碰撞按原顺序结算，提前排除同阵营野怪的无效接触。Node/WASM 直接读取进程的线程数配置。

接触阶段按实体缓存本阶段固定的体型和类别。碰撞仍按原候选顺序读取实时位置、角度、死亡状态并写回伤害和推挤。

复制快照前检查连接待发字节。C++ 和 Web 传输层可见积压达到 64 KiB 时跳过该连接的本轮编码，既有队列和 ClientView 增量基线保留。排空后继续从该基线写出当前状态。

本地共享账号候选由两个独立的游戏进程和一个协调器组成。每个游戏进程各跑一套地图和模拟循环，同账号切换进程时先提交进度并释放占用，再由目标进程载入同一份背包。Boss 和血祭倒计时按当前分片追加到持久化日志，纯世界状态保存绕过共享表版本检查。同批账号更新仍受独占和进度版本检查。协调器与适配器须配套更新。新协调器可以重放历史日志，旧协调器会拒绝含新版世界状态记录的日志。

Linux 可用 `cmake --build <native-build> --target shard_coordinator` 构建协调器。构建 Web 发布包后，用 `SHARD_COORDINATOR_BIN=<coordinator> node --test scripts/shard-shared-game.test.cjs` 在回环地址启动两个游戏进程和测试协调器。玩家选择不同分片才会分担负载，同一分片中的热点仍由该进程承担。生产集群发布脚本当前要求两台不同的主机。

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
