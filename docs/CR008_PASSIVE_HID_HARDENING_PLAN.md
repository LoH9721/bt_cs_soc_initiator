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
| CR008-001 | Pairing 超时/成功/失败/取消统一清理 | P0 | BUILT |
| CR008-002 | PASSIVE_PAIR_READY 检查 SM API 返回值并失败回滚 | P0 | BUILT |
| CR008-003 | 新连接时清理上一连接的测距状态 | P0 | BUILT |
| CR008-004 | 跟踪当前连接的 Bond、加密和安全状态 | P0 | TESTED |
| CR008-005 | Android 忽略配对后的旧 Bond 安全替换 | P0 | TESTED |
| CR008-006 | 在 APP 授权 Pairing 窗口内建立授权手机 Bond 关联 | P0 | CODED |
| CR008-007 | UNBIND 后未授权连接准入与 APP 重绑定观察窗口 | P0 | CODED |
| CR008-008 | 启动恢复时校验 Passive 配置与授权 Bond | P1 | TESTED |
| CR008-009 | PEPS 改用授权加密链路和当前连接的新鲜测距 | P0 | BUILT |
| CR008-010 | 自动落锁只监听授权 Passive 链路断开 | P0 | TESTED |
| CR008-011 | Release 禁止固定调试 PIN 和 PIN 日志 | P2 | TODO |
| CR008-012 | 清理重复状态和 32 位定时器回绕问题 | P2 | TODO |
| CR008-013 | 0x65 灵敏度设置成功响应回显最新档位 | P1 | CODED |

### CR008-007 计划边界

- BG24 完成 UNBIND 后，即使 Android 仍保留旧系统 Bond，系统/HID 后台连接也不得长期保持、不得生成新 Bond、不得占用唯一连接阻塞 APP。
- `bond=0xFF` 的物理连接不能一律立即拒绝；APP 首次绑定和重新绑定仍需通过该 GATT 通道，因此需要受限观察窗口识别合法 APP 业务活动。
- 新 Bond 必须在协议栈落盘前由应用确认；只有业务绑定身份完整且当前处于 APP `PREPARE/READY` 授权系统 Pairing 窗口才接受，其他手机请求必须直接拒绝并断开。
- 手机和 Central 钥匙共享整机 SM 配置；Bond确认必须按 connection handle 分流，钥匙侧只有显式钥匙配对流程才接受，已有手机/钥匙 Bond重连不受影响。
- `SM_BONDED` 窗口外删除继续保留为异常兜底；观察期内没有合法 APP 业务活动也应超时断开并恢复广告。
- 不修改串口调试接口；观察窗口时长、合法 APP 活动判据和错误时序在 CR008-007 开始前单独确认。

## 5. 实施顺序

1. Pairing 生命周期清理和错误回滚。
2. 测距连接生命周期和时效。
3. 只增加 Bond/加密/授权状态跟踪与日志，不改变 PEPS 输出。
4. Android 实机验证安全事件时序。
5. 修复 Android 忽略配对后两端 Bond 不一致导致的重配失败。
6. 建立授权手机 Bond 关联。
7. 收紧 UNBIND 后未授权连接准入，并保留 APP 重绑定通道。
8. 启动时校验 Passive 配置与授权 Bond。
9. PEPS 改用授权加密链路和当前连接的新鲜测距。
10. 自动落锁只监听授权 Passive 链路断开。
11. 量产安全整理。
12. 清理低优先级重复状态和计时器回绕问题。
13. 0x65 灵敏度设置成功响应回显最新档位。

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

## 8. 变更记录

每项修改只记录：修改原因、不修改的风险、实际改动、影响范围和验证方法。

### CR008-001 Pairing 生命周期统一清理

