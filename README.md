# bt_cs_soc_initiator

BG24 BLE CS Initiator SoC 固件工程：手机蓝牙钥匙 / 无感解锁（Passive HID）与 CAN 桥接。

## 功能模块

- `user_phone/`：手机 BLE 通信、GATT 协议、配对/Bond、无感钥匙（PASSIVE）状态机
- `user_hid/`：HID 承载层，无感开启时在广播中加入 HID 特征，供手机后台自动回连
- `user_app_fun/`：CAN ↔ BLE 应用桥接、PEPS 区域判断与自动解闭锁
- `user_eeprom/`：统一 NVM3 存储层（绑定记录、授权 Bond、Passive 配置等）
- `key_connect/`、`cs_key_rang/`：预留的钥匙/Central 与 CS 测距模块
- `config/`：工程配置与 GATT 数据库源文件

## 构建

使用 Simplicity Studio 打开 `bt_cs_soc_initiator.slcp` 构建并烧录。

## 关键文档

- `PROJECT_HANDOFF.md`：项目交接说明
- `docs/CR008_PASSIVE_HID_HARDENING_PLAN.md`：CR008 计划与验证记录
- `docs/CR008_CHANGE_RECORD.md`：CR008-001～010 修改记录（含代码改动点）
- `docs/GIT_COMMIT_GUIDE.md`：Git 提交与上传操作手册

## 当前状态

- CR008-001～005：编译通过，其中 004/005 实机验证通过
- CR008-006～007：代码完成（CODED）
- CR008-008：实机验证通过（TESTED）
- CR008-009：编译与正向测试通过（BUILT），负向项待补测
- CR008-010：实机验证通过（TESTED）
