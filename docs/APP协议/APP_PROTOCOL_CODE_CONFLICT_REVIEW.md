# APP V1.2 协议与当前代码一致性审查清单

更新时间：2026-08-20  
协议基线：`APP-BG24蓝牙通讯协议_V1.2_正式版.docx`。本轮以 V1.2 正式版全文为唯一协议基线，不再沿用 V1.1 的修订解释。  
代码基线：分支 `feature/cr008-passive-hid-hardening`，审查时 HEAD 为 `cd3b60e`。

## 1. 结论边界

- 本文件用于逐项确认协议与实现差异，不是代码修改指令。
- **仅 C-001 已由项目负责人确认，可作为正式结论：当前 GATT 的 RX 特征值只配置了 `Write Without Response`。**
- 除 C-005、C-006 已与 V1.2 对齐，以及 C-011 归为协议待澄清外，C-002～C-026 其余条目均为“待确认”；逐项确认后才能决定修改代码、修改协议或接受偏差。
- “已对齐”表示 V1.2 文本与当前实现已一致，不代表已完成真机互操作验证。
- 本轮只做文档结构化读取和代码静态对比；DOCX 页面渲染工具缺少 LibreOffice，未完成逐页视觉排版检查，不影响正文、表格字段的结构化读取。

## 2. 状态定义

| 状态 | 含义 |
|---|---|
| 已确认 | 已由项目负责人确认，可作为正式结论。 |
| 待确认 | 已发现明确的文本/实现差异或风险，但尚未决定以哪一侧为准。 |
| 已对齐 | V1.2 协议已覆盖当前实现，或当前实现符合该条协议；仍需联调/测试证明。 |
| 协议待澄清 | 协议内部矛盾、不可实现或缺少系统级定义，不能直接归责为代码缺陷。 |

## 3. 如何快速定位

### 3.1 使用方法

1. 打开 `APP-BG24蓝牙通讯协议_V1.2_正式版.docx`，按 `Ctrl+F` 搜索下表中的关键词；章节号用于确认搜索结果是否落在正式定义而不是历史示例。
2. 在代码编辑器中按 `Ctrl+P`，输入 `文件路径:行号`；也可以在工程根目录执行 `rg -n "符号名" 文件路径`。
3. 协议第 19、21 章包含 V1.1 历史示例；出现冲突时，优先核对第 22 章统一基线和第 23 章 V1.2 扩展。
4. 行号以审查时 HEAD `cd3b60e` 为准；代码变化后，优先用表中的函数或宏名重新搜索。

### 3.2 代码冲突/对齐项定位表

