# CR008 被动式 HID 加固（Passive HID Hardening）修改记录

- 基线提交：`78fbe96 baseline: verified build before CR-008 passive HID hardening`
- 工作分支：`feature/cr008-passive-hid-hardening`
- 状态规则：`CODED`=代码完成；`BUILT`=编译通过；`TESTED`=实机验证通过
- 记录日期：2026-08-18
- 说明：本记录汇总 CR008-001～010 的改动点；每个 CR 独立修改、独立验证，用户负责编译。

---

## CR008-001 Pairing 生命周期统一清理

- 状态：BUILT（用户确认编译通过）
- 改动点：
  - `phone_sm` 增加统一的 `pairing_context_destroy()`，超时、取消、Bond 成功/失败均走同一清理出口；
  - 完整恢复默认 SM 配置（SC-only + NoIO）、关闭 Bondable、清除窗口 deadline/passkey/身份快照；
  - 删除 `phone_session` 中提前清除窗口状态的部分逻辑，避免 SM 层无法再进入完整清理。
- 代码改动点：
  - `user_phone/data/phone_sm.c`：`pairing_context_destroy()` 统一恢复 SM 配置、`sl_bt_sm_set_bondable_mode(0)`、清 `pairing_window_active/awaiting_system/deadline`、恢复 HID runtime、清身份快照；
  - `user_phone/data/phone_sm.c` 各超时/取消/Bonded/BondingFailed 分支统一改为调用 `pairing_context_destroy()`；
  - `user_phone/data/phone_session.c` 删除 session 层提前清窗口状态的逻辑，避免跳过 SM 层完整清理。
- 涉及文件：`user_phone/data/phone_sm.c`、`user_phone/data/phone_session.c/.h`
- 影响：不改变 APP 协议、GATT、HID、Appearance、Pairing 窗口时长和 Bond 数据格式。
- 验证：PREPARE 30s 超时、READY 60s 超时、取消、正确/错误 PIN；超时后可重新 PREPARE，HID 恢复原 Passive 状态。

## CR008-002 PASSIVE_PAIR_READY 失败回滚

- 状态：BUILT（用户确认编译通过）
- 改动点：
  - `handle_passive_pair_ready()` 逐项检查 Bond 配置、SM 安全模式、Passkey、Bondable、响应构造/发送及断开请求的返回值；
  - 任一步失败调用统一清理，SM 配置全部成功后才回复 APP 成功。
- 代码改动点：
  - `user_phone/data/phone_sm.c`：`handle_passive_pair_ready()` 逐步检查 `sl_bt_sm_configure/set_bondable_mode/set_passkey/bonding_confirm`、响应发送和 `sl_bt_connection_close` 返回值；失败时 `pairing_context_destroy()` 并提前返回。
- 涉及文件：`user_phone/data/phone_sm.c`
- 影响：不改变 APP 协议、TLV、60s 窗口、GATT、HID、Appearance 和 Bond 数据格式。
- 验证：正常 READY 返回 `sc=0x0000` 并进入系统 Pairing；异常返回时清理窗口且不进入系统 Pairing。

## CR008-003 当前连接测距生命周期

- 状态：BUILT（用户确认编译通过）
- 改动点：
  - 手机新连接建立（`sl_bt_evt_connection_opened_id`）时调用 `phone_rang_reset()`，清空 RSSI/卡尔曼/融合距离；
  - 未连接时距离接口返回无效（`phone_rang_is_valid()` 依赖连接状态）。
- 代码改动点：
  - `user_phone/phone_comm.c`：`phone_comm_on_bt_event()` 的 `sl_bt_evt_connection_opened_id` 分支调用 `phone_rang_reset()`；
  - `user_phone/data/phone_rang.c`：`phone_rang_is_valid()` 增加 `phone_comm_is_connected()` 前置条件。
- 涉及文件：`user_phone/phone_comm.c`、`user_phone/data/phone_rang.c/.h`
- 影响：不改变 RSSI 距离算法、300ms 轮询周期、PEPS 阈值、APP 协议、HID 和车辆接口。
- 验证：断连后距离无效；频繁重连时本连接首个 RSSI 前不得复用上一连接距离。

## CR008-004 当前连接 Bond 与链路安全状态

