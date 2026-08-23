# BG24 手机钥匙项目交接

更新时间：2026-08-20

## 1. 明天从这里开始

工程目录：

`D:\myProject\20260817\bt_cs_soc_initiator_slc_failed_backup`

当前工作分支：

`feature/cr008-passive-hid-hardening`

下一项工作：

`CR009-004 编译与实机验证 → CR009-001、CR009-005 实机补测 → CR008-013 编译与实机验证及 CR008-006、CR008-007、CR008-009 剩余补测`

当前进度：CR008-001～005 已完成；CR008-006 已完成取消、PREPARE 超时和错误 PIN 等负向实测，授权记录提交、清除和重启恢复仍需补测；CR008-007 已有系统蓝牙直接配对拒绝、观察窗口超时和 APP 场景实机日志，完整回归矩阵仍需补齐；CR008-008、010 已实机验证；CR008-009 编译与正向测试通过、负向项待补测；CR008-013 代码与文档已提交推送，等待编译和实机验证。CR008-011、012 暂停，不处理。CR009 已进入实施阶段：001 为 BUILT（实机待测），004 为 CODED（编译待确认），005 为 BUILT（实机待测），006 为 TESTED；002、003、008、009 已取消，007 仍为候选。

跨 CR 实时进度统一见 `docs/CR_STATUS.md`；开发中途发现的问题先记录在 `docs/ISSUE_LOG.md`。本文件保留阶段交接快照，不作为逐项实时状态表。

开始前先只读检查本文件、`docs/CR_STATUS.md`、当前 CR 的 PLAN/CHANGE_RECORD 和 Git 状态，不要直接修改，不要编译。CR008-011、012 暂停；CR009 中仅已记录为实施状态的子项可继续验证，候选子项仍须先完成方案确认。

## 2. 协作约定

- 一个 CR 一个 CR 地讨论、修改和验证。
- 修改前说明：原有逻辑、修改原因、不修改的风险、修改位置、协议影响和验证方法。
- Codex 不编译，由用户在 Simplicity Studio 编译和实机验证。
- 用户说“下一个 CR”时，默认上一项已经编译通过；是否实机通过仍以用户明确反馈为准。
- 使用 `apply_patch` 修改文件，保留已有未提交内容，不做无关清理和大范围重构。
- 未经用户明确要求，不执行 Git add、commit、reset、checkout、push 或 SVN 操作。

## 3. 产品与架构边界

- 当前只有手机钥匙，暂时没有实体钥匙。
- `APP_KEY_ENABLE=0`，`key_connect/`、`cs_key_rang/` 属于历史或预留代码，本阶段不启用、不删除。
- HID 仅用于 Android 后台连接承载，不是业务身份认证本身。
- APP-BG24 自定义业务协议、命令字、TLV 和加密格式保持不变。
- `PASSIVE_ENABLE / PASSIVE_DISABLE / PASSIVE_PAIR_PREPARE / PASSIVE_PAIR_READY / PASSIVE_PAIR_CANCEL` 协议保持不变。
- GATT Handle、Appearance 和 HID Report Map 已冻结：
  - GAP Appearance：`0x08C1`，Car；
  - HID Report Map：23 字节 Vendor-defined；
  - 不包含 Mouse/Keyboard Usage；
  - 当前业务代码不发送 HID Report。
- 手机地址为稳定 Public Identity Address，APP 应优先使用缓存地址 GATT 直连，扫描仅作为兜底。

## 4. Git 与备份状态

已提交记录：

- `78fbe96 baseline: verified build before CR-008 passive HID hardening`
- `8e2eb43 docs: establish CR-008 passive HID hardening plan`
- `da29044 feat(cr008): passive HID hardening CR008-006~010`
- `eb73355 docs(cr008): add project docs, change records and git guide`
- `033599e feat(cr008): CR008-013 0x65 灵敏度设置成功响应回显最新档位`
- `ca4d3e0 feat(cr009): CR009-006 自动解锁额度解耦断连闭锁`
- `4a18c0a feat(cr009): CR009-001 车辆条件门控（静默/门/尾门）`
- `2d071db feat(cr009): CR009-005 断连闭锁兜底（断开前走远证据）`
- `ab9734b feat(cr009): CR009-004 连接状态走远闭锁（门资格）`
- `9233acc docs(cr009): 补 CR009-004/005 独立设计文档`
- `cd3b60e docs(cr009): 新增 CR009 使用说明（md/Word/生成脚本），恢复量产调试宏`