- 状态/日期：`BUILT`，2026-08-17。
- 原因：Pairing 超时清理由 `phone_session` 和 `phone_sm` 分别处理；Session 层先清除窗口状态后，SM 层无法再进入完整清理。
- 风险：Bondable、临时 Passkey/安全配置、`pairing_awaiting_system` 或 HID runtime 可能残留，影响下一次 Pairing，并延长系统 Pairing 暴露时间。
- 改动：删除 Session 层的部分清理；由 `phone_sm` 在超时、取消、Bond 成功/失败时统一调用 `pairing_context_destroy()`；完整清理同时清零 deadline。
- 影响：不改变 APP 协议、GATT、HID Report Map、Appearance、Pairing 窗口时长及 Bond 数据格式。
- 验证：编译通过；分别验证 PREPARE 30 秒超时、READY 60 秒超时、取消、正确/错误 PIN；确认超时后可重新 PREPARE，且 HID 恢复到原 Passive 状态。
- 当前：用户确认编译通过，等待实机验证。

### CR008-002 PASSIVE_PAIR_READY 失败回滚

- 状态/日期：`BUILT`，2026-08-17。
- 原因：原代码忽略 SM 配置和断开连接的返回值，并在配置前回复成功。
- 风险：配置失败时 APP 仍收到成功，可能导致错误 PIN、无法 Bond、连接未断开或系统搜不到设备。
- 改动：逐步检查 Bond 配置、SM 安全模式、Passkey、Bondable、响应构造/发送及断开请求；失败时调用统一清理，SM 配置全部成功后才回复成功。
- 影响：不改变 APP 协议、TLV、60 秒窗口、GATT、HID、Appearance 和 Bond 数据格式。
- 验证：编译通过；正常 READY 应依次返回 `sc=0x0000`、APP 收到成功后断连并进入系统 Pairing；异常返回时应清理窗口且不进入系统 Pairing。
- 当前：用户确认编译通过，等待实机验证。

### CR008-003 当前连接测距生命周期

- 状态/日期：`BUILT`，2026-08-17。
- 原因：测距重置函数没有调用者，新连接可能沿用上一连接的RSSI、融合距离和卡尔曼状态。
- 风险：手机频繁断开、重连时，PEPS可能在本连接首个RSSI产生前使用上一连接的距离。
- 改动：手机新连接建立时重置测距状态；未连接时距离接口返回无效；原有本端超时后采用远端的逻辑保持不变。
- 影响：不改变RSSI距离算法、300ms轮询周期、PEPS阈值、APP协议、HID和车辆接口。
- 验证：编译通过；断连后距离接口返回无效；频繁重连时，本连接首个RSSI产生前不得返回上一连接距离；首个新RSSI产生后距离恢复有效。
- 当前：用户按约定确认编译通过，等待实机验证。

### CR008-004 当前连接 Bond 与链路安全状态

- 状态/日期：`TESTED`，2026-08-17。
- 原因：原代码只能查询 Bond 表是否非空，并将任意手机物理连接视为 HID 已连接，无法判断当前连接是否关联 Bond、是否加密及实际安全等级。
- 风险：后续授权门控可能误把“存在其他 Bond”或“仅物理连接”当作当前授权安全链路。
- 改动：由 `phone_link` 跟踪当前连接的 Bond handle 和 Security Mode，在 OPEN、BONDED、安全等级变化及 CLOSED 时记录统一日志；由 `phone_comm` 提供只读查询接口。当前链路已 Bond 不等于已被 APP 授权。
- 影响：仅增加 RAM 状态、日志和查询接口；不改变 Pairing、Bonding、PASSIVE、HID、PEPS、APP协议及NVM。
- 验证：编译通过；验证普通未绑定连接为 `bond=0xFF/L1`，Pairing成功预期为有效 Bond/`L4`，后台重连预期从已 Bond/`L1`升级为已 Bond/`L4`，断开后状态清零。
- 当前：用户已完成编译和 Android 实机验证；确认后台重连从有效 Bond/L1 升至 L4，断开后状态清零，频繁重连正常。

