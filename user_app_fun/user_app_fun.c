/*******************************************************
 * Name    :user_app_fun.c
 * Function:应用层功能衔接 —— CAN ↔ BLE 模块桥接
 *          - CAN RX 诊断命令上升沿检测 → 功能模块触发
 *          - CAN RX 车辆状态读取 → Phone/Key 模块
 *          - BLE 模块状态 (Key/Phone) → CAN TX 信号写入
 *          - 钥匙 RSSI 距离估算 → CAN TX 位置区
 *          - 手机 RSSI 距离估算 → CAN TX 位置区
 *******************************************************/
#include "user_app_fun.h"
#include "app_config.h"
#include "user_can_common/CanMatrix/CanMatrix_Cfg.h"
#include "user_can_common/RteSys.h"
#include "user_log_console.h"

/* 钥匙/手机模块 */
#if APP_KEY_ENABLE
#include "key_connect/key_connect.h"
#include "key_connect/key_gatt_cmd.h"
#include "key_connect/key_gatt_rang.h"
#include "user_app_fun/user_app_key_peps.h"
#include "user_app_fun/user_app_peps.h"
#endif
#include "user_app_fun/user_app_phone_peps.h"
#include "user_phone/phone_comm.h"
#include "user_phone/data/phone_rang.h"
#include "user_vehicle_state.h"
#include "user_vin.h"

#include <string.h>


/*============= 上升沿检测状态 ===========================*/

static struct {
    uint8_t prev_phone_pair;
#if APP_KEY_ENABLE
    uint8_t prev_key_pair;
    uint8_t prev_key_data_reset;
#endif
    bool    init_done;
} g_diag_edge;


/*============= NM 休眠请求 (自动判睡) ====================*/
#define NM_SLEEP_DEBOUNCE_MS  30000U   /* 本地条件持续满足 30s 才请求休眠 */

static uint32_t g_nm_sleep_start_ms = 0;
static bool     g_nm_sleep_req      = false;

/* 本地休眠条件: 无手机/钥匙连接 + 车辆非 IGN/START + 无 PEPS 活动区 */
static bool nm_check_sleep_conditions(void)
{
    if (phone_comm_is_connected()) return false;
#if APP_KEY_ENABLE
    if (key_connect_is_connected()) return false;
#endif
    {
        uint8_t pwr = user_can_rte_read_canSig(RTE_282_BCM_PowerSt);
        if (pwr == 0x02U || pwr == 0x03U) return false;   /* IGN / START */
    }
    {
        uint8_t zone = user_app_phone_peps_get_zone();
        if (zone != (uint8_t)APP_PROTO_ZONE_DISCONNECTED_UNKNOWN
            && zone != (uint8_t)APP_PROTO_ZONE_PARKING_INVALID) return false;
    }
    return true;
}

/* 每主循环调用: 条件满足 30s → 请求休眠; 任一条件失效 → 立即取消请求
 * 手动 can_nm_sleep 闩锁激活时跳过 (串口命令优先) */
static void nm_sleep_process(void)
{
    if (RteSys_GetManualSleepReq()) return;

    if (nm_check_sleep_conditions()) {
        if (g_nm_sleep_req == false) {
            g_nm_sleep_req      = true;
            g_nm_sleep_start_ms = RteSys_GetSysTimeMs();
            USER_LOG_INFO("[NM] sleep cond met, debounce %lu ms" USER_LOG_NL,
                          (unsigned long)NM_SLEEP_DEBOUNCE_MS);
        } else if ((RteSys_GetSysTimeMs() - g_nm_sleep_start_ms) >= NM_SLEEP_DEBOUNCE_MS) {
            if (RteSys_GetLocalSleepFlag() == false) {
                RteSys_SetLocalSleepFlag(true);
                USER_LOG_INFO("[NM] localSleep -> TRUE (request sleep)" USER_LOG_NL);
            }
        }
    } else {
        if (g_nm_sleep_req || RteSys_GetLocalSleepFlag()) {
            USER_LOG_INFO("[NM] sleep cond released -> localSleep FALSE (stay awake)" USER_LOG_NL);
        }
        g_nm_sleep_req = false;
        RteSys_SetLocalSleepFlag(false);
    }
}


#if APP_KEY_ENABLE
/*============= Key 回调缓存 (CAN TX 使用) ================*/