| ID | 协议定位（章节；Ctrl+F 关键词） | 代码定位（文件:行号；符号） | 建议先看 |
|---|---|---|---|
| C-001 | §4；`Write With Response` | `config/btconf/gatt_configuration.btconf:157`；`phone_rx`，具体属性在 `:160` | GATT XML |
| C-002 | §5、§22.2；`NeedRsp`、`Response`、`Flags` | `user_phone/phone_cfg.h:45`、`:48`；`PHONE_FLAGS_PLAIN_RSP`、`PHONE_FLAGS_ENCRYPTED_RSP`，发送点 `user_phone/data/phone_sm.c:1269`、`:1383` | 两个宏值 |
| C-003 | §11.2、§22.3；`AUTH_CHALLENGE_REQ`、`AUTH_CHALLENGE_RSP` | `user_phone/data/phone_sm.c:2668`、`:2759`；两个 AUTH handler，必选字段在 `:2674`、`:2765` | 协议 Schema 表与两个 `req_tlvs[]` |
| C-004 | §16、§22.9；`AUTH challenge`、`10 秒` | `user_phone/phone_cfg.h:265`；`PHONE_TIMEOUT_AUTH_CHALLENGE_MS`，使用点 `user_phone/data/phone_session.c:231` | 超时宏 |
| C-005 | §23.1、§23.3～§23.9；`0x60`、`0x66` | `user_phone/phone_cfg.h:72`；`PHONE_CMD_PASSIVE_ENABLE` 起始的一组命令宏 | 命令宏连续区 |
| C-006 | §7、§23.1；`capabilityFlags.bit5`、`0x0000003F` | `user_phone/phone_cfg.h:120`～`:122`；TLV 0x26～0x28，`:243`；`PHONE_CAP_DEFAULT_V11` | TLV/能力宏 |
| C-007 | §13、§21.9；`5 秒内无法确认最终结果`、`TIMEOUT` | `user_phone/data/phone_sm.c:3651`；CTRL 延迟验签后处理，立即下发/响应在 `:3704`～`:3733` | `PHONE_CMD_CTRL_COMMAND` 后处理 |
| C-008 | §12、§21.12、§22.2；`完整请求字节流`、`幂等` | `user_phone/data/phone_session.c:361`；`phone_session_check_idempotent`，提前命中点 `user_phone/data/phone_sm.c:1711` | 缓存匹配条件 |
| C-009 | §8、§22.6；`tokenBody 字段顺序固定`、`原始 UTF-8 字节` | `user_phone/data/phone_sm.c:3803`；QR 验签后字段检查，`strstr()` 从 `:3819` 开始 | QR 后处理 |
| C-010 | §8、§22.6；`QR_VERIFY`、`全部校验成功` | `user_phone/data/phone_sm.c:3803`～`:3807`；`phone_session_set_qr_verified` 位于字段检查之前 | 状态置位顺序 |
| C-011 | §6、§7/§8；`tokenBody`、`UTF-8 max 320`、`MTU=247` | `user_phone/phone_cfg.h:283`；单帧 236 Byte，`user_phone/data/phone_sm.c:790`；token 缓存 256 Byte，拷贝点 `:2375` | 最大长度计算 |
| C-012 | §11.1、§12、§22.10；`同一个绑定记录`、`原子切换`、`保留原绑定` | `user_phone/data/phone_storage.c:548`；`phone_storage_atomic_register`，主区顺序写 `:663`，换绑复用 `:745` | 存储事务流程 |
| C-013 | §12；`原子提交 appCounter`、`再执行一次业务` | `user_phone/data/phone_storage.c:535`；提交函数，返回值被忽略于 `user_phone/data/phone_sm.c:3707`、`:3769` | 两个调用点 |
| C-014 | §14、§22.5；`SILENT`、`恢复原绑定状态` | `user_phone/data/phone_session.c:401`；进入 SILENT，`:486`；到期恢复 | `bindState` 写入/读取顺序 |
| C-015 | §5、§22.7；`MsgType`、`Flags`、`PayloadLen` | `user_phone/data/phone_frame.c:217`；`phone_frame_decode`，长度判断 `:250`～`:278`；业务入口 `user_phone/data/phone_sm.c:1566` | 解码器的严格性 |
| C-016 | §7、§22.7；`重复 TLV`、`空 Payload` | `user_phone/data/phone_tlv.c:104`；`phone_tlv_has_duplicates`，接收入口 `user_phone/data/phone_sm.c:1566`，无参分发 `:1928` 等 | 重复函数是否被调用 |
| C-017 | §11.3、§22.3；`controlFlags`、`controlCmd`、`authSessionId` | `user_phone/data/phone_sm.c:3375`；`handle_ctrl_command`，字段读取/签名在 `:3446`～`:3581` | 必选存在与值域校验的区别 |
| C-018 | §13；`remainingRange`、`doorState`、`UNKNOWN` | `user_vehicle_state.c:24`～`:35`；CAN 状态零初始化，getter 在 `:122`～`:137`；GET_STATUS 直接读取点在 `user_phone/data/phone_sm.c:2993`～`:3003` | 初值、有效位和超时 |
| C-019 | §4；`Notify 使能成功前`、`CCCD` | `user_phone/phone_comm.c:106`～`:121`；CCCD 事件，`user_phone/data/phone_sm.c:4366`；仅有 enabled 回调 | CCCD 禁用分支 |
| C-020 | §17、§21.12、§23.15；`Secure Debug`、`日志` | `user_phone/phone_cfg.h:398`；`PHONE_DUMP_FRAMES=1`，会话 key 日志示例 `user_phone/data/phone_sm.c:1691`～`:1800` | 量产宏和 UART 入口 |
| C-021 | §2、§3；`deviceId`、`产线` | `user_phone/data/phone_storage.c:56`；`generate_device_id`，初始化调用 `:95`～`:98` | 身份来源 |
| C-022 | §3、§23.1/§23.15；`Company ID`、`0x1234` | `user_phone/phone_cfg.h:349`；`PHONE_ADV_COMPANY_ID`，组包点 `user_phone/data/phone_adv.c:115` | 量产配置宏 |
| C-023 | §23.2、§23.6、§23.13；`当前认证 connection`、`新连接`、`PASSIVE_PAIR_READY` | `user_phone/data/phone_sm.c:4744`；READY 配置，Bondable 提前开启 `:4788`，响应 `:4800`，主动断开 `:4842` | READY 时序图 |
| C-024 | §23.7；`PASSIVE_PAIR_CANCEL`、`pairingWindowId` | `user_phone/data/phone_sm.c:4870`；ID 不匹配仍清理 `:4882`～`:4895` | mismatch 分支 |
| C-025 | §23.2；`每次 Pairing`、`新 Passkey`、`TRNG` | `user_phone/data/phone_sm.c:4599`；固定 PIN 优先分支 `:4601`～`:4605` | 调试 PIN 是否能进入量产 |
| C-026 | §23.3～§23.9、§23.11；`持久化`、`重启恢复` | `user_phone/data/phone_sm.c:5157`、`:5240`、`:5302`、`:5356`；四类持久化调用均忽略返回值 | NVM 失败路径 |