- 状态：TESTED（用户完成编译和 Android 实机验证）
- 改动点：
  - `phone_link` 跟踪当前连接的 Bond handle 与 Security Mode，在 OPEN、BONDED、安全等级变化、CLOSED 时输出统一 `[PHONE] LINK_SECURITY ...` 日志；
  - `phone_comm` 提供只读查询：`phone_comm_current_link_is_bonded()/is_encrypted()/bonding_handle_get()/security_mode_get()`；
  - 明确“已 Bond 不等于已被 APP 授权”。
- 代码改动点：
  - `user_phone/link/phone_link.c`：`phone_link_on_bt_event()` 中维护 `phone_link_security_state`（bonding_handle + security_mode）；OPEN/BONDED/SECURITY_CHANGED/CLOSED 时调用 `phone_link_security_log()`；
  - `user_phone/link/phone_link.c`：新增 `phone_link_current_is_bonded()/is_encrypted()/current_bonding_handle_get()/current_security_mode_get()`；
  - `user_phone/phone_comm.c`：增加对应只读转发接口。
- 涉及文件：`user_phone/link/phone_link.c/.h`、`user_phone/phone_comm.c/.h`
- 影响：仅增加 RAM 状态、日志和查询接口；不改变 Pairing/Bonding/PASSIVE/HID/PEPS/APP 协议/NVM。
- 验证：未绑定连接 `bond=0xFF/L1`；Pairing 后有效 Bond/L4；后台重连从已 Bond/L1 升至 L4；断开清零。

## CR008-005 Android 忽略配对后的旧 Bond 安全替换

- 状态：TESTED（用户确认编译和实机验证通过）
- 改动点：
  - 仅当 `pairing_window_active && pairing_awaiting_system` 时，在 READY 成功响应后的 APP 断连事件中调用 `sl_bt_sm_delete_bondings()` 清除旧 BLE Bond；
  - 删除发生在广播恢复前；删除失败销毁 Pairing 上下文并恢复 Non-Bondable。
- 代码改动点：
  - `user_phone/data/phone_sm.c`：`phone_sm_on_connection_closed()` 在 `pairing_window_active && pairing_awaiting_system` 时先 `sl_bt_sm_delete_bondings()`，成功后清除授权记录并 `authorized_bond_force_passive_off()`，失败则 `pairing_context_destroy()`。
- 涉及文件：`user_phone/data/phone_sm.c`
- 影响：不改变 APP 协议、PREPARE/READY 响应、Pairing 窗口、HID、GATT、Appearance、PEPS 和业务绑定数据；当前单手机产品清除全部 Bond 符合边界。
- 验证：Android 忽略设备后重新开启无感，不再阻塞于旧 Bond（`0x1205` 消失），重启后 HID 自动重连正常。

## CR008-006 APP 授权手机 Bond 身份关联

- 状态：CODED（代码和只读差异审查完成，尚未完整实机验收）
- 改动点：
  - 新增 NVM 条目 `0x5520 PHONE_AUTHORIZED_BOND`（28 字节数据 + CRC16）：格式版本、Identity Address Type、Identity Address、`appKeyId`、大端 `bindVersion`；
  - `phone_storage` 增加授权 Bond 的读、同步写、清除、存在性接口；
  - PREPARE 保存已认证 APP 身份快照；READY 和最终提交前再次校验业务绑定未变化；
  - `SM_BONDED` 只接受当前授权窗口内的有效 Bond/L4；主循环查询 bonding details，要求 L4、16 字节密钥并同步落盘；
  - 写入或属性校验失败时清除记录并删除新 Bond；删除旧 Bond、换绑、解绑、工厂复位均清除授权记录。
- 代码改动点：
  - `user_eeprom/user_eeprom_items.def`：`EEPROM_ITEM_X(PHONE_AUTHORIZED_BOND, 0x5520, 28, ...)`；
  - `user_phone/data/phone_storage.h`：新增 `phone_authorized_bond_t` 及 `get/set_authorized_bond_sync/clear/has_authorized_bond`；
  - `user_phone/data/phone_storage.c`：实现固定 28 字节序列化（版本、地址类型、6 字节地址、16 字节 `appKeyId`、4 字节大端 `bindVersion`），写操作走 `user_eeprom_write_sync`；
  - `user_phone/data/phone_sm.c`：`authorized_bond_commit_process()` 主循环延迟执行，`sl_bt_sm_get_bonding_details` 校验 L4/16 字节密钥后 `phone_storage_set_authorized_bond_sync` 落盘，失败走回滚删除 Bond；
  - `user_phone/data/phone_sm.c`：提交成功后缓存授权 Bond handle（`g_authorized_bonding_handle`）；
  - `user_phone/phone_comm.c`：转发 `sl_bt_evt_sm_bonded_id` 到 `phone_sm_on_sm_bonded()`。