static struct {
    uint8_t  btn1;
    uint8_t  btn2;
    uint8_t  btn3;
    uint16_t battery_mv;
    uint8_t  fault_flags;
    int8_t   remote_rssi;
    bool     has_new_button;
    bool     has_new_status;
} g_key_cache;


/* 按键状态 → 遥控命令: btn1 短按=解锁, btn2 短按=闭锁, btn2 双击=寻车 */
static uint8_t buttons_to_cmd(uint8_t btn1, uint8_t btn2)
{
    if (btn1 == 1) return (uint8_t)APP_PROTO_REMOTE_CMD_UNLOCK;
    if (btn2 == 1) return (uint8_t)APP_PROTO_REMOTE_CMD_LOCK;
    if (btn2 == 2) return (uint8_t)APP_PROTO_REMOTE_CMD_FIND;
    return 0;
}


/*============= Key 模块回调 =============================*/

static void app_fun_on_key_button(uint8_t btn1, uint8_t btn2, uint8_t btn3,
                                   uint16_t battery_mv, uint8_t fault_flags)
{
    g_key_cache.btn1       = btn1;
    g_key_cache.btn2       = btn2;
    g_key_cache.btn3       = btn3;
    g_key_cache.battery_mv = battery_mv;
    g_key_cache.fault_flags = fault_flags;
    g_key_cache.has_new_button = true;

    USER_LOG_DEBUG("[APP_FUN] KeyBtn: %u/%u/%u bat=%umV fault=0x%02X" USER_LOG_NL,
                   btn1, btn2, btn3, battery_mv, fault_flags);
}

static void app_fun_on_key_status(uint16_t battery_mv, int8_t rssi, uint8_t fault_flags)
{
    g_key_cache.battery_mv   = battery_mv;
    g_key_cache.remote_rssi  = rssi;
    g_key_cache.fault_flags  = fault_flags;
    g_key_cache.has_new_status = true;

    /* 远端 RSSI 喂入距离估算模块 */
    key_rssi_rang_feed_remote_rssi(rssi);

    USER_LOG_DEBUG("[APP_FUN] KeyStatus: rssi=%d bat=%umV fault=0x%02X" USER_LOG_NL,
                   rssi, battery_mv, fault_flags);
}
#endif /* APP_KEY_ENABLE */


/*============= 初始化 ===================================*/

void user_app_fun_init(void)
{
    /* 诊断触发沿检测 */
    memset(&g_diag_edge, 0, sizeof(g_diag_edge));
    g_diag_edge.init_done = true;


#if APP_KEY_ENABLE
    /* Key 回调缓存 */
    memset(&g_key_cache, 0, sizeof(g_key_cache));

    /* 注册 Key 协议回调 */
    key_gatt_proto_set_button_callback(app_fun_on_key_button);
    key_gatt_proto_set_status_callback(app_fun_on_key_status);

    /* RSSI 距离估算初始化 */
    key_rssi_rang_init();

    /* PEPS: RTT+RSSI 融合 + zone + 自动解闭锁 */
    user_app_key_peps_init();

    /* 组合 PEPS: Key + Phone 解闭锁命令仲裁 */
    user_app_peps_init();
#endif

    /* 手机 PEPS: RSSI 距离 → 迟滞区域 + 自动解闭锁 (不依赖 Key) */
    user_app_phone_peps_init();

    USER_LOG_INFO("[APP_FUN] init done");
}


/*============= 内部：诊断触发上升沿检测 ===================*/

static bool diag_rising_edge(uint8_t cur, uint8_t *prev)
{
    if (cur != 0U && *prev == 0U) {
        *prev = cur;
        return true;
    }
    *prev = cur;
    return false;
}


/*============= 内部：诊断命令处理 =========================*/

static void handle_phone_pair_trigger(void)
{
    USER_LOG_INFO("[APP_FUN] Diag_PhonePair trigger -> phone_comm_switch_to_secondary");
    phone_comm_switch_to_secondary();
}

#if APP_KEY_ENABLE
static void handle_key_pair_trigger(void)
{
    USER_LOG_INFO("[APP_FUN] Diag_KeyPair trigger -> key_connect_enter_pairing_mode");
    key_connect_enter_pairing_mode();
}

static void handle_key_data_reset_trigger(void)
{
    USER_LOG_INFO("[APP_FUN] Diag_KeyDataReset trigger -> factory reset");
    phone_comm_disconnect_for_factory_reset();
    key_gatt_proto_reset_pairing();
}
#endif /* APP_KEY_ENABLE */