当前功能分支：`feature/cr008-passive-hid-hardening`。交接更新时 HEAD 为 `cd3b60e`，与 `origin/feature/cr008-passive-hid-hardening` 同步，工作区 clean。

远程仓库：`https://github.com/LoH9721/bt_cs_soc_initiator.git`。

远程分支：`origin/feature/cr008-passive-hid-hardening`，已建立 upstream 并完成同步。交接文档更新前工作区为 clean；恢复工作时仍应以 `git status --short --branch` 和 `git log --oneline -5` 的实时结果为准。

本机 Git 全局配置中存在 `https.proxy=https://127.0.0.1:7897`；代理未启动时普通 `git push` 会失败。已验证可用的临时绕过命令为：

```bash
git -c https.proxy= -c http.proxy= -c http.version=HTTP/1.1 push
```

## 5. 当前调试构建配置

`config/app_config.h` 当前为：

```c
#define APP_KEY_ENABLE          0
#define APP_NO_CAN_PHONE_DEBUG  0
#define PHONE_PEPS_LEAVE_LOCK_ENABLE 1
```

`APP_NO_CAN_PHONE_DEBUG=0` 已恢复量产取向；当前固件不再允许用固定 VIN 绕过 CAN、IGN 和实时 VIN。需要进行无 CAN 调试时，必须经单独确认后临时开启，并在结束前恢复为 `0`。`PHONE_PEPS_LEAVE_LOCK_ENABLE=1` 启用 CR009-004 的连接状态走远闭锁；设置为 `0` 时只保留 CR009-005 的断连闭锁兜底。

## 6. CR008 当前状态

| CR | 内容 | 状态 | 证据 |
|---|---|---|---|
| CR008-001 | Pairing 超时/成功/失败/取消统一清理 | BUILT | 用户确认编译通过，完整超时/取消实机矩阵尚未全部记录 |
| CR008-002 | READY 检查 SM API 返回值和失败回滚 | BUILT | 用户确认编译通过，正常 READY 日志通过 |
| CR008-003 | 新连接清理上一连接测距状态 | BUILT | 用户确认编译通过；未单独记录完整距离边界实测 |
| CR008-004 | 当前连接 Bond、加密和安全状态跟踪 | TESTED | 后台重连由有效 Bond/L1 升至 L4，断开清零，频繁重连通过 |
| CR008-005 | Android 忽略配对后的旧 Bond 安全替换 | TESTED | 用户确认编译和实机验证通过，可重新系统 Pairing，不再被旧 Bond 阻塞 |
| CR008-006 | 授权手机 Bond 身份关联持久化 | CODED | 代码完成，等待用户编译和实机验证 |
| CR008-007 | UNBIND 后未授权连接准入与 APP 重绑定观察窗口 | CODED | 代码完成；APP连接+后台蓝牙配对场景有实机通过记录，正式标记待确认 |
| CR008-008 | 启动恢复校验 Passive 与授权 Bond | TESTED | 用户确认编译和实机测试完成 |
| CR008-009 | PEPS 使用授权加密链路和新鲜测距 | BUILT | 编译和正向实机通过；未授权连接、1500ms 超时等负向项待补测 |
| CR008-010 | 自动落锁只监听授权 Passive 链路断开 | TESTED | 用户确认编译和实机测试完成；授权断开5s闭锁、5s内重连取消、未授权不闭锁通过 |
| CR008-011 | Release 禁止固定调试 PIN 和 PIN 日志 | TODO | 量产整理 |
| CR008-012 | 重复状态和 32 位定时器回绕整理 | TODO | 低优先级 |

详细修改原因和验证方法见：`docs/CR008_PASSIVE_HID_HARDENING_PLAN.md`；每个 CR 的代码改动点（文件+函数）见：`docs/CR008_CHANGE_RECORD.md`。

## 7. CR008-005 最终实现

已复现的旧问题：