- 涉及文件：`user_eeprom/user_eeprom_items.def`、`user_phone/data/phone_storage.c/.h`、`user_phone/data/phone_sm.c/.h`、`user_phone/phone_comm.c`
- 影响：不改变 APP 命令字、TLV、加密格式、GATT、HID Report Map、Appearance、PEPS 输出和自动落锁门控。
- 验证：正常 Pairing 后出现授权 Bond 提交日志和有效 NVM 条目；错误 PIN/超时/取消不产生记录；换绑/解绑/工厂复位清除记录；回归 Android HID 后台重连。

## CR008-007 UNBIND 后未授权连接准入与 APP 重绑定观察窗口

- 状态：CODED（代码和只读差异审查完成，尚未完整实机验收）
- 改动点：
  - 无 Bond 连接进入受限观察窗口：等待业务 Notify 3s，Notify 后等待合法 `BIND_HELLO`/`AUTH_CHALLENGE_REQ` 5s；
  - 手机/钥匙 SM 配置增加新 Bond 确认要求（`sl_bt_sm_configure` 增加 BONDING_REQUEST_REQUIRED）；
  - 手机收到 `SM_CONFIRM_BONDING` 时，仅在当前 connection、业务绑定 BOUND、APP 公钥存在、`appKeyId/bindVersion` 与 PREPARE/READY 快照一致、授权窗口未过期时接受；否则在 `SM_BONDED` 前拒绝并断开；
  - 钥匙事件只处理当前 Central connection 且只在显式钥匙配对流程接受；
  - 窗口外 `SM_BONDED` 删除仍作为异常兜底。
- 代码改动点：
  - `user_phone/data/phone_sm.c`：`phone_sm_on_connection_opened()` 无 Bond 时进入 `PHONE_ADMISSION_WAIT_NOTIFY`，Notify 后进入 `WAIT_APP`；
  - `user_phone/data/phone_sm.c`：`phone_sm_on_notify_enabled()` 推进观察窗口状态并设置 APP 证明超时；
  - `user_phone/data/phone_sm.c`：`phone_sm_on_sm_confirm_bonding()` 按 connection 分流，手机侧仅授权窗口接受，钥匙侧仅显式配对接受，拒绝时 `sl_bt_sm_bonding_confirm(0)`；
  - `user_phone/link/phone_link.c`：`phone_link_init()` SM 配置增加 `SL_BT_SM_CONFIGURATION_BONDING_REQUEST_REQUIRED`；
  - `user_phone/data/phone_sm.c`：新增 `g_connection_admission` 状态机（DISABLED/WAIT_NOTIFY/WAIT_APP/APP_ALLOWED/REJECTING）。
- 涉及文件：`user_phone/data/phone_sm.c/.h`、`user_phone/link/phone_link.c`
- 影响：不改变 APP 命令字、TLV、加密格式、NVM 格式、HID Report Map、Appearance、串口 `phone_unbind`、PEPS 输出和自动落锁门控；已有合法手机/钥匙 Bond 重连不受影响。
- 验证：UNBIND 后 Android 后台配对必须 `Bond request REJECT`；无 Notify 3s 超时；Notify 后无合法业务 5s 超时；APP 首次绑定/重新绑定正常；合法授权 Pairing 后 `Bond request ACCEPT` 并提交 L4 授权 Bond。

## CR008-008 启动恢复时校验 Passive 配置与授权 Bond

- 状态：TESTED（用户确认编译和实机测试完成）
- 改动点：
  - 删除 `app_init` 中仅凭 `passive_enabled` 直接恢复 HID 的旧逻辑；
  - `phone_comm` 在 `system_boot` 时先调用 `phone_sm_on_system_boot()`，再构造首个广播；
  - 新增统一校验 `authorized_bond_validate()`：APP BOUND 状态、公钥、`appKeyId/bindVersion`、授权记录、Identity Address、Bond L4、16 字节密钥；
  - 校验通过才恢复 HID；逻辑不一致关闭并持久化 Passive OFF、清除授权记录；启动阶段只删除按授权地址精确命中的 Bond；协议栈查询异常仅安全关闭不删除；
  - `PASSIVE_ENABLE` 复用同一校验，禁止“任意 Bond 存在”绕过；连接中失败保留旧 Bond 以先返回错误，交由 PREPARE/READY 修复。