### 3.3 协议自身问题定位表

| ID | 协议定位（章节；Ctrl+F 关键词） | 代码/工程辅助定位 | 需要确认的人 |
|---|---|---|---|
| P-001 | §7 与 §23.1；分别搜索 `bit5-31 继续保留`、`capabilityFlags.bit5` | `user_phone/phone_cfg.h:243` 当前采用 `0x3F` | 协议负责人 + APP |
| P-002 | §6、TLV 表、§8；搜索 `MTU=247`、`tokenBody`、`max 320` | `user_phone/phone_cfg.h:283` 当前单帧上限 236 | 协议负责人 + APP/BG24 |
| P-003 | 文档首页状态栏；搜索 `待 00_项目总控登记`、`接口冻结` | 文件名为 `V1.2_正式版`，无代码项 | 项目负责人 |
| P-004 | 当前协议全文缺项；搜索 `RSSI`、`绑定距离`、`通信距离` 可验证没有验收定义 | RF/功率参数不在 APP 帧协议代码中；应追溯系统和射频需求 | 系统/射频/APP |
| P-005 | §12、§22.10；搜索 `原子切换`、`存储异常恢复` | `user_phone/data/phone_storage.c:548` 可用于反推所需断电点 | 协议负责人 + BG24 |
| P-006 | §3、§17、§23.15；搜索 `量产前替换`、`Secure Debug` | `user_phone/phone_cfg.h:349`、`:398`，以及 UART/HID 调试 PIN 入口 | 发布/安全负责人 |

## 4. 总览

