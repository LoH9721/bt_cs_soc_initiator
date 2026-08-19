# CR009-001 车辆自动动作门控 设计

状态：`IN_PROGRESS`（设计待确认，未改码）

更新时间：2026-08-19

## 1. 目标

让 Phone PEPS 自动解锁/闭锁在发出命令前先通过一道“车辆条件门控”：

- 自动解锁：仅在车辆明确 `LOCKED`、非静默时允许发起。
- 自动闭锁：仅在车辆明确 `UNLOCKED`、门/尾门关闭、非静默时允许发起。

不修改 CR008 的授权门控语义，只在其上叠加车辆条件。

## 2. 原有逻辑（引用文件/函数）

- 授权门控：`phone_comm_current_link_is_authorized_passive()`（`user_phone/phone_comm.c:191`）= passive_enabled && 授权 Bond && L4，不含车辆条件。
- 自动解锁触发：`detect_auto_cmd()`（`user_app_fun/user_app_phone_peps.c:260`），闭锁区/无效区 → 解锁区/车内。
- 自动解锁跳过：`user_app_phone_peps.c:465-470`，`lock_state == UNLOCKED` 时跳过。
- 断连闭锁：`user_app_phone_peps.c:376-402`，启动条件为 `quota>0 && lock_state != LOCKED`。
- 锁状态 getter：`phone_comm_get_vehicle_lock_state()`（`user_phone/phone_comm.c:235`）→ `phone_sm_get_vehicle_lock_state()`。
- 静默态：`g_sess.is_silent` 仅 `user_phone/data/phone_sm.c` 内部使用，无公开访问器。
- 门状态：`vehicle_state_get_door_status()`（`user_vehicle_state.c:134`）只含四门；尾门 `RTE_282_BCM_BackDoorSt` 仅在 `user_app_fun.c:262` 读，未进 vehicle_state。

## 3. 必要性

当前自动命令不看门/尾门、不看静默态；锁状态只有“跳过解锁”一处校验。离车闭锁前若门/尾门开着会误闭锁；静默期仍可能自动动作。

## 4. 不修改的风险

门开/尾门开时仍可能发闭锁；静默期仍自动解锁/闭锁；断连闭锁仍被额度卡住（后者归 CR009-006 解耦）。

## 5. 修改位置（文件+函数）

1. `user_phone/phone_comm.h/.c`：新增 `bool phone_comm_is_silent(void)`，转发到 `phone_sm_is_silent()`。
2. `user_phone/data/phone_sm.h/.c`：新增 `bool phone_sm_is_silent(void)`，返回 `g_sess.is_silent`。
3. 尾门接入 PEPS：把 `RTE_282_BCM_BackDoorSt` 纳入车辆状态（扩展 `vehicle_state_get_door_status` 的位，或新增独立尾门 getter）。
4. `user_app_fun/user_app_phone_peps.c`：
   - 解锁路径：把“已解锁跳过”改为显式“仅 LOCKED 才解锁”+ 非静默。
   - 断连闭锁启动条件：加门/尾门关闭 + 非静默 + 明确 UNLOCKED；`quota>0` 的移除随 CR009-006。

## 6. 影响点与冲突检查

- 与 CR008-009 授权门控：不修改其语义，门控叠加其上。
- 与 CR008-010 断连闭锁：本项会收紧其启动条件（加门/尾门/静默），需回归 CR008-010 既有通过项（授权断开 5s 闭锁、重连取消、未授权不闭锁）。
- 与 CR009-006：`quota>0` 移除与断连闭锁条件同处一段代码，建议 CR009-006 先单独解耦，避免和本项混改。
- 与 CR009-002/003：本项只建门控，不碰周期门闩和命令闭环。
- 冻结接口：不改 APP-BG24 命令字/TLV/加密/GATT/HID Report Map/Appearance/串口调试接口。

## 7. 编译方法

由用户在 Simplicity Studio 编译；改动仅 `.c/.h`，无生成数据库/GATT 变更。编译后核对 `[PHONE_PEPS]` 相关状态日志。

## 8. 前后对比实测方法

- 车辆 LOCKED + 授权走近 → 允许自动解锁；UNLOCKED + 走近 → 跳过（回归现有）。
- 车辆 UNLOCKED + 授权断连 5s → 允许启动闭锁计时；LOCKED → 不启动。
- 任一监控门开或尾门开 + 离车候选 → 不闭锁；关门后重新确认。
- 静默期 → 不产生任何自动解锁/闭锁。
- 回归 CR008-009（未授权/STALE 不动作）、CR008-010（授权断连来源）。

## 9. 待确认

- 尾门接入方式：扩 `door_status` 位 vs 独立 getter（不改变冻结的串口/协议接口）。
