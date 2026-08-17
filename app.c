/***************************************************************************//**
 * @file
 * @brief CS Initiator application entry (delegates ranging to cs_key_rang module)
 *
 * APP_KEY_ENABLE  = 1 → 启用钥匙相关全部功能 (扫描/连接/协议/CS测距/PEPS)
 *                 = 0 → 关闭钥匙功能, 仅保留手机通信 + CAN 桥接
 * APP_CS_ENABLE   = 1 → 恢复 CS 测距相关逻辑 (依赖 APP_KEY_ENABLE)
 ******************************************************************************/
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "sl_bluetooth.h"
#include "sl_component_catalog.h"
#include "sl_string.h"
#include "app_assert.h"
#include "app.h"
#include "trace.h"
#include "user_log_console.h"
#include "app_config.h"

#include "ble_peer_manager_central.h"
#include "ble_peer_manager_connections.h"
#include "ble_peer_manager_filter.h"

/* ====== 钥匙功能总开关 见 app_config.h ====== */
#ifndef APP_CS_ENABLE
#define APP_CS_ENABLE 1
#endif

#if APP_KEY_ENABLE
#include "cs_key_rang/cs_key_rang.h"
#include "cs_key_rang/cs_key_rang_config.h"
#include "cs_key_rang/cs_key_console.h"
#include "key_connect/key_connect.h"
#include "key_connect/key_gatt_comm.h"
#include "key_connect/key_gatt_cmd.h"
#include "user_app_fun/user_app_key_peps.h"
#endif

#include "user_console.h"
#include "user_phone/phone_comm.h"
#include "user_phone/data/phone_storage.h"
#include "user_hid/hid_service.h"
#include "user_voltage.h"
#include "user_can_common/RteSys.h"
#include "user_can_common/CanManage/CanManage.h"
#include "user_can_common/CanTransciever/CanTransciever.h"
#include "user_can_common/CanMatrix/CanMatrix.h"
#include "user_can_driver/TCAN_Driver.h"
#include "user_app_fun/user_app_fun.h"

/* 统一 EEPROM 存储 (NVM3 驱动, 上电预加载 + 异步写队列) */
#include "user_eeprom/user_eeprom.h"
#include "user_eeprom/user_eeprom_driver_nvm3.h"
#include "user_vin.h"
#include "user_vehicle_state.h"



uint8_t app_key_conn_handle = SL_BT_INVALID_CONNECTION_HANDLE;
 
// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------

void app_init(void)
{
  /* ---- 第一优先级: 统一 EEPROM 存储 (上电预加载全部条目到 RAM) ---- */
  user_eeprom_driver_register(&g_eeprom_driver_nvm3);
  user_eeprom_init();

  trace_init();
  user_log_console_init();
  user_log_console_set_command_handler(app_user_log_on_command, NULL);

#if APP_KEY_ENABLE
  key_gatt_proto_init();
#if APP_CS_ENABLE
  cs_key_rang_init();
  // GATT 就绪后兜底触发 CS 能力读取；主路径为 cs_key_rang 内 connection_parameters 事件
  key_gatt_comm_set_ready_callback(cs_key_rang_trigger_capabilities_read);
#endif
#endif /* APP_KEY_ENABLE */

  phone_comm_init();

  /* V1.2: HID 无感上电恢复 — 持久化 passive_enabled 为 ON 时恢复 HID 广播,
   * 复用已存 Bond 供手机 OS 后台自动回连 (不新增额度/不提前解除静默) */
  {
    bool passive_on = false;
    (void)phone_storage_get_passive_enabled(&passive_on);
    hid_service_set_runtime_enabled(passive_on);
  }

  /* HID 调试 PIN 上电恢复 (串口 hid_pin 写入 EE, 断电保持) */
  {
    uint32_t pin = 0;
    if (user_eeprom_read(EEPROM_HID_CFG_PIN, &pin, sizeof(pin)) == SL_STATUS_OK
        && pin >= 100000UL && pin <= 999999UL) {
      hid_service_set_pin(pin);
    }
  }

  /* V1.2: VIN 状态机 + 车辆状态抽象层 (在 phone_storage 之后, CAN 之前) */
  vehicle_state_init();
  user_vin_init();

  // 电压测量 + CAN 通信初始化
  UserVoltage_Init();
  RteSys_Init();      /* 启动 SysTick 1ms — CAN 栈全部定时器基准 */
  CanMatrix_Init();   /* 初始化 BLE 报文表 + RTE */

  // 应用层桥接 (CAN ↔ BLE 模块), 内部注册 Key 回调
  user_app_fun_init();
}