- 代码改动点：
  - `app.c`：删除 `phone_storage_get_passive_enabled()` + `hid_service_set_runtime_enabled(passive_on)` 的旧恢复块；
  - `user_phone/phone_comm.c`：`phone_comm_on_bt_event()` 在 `sl_bt_evt_system_boot_id` 分支先调用 `phone_sm_on_system_boot()`，再走 `phone_link_on_bt_event()`；
  - `user_phone/data/phone_sm.c`：`authorized_bond_validate()` 读授权记录 → `sl_bt_sm_get_bonding_handles` → 按 Identity Address 匹配 → 校验 L4/16 字节 → 校验 APP 锚点与 `appKeyId/bindVersion`；
  - `user_phone/data/phone_sm.c`：`authorized_bond_disable_invalid()` 关 HID、`phone_storage_set_passive_enabled(false)`、逻辑不一致才清记录/删精确 Bond；
  - `user_phone/data/phone_sm.c`：`phone_sm_on_system_boot()` Passive OFF 打 `SKIP`，校验通过置 `g_authorized_bonding_handle` 并开 HID；
  - `user_phone/data/phone_sm.c`：`handle_passive_enable()` `authorized_bond_validate()` 通过后才启用，失败返回 `PAIRING_REQUIRED`/`INTERNAL_ERROR`。
- 涉及文件：`app.c`、`user_phone/phone_comm.c`、`user_phone/data/phone_sm.c/.h`
- 影响：合法授权配置仍恢复 HID/后台重连；Passive OFF 时保留授权记录/Bond，可免 PIN 重新启用；不改变协议、NVM 格式、GATT、HID、PEPS、自动落锁和串口接口。
- 验证：合法重启 `startup restore PASS` 并 L4 后台重连；Passive OFF 重启 `SKIP`；`hid_cfg 2` 制造 Bond 缺失后重启 `startup restore FAIL`、HID 保持 OFF。

## CR008-009 PEPS 使用授权加密链路和新鲜测距

- 状态：BUILT（用户确认编译和正向实机测试通过；负向/超时项待补测）
- 改动点：
  - 运行期缓存经 CR006 提交或 CR008 验证通过的授权 Bond handle（`g_authorized_bonding_handle`），并随解绑/换绑/回滚/启动校验失效；
  - `phone_comm_current_link_is_authorized_passive()` 统一组合：Passive ON + 当前 Bond == 授权 Bond + 当前安全等级 L4；
  - PEPS 区域判断与自动解锁只接受该授权门控；门控 OPEN/CLOSED 转换输出日志；OPEN 时 `phone_rang_reset()` 禁止复用 L1/L2 阶段样本；
  - `phone_rang` 记录最后一次成功融合测距时间，超过 `PHONE_RANG_FRESH_TIMEOUT_MS`（1500ms）返回无效；FRESH/STALE 转换输出日志；
  - 外部解绑使 APP 锚点变为 UNBOUND 时同步清除运行期授权和 Passive 状态。
- 代码改动点：
  - `user_phone/data/phone_sm.c`：`static uint8_t g_authorized_bonding_handle`，在 `phone_sm_init`、启动校验、`authorized_bond_force_passive_off`、`authorized_bond_disable_invalid`、换绑、外部解绑处复位；
  - `user_phone/data/phone_sm.c`：`phone_sm_is_authorized_bonding(bonding)` 比较运行期缓存 handle；
  - `user_phone/data/phone_sm.c`：`phone_sm_process_action()` SILENT 恢复后发现存储态 UNBOUND 时清授权/P passive/HID；
  - `user_phone/phone_comm.c`：`phone_comm_current_link_is_authorized_passive()` 组合 `passive_enabled && is_authorized_bonding && security_mode==L4`；
  - `user_phone/data/phone_rang.h`：`#define PHONE_RANG_FRESH_TIMEOUT_MS 1500U`；
  - `user_phone/data/phone_rang.c`：新增 `g_fused_last_ms`，`rang_kf_update()` 每次成功融合更新时间戳；
  - `user_phone/data/phone_rang.c`：`phone_rang_is_valid()` 要求 `connected && fused_valid && (now-last)<=1500ms`；
  - `user_app_fun/user_app_phone_peps.c`：`user_app_phone_peps_process()` 新增 `g_authorized_gate_last`/`g_range_fresh_last` 转换日志；授权门控 OPEN 时 `phone_rang_reset()` 并置 `dist_valid=false`。