/*============= 内部：BCM 车辆状态 → BLE 桥接 ==============*/

static void bridge_bcm_to_ble(void)
{
    uint8_t power_st  = AppProto_GetVehiclePowerSt();

    /* PEPS/车辆授权条件: VIU_B_BCMPowerSt 驱动
     * 0x2=IGN / 0x3=Crank → 满足, 其他 → 不满足 */
    {
      static uint8_t prev_auth_from_can = 0xFFU;  /* 0xFF 强制首轮生效 */
      bool auth_met = (power_st == 0x2U) || (power_st == 0x3U);
      uint8_t auth_byte = auth_met ? 1U : 0U;
      if (auth_byte != prev_auth_from_can) {
        prev_auth_from_can = auth_byte;
        phone_comm_set_auth_condition(auth_met);
      }
    }

    /* V1.2: 完整车辆状态快照推送 (lock + ignition + range + doors)
     * 从 vehicle_state 模块读取, 任一字段变化则推送完整快照 */
    {
      static uint8_t  prev_lock     = 0xFFU;  /* 0xFF 强制首轮推送 */
      static uint8_t  prev_ignition = 0xFFU;
      static uint16_t prev_range    = 0xFFFEU;
      static uint8_t  prev_doors    = 0xFFU;
      static uint8_t  prev_back     = 0xFFU;

      uint8_t  lock     = vehicle_state_get_final_lock_state();
      uint8_t  raw_ign  = vehicle_state_get_ignition_gear();
      uint16_t range    = vehicle_state_get_remaining_range_km();
      uint8_t  raw_door = vehicle_state_get_door_status();
      uint8_t  back_door = user_can_rte_read_canSig(RTE_282_BCM_BackDoorSt);

      /* 映射 ignition: 底层值+1, 非法值→0x00(UNKNOWN) */
      uint8_t ignition;
      switch (raw_ign) {
        case IGNITION_OFF:   ignition = 0x01U; break;  /* PHONE_IGNITION_STATE_OFF */
        case IGNITION_ACC:   ignition = 0x02U; break;  /* PHONE_IGNITION_STATE_ACC */
        case IGNITION_ON:    ignition = 0x03U; break;  /* PHONE_IGNITION_STATE_ON */
        case IGNITION_START: ignition = 0x04U; break;  /* PHONE_IGNITION_STATE_START */
        default:             ignition = 0x00U; break;  /* UNKNOWN */
      }

      /* 映射 door: 4门 bitmask → 左右简化 */
      uint8_t doors_proto;
      {
        bool left  = (raw_door & (DOOR_FL_MASK | DOOR_RL_MASK)) != 0U;
        bool right = (raw_door & (DOOR_FR_MASK | DOOR_RR_MASK)) != 0U;
        if (left && right)      doors_proto = 0x03U;  /* PHONE_DOOR_STATE_BOTH_OPEN */
        else if (left)          doors_proto = 0x01U;  /* PHONE_DOOR_STATE_LEFT_OPEN */
        else if (right)         doors_proto = 0x02U;  /* PHONE_DOOR_STATE_RIGHT_OPEN */
        else                    doors_proto = 0x00U;  /* PHONE_DOOR_STATE_ALL_CLOSED */
      }

      /* debug: 每 2s 打印当前状态 */
      {
        static uint8_t dbg_cnt = 0;
        if (++dbg_cnt >= 20U) {  /* ~100ms周期, 20次≈2s */
          dbg_cnt = 0U;
        //   USER_LOG_INFO("[BRIDGE] raw_ign=%u ign=%u lock=%u range=%u door_raw=0x%02X door=%u" USER_LOG_NL,
        //                 (unsigned)raw_ign, (unsigned)ignition, (unsigned)lock,
        //                 (unsigned)range, (unsigned)raw_door, (unsigned)doors_proto);
        }
      }

      if (lock != prev_lock || ignition != prev_ignition
          || range != prev_range || doors_proto != prev_doors
          || back_door != prev_back) {
        USER_LOG_INFO("[BRIDGE] CHANGE: lock %u->%u ign %u->%u range %u->%u door %u->%u back %u->%u" USER_LOG_NL,
                      (unsigned)prev_lock, (unsigned)lock,
                      (unsigned)prev_ignition, (unsigned)ignition,
                      (unsigned)prev_range, (unsigned)range,
                      (unsigned)prev_doors, (unsigned)doors_proto,
                      (unsigned)prev_back, (unsigned)back_door);
        prev_lock     = lock;
        prev_ignition = ignition;
        prev_range    = range;
        prev_doors    = doors_proto;
        prev_back     = back_door;
        phone_comm_notify_vehicle_state(lock, ignition, range, doors_proto);
      }
    }
}


