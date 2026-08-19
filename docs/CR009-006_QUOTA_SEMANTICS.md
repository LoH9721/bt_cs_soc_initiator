# CR009-006 自动解锁额度语义 设计

状态：`IN_PROGRESS`（设计待确认，未改码）

更新时间：2026-08-19

## 1. 目标

额度 = 蓝牙自动解锁的剩余次数计数：只在“发出蓝牙自动解锁命令”时扣 1，不管最终成功/失败、不返还；手动解锁、VIU、PEPS、自动闭锁都不扣。额度只决定“是否还允许发自动解锁”，不再卡自动闭锁。

## 2. 原有逻辑（引用文件/函数）

- 额度扣减：`phone_sm_consume_passive_quota()`（`user_phone/data/phone_sm.c:4431`），剩余为 0 返回 false；否则 -1 并持久化。
- 扣减触发点：`user_app_fun/user_app_phone_peps.c:474`，仅在自动解锁路径调用 `phone_comm_consume_passive_quota()`。已是“发命令即扣、不返还”，无改动。
- 额度耦合点 1：`user_app_fun/user_app_phone_peps.c:378`，断连闭锁启动条件含 `phone_comm_get_passive_quota() > 0U`。
- 额度耦合点 2：`user_app_fun/user_app_phone_peps.c:387`，断连闭锁取消条件含 `phone_comm_get_passive_quota() == 0U`。
- 额度重置：`handle_passive_quota_refresh()`（`user_phone/data/phone_sm.c:5330`），0x66 重置为 `PHONE_PASSIVE_QUOTA_DEFAULT=5`，保持不变。

## 3. 必要性

当前“自动解锁额度”被错误地用作断连自动闭锁的前置。额度耗尽后，授权链路断连 5s 也不再自动闭锁，与“解锁额度”和“闭锁安全”两件事混淆。

## 4. 不修改的风险

额度=0 时，离车断连不再自动闭锁，车辆可能未落锁。

## 5. 修改位置（文件+函数）

仅改 `user_app_fun/user_app_phone_peps.c` 的断连闭锁逻辑，两处：

1. 启动条件（约 378 行）：删除 `&& phone_comm_get_passive_quota() > 0U`，保留 `authorized_disconnected && 计时未启动 && 锁状态 != LOCKED`。
2. 取消条件（约 387 行）：删除 `phone_comm_get_passive_quota() == 0U ||`，只保留 `锁状态 == LOCKED` 时取消。

额度扣减逻辑（`phone_sm_consume_passive_quota` 及解锁路径调用）不动。

## 6. 影响点与冲突检查

- 与 CR008-010：只删除断连闭锁的额度前置，不改授权断连事件来源，需回归既有通过项。
- 与 CR009-001：两处同段代码。本项先做，避免和 CR009-001 的门/尾门/静默条件混改。
- 与 CR009-002/003：无关。
- 冻结接口：不改命令字/TLV/加密/GATT/HID/Appearance/串口。

## 7. 编译方法

由用户在 Simplicity Studio 编译；仅改 `.c`，无生成数据库变更。

## 8. 前后对比实测方法

- 额度=0 时：授权 Passive L4 断连 5s 仍产生自动闭锁（改动前不产生）。
- 额度>0 时：断连闭锁行为与改动前一致。
- 自动解锁仍扣额度，扣到 0 后自动解锁被阻止（回归现有）。
- 回归 CR008-010：授权断开 5s 闭锁、重连取消、未授权/L1/Passive OFF 断开不闭锁。

## 9. 附带观察（不改）

`phone_session.h:52` 注释“passive_quota_remaining transient, 不持久化”与实际不符（代码经 `phone_storage_set_passive_quota` 已持久化）。本项不动，仅记录。