| ID | 状态 | 主题 | V1.2 协议要求 | 当前代码 | 影响与确认重点 |
|---|---|---|---|---|---|
| C-001 | **已确认** | GATT RX 写属性 | 第 4 章要求 APP→BG24 使用 `Write With Response`。 | `phone_rx` 只声明 `write_no_response`。 | 正式结论仅确认“当前只配置 WNR”；后续仍需决定改 GATT 还是改协议。 |
| C-002 | 待确认 | Response Flags | Response 的 `NeedRsp=0`：明文 `0x02`、加密 `0x0E`。 | 明文 Response 使用 `0x03`，加密 Response 使用 `0x0F`。 | 严格 APP、抓包校验及测试向量可能拒绝响应。 |
| C-003 | 待确认 | AUTH 两步字段分配 | 0x20 Request 只带 `appKeyId`；0x21 Request 带 `nonceA`、`appEcdhPublicKey` 等。 | 0x20 强制携带 `appKeyId+nonceA+appEcdhPublicKey`，0x21 不再接收后两项。 | 按 V1.2 开发的 APP 会在首个 AUTH 请求被拒绝。 |
| C-004 | 待确认 | AUTH challenge 时限 | 第 16 章规定 10 秒。 | `PHONE_TIMEOUT_AUTH_CHALLENGE_MS=5000`。 | APP 在 5～10 秒内返回时，协议认为有效而 BG24 已过期。 |
| C-005 | 已对齐 | PASSIVE 命令集 | 第 23 章正式定义 0x60～0x66。 | 已实现 0x60～0x66。 | V1.1 的“命令未入协议”问题已被 V1.2 消除。 |
| C-006 | 已对齐 | PASSIVE TLV 与能力位 | 正式定义 0x26～0x28，capability bit5=Passive Key，建议值 `0x3F`。 | 已定义 0x26～0x28，默认能力值 `0x3F`。 | V1.1 的“TLV/bit5 未入协议”问题已被 V1.2 消除；协议内部仍有 P-001。 |
| C-007 | 待确认 | CTRL_COMMAND 最终结果 | 只有车辆最终状态与命令一致才能返回成功；5 秒无确认应返回 `FAIL/TIMEOUT/UNKNOWN`。 | 验签后立即写 `g_pending_control_cmd`，随即返回 OK 和当时的锁状态。 | APP 会把“命令已排队”误认为“车辆已执行完成”。 |
| C-008 | 待确认 | 幂等命中条件 | 同 `Cmd+Seq` 还必须比较完整请求字节流或摘要。 | 缓存只比较 `Cmd+Seq`，并在 Counter/Tag 校验前命中。 | 不同请求可能得到旧响应，且绕过本次密文认证。 |
| C-009 | 待确认 | QR tokenBody 严格校验 | 原始字节保留，严格校验 `v/type/did/name/qid/cv/bs` 及固定顺序。 | 验签后只用 `strstr()` 检查 `"BIND"`、deviceId、qid。 | 未校验 cv、bs、name、JSON 边界与顺序。 |
| C-010 | 待确认 | QR 成功状态时序 | 全部字段与签名校验成功后才允许 `qrVerified=true`。 | 先调用 `phone_session_set_qr_verified()`，之后才检查字符串；失败路径未回滚。 | 负向 QR 请求失败后可能遗留已验证状态。 |
| C-011 | 协议待澄清 | tokenBody 长度与单帧 MTU | tokenBody 最大 320 Byte，同时要求单帧并以 ATT MTU 247 为目标。 | 单帧 Payload 上限 236 Byte；延迟缓存还会把 tokenBody 截到约 255 Byte。 | 最大合法请求无法由一次 ATT 写入承载，协议自身需先定稿。 |
| C-012 | 待确认 | 绑定/换绑原子性 | VIN、APP 凭据、KeyId、版本、Counter、bindState 应作为完整记录原子切换；失败保留旧绑定。 | 备用区和主区均为多条顺序写；无提交标记和启动恢复。换绑复用同一函数，主区失败时还会删除已有主记录。 | 断电或写失败可能留下半条记录，换绑失败可能破坏旧绑定。 |
| C-013 | 待确认 | appCounter 落盘失败 | Counter 必须先原子提交，提交失败不得继续接受安全业务。 | AUTH 和 CTRL 都忽略 `phone_storage_commit_app_counter()` 返回值。 | NVM 写失败后仍可能认证成功或下发控制，重放保护不闭环。 |
| C-014 | 待确认 | SILENT 到期恢复 | 60 秒后恢复进入 SILENT 前的实际绑定状态。 | 进入 SILENT 时把持久化 `bindState` 覆盖为 SILENT；到期又从该值读取，因此仍可能恢复为 SILENT。 | 状态可能无法自动回到 BOUND，重启场景风险更高。 |
| C-015 | 待确认 | 帧总长度与头部语义 | 必须精确匹配总长度；APP 输入必须为 Request，并满足规定 Flags 组合。 | 解码仅检查 `len >= expected_min`；未统一校验 MsgType=Request 和完整 Flags。 | 尾随字节、Response/Event 冒充请求、异常 Flags 可能进入业务处理。 |
| C-016 | 待确认 | 重复 TLV 与空 Payload | 重复 TLV 必须拒绝；GET_DEVICE_INFO、GET_STATUS、CTRL_CHALLENGE_REQ 等要求空 Payload。 | 有 `phone_tlv_has_duplicates()` 但接收路径未调用；多个无参 handler 忽略额外 TLV。 | 实现的 Schema 比正式协议宽松。 |
| C-017 | 待确认 | 会话 ID、命令值及 flags | `authSessionId/bindSessionId` 必须等于当前会话；`controlCmd` 仅 01/02/03；`controlFlags=00`。 | CTRL 只完整校验 challengeId；未见 authSessionId 相等、controlCmd 枚举和 flags=0 的拒绝逻辑。 | 请求字段可能与当前会话或正式命令范围不一致。 |
| C-018 | 待确认 | 车辆 UNKNOWN 初值 | 未知点火、续航、门状态分别使用 00、FFFF、FF。 | `g_can_state` 静态零初始化，三个 getter 没有有效位/超时判断；GET_STATUS 直接读取后会映射为 OFF、0 km、门全关。虽然 `phone_sm` 的事件缓存初值是 UNKNOWN，但 GET_STATUS 没有使用这些缓存值。 | APP 可能把“尚未收到有效 CAN”显示成真实车辆状态。 |
| C-019 | 待确认 | Notify 被关闭后的准入 | Notify 是业务命令前置条件；关闭后应有确定的禁止语义。 | CCCD 使能会置位；CCCD 禁用只记录日志，不清 `g_notify_enabled`。 | APP 关闭 Notify 后 BG24 仍可能接收命令，却无法回传结果。 |
| C-020 | 待确认 | 调试日志与安全材料 | 量产不得泄露会话密钥、绑定秘密等机密材料；安全过程日志也应受控。 | `PHONE_DUMP_FRAMES=1`，日志会输出解密 key、nonce、AAD、tokenBody、签名、公钥及派生输入；UART 仍有安全配置入口。 | 会话密钥是直接泄密风险，其他完整认证材料也会扩大攻击面；需明确量产编译与 Secure Debug 策略。 |
| C-021 | 待确认 | deviceId 来源 | deviceId 属于产线写入/配置的设备身份。 | 每次上电由芯片 UID 计算 `BG24_xxxxxxxx`，仅存 RAM。 | 二维码 did、后台设备记录和 BG24 身份来源可能不一致。 |
| C-022 | 待确认 | Company ID | `0x1234` 仅用于开发，量产前必须替换。 | 广播仍使用开发值 `0x1234`。 | 当前可作为开发构建；必须进入发布准入检查。 |
| C-023 | 待确认 | PASSIVE_PAIR_READY 连接流程 | READY 成功响应发送/排队后才启用 Bondable，并在**当前已认证连接**上触发系统 Pairing；其他/新连接应拒绝。 | 先启用 Bondable，再发响应；随后主动关闭当前 APP 连接，保存上下文并等待一次新系统连接完成 Bond。 | 这是 V1.2 与代码的流程级冲突，直接影响 APP/OS 状态机、安全授权边界和重连体验。 |
| C-024 | 待确认 | PASSIVE_PAIR_CANCEL 的错误 ID | 有活动上下文且请求带 windowId 时，ID 必须匹配。 | ID 错误只打印日志，仍销毁上下文并返回 SUCCESS。 | 旧窗口或错误请求可取消当前有效 Pairing，APP 无法区分误取消。 |
| C-025 | 待确认 | 每次 Pairing 的随机 Passkey | 每次 Pairing procedure 都应由 TRNG 生成新的 6 位 Passkey。 | 若 UART/HID 设置了固定调试 PIN，PREPARE 优先重复使用固定值。 | 若量产入口未禁用，将违反一次性 Passkey 约束。 |
| C-026 | 待确认 | PASSIVE 配置持久化失败 | enable/disable/sensitivity/quota 的成功状态应与可恢复持久化状态一致。 | 多处忽略 `phone_storage_set_passive_*()` 返回值并直接回复 SUCCESS。 | 写失败时本次 RAM 状态与重启后状态不一致。 |