```text
Android 删除系统 Bond
→ BG24 仍保存 bond=0x00 和旧 LTK
→ APP 的 PREPARE/READY 均返回成功
→ Android createBond
→ BG24 报 SM_BONDING_FAILED reason=0x1205
```

当前修复时序：

```text
业务认证
→ PREPARE
→ READY 成功响应
→ APP 收到响应
→ BG24 主动断开 APP
→ connection_closed 中删除旧 BLE Bond
→ 恢复系统 Pairing 广播
→ Android 建立新 Bond
```

删除旧 Bond 仅在以下条件同时成立时执行：

```c
g_sess.pairing_window_active
&& g_sess.pairing_awaiting_system
```

关键日志：

```text
[SM] CR008-005 stale bonds delete before system pairing sc=0x0000
```

当前只有一把手机钥匙，因此授权重配流程中使用 `sl_bt_sm_delete_bondings()` 清除全部 BLE Bond 符合当前产品边界。未来增加第二把手机钥匙或实体钥匙时，必须改成按授权身份精确删除。

## 8. APP 后台连接结论

分析文档：`docs/APP_BACKGROUND_GATT_CONNECTION_ANALYSIS.md`

根因不是连接数量不足：Android 系统 HID 已建立物理连接后，BG24 停止可连接广播；APP 若没有保存车辆地址且只扫描，就会扫描超时。

APP 推荐策略：

1. 持久化 `vehicleId/deviceId -> BG24 Public Identity Address`；
2. 有缓存地址时直接 `connectGatt`；
3. 无 APP 缓存但系统 Bond 存在时，从 Bond 列表恢复地址后直连；
4. 只有没有地址或直连失败时才扫描；
5. 不建议为了 APP 扫描而让当前单手机状态机接受第二个物理手机连接。

日志已证明：APP 获得稳定地址后可以在没有新 `connection_opened` 的情况下重新订阅 CCCD、完成业务认证，即复用 Android 已持有的物理 BLE 连接。

## 9. CR008-006～010 已实施要点

这些 CR 已按 `docs/CR008_CHANGE_RECORD.md` 实施，并在提交 `da29044` 中提交到功能分支；相关文档在提交 `eb73355` 中补齐，均已推送到远程。核心链路：

```text
CR008-006 授权 Bond 记录持久化（NVM 0x5520，28B+CRC16）
→ CR008-007 未授权连接准入观察窗口 + Bond 确认分流
→ CR008-008 启动时校验 Passive 配置与授权 Bond
→ CR008-009 PEPS 运行期授权链路门控（Passive ON + 授权 Bond + L4）+ 1500ms 测距新鲜度
→ CR008-010 自动落锁只监听授权 Passive 链路断开（5s 持续断开）
```

已确认的关键事实：

- 授权记录格式：`formatVersion(1) + identityAddrType(1) + identityAddress(6) + appKeyId(16) + bindVersion(4, BE)`，地址 `0x5520`；
- 运行期授权 Bond handle 不持久化，只用于把当前链路映射回持久化授权身份；
- `hid_service_is_connected()` 已退出 PEPS 区域判断和自动落锁，仅保留状态查询；
- 自动落锁不再监听任意物理连接下降沿，只接受 SM 在断开前确认的授权 Passive L4 快照，PREPARE/READY 主动切换不触发。

待补测项：

- CR008-006：正常 Pairing 提交日志、错误 PIN/超时/取消不产生记录、换绑/解绑/工厂复位清除记录、Android HID 后台重连回归；
- CR008-007：完整拒绝/超时/合法重绑定矩阵（部分场景已有实机通过记录）；
- CR008-009：未授权连接不得输出有效区域或自动解锁；1500ms `STALE` 超时；新连接首个 RSSI 前不得复用旧距离。

## 10. CR009 当前实施状态

- CR009-001（车辆条件门控）：已编译，待实机验证静默、门/尾门开着抑制，以及全关后的断连闭锁回归。
- CR009-004（连接状态走远闭锁）：代码已提交，尚无用户确认的编译或实机证据。仅在授权链路、解锁状态、非静默、门/尾门全关、且已取得“任一门开→所有门关”资格后，进入约 15m 无效区才请求闭锁；闭锁一次后消耗资格。
- CR009-005（断连闭锁兜底）：已编译，待实机验证。断开前必须已有远区证据；车旁闪断不应闭锁，远区断连持续 5s 应闭锁，5s 内重连应取消。
- CR009-006（自动解锁额度语义）：已编译并完成用户确认的实机测试。自动解锁额度仅限制自动解锁，不再阻断断连闭锁。
- CR009-002、003、008、009 已取消；CR009-007 仍是候选，不得直接修改代码。

