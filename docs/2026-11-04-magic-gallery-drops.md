# 魔法球条件掉落与图鉴修复（2026-11-04）

## 已修改

- 图鉴原先只读取普通rarityChances，忽略magicItemType和magicRarityChances，也不读取当前装备。现按最高有效主动槽魔法球切换掉落物、条件概率与稀有度上限；备用槽不生效，拟态按解析后的物品/稀有度生效。转换后落在同一稀有度的概率合并，装备变化时立即选择对应预览，不需重开面板。
- 覆盖掉落表全部显式魔法转换以及现有magicForm映射。没有凭空给不存在的魔法变体新增物品。
- 丛林瓢虫原本缺少魔法球配置，在dahlia行补magicItemType=mana_orb；普通/沙漠瓢虫已有rose→mana_orb，因此三种瓢虫均支持带球转换。不带球仍掉原物品，不显示条件魔法产物。
- H怪额外E掉落池原先只查自动magicForm，漏掉显式magicItemType，导致带球仍可能出现普通源花瓣。现在该路径同样读显式映射，保留原总预算与球稀有度上限。图鉴额外池按原物品去重计权，转换后合并显示，不重复增加预算。

## 验证

- 原生FLIX_TEST_FILTER=orb：4项0失败，包含金色叶虫条件概率、蜈蚣身体转换、三种瓢虫戴球/不戴球实际掉落及H额外池不混入普通源物品。新增三瓢虫测试为每种带球和不带球各80次H击杀。
- Chrome Canvas21项0失败；新增预览测试覆盖全部配置转换、三种瓢虫、低阶球上限、金色叶虫特殊条件概率求和、备用槽和拟态。
- Chrome实际图鉴悬浮截图：同一只H瓢虫，装备E魔法球显示法力宝珠，卸下即时恢复玫瑰；普通光保持原样。源码在cpp/tests/browser/magic_gallery；临时截图位于D:/ashflor/temp/magic-gallery-ui/with-orb.png与without-orb.png。
- 客户端/服务端WASM静态库重编成功，git diff --check通过。未重建正式dist，未提交或部署。data/mob_drops.json发生变化，下次上线必须完整重建配套两端。

## 文件与边界

cpp/client/ui/menu_gallery.cpp、gallery_drop_preview.h；cpp/server/systems/loot.cpp；data/mob_drops.json；cpp/tests/gallery_magic_drop_tests.cpp、mob_drop_reference_tests.cpp及浏览器验收源。
保留此前H榜/沙暴、骰子/重瓣/灰尘和其他任务未提交修改。当前源码协议89（来自H榜）；线上仍为已部署协议88版本。