## 5. 已确认项详情

### C-001：GATT 只配置了 Write Without Response

**正式结论：当前 GATT 的 APP→BG24 RX 特征值只配置了 `Write Without Response`。**

V1.2 第 4 章规定 RX 为 `Write With Response`。当前配置位于 `config/btconf/gatt_configuration.btconf`：

```xml
<write_no_response authenticated="false" bonded="false" encrypted="false"/>
```

本结论不自动决定修复方向。后续逐项确认时再决定：

1. 将 GATT 增加/改为 `write`，使代码服从协议；或
2. 正式修订协议，明确允许 `Write Without Response`，并补充 APP 侧发送可靠性、流控和错误恢复要求。

## 6. V1.2 已明确对齐的部分

以下内容从静态实现上与 V1.2 基本一致，但仍需要 APP+BG24 真机互操作验证：

| 项目 | 对齐情况 | 主要证据 |
|---|---|---|
| 自定义 GATT 服务与 TX Notify | 服务 UUID、RX/TX 特征 UUID 和 TX Notify 已配置；仅 RX 写属性存在 C-001。 | `config/btconf/gatt_configuration.btconf` |
| 固定系统服务 | HID、Battery、Device Information、Generic Attribute/GATT caching 均在固定 GATT 数据库中。 | `config/btconf/gatt_configuration.btconf` |
| 线协议版本 | Frame、protocolVersion、advVersion 仍使用 `0x11`。 | `user_phone/phone_cfg.h`、`user_phone/data/phone_adv.c` |
| MTU 门槛 | 目标 247，绑定最低 219，控制/状态最低 194。 | `user_phone/phone_cfg.h:245` 附近、`phone_sm.c:1663` |
| PASSIVE 命令/TLV/能力 | 0x60～0x66、0x26～0x28、capability `0x3F` 已实现。 | `user_phone/phone_cfg.h` |
| 默认安全配置 | 默认 Non-Bondable；READY 配置 SC Only、MITM、DisplayOnly、Bonding Required。 | `user_phone/link/phone_link.c:259`、`phone_sm.c:4762` |
| 绑定后再 ENABLE | Bond 成功只提交授权关联，不直接开启 Passive；PASSIVE_ENABLE 再做授权 Bond 校验。 | `phone_sm.c:4990`、`phone_sm.c:5081` |
| 灵敏度三档 | 只接受 Near/Standard/Far，回复回显最新档位。 | `phone_sm.c:5269` |
| 额度刷新 | 将额度重置为默认 N，不是累加，未在该 handler 中重置解锁 re-arm/rate-limit。 | `phone_sm.c:5335` |
| 启动恢复 | Passive 开启且授权 Bond、L4、Key Size 校验通过后才恢复 HID runtime。 | `phone_sm.c:4088` |

