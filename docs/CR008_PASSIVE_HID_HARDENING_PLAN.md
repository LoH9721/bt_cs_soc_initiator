# CR-008 手机无感钥匙 HID 链路加固计划

## 1. 当前产品边界

- 当前产品只有手机钥匙，暂时没有实体钥匙。
- `config/app_config.h` 中 `APP_KEY_ENABLE` 当前为 `0`。
- `key_connect/`、`cs_key_rang/` 等代码按历史或预留代码处理，本阶段不删除、不重构。
- 实体钥匙与手机钥匙并发 Pairing/共享 SM 的问题不属于当前版本直接需求；如果未来启用 `APP_KEY_ENABLE`，必须重新评审。

## 2. 必须保持不变

- APP-BG24 业务协议、命令字、TLV 和加密格式。
- APP 正向绑定流程。
- `PASSIVE_ENABLE`、`PASSIVE_DISABLE`、`PASSIVE_PAIR_PREPARE`、`PASSIVE_PAIR_READY`、`PASSIVE_PAIR_CANCEL` 流程。
- Android 系统 Pairing/Bonding 和后台自动重连能力。
- GATT Handle。
- GAP Appearance `0x08C1`（Car）。
- 当前 23 字节 Vendor-defined HID Report Map。
- HID 只作为 Android 后台连接承载，不作为业务身份认证本身。

## 3. 目标授权条件

无感 PEPS 只有同时满足以下条件才允许工作：

```text
passive_enabled
&& authorized_phone_bond_connected
&& link_encrypted
&& ranging_fresh_for_current_connection
&& vehicle_conditions_allowed
```

物理 BLE 连接或 `hid_service_is_connected()` 不能单独作为解闭锁授权条件。

## 4. 工作项

| ID | 工作内容 | 优先级 | 状态 |
|---|---|---:|---|
| CR008-001 | Pairing 超时/成功/失败/取消统一清理 | P0 | TODO |
| CR008-002 | PASSIVE_PAIR_READY 检查 SM API 返回值并失败回滚 | P0 | TODO |
| CR008-003 | 断开及重连时清理测距，增加测距时效 | P0 | TODO |
| CR008-004 | 跟踪当前连接的 Bond、加密和安全状态 | P0 | TODO |
| CR008-005 | 在 APP 授权 Pairing 窗口内建立授权手机 Bond 关联 | P0 | TODO |
| CR008-006 | 启动恢复时校验 Passive 配置与授权 Bond | P1 | TODO |
| CR008-007 | PEPS 改用授权加密链路和当前连接的新鲜测距 | P0 | BLOCKED BY CR008-004/005 |
| CR008-008 | 自动落锁只监听授权 Passive 链路断开 | P0 | BLOCKED BY CR008-004/005 |
| CR008-009 | Release 禁止固定调试 PIN 和 PIN 日志 | P2 | TODO |
| CR008-010 | 清理重复状态和 32 位定时器回绕问题 | P2 | TODO |

## 5. 实施顺序

1. Pairing 生命周期清理和错误回滚。
2. 测距连接生命周期和时效。
3. 只增加 Bond/加密/授权状态跟踪与日志，不改变 PEPS 输出。
4. Android 实机验证安全事件时序。
5. 建立授权手机 Bond 关联和启动恢复策略。
6. 收紧 PEPS 和自动落锁门控。
7. 量产安全整理。

每个工作项独立执行：

```text
修改 -> Diff 审查 -> 编译 -> 单项测试 -> 正向绑定回归 -> 后台重连回归 -> 提交
```

## 6. 基线

- Git 基线提交：`78fbe96 baseline: verified build before CR-008 passive HID hardening`
- 工作分支：`feature/cr008-passive-hid-hardening`
- 用户已确认当前正向绑定正常。
- 当前 GATT Appearance、HID Report Map 和生成数据库一致，不纳入本阶段修改。

## 7. 状态更新规则

- `TODO`：尚未开始。
- `IN_PROGRESS`：正在修改，尚未完成验证。
- `CODED`：代码完成但未通过完整验证。
- `BUILT`：编译通过。
- `TESTED`：单项和回归测试通过。
- `ACCEPTED`：用户确认验收。
- `BLOCKED`：依赖项或实机证据不足。