### CR008-005 Android 忽略配对后的旧 Bond 安全替换

- 状态/日期：`TESTED`，2026-08-17。
- 原因：Android 忽略设备只删除手机侧密钥，BG24 仍保留旧 Bond；APP 的 PREPARE/READY 虽成功，系统重新 Pairing 时仍命中旧 Bond 并报 `0x1205`。
- 风险：用户无法恢复系统配对和 HID 自动重连；重复开启无感仍会在系统 Pairing 阶段失败。
- 改动：仅当 `pairing_window_active && pairing_awaiting_system` 时，在 READY 成功响应后的 APP 断连事件中清除旧 BLE Bond；删除发生在广播恢复前，失败则销毁 Pairing 上下文并恢复 Non-Bondable。
- 影响：不改变 APP 协议、PREPARE/READY 响应、Pairing 窗口、HID、GATT、Appearance、PEPS 和业务绑定数据；当前产品只有一把手机钥匙，因此该授权恢复流程中清除全部 BLE Bond 符合产品边界。
- 验证：Android 忽略设备后由 APP 重新开启无感；确认日志先出现 `stale bonds delete ... sc=0x0000`，再恢复广播；随后应弹出 PIN、出现 `SM_BONDED`，且不得再出现 `SM_BONDING_FAILED reason=0x1205`；重启后验证 HID 自动重连和无感功能。
- 当前：用户确认编译和实机验证通过；Android 忽略设备后可由 APP 重新完成系统 Pairing，不再阻塞于旧 Bond。

### CR008-006 APP 授权手机 Bond 身份关联

- 状态/日期：`CODED`，2026-08-18。
- 原因：当前仅能确认 Bond 表非空或当前连接关联了某个 Bond，无法在重启后证明该 Bond 是由当前业务绑定 APP 授权建立的；Bond handle 删除后还可能复用，不能作为持久身份。
- 风险：若继续把任意 Bond 或 HID 物理连接视为授权手机，后续 PEPS 和自动落锁可能接受与当前 `appKeyId/bindVersion` 无关的连接。
- 改动：新增 `0x5520`、28 字节授权 Bond 记录，显式保存格式版本、Identity Address Type、Identity Address、`appKeyId` 和大端 `bindVersion`，CRC16 仍由统一 EEPROM 层追加；PREPARE 保存已认证 APP 身份快照，READY 和最终提交前再次校验业务绑定未变化；`SM_BONDED` 必须属于当前授权窗口且为有效 Bond/L4，主循环再查询 bonding details，要求 L4、16 字节密钥并同步落盘；写入或属性校验失败时清除记录并删除新 Bond；删除旧 Bond、换绑、解绑和工厂复位会清除授权记录。
- 边界：单手机、无历史 Bond 迁移；没有授权记录时必须重新走 APP 认证和系统 Pairing。不改变 APP 命令字、TLV、加密格式、GATT、HID Report Map、Appearance、PEPS 输出和自动落锁门控。UNBIND 后未授权连接准入、后台连接阻塞和 APP 重绑定观察窗口由 CR008-007 单独处理。
- 验证：待用户编译；实机需验证正常 Pairing 后出现授权 Bond 提交日志和有效 NVM 条目，错误 PIN/超时/取消不产生记录，NVM 失败回滚会删除新 Bond，换绑/解绑/工厂复位会清除记录，并回归 Android HID 后台重连。
- 当前：代码和只读差异审查完成，尚未编译及实机验证。

### CR008-007 UNBIND 后未授权连接准入与 APP 重绑定观察窗口

