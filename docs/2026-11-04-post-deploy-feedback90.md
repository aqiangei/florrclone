# 2026-11-04 部署后反馈（本地，尚未发布）

## 修改

- `cpp/client/app.cpp`：键盘移动仍保留光标；批量合成黑名单变更立即持久化，其他设置仍沿原保存流程。
- `cpp/client/ui/menu_crafting.cpp`、`menus.h/.cpp`：按花瓣稳定ID记忆黑名单，编辑态点击已有花瓣切换，支持清空；跨稀有度排除，手动单种合成不受影响。保存范围为同一浏览器，不承诺跨设备同步。
- `net_client.h/.cpp`、`game_server.cpp`：BulkCraft携带排除列表，服务端先完整校验再合成，不扣黑名单材料。协议90，需配套构建上线。
- `cpp/client/ui/menu_guild.cpp`：在线成员行常驻组队按钮；会长的更多菜单单独打开踢出，再确认，不与组队直接相邻。
- `maps/garden.tmj`：工厂入口由1536方形改为2560；开放的左门洞SVG(80,240)对齐原传送坐标(22493.9393939394,15487.8787878788)。没有改内部地图、传送位置或碰撞层。
- `cpp/server/systems/petals.cpp`：花粉云寿命5→4秒，重复命中间隔500→600毫秒，单次伤害及原装填时间不变。原代码不是无限寿命，但云可驻留重叠连击，此调整削减驻留收益。
- `cpp/server/game_server.cpp`：生命之符(ankh)破碎时将仍活着的拥有者移至破碎位置，并进行地形圆碰撞修正、清掉移动速度；不治疗、不复活，不移动另一具身体。
- `data/mobs.json`：仅四种丛林/地狱蜈蚣头、身体扩展SVG viewBox与对应visual_scale，避免圆边/触角被矩形裁剪；保留原身体直径及玩法碰撞半径。其他95种怪配置逐项比对未变。
- `cpp/server/splitter.cpp`：分裂结束时把已释放Projectile的owner/creditTo及GroundEffect.owner转交存活身体。原先分身消失后攻击仍存在，归属追踪却得到NULL_ENTITY，延迟击杀没有任何掉落资格。

## 验证

- 原生回归：`splitter_parked` 2项通过（分身拾取入包一次、黑名单保留岩石且叶子仍被合成、40次存活分身击杀、40次分身死亡后的子弹/范围攻击击杀；最新已换为H屏幕server/H沙尘暴交替场景）。延迟伤害归属用例在修前2个断言失败，修后通过；掉落按堆叠数量统计。
- 原生 `ankh_break` 1项通过（破碎传送及死亡不复活）、`defending_drops_pollen` 1项通过（消耗花瓣、寿命及间隔）。使用现有独立原生构建 /root/feedback88-native-build；花粉用例由现有petal_tests提取进入临时定向构建。
- 两端WASM静态库编译；Chrome既有绘制回归22项通过。新增实际合成面板交互验证黑名单添加/取消均刷新沿用；公会更多入口及蜈蚣美术实际截图检查。测试夹具保留于 `cpp/tests/browser/post_deploy_feedback/`，临时构建和截图位于 `D:/ashflor/temp/feedback90-ui/`。
- 工厂JSON验证开放门洞锚点与传送点精确一致，碰撞层不变。尚未在正式线上实走验收。

## 边界与交接

本轮没有提交、推送或部署，dist仍为已发布协议89。上线须重新构建成对客户端/服务端并走维护与停服备份流程。没有改线上存档、没有重复发放或解除。

玩家明确“分身打死怪后没掉落”。目前找到并复现的是分身消失后的延迟攻击归属丢失，普通存活分身击杀测试本来能掉落；玩家补充为H屏幕与H沙尘暴；屏幕按server美术对应测试。分身当时生死及距离仍不明确，不能宣称其他未知条件全部排除。

其他任务的README、CMake、未跟踪工具及原有换行修改未纳入本任务。

## 补充：H屏幕/H沙尘暴专项

- `reported_hyper_screen_and_sandstorm_keep_split_account_loot`通过：每种对比300次未分裂与300次分裂击杀，同种在相同随机种子下逐种花瓣/稀有度/数量完全相同。server普通产物21252、sandstorm普通产物27000（均为300次累计堆叠数量，不是永恒数量）。两个类型的单次击杀也均有普通掉落。
- `splitter_parked_kills_keep_loot_entitlement_for_the_active_body`改为真实H server/H sandstorm数据并通过：存活分身击杀有领取权，分身消失后残留子弹与云继续击杀也有领取权。
- 检查现有规则：H免1%参与伤害门槛，但保留最多25人领取上限，队伍按平均伤害排序、同账号双身只占一份。未擅改此规则。不能从本地成功试算倒推玩家当时是否进入前25份或分身是否刚死亡。
- 本次追加只扩展测试和记录，沿用上一轮已修的延迟攻击归属转移；仍未发布。