实现与测试细节分别见 `docs/CR009_CHANGE_RECORD.md`、`docs/CR009_PHONE_PEPS_AUTO_LOCK_PLAN.md` 和 `docs/CR009_USAGE_GUIDE.md`；逐项实时状态以 `docs/CR_STATUS.md` 为准。

## 11. 已知风险与不要做的事情

- 不要把“Bond 表非空”当作当前连接已授权。
- 不要把 HID 连接本身当作业务身份认证。
- 不要在任意 `0x1205` 事件中自动删除 Bond；未经业务认证的设备可能借此造成拒绝服务。
- 不要在 PREPARE 阶段删除旧 Bond；用户取消或 APP 异常会破坏原本可用的自动重连。
- 不要直接修改生成的 GATT 数据库；当前 Appearance/Report Map 已确认正确。
- 不要为方便调试而将 `APP_NO_CAN_PHONE_DEBUG=1` 的固件用于量产或真实车辆验收；当前值必须保持为 `0`。
- CR008-001～010 已提交并推送到功能分支；恢复工作时先确认当前分支和工作区，不要 reset、checkout 或覆盖用户修改。
- CR008-009 的 1500ms 测距新鲜度是运行期判据，不改变 RSSI 算法和 300ms 轮询；首次区域直接为解锁区时不自动解锁，属既有策略。
- CR008-010 自动落锁只接受授权 Passive L4 断开事件；未授权连接、L1 断开、Passive OFF 断开和 PREPARE/READY 主动切换都不触发闭锁。
- CR009-004/005 的实机验证尚未完成，特别是门资格、折回、门/尾门打开、车旁闪断、远区断连与重连取消组合场景不得凭代码或编译结果标记为通过。

## 12. 新窗口恢复提示词

可在明天的新窗口直接发送：

```text
请读取并以此为唯一项目交接基线：
D:\myProject\20260817\bt_cs_soc_initiator_slc_failed_backup\PROJECT_HANDOFF.md

同时只读检查：
D:\myProject\20260817\bt_cs_soc_initiator_slc_failed_backup\docs\CR008_PASSIVE_HID_HARDENING_PLAN.md
D:\myProject\20260817\bt_cs_soc_initiator_slc_failed_backup\docs\CR008_CHANGE_RECORD.md
D:\myProject\20260817\bt_cs_soc_initiator_slc_failed_backup\docs\CR009_PHONE_PEPS_AUTO_LOCK_PLAN.md
D:\myProject\20260817\bt_cs_soc_initiator_slc_failed_backup\docs\CR009_CHANGE_RECORD.md
D:\myProject\20260817\bt_cs_soc_initiator_slc_failed_backup\docs\CR009_USAGE_GUIDE.md

当前分支为 feature/cr008-passive-hid-hardening。交接更新时 HEAD 为 cd3b60e，已与 origin/feature/cr008-passive-hid-hardening 同步。CR009-001 已 BUILT、004 已 CODED、005 已 BUILT、006 已 TESTED；不要将尚未有用户实机确认的 001/004/005 标为 TESTED。

当前优先项为：先编译并实机验证 CR009-004；再补测 CR009-001、005；随后完成 CR008-013 编译/实机验证及 CR008-006、007、009 的剩余矩阵。CR008-011、012 仍暂停。

先不要修改代码。先只读检查 Git 状态、CR009-004/005 的文件与函数位置、当前配置和测试矩阵；说明编译与实机验证顺序、前后对比方法、与 CR008-009/010 及 CR009-001/006 的回归关系。

我自己负责编译和实机测试；未经明确反馈不得虚构验证结果。一个 CR 一个 CR 来，正式修改后必须说明改动文件和位置、前后对比测试方法；未经明确要求不执行 Git 提交或推送，也不修改串口调试接口。
```