- 状态/日期：`CODED`，2026-08-18。
- 原因：BG24 单边执行 UNBIND 后，Android 仍可能保留旧系统 Bond；首版观察窗口只在 `SM_BONDED` 后删除新 Bond，实机证明 Android 已先显示配对成功，属于事后回滚而非直接拒绝。
- 风险：若继续在 `SM_BONDED` 后处理，手机端仍会形成单边残留 Bond；若用整机全局 Bondable开关粗暴阻止，又可能与同芯片 Central钥匙的新配对互相干扰；若拒绝全部 `bond=0xFF` 连接，则APP首次绑定和重新绑定也无法进行。
- 改动：保留无 Bond连接等待业务 Notify 3秒、Notify后等待合法`BIND_HELLO`或`AUTH_CHALLENGE_REQ` 5秒的观察窗口；所有手机/钥匙 SM配置增加新Bond确认要求；手机收到确认事件时只在当前connection、业务绑定`BOUND`、APP公钥存在、`appKeyId/bindVersion`有效且与`PREPARE/READY`快照一致、授权窗口未过期时接受，否则在`SM_BONDED`前拒绝并断开；钥匙事件只处理当前Central connection且只在显式钥匙配对流程接受；窗口外`SM_BONDED`删除仍作异常兜底。
- 影响：不改变APP命令字、TLV、加密格式、NVM格式、HID Report Map、Appearance、串口`phone_unbind`接口、PEPS输出和自动落锁门控；已有合法手机Bond和钥匙Bond重连不产生新Bond确认事件，不受该门控影响；新钥匙Bond必须通过现有显式钥匙配对模式。
- 验证：待用户编译；首先复现“BG24 UNBIND、Android不忘记、系统蓝牙点击并确认配对”，必须出现`Bond request REJECT`且不得再出现`SM_BONDED`；再验证无Notify 3秒超时、Notify后无合法BIND/AUTH 5秒超时、APP首次绑定、APP已绑定但未选择无感时拒绝系统配对、APP完成`PREPARE/READY`后出现`Bond request ACCEPT`并最终L4授权Bond提交、已有授权手机后台重连、已有钥匙自动重连及显式新钥匙配对。
- 当前：代码和只读差异审查完成，尚未编译及实机验证。

### CR008-008 启动恢复时校验 Passive 配置与授权 Bond

- 状态/日期：`TESTED`，2026-08-18。
- 原因：原上电逻辑只读取 `passive_enabled`，为 ON 就直接恢复 HID；`PASSIVE_ENABLE` 也只检查 Bond 表非空，无法证明该 Bond 属于当前业务绑定 APP。
- 风险：Bond 被删除、授权记录损坏、换绑数据不一致或仅剩其他 Bond 时，设备仍可能恢复 HID 后台连接，并为后续 PEPS 提供错误前提。
- 改动：移除 `app_init` 中按单一布尔值恢复 HID；Bluetooth `system_boot` 时在首个广播构造前校验 APP `BOUND` 状态、公钥、`appKeyId/bindVersion`、CR008-006 授权记录、Identity Address、Bond L4 和 16 字节密钥；`PASSIVE_ENABLE` 复用同一校验，禁止以“任意 Bond 存在”绕过。确定的逻辑不一致会关闭并持久化 Passive OFF、清除授权记录；启动阶段只删除按授权 Identity Address 精确命中的 Bond，连接中的 `PASSIVE_ENABLE` 失败保留旧 Bond 以先返回错误并交由现有 PREPARE/READY 修复流程处理；协议栈查询异常仅安全关闭，不删除 Bond。
- 影响：合法授权配置仍恢复 HID 和后台重连；用户主动关闭 Passive 时保留合法授权记录/Bond，后续可免 PIN 重新启用。不改变 APP 命令字、TLV、NVM 格式、GATT、HID Report Map、Appearance、PEPS、自动落锁和串口调试接口。
- 验证：待用户编译；合法授权且 Passive ON 重启应在广告前打印 `startup restore PASS` 并后台 L4 重连；Passive OFF 重启应打印 `SKIP` 且 APP GATT仍可连接；用现有 `hid_cfg 2` 制造“Passive ON但Bond缺失”后重启，应打印 `startup restore FAIL`、HID保持 OFF且不得恢复后台无感连接；再验证 APP 重新 Pairing/Enable、重启和后台重连。
- 当前：用户确认编译和实机测试完成；合法恢复、APP运行中开关及失败关闭行为通过。