## H锤追加回调（用户确认执行）

- `cpp/shared/game/config.cpp`：H mjolnir雷击基础48→60；在原有H耐久缩放/封顶后再提高50%（对应50→75基础耐久的增幅），没有直接绕过耐久缩放套原始H倍率。重装6000ms、无连锁、E限时锤均保持。
- `hyper_hammer_buff_preserves_reload_and_temporary_eternal_hammer`原生1项通过，核对H雷击、耐久、重装、连锁及E限时锤12/8000ms。
- 使用现有未跟踪DPS工具只读编译到/root/hammer90-bench，与此前/root/hammer-bench做相同30秒无反击假人对比：距离75攻击总伤42042888→52553610（89击）；防御97785144→122231430（207击）；距离150攻击34957008→43696260（74击）。有效输出均增加25%，频率未增加。无反击测试不能体现耐久提高的实战收益，不能据此承诺玩家实测一定10～12m。
- 尚未提交/部署，dist未更新；工具原文件不改、不纳入本任务提交。


## 后续追加：协议91（未部署）

- 本局收集原先只记当前操控身体且每堆只加1。PickedUp事件增加quantity/account归属尾字段，服务端发出账号远处拾取事件，客户端累计真实数量；协议91。splitter_parked两项通过，含离本体10000单位的分身拾取7个物品、客户端收到quantity=7与归属标记。
- 重构机：Npc.serviceRadius按正常美术半径计算，客户端nearbyNpc和服务端findService一致；Body仍封顶60。assembler_size全档两种机型边界内/外回归通过。
- 胡萝卜：data/petals.json只改speed300→450，distance500/reload1000不变；carrot_projectile_speed回归通过。首次测试使用未更新暂存data报300，完成flix_data后通过，未把旧数据测试算成通过。
- S刷怪范围：共享difficulty.h固定名额8→12、候选窗口10→15，10张常规地图及小地图共享规则，保留原U自然刷怪、5个游荡S和E/H机制。super_farm_has原生通过；这不是新增纯S地图或修改普通U掉率。
- 花粉用户要求避免重削，最终采用驻留4秒/间隔600ms，替换此前未上线的3秒/750ms。伤害/数量/冷却/玩家库存不动；defending_drops_pollen通过。
- 粉末：当前神秘粉末无周边怪失效逻辑；删除遗留powder中文失效描述，未改加成。
- 机械导弹：原始嵌入PNG头朝右下(747x1124)，Projectile绘制扣除atan2(1124,747)，不改槽图或弹道。Chrome实际WorldRenderer四向截图验证弹头朝右/下/左/上；测试夹具新增mode3。
- H掉落倒挂核实：E总预算0.35/尸体均分全部可E物品，S来自原始表。已建议按S权重分E、保留预算，暂未改规则。
- 骰子新反馈：新增dice_real完整登录、发放测试装备、SetLoadout、2400服务端tick、客户端收包测试，已确认高倍伤害可达客户端；正在追加Critical标识及独立金色显示。并未改变暴击概率或把动画点数绑定暴击，玩家所述所有同伤场景未复现。


### 骰子后续验证完成（本地）

- 玩家补充“全部都是伤害数字始终一样”。保留原5%/35倍，不擅自调概率；服务端暴击写现有DamageCritical位，客户端独立金色/放大显示，不进入普通数字合并桶。没有新消息布局，沿用本轮协议91。
- dice_real_equipment_server_ticks_deliver_critical_damage通过：完整登录、装备E骰子、2400服务器tick，实际累计伤害出现高倍命中，客户端收到高倍数值及Critical位。player_dice全档/怪与假人/伤害倍率定向测试通过（1项内10240次碰撞）。
- 两端WASM库与浏览器夹具链接通过，Chrome实际显示普通100与金色3500；截图D:/ashflor/temp/feedback90-ui/dice-critical.png。mode4作为可复查样例保留；mode3机械导弹四向画面再次确认。
- 当前能证明本地完整链路有暴击并补足识别显示，尚不能证明线上玩家全部同伤原因。没有进行本轮线上检查或部署，未改库存。