- 涉及文件：`user_phone/data/phone_sm.c/.h`、`user_phone/phone_comm.c/.h`、`user_phone/data/phone_rang.c/.h`、`user_app_fun/user_app_phone_peps.c`
- 影响：合法后台重连需 L4 + 首个新 RSSI 后才恢复 PEPS；L1/无 Bond/非授权 Bond/Passive OFF 不得输出有效区域或自动解锁；不改变协议、阈值、300ms 轮询、车辆接口、串口接口。
- 验证：合法重连 `gate=OPEN` → `range=STALE` → `range=FRESH` → 区域；走远再走近自动解锁；APP 退出后后台无感继续；未授权连接不得有效区域。

## CR008-010 自动落锁只监听授权 Passive 链路断开

- 状态：TESTED（用户确认编译和实机测试通过）
- 改动点：
  - `phone_link` 在清空当前 Bond/L4 状态前，把断开快照（conn、bonding、security_mode）传给 `phone_sm_on_connection_closed()`；
  - `phone_sm` 生成一次性授权断连事件：断开前必须 Passive ON、APP 绑定锚点/授权记录有效、当前 Bond == 授权 Bond、链路 L4，且不是 PREPARE/READY 主动切换；
  - `phone_comm` 提供一次性消费接口 `phone_comm_consume_authorized_passive_disconnect()`；
  - PEPS 完全移除 `hid_service_is_connected()` 下降沿；只有消费到授权断连事件才启动 5s 计时；只有授权 Passive L4 链路恢复才能取消；
  - 未授权连接既不能启动也不能取消计时；Passive OFF 时清零计时；额度耗尽/已闭锁时取消计时。
- 代码改动点：
  - `user_phone/link/phone_link.c`：`phone_link_on_bt_event()` 的 `connection_closed` 分支在 `phone_link_security_reset()` 前把 `bonding_handle/security_mode` 传入 `phone_sm_on_connection_closed()`；
  - `user_phone/data/phone_sm.c`：`phone_sm_on_connection_closed(conn, bonding, security_mode)` 计算 `authorized_link && !pairing_switch` 后置 `g_authorized_passive_disconnect_pending=true`，输出 `disconnect snapshot ... arm=Y/N` 日志；
  - `user_phone/data/phone_sm.c`：`static bool g_authorized_passive_disconnect_pending`；`phone_sm_consume_authorized_passive_disconnect()` 一次性读取并清零；
  - `user_phone/phone_comm.c`：`phone_comm_consume_authorized_passive_disconnect()` 转发；
  - `user_app_fun/user_app_phone_peps.c`：`user_app_phone_peps_process()` 每周期消费一次事件；只有事件到达才启动 5s 计时并检查额度/锁状态；只有 `authorized_link` 为真才取消计时；
  - `user_app_fun/user_app_phone_peps.c`：删除 `hid_service_is_connected()` 相关状态与下降沿检测。
- 涉及文件：`user_phone/link/phone_link.c`、`user_phone/data/phone_sm.c/.h`、`user_phone/phone_comm.c/.h`、`user_app_fun/user_app_phone_peps.c`
- 影响：合法授权断开 5s 照常自动闭锁；5s 内恢复授权 L4 取消；未授权/L1/Passive OFF/主动 Pairing 断开不闭锁；不改变协议、NVM、测距、串口接口。
- 验证：授权断开 `disconnect snapshot ... arm=Y` → 5s 后 `AUTO LOCK`；5s 内恢复取消；未授权断开 `arm=N` 且不闭锁。

---

## 跨 CR 说明

- `hid_service_is_connected()` 已退出 PEPS 区域判断（CR009）和自动落锁（CR010），仅保留作为 HID 连接状态查询。
- 授权身份链条：CR006 持久化授权记录 → CR008 启动校验 → CR009 运行期门控 → CR010 断连落锁依据。
- 全程未修改：APP 业务协议/命令字/TLV/加密格式、GATT Handle、HID Report Map、Appearance、串口调试接口、NVM 既有条目格式。
- 全程未提交 Git；当前全部改动在工作区中，由用户统一编译和提交。
