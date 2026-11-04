# 2026-11-04 追加反馈上线及玩家发放

用户授权部署已验收候选，并向 king 发放2个永恒甲虫蛋、5个超级黄金叶子；同时执行此前 Hzl 闪电/扁豆解除请求。

- 北京时间18:38完成，发布目录 `/opt/florrclone/releases/cpp-20261104-followup88`；游戏源码和dist来自已推送提交a34f0a8，解除清单来自7fc5aea。协议88，内容哈希139922817。
- 使用 scripts/deploy-with-backup.cjs：维护小游戏先开启，正常停服后下载最新存档，本地与远端SHA256匹配后才处理候选存档与切换版本。
- 本地备份 `D:/ashflor/backups/florrclone/20261104T103642Z/inventory.json`，2310账号，SHA256 `9a85270ee020b2f5d18f3b0484a751c9359fd5c2851edf2ec15f58d37c3f5f97`。本次没有回档。
- king账号24gg6n0t221h：unique/petal_beetle_egg从0增至2，super/petal_golden_leaf从0增至5。新增幂等发放脚本，数量增加、无关进度保留、重复执行和身份冲突测试通过。
- Hzl账号oc4h1r4344aj：lightning、lentil各移除1个H、解除1份持有权、返还4个同种E。旧解除任务均alreadyApplied，没有重复退款。
- 后端健康及资源检查通过；8788及8088握手通过；额外复核8088返回protocol88/contentHash139922817/accepted=true。外部HTTP健康ok，systemd active，维护标记已撤。
- 已重读线上持久化存档，确认king数量和发放回执、Hzl解除回执及目标持有权释放。
- 本地与上传的客户端/服务端产物SHA256一致；沿用上一轮Chrome18项、原生针对性24项以及完整构建验收，本次没有宣称重新运行全仓测试。
- 本次部署脚本发放接入、发放脚本及测试、release-notes和状态记录尚未提交；其他任务修改保留。历史账号旧进度未恢复问题仍有证据限制，不视为解决。