/*============= 内部：BLE 模块状态 → CAN TX (0x2BE) ========*/

static void bridge_ble_to_can_tx(void)
{
#if APP_KEY_ENABLE
    /* ---- Key 侧 ---- */
    {
        bool key_conn = key_connect_is_connected();
        AppProto_Key_SetConnected(key_conn);

        /* 遥控命令: btn1 短按=解锁, btn2 短按=闭锁, btn2 双击=寻车 */
        uint8_t cmd = buttons_to_cmd(g_key_cache.btn1, g_key_cache.btn2);
        AppProto_Key_SetCmd(cmd);
        g_key_cache.btn1 = 0;
        g_key_cache.btn2 = 0;
        g_key_cache.has_new_button = false;

        /* PEPS 位置区 (user_app_key_peps → user_app_peps 转发) */
        AppProto_Key_SetPos(user_app_peps_get_zone());

        /* PEPS 自动解闭锁 (组合仲裁, one-shot, 消费后清零) */
        AppProto_Key_SetLockCmd(user_app_peps_get_lock_cmd());

        /* PEPS 融合距离: 1m 精度, 0xFF=断连/无效 */
        /* Key 距离信号已从 V9 Matrix 移除 */
    }
#else
    /* Key 关闭时: 全部输出默认值 */
    AppProto_Key_SetConnected(false);
    AppProto_Key_SetCmd(0);
    AppProto_Key_SetPos(APP_PROTO_ZONE_DISCONNECTED_UNKNOWN);
    AppProto_Key_SetLockCmd(0);
#endif /* APP_KEY_ENABLE */

    /* ---- Phone 侧: 委托 user_app_phone_peps 模块 ---- */
    {
        bool phone_conn = phone_comm_is_connected();
        AppProto_Phone_SetConnected(phone_conn);
        /* 手机自动解闭锁使能: 绑定无感钥匙使能状态 (passive_enabled, NVM 持久化, 上电由 EE 恢复) */
        AppProto_Phone_SetAutoEnable(phone_comm_is_passive_enabled());
        /* TODO: AppProto_Key_SetAutoEnable(key_peps_auto_enabled()); 需要上游提供getter */

        /* 手机远程命令: 通过 CAN 0x2BE 周期发送 (必须每次写入, 含0) */
        AppProto_Phone_SetCmd(phone_comm_get_pending_control_cmd());

        /* 手机位置区 (迟滞+去抖, 由 phone_peps 模块提供) */
        AppProto_Phone_SetPos(user_app_phone_peps_get_zone());

        /* 手机 PEPS 自动解闭锁: 通过 CAN 0x2BE 周期发送 (必须每次写入, 含0) */
        AppProto_Phone_SetLockCmd(user_app_phone_peps_get_auto_cmd());

        /* 手机 RSSI + 距离 → CAN 0x2BE (RSSI: int8 补码 dBm; 距离: dm, 255=无效) */
        {
            int8_t  rssi = 0;
            float   dist_m = 0.0f;
            bool    rssi_ok = phone_rang_get_local_rssi(&rssi);
            bool    dist_ok = phone_rang_get_distance(&dist_m);
            uint8_t dis = 0xFFU;  /* 无效 */

            AppProto_Phone_SetRssi(rssi_ok ? (uint8_t)rssi : 0U);

            if (dist_ok) {
                int32_t dm = (int32_t)(dist_m * 10.0f + 0.5f);
                if (dm < 0)   dm = 0;
                if (dm > 254) dm = 254;
                dis = (uint8_t)dm;
            }
            AppProto_Phone_SetDis(dis);
        }
    }
}


/*============= 主循环 ===================================*/

