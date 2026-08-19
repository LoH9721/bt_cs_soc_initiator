# CR009-005 断连闭锁兜底（断开前走远证据）设计

状态：`BUILT`（已提交 `2d071db`，编译通过、实机待测）

更新时间：2026-08-19

## 1. 目标

自动闭锁的最后兜底路径：授权 Passive L4 链路断开后，仅当"断开前最后确认区域为远区（OUTSIDE_LOCK / PARKING_INVALID）"时才允许闭锁，修复车旁蓝牙闪断导致的误锁。

## 2. 原有逻辑（引用文件/函数）

- 断连闭锁来源：`phone_sm_on_connection_closed()`（`user_phone/data/phone_sm.c:4275`）保存"授权 Passive L4 断开"快照，`phone_comm_consume_authorized_passive_disconnect()` 消费。
- 断连计时：`user_app_phone_peps_process()`（`user_app_fun/user_app_phone_peps.c`），授权断开 5s（`PHONE_PEPS_DISCONNECT_LOCK_SUSTAIN_MS`）后置 `g_auto_cmd_pending = LOCK`。
- 启动条件（CR008-010 + CR009-001）：授权断开 + 未计时 + `lock_state != LOCKED` + 非静默 + 门/尾门全关。
- 修改前无"断开前区域"要求：车旁蓝牙闪断 5s 也会闭锁。

## 3. 必要性

车旁瞬时断连（信号波动、路由切换）不表示人已离车，直接闭锁会造成误锁。需要"断开前已在远区"作为离车证据。

## 4. 不修改的风险

车旁蓝牙闪断 5s 仍会误闭锁；用户在近区时手机断电/异常断连也会锁车。

## 5. 修改位置（文件+函数）

仅改 `user_app_fun/user_app_phone_peps.c` 的 `user_app_phone_peps_process()`：

1. 非授权分支开头、复位区域状态前，取 `last_zone = g_current_zone`（断开前最后确认区域，必须在复位前取）。
2. 断连闭锁启动条件增加：
   ```c
   && (last_zone == APP_PROTO_ZONE_OUTSIDE_LOCK
       || last_zone == APP_PROTO_ZONE_PARKING_INVALID)
   ```
3. 启动被拒时日志改为 `CR009-005 disconnect lock skipped (need far zone before drop)`。

## 6. 影响点与冲突检查

- 与 CR008-010：收紧断连闭锁启动条件，需回归既有通过项（授权断开 5s 闭锁、重连取消、未授权不闭锁）。
- 与 CR009-001：静默/门控与之叠加；门开着时 004 与 005 都不锁（防夹）。
- 与 CR009-004：004 是连接状态主路径，005 是断连兜底；004 已闭锁后 005 因 `lock_state == LOCKED` 不重复。
- 与 CR009-006：005 不看额度，与额度语义无关。
- 冻结接口：不改命令字/TLV/加密/GATT/HID/Appearance/串口调试接口。

## 7. 编译方法

由用户在 Simplicity Studio 编译；仅改 `.c`。

## 8. 前后对比实测方法

- 车旁蓝牙闪断 5s → 不闭锁（改前会）。
- 走远到远区后断开 5s → 正常闭锁。
- 近区断开（手机在车内/解锁区时断开）→ 不闭锁。
- 门开着时断连 → 不闭锁（防夹）。
- 回归 CR008-010、CR009-001/004/006。

## 9. 待确认

- 5s 持续断开时长（`PHONE_PEPS_DISCONNECT_LOCK_SUSTAIN_MS`）先按当前值实机验证。