## 7. 协议自身矛盾或缺失

这些问题需要先修改/澄清协议或上层系统需求，不能直接判定代码错误。

| ID | 状态 | 协议问题 | 影响 | 建议补充 |
|---|---|---|---|---|
| P-001 | 协议待澄清 | capability 表和第 23 章已定义 bit5=Passive，但紧邻能力表的说明仍写“本次修订不新增 capabilityFlags，bit5～31 保留”。 | APP 可能按不同段落得到 `0x1F` 或 `0x3F`。 | 删除旧句，明确 V1.2 唯一值与未知位处理规则。 |
| P-002 | 协议待澄清 | tokenBody 最大 320 Byte 与“单帧 + MTU 247”无法同时满足。 | APP 无法构造覆盖最大边界的合法 QR_VERIFY。 | 三选一：缩小 tokenBody 上限、恢复应用层分片、改用其他承载方式。 |
| P-003 | 协议待澄清 | 文档名和版本为“V1.2 正式版”，但文档状态仍写“待总控登记，F2/F3 后进入统一审查/接口冻结”。 | 项目成员可能不清楚它是当前开发基线还是待冻结稿。 | 在首页写明当前效力、审批人、冻结日期和被替代版本。 |
| P-004 | 协议待澄清 | 未定义 GATT 通信距离与“允许绑定距离”的独立指标，也未定义 RSSI、Tx Power、天线姿态、持续时间或车辆条件的绑定近场判据。 | 当前 APP 上看起来 GATT 可连接距离≈可发起绑定距离；无法验收“寻车通信更远、绑定更近”。 | 放到系统/蓝牙 RF 需求中定义两条边界及测试场景；APP 协议只承载结果和错误码。 |
| P-005 | 协议待澄清 | 原子绑定只描述结果，没有定义 NVM 提交标记、启动恢复、换绑断电点和旧记录回滚验收矩阵。 | 开发方可能把“逐条同步写”误认为“整记录原子”。 | 增加断电点、提交标志、恢复优先级和旧绑定保留的可测试准则。 |
| P-006 | 协议待澄清 | 量产要求提到 Company ID 和安全材料，但没有完整的 release profile。 | 调试日志、固定 PIN、UART 写入口可能被带入量产。 | 增加量产构建表：日志宏、UART 命令、固定 PIN、Secure Debug、Company ID、密钥存储。 |

## 8. 建议确认顺序

建议仍然一个一个确认，不并行修改代码：

1. C-002：Response Flags（影响所有响应帧，范围明确）。
2. C-003：AUTH 两步字段（决定 APP 与 BG24 能否互通）。
3. C-004：AUTH 10 秒还是 5 秒。
4. C-023：PAIR_READY 是否允许主动断开并在新连接配对。
5. C-007：车辆控制最终状态闭环。
6. C-008～C-010：幂等与 QR 安全边界。
7. C-012～C-014：绑定记录、Counter、SILENT 的断电恢复。
8. C-015～C-026：输入严格性、状态初值、量产安全与 Passive 持久化。
9. P-001～P-006：协议内部问题和系统级缺失。

在某一项得到明确结论前，不根据该项修改 GATT、帧协议、绑定认证、车辆控制或 Passive 代码。