void user_app_fun_process(void)
{
#if APP_KEY_ENABLE
    /* ---- 1. 钥匙 RSSI 距离估算 (周期获取本端 RSSI) ---- */
    key_rssi_rang_process();

    /* ---- 1.5. Key PEPS: RTT+RSSI 融合 + zone + 自动解闭锁 ---- */
    user_app_key_peps_process();

    /* ---- 1.6. 组合 PEPS: Key + Phone 解闭锁命令仲裁 ---- */
    user_app_peps_process();
#endif

    /* ---- 1.7. Phone PEPS: RSSI 距离 → 迟滞区域 + 自动解闭锁 ---- */
    user_app_phone_peps_process();

    /* ---- 2. 诊断触发上升沿检测 (RX 0x6DE) ---- */
    {
        uint8_t phone_pair = user_can_rte_read_canSig(RTE_6DE_Diag_PhonePair);

        if (g_diag_edge.init_done) {
            if (diag_rising_edge(phone_pair, &g_diag_edge.prev_phone_pair)) {
                handle_phone_pair_trigger();
            }
#if APP_KEY_ENABLE
            {
                uint8_t key_pair       = user_can_rte_read_canSig(RTE_6DE_Diag_KeyPair);
                uint8_t key_data_reset = user_can_rte_read_canSig(RTE_6DE_Diag_KeyDataReset);

                if (diag_rising_edge(key_pair, &g_diag_edge.prev_key_pair)) {
                    handle_key_pair_trigger();
                }
                if (diag_rising_edge(key_data_reset, &g_diag_edge.prev_key_data_reset)) {
                    handle_key_data_reset_trigger();
                }
            }
#endif
        }
    }

    /* ---- 3. V1.2: CAN车辆状态 → vehicle_state (必须先于 bridge_bcm_to_ble) ---- */
    {
        /* VIN 已由 CanMatrix_Rx61A_Handle 直接推入 vehicle_state */

        /* 0x6F2: 剩余续航拼接 */
        uint8_t  range_h = user_can_rte_read_canSig(RTE_VIU_REMAINING_RANGE_H);
        uint8_t  range_l = user_can_rte_read_canSig(RTE_VIU_REMAINING_RANGE_L);
        uint16_t range   = (uint16_t)(((uint16_t)range_h << 8) | (uint16_t)range_l);

        /* 门状态合并: 0x282 FLDoorSt/FRDoorSt + 0x6F2 RLDoor/RRDoor */
        uint8_t doors = 0U;
        if (user_can_rte_read_canSig(RTE_282_BCM_FLDoorSt)) doors |= 0x01U;
        if (user_can_rte_read_canSig(RTE_282_BCM_FRDoorSt)) doors |= 0x02U;
        if (user_can_rte_read_canSig(RTE_VIU_DOOR_RL))      doors |= 0x04U;
        if (user_can_rte_read_canSig(RTE_VIU_DOOR_RR))      doors |= 0x08U;

        /* 锁状态: 0x282 VIU_BCMDriverDoorLockSt → 协议锁状态
         * 0=Lock → LOCKED(0x01), 1=Unlock → UNLOCKED(0x02) */
        uint8_t lock = user_can_rte_read_canSig(RTE_282_BCM_DriverDoorLockSt) ? 0x02U : 0x01U;

        /* 点火: 0x282 PowerSt → 映射到 V1.2 ignition gear */
        uint8_t pwr = user_can_rte_read_canSig(RTE_282_BCM_PowerSt);
        uint8_t ign;
        switch (pwr) {
            case 0x0U: ign = 0U; break;  /* Off */
            case 0x1U: ign = 1U; break;  /* ACC */
            case 0x2U: ign = 2U; break;  /* IGN/ON */
            case 0x3U: ign = 3U; break;  /* Crank/START */
            default:   ign = 0U; break;
        }

        /* PEPS key in car: 0x6F1 handler 已推入 RTE */
        bool peps = user_can_rte_read_canSig(RTE_VIU_PEPS_KEY_IN_CAR) != 0U;

        /* 仅当非 override 模式时同步 CAN 数据到 vehicle_state */
        if (!vehicle_state_is_override_active()) {
            vehicle_state_update_from_can(ign, range, doors, lock, peps);
        }

        /* VIN 关联状态 → CAN TX RTE */
        user_can_rte_write_canSig(RTE_BG24_VIN_ASSOC_STATE, user_vin_get_state());
    }

    /* ---- 4. vehicle_state → BLE (状态变化推送) ---- */
    bridge_bcm_to_ble();

    /* ---- 5. BLE 模块状态 → CAN TX (TX 0x2BE) ---- */
    bridge_ble_to_can_tx();

    /* ---- 6. NM 休眠条件判定 (30s 去抖, 手动闩锁时跳过) ---- */
    nm_sleep_process();
}