## 地狱蜈蚣身体无掉落（无分裂）

- 根因：data/mob_drops.json有hel_centipede但缺hel_centipede_body；其余四种蜈蚣身体与头部掉落表均一致。蜈蚣身体独立血池、独立Bounty，击杀身体查到空表，随后被标记LootAwarded。不是灵魂分裂问题。
- 只新增hel_centipede_body表，内容逐字段等于现有hel_centipede；JSON结构比较确认原所有条目完全未变。未给共享血池的leech_body添加额外掉落，避免重复结算。
- 新增centipede_body_kills_award_loot_without_soul_splitter：真实SpawnSystem生成完整链，直接击杀身体并检查头还活着、身体贡献账本存在、LootSystem生成玩家可拾取掉落；五种蜈蚣×S/E/H共15场景。修前仅Hel三档quantity断言失败，修后全部通过。
- 另跑reported_hyper_screen_and_sandstorm_keep_split_account_loot通过，两种各300次的对比分裂/未分裂总量保持一致。
- 测试使用源码目录掉落表，未以旧暂存数据冒充新数据。尚未重建dist/提交/部署；数据内容哈希会变，上线必须成对重建与部署。


## 收尾：H掉落S/E倒挂修复

- shared/game/drop_rates.h统一计算：显式概率表按S权重分配额外E，每项E单次概率最多同种S的一半；整尸E预算仍以上限0.35为限，S总权重不足时实际E预算减少。没有提高S概率。无显式概率表的旧条目保留原均分配额，防止无依据重算旧表。
- 服务端systems/loot.cpp按共享概率采样；图鉴menu_gallery.cpp使用同一函数，并保留sourceRates用于魔法球转换前的来源权重，避免图鉴与实际分配不同。
- hyper_eternal_allocations回归通过：全注册怪物显式表检查E≤S/2、总预算≤0.35/尸体；4000次机械花击杀的电池/cogwheel/fragment/mimic均S数量高于E。hyper_drop_budget两项通过，包括Bonus1.5/2倍率；旧固定0.35断言已改为新封顶规则的计算期望。
- centipede_body_kills再次通过。剩余回归及完整Web构建进行中，尚未提交/推送/部署。
- 工作区另有协议92/电网/60TPS/并行快照任务修改与独立release92构建，保留它们；本任务不操作其构建目录或部署。临时原生测试构建补入新parallel_snapshots.cpp以兼容当前工作区，没有把原链接缺文件报错算成通过。


### 2026-11-05收尾验证

- 当前工作区60TPS/并行快照条件下，splitter_parked两项、dice_real、defending_drops_pollen、ankh_break、assemblers_keep、carrot_projectile_speed、super_farm_has均通过。原生只运行列出的定向用例，不代表全仓测试全绿。
- Chrome夹具通过图鉴S/E上限断言，机械花实际读到battery S0.279%/E0.100370%、cogwheel及fragment S1.476%/E0.530990%、mimic S0.012%/E0.004317%；瓢虫和Hel身体也核对通过。黑名单增删清空及刷新持久化复测通过，机械导弹四向和金色暴击截图重新生成。
- 完整flowrix_client/flowrix_server构建使用本仓cpp/build-web（与另一个任务D:/ashflor/temp/release92-20261104构建目录分开）；客户端链接已完成，服务端仍在构建。不会据此声称另一个发布快照包含本次最新掉落修复。


### 构建与隔离启动收尾（2026-11-05）

- 完整flowrix_client与flowrix_server构建成功；保留构建日志/root/feedback92-complete.log。仅编译产物，没有暂存或提交dist。三份JSON暂存文件与源码逐字节一致。
- Windows Node运行新cpp/build-web/server.js，临时数据库D:/ashflor/temp/feedback92-local-smoke/inventory.json、端口13091；实际监听、游客注册、账号注册/登录、错误密码拒绝全部通过。smoke-auth显示协议92/内容哈希2859263876。测试进程已退出，未触碰正式数据库。
- 首次脚本错误等待裸服务端不存在的/health路径而超时，未计为通过；改为实际TCP监听与完整鉴权握手验证。/api/health属于部署包装层，本次未把它冒充线上健康检查。
- 以上本轮明确实现项收尾；骰子玩家所有同伤场景仍未在线上复现，本地完整链路有暴击且已独立金色显示，不能把这一限制隐去。旧未知账号存档缺身份材料，不宣称恢复。
- 同工作区其他任务的电网/60TPS/并行快照和发布快照状态由其记录负责；本次没有提交、推送或部署，也未操作其release目录。