### CR008-009 PEPS 使用授权加密链路和新鲜测距

- 状态/日期：`BUILT`，2026-08-18。
- 原因：原 PEPS 仅检查 `passive_enabled + hid_service_is_connected + 曾有距离`；任意 Peripheral物理连接都会被标记为 HID连接，且一次成功测距会在整条连接期间永久有效。
- 风险：未授权 APP观察窗口、旧/错误 Bond或尚未升到L4的连接可能提前进入区域判断；RSSI更新停止后仍可能继续使用陈旧距离。CR008-006/CR008-008 建立的授权身份没有真正进入自动解锁执行门控。
- 改动：运行期缓存经 CR008-006 提交或 CR008-008 验证通过的授权 Bond handle；`phone_comm`统一组合 Passive ON、当前连接Bond等于授权Bond和当前安全等级L4；PEPS区域与自动解锁只接受该统一门控。`phone_rang`记录最后一次成功融合时间，超过1500ms即返回无效；新连接继续由 CR008-003 清空全部测距状态。增加门控OPEN/CLOSED和距离FRESH/STALE转换日志。
- 边界：CR008-009实施时暂时保留的`hid_service_is_connected()`自动落锁下降沿现已由CR008-010替换；首次区域直接为解锁区时不立即自动解锁的现有策略保持不变。不改变APP协议、Pairing、NVM格式、HID、区域阈值、300ms轮询、车辆接口和串口调试接口。
- 验证：待用户编译；合法后台重连必须先L4再打开授权门控，首个新RSSI后打印`range=FRESH`并恢复区域；L1、无Bond、非授权Bond和Passive OFF均不得输出有效区域或自动解锁；新连接首个RSSI前不得复用旧距离；连续1500ms无成功融合时应打印`range=STALE`且不得产生新命令；回归APP退出后的后台无感、CR007未授权配对拒绝和CR008启动恢复。
- 当前：用户确认编译和正向实机测试通过；未授权连接、1500ms超时等负向项仍待补测，因此尚不标记`TESTED`。

### CR008-010 自动落锁只监听授权 Passive 链路断开

- 状态/日期：`TESTED`，2026-08-18。
- 原因：原自动落锁监听 `hid_service_is_connected()` 的下降沿，而该状态代表任意 Peripheral物理连接，不代表当前连接是授权手机。
- 风险：未授权 APP连接、系统蓝牙直接配对、CR007观察窗口超时或拒绝连接后，也可能启动5秒计时并错误发送自动闭锁。
- 改动：`phone_link`在清空当前Bond/L4状态前把断开快照传给`phone_sm`；只有断开前为Passive ON、当前Bond等于运行期授权Bond、链路为L4且不是PREPARE/READY主动切换时，才生成一次性授权断连事件。PEPS仅消费该事件启动5秒计时，且只有授权Passive L4链路恢复才能取消；未授权连接既不能启动也不能取消计时。
- 边界：保留原5秒持续时间、额度未耗尽和车辆尚未闭锁条件；不改变自动解锁、1500ms测距、APP协议、Pairing、NVM、HID、车辆接口和串口调试接口。
- 验证：待用户编译；授权L4断开应打印`disconnect snapshot ... arm=Y`并启动计时，持续5秒后自动闭锁；5秒内恢复授权L4应取消；未授权连接断开、L1断开、Passive OFF断开和PREPARE/READY主动断开均必须`arm=N`且不得自动闭锁；车辆已闭锁或额度耗尽不得重复发送闭锁。
- 当前：用户确认编译和实机测试通过；授权断开5秒自动闭锁、5秒内重连取消、未授权/L1/Passive OFF/主动Pairing断开不闭锁等项通过。