void app_process_action(void)
{
  user_log_console_process();

#if APP_KEY_ENABLE
  key_connect_process_action();
  key_gatt_comm_process_action();
  key_gatt_proto_process_action();
#if APP_CS_ENABLE
  cs_key_rang_process_action();
#endif
#endif /* APP_KEY_ENABLE */

  /* === CAN 优先: 确保周期性 CAN 发送不被 phone_comm 加密阻塞 === */
  /* ── RX 桥接: TCAN4550 ISR 缓冲 → CAN 栈 CanIf FIFO ── */
  TCAN_PollMain();  /* 后备 RX/TX/BusOff 硬件轮询 */

  while (TCAN_CheckIfReceive())
  {
      uint16_t rx_id;
      uint8_t  rx_buf[8];
      uint8_t  rx_len = 0;
      if (TCAN_ReadReceivedFrame(&rx_id, rx_buf, &rx_len) != 0)
      {
          CanTransciever_RxMsgHandler(CAN, (uint32_t)rx_id, rx_buf, rx_len);
      }
  }

  /* ── CAN 通信主循环处理 (1ms tick 由 SysTick 硬件中断驱动) ── */
  CanManage_Main();

  /* ── App RX 调度: CanIf App RX FIFO → Matrix handler → RTE ── */
  CanMatrix_RxMsgMain();

  /* ── 应用层桥接 (CAN ↔ BLE): 将上一轮 BLE 状态写入 CAN RTE ── */
  user_app_fun_process();

  /* ── V1.2: VIN 定期比对 + 车辆状态超时检查 ── */
  user_vin_process();
  vehicle_state_process();

  /* ── Phone 通信 (可能在此阻塞进行 ECDSA 验签等加密操作) ── */
  phone_comm_process_action();

  // 异步写队列: 每次主循环处理一个写任务 (NVM 落盘, 约 3~5ms)
  user_eeprom_process(0);
}

void sl_bt_on_event(sl_bt_msg_t *evt)
{
  // ---- 按角色隔离事件：Central(钥匙) ↔ Peripheral(手机) 互不干扰 ----
#if APP_KEY_ENABLE
  // 使用独立变量追踪钥匙连接句柄，避免 peer_manager 回调提前清零 app_key_conn_handle
  static uint8_t key_conn = SL_BT_INVALID_CONNECTION_HANDLE;

  bool to_key   = true;
  bool to_phone = true;

  switch (SL_BT_MSG_ID(evt->header)) {
    case sl_bt_evt_connection_opened_id:
      if (evt->data.evt_connection_opened.role == sl_bt_connection_role_peripheral) {
        to_key = false;
      } else {
        to_phone = false;
        key_conn = evt->data.evt_connection_opened.connection;
      }
      break;

    case sl_bt_evt_connection_closed_id:
      if (evt->data.evt_connection_closed.connection == key_conn) {
        to_phone = false;
        key_conn = SL_BT_INVALID_CONNECTION_HANDLE;
      } else {
        to_key = false;
      }
      break;

    case sl_bt_evt_gatt_server_attribute_value_id:
    case sl_bt_evt_gatt_server_characteristic_status_id:
      to_key = false;
      break;

    case sl_bt_evt_gatt_procedure_completed_id:
    case sl_bt_evt_gatt_characteristic_value_id:
    case sl_bt_evt_gatt_service_id:
    case sl_bt_evt_gatt_characteristic_id:
    case sl_bt_evt_gatt_descriptor_id:
    case sl_bt_evt_gatt_descriptor_value_id:
      to_phone = false;
      break;

    case sl_bt_evt_gatt_mtu_exchanged_id:
      if (evt->data.evt_gatt_mtu_exchanged.connection != key_conn)
        to_key = false;
      else
        to_phone = false;
      break;

    case sl_bt_evt_connection_parameters_id:
      if (evt->data.evt_connection_parameters.connection != key_conn)
        to_key = false;
      else
        to_phone = false;
      break;

    default:
      break;
  }

  if (to_key) {
#if APP_CS_ENABLE
    cs_key_rang_on_bt_event(evt);
#endif
    key_connect_on_bt_event(evt);
    key_gatt_comm_on_bt_event(evt);
    key_gatt_proto_on_bt_event(evt);
  }

  if (to_phone) {
    phone_comm_on_bt_event(evt);
    hid_service_on_bt_event(evt);
  }
#else  /* !APP_KEY_ENABLE: 仅手机通信, key_conn 路由不需要 */
  (void)evt;
  phone_comm_on_bt_event(evt);
  hid_service_on_bt_event(evt);
#endif
}

void ble_peer_manager_on_event_initiator(ble_peer_manager_evt_type_t *event)
{
#if APP_KEY_ENABLE
#if APP_CS_ENABLE
  cs_key_rang_on_peer_manager_event(event);
#endif

  switch (event->evt_id) {
    case BLE_PEER_MANAGER_ON_CONN_OPENED_CENTRAL:
      app_key_conn_handle = event->connection_id;
      USER_LOG_INFO("[APP] Reflector connected (handle %u)" USER_LOG_NL,app_key_conn_handle);
      break;

    case BLE_PEER_MANAGER_ON_CONN_CLOSED:
      if (app_key_conn_handle == event->connection_id) {
        USER_LOG_INFO("[APP] Reflector disconnected (handle %u)" USER_LOG_NL, app_key_conn_handle);
        app_key_conn_handle = SL_BT_INVALID_CONNECTION_HANDLE;
        user_app_key_peps_on_disconnected();
      }
      break;

    default:
      break;
  }

  key_connect_on_peer_manager_event(event);
#else
  (void)event;
#endif /* APP_KEY_ENABLE */
}

