/***************************************************************************//**
 * @file user_app_phone_peps.c
 * @brief 手机 PEPS 模块: RSSI 距离 → 迟滞区域判断 → 去抖 → 自动解闭锁
 *
 * 数据流:
 *   phone_rang (卡尔曼滤波距离) → 迟滞区域判定 → 去抖计数(5次)
 *   → zone 跳变检测 → one-shot 自动命令 (+2s冷却)
 *
 * 与 Key PEPS 的区别:
 *   - 只有 RSSI 距离, 无 RTT (手机不支持 CS 测距)
 *   - 卡尔曼滤波已在 phone_rang.c 完成, 无需二次 EMA
 *   - 迟滞间隙更大 (1.5-4m) 适配 BLE 波动
 *   - 6 个阈值可串口标定, EEPROM 持久化
 *
 * 参考:
 *   car_comm_zone_update_with_hysteresis() — 迟滞状态机
 *   user_app_key_peps.c — 模块结构模板
 ******************************************************************************/
#include "user_app_phone_peps.h"
#include "user_phone/phone_comm.h"
#include "user_phone/phone_cfg.h"
#include "user_phone/data/phone_rang.h"
#include "user_vehicle_state.h"
#include "user_eeprom/user_eeprom.h"
#include "user_can_common/CanMatrix/CanMatrix_Cfg.h"
#include "user_log_console.h"
#include "sl_sleeptimer.h"
#include <string.h>

/* ========== 编译期默认阈值 (cm, EEPROM 无效时的回退值) ========== */
/*
 * 迟滞逻辑 (单位 cm):
 *   往外走: 车内(≤300) → 解锁区(>600进入) → 闭锁区(>1000进入) → 无效区(>1500进入)
 *   往里走: 无效区(≤1200回闭锁) → 闭锁区 → 解锁区(≤700回解锁) → 车内(≤150回车内)
 *
 *   阈值映射:
 *     in_enter      = 150   (走近: 解锁区→车内 的进入阈值)
 *     in_exit       = 600   (走远: 车内→解锁区 的离开阈值)
 *     unlock_enter  = 700   (走近: 闭锁区→解锁区 的进入阈值)
 *     unlock_exit   = 1000  (走远: 解锁区→闭锁区 的离开阈值)
 *     invalid_enter = 1500  (走远: 闭锁区→无效区 的离开阈值)
 *     invalid_exit  = 1200  (走近: 无效区→闭锁区 的进入阈值)
 */
#define PHONE_PEPS_DEFAULT_IN_ENTER_CM         150U
#define PHONE_PEPS_DEFAULT_IN_EXIT_CM          600U
#define PHONE_PEPS_DEFAULT_UNLOCK_ENTER_CM     700U
#define PHONE_PEPS_DEFAULT_UNLOCK_EXIT_CM      1000U
#define PHONE_PEPS_DEFAULT_INVALID_ENTER_CM   1500U
#define PHONE_PEPS_DEFAULT_INVALID_EXIT_CM    1200U

/* ========== 三档灵敏度走近解锁距离 ==========
 * 只控制「走近方向」从闭锁区/无效区进入解锁区的边界, 默认值见头文件
 * (近=300 / 标准=700 / 远=850cm, 数值越大越灵敏越远可解锁)。
 * 使用时按 unlock_exit 迟滞钳制。 */
#define PHONE_PEPS_SENS_HYST_GUARD_CM      50U /* 与 unlock_exit 的最小迟滞余量 */

/* ========== 去抖与冷却 =================================== */
#define PHONE_PEPS_DEBOUNCE_COUNT          5     /* 连续 N 次同区域才确认 */
#define PHONE_PEPS_AUTO_CMD_COOLDOWN_MS    2000U /* 自动命令冷却 2 秒 */
#define PHONE_PEPS_DISCONNECT_LOCK_SUSTAIN_MS 5000U /* 断开后需连续断开该时长(5s)才自动闭锁 */

/* 区域名称 (对应 APP_PROTO_ZONE_* 索引 0-4) */
static const char *const g_zone_names[] = {
    "未知", "车内", "解锁区", "闭锁区", "无效区"
};

/* ========== 内部类型 ===================================== */

typedef struct {
    uint16_t in_enter_cm;
    uint16_t in_exit_cm;
    uint16_t unlock_enter_cm;
    uint16_t unlock_exit_cm;
    uint16_t invalid_enter_cm;
    uint16_t invalid_exit_cm;
} phone_peps_zone_thresholds_t;

/* ========== 模块状态 ===================================== */

static phone_peps_zone_thresholds_t g_th;     /* 运行时阈值 */

static uint8_t  g_current_zone;               /* 已确认的区域 (APP_PROTO_ZONE_*) */
static uint8_t  g_pending_zone;               /* 去抖中的候选区域 */
static uint8_t  g_debounce_cnt;               /* 连续同区域计数 */
static bool     g_zone_init_done;             /* 首次区域确认完成 */
static float    g_distance_m;                 /* 当前卡尔曼距离 (m) */
static bool     g_distance_valid;             /* 距离有效标记 */
static uint8_t  g_auto_cmd_pending;           /* one-shot 自动命令 */
static uint32_t g_cmd_cooldown_ms;            /* 冷却截止时间戳 */
static uint32_t g_disconnect_since_ms;        /* 断开持续计时起点 (0=未计时/已触发) */
static int8_t   g_authorized_gate_last;        /* -1=未记录 0=关闭 1=开放 */
static int8_t   g_range_fresh_last;            /* -1=未记录 0=过期 1=新鲜 */

/* 记录每个阈值对是否从 EEPROM 加载 (用于 phone_zone 命令显示来源) */
static bool     g_th_from_eeprom[3];

/* 三档灵敏度走近解锁距离 (cm, 索引 = 协议档位 - 1) 及整体来源标记 */
static uint16_t g_sens_dist[3];
static bool     g_sens_from_eeprom;


/* ========== 内部: 阈值加载 ================================= */

static void thresholds_load(void)
{
    uint8_t buf[4];

    /* ---- 车内区阈值对 ---- */
    if (user_eeprom_is_valid(EEPROM_PHONE_PEPS_ZONE_IN_TH)) {
        (void)user_eeprom_read(EEPROM_PHONE_PEPS_ZONE_IN_TH, buf, sizeof(buf));
        g_th.in_enter_cm = (uint16_t)((uint16_t)buf[0] | ((uint16_t)buf[1] << 8));
        g_th.in_exit_cm  = (uint16_t)((uint16_t)buf[2] | ((uint16_t)buf[3] << 8));
        g_th_from_eeprom[0] = true;
    } else {
        g_th.in_enter_cm = PHONE_PEPS_DEFAULT_IN_ENTER_CM;
        g_th.in_exit_cm  = PHONE_PEPS_DEFAULT_IN_EXIT_CM;
        g_th_from_eeprom[0] = false;
    }

    /* ---- 解锁区阈值对 ---- */
    if (user_eeprom_is_valid(EEPROM_PHONE_PEPS_ZONE_UNLOCK_TH)) {
        (void)user_eeprom_read(EEPROM_PHONE_PEPS_ZONE_UNLOCK_TH, buf, sizeof(buf));
        g_th.unlock_enter_cm = (uint16_t)((uint16_t)buf[0] | ((uint16_t)buf[1] << 8));
        g_th.unlock_exit_cm  = (uint16_t)((uint16_t)buf[2] | ((uint16_t)buf[3] << 8));
        g_th_from_eeprom[1] = true;
    } else {
        g_th.unlock_enter_cm = PHONE_PEPS_DEFAULT_UNLOCK_ENTER_CM;
        g_th.unlock_exit_cm  = PHONE_PEPS_DEFAULT_UNLOCK_EXIT_CM;
        g_th_from_eeprom[1] = false;
    }

    /* ---- 无效区阈值对 ---- */
    if (user_eeprom_is_valid(EEPROM_PHONE_PEPS_ZONE_INVALID_TH)) {
        (void)user_eeprom_read(EEPROM_PHONE_PEPS_ZONE_INVALID_TH, buf, sizeof(buf));
        g_th.invalid_enter_cm = (uint16_t)((uint16_t)buf[0] | ((uint16_t)buf[1] << 8));
        g_th.invalid_exit_cm  = (uint16_t)((uint16_t)buf[2] | ((uint16_t)buf[3] << 8));
        g_th_from_eeprom[2] = true;
    } else {
        g_th.invalid_enter_cm = PHONE_PEPS_DEFAULT_INVALID_ENTER_CM;
        g_th.invalid_exit_cm  = PHONE_PEPS_DEFAULT_INVALID_EXIT_CM;
        g_th_from_eeprom[2] = false;
    }

    USER_LOG_INFO("[PHONE_PEPS] thresholds: in=%u/%u unlock=%u/%u invalid=%u/%u (cm)" USER_LOG_NL,
                  (unsigned)g_th.in_enter_cm, (unsigned)g_th.in_exit_cm,
                  (unsigned)g_th.unlock_enter_cm, (unsigned)g_th.unlock_exit_cm,
                  (unsigned)g_th.invalid_enter_cm, (unsigned)g_th.invalid_exit_cm);
}


/* ========== 内部: 三档灵敏度走近解锁距离加载 ================= */

/* 协议档位 (1=近 2=标准 3=远) → 数组索引 (0/1/2), 非法回退标准 */
static uint16_t sens_dist_level_to_index(uint8_t level)
{
    if (level >= PHONE_PASSIVE_SENS_NEAR && level <= PHONE_PASSIVE_SENS_FAR) {
        return (uint16_t)(level - PHONE_PASSIVE_SENS_NEAR);
    }
    return PHONE_PEPS_SENS_IDX_STANDARD;
}

static void sens_dist_load(void)
{
    uint8_t buf[6];

    if (user_eeprom_is_valid(EEPROM_PHONE_PEPS_SENS_ENTER_TH)) {
        (void)user_eeprom_read(EEPROM_PHONE_PEPS_SENS_ENTER_TH, buf, sizeof(buf));
        g_sens_dist[PHONE_PEPS_SENS_IDX_NEAR]     = (uint16_t)((uint16_t)buf[0] | ((uint16_t)buf[1] << 8));
        g_sens_dist[PHONE_PEPS_SENS_IDX_STANDARD] = (uint16_t)((uint16_t)buf[2] | ((uint16_t)buf[3] << 8));
        g_sens_dist[PHONE_PEPS_SENS_IDX_FAR]      = (uint16_t)((uint16_t)buf[4] | ((uint16_t)buf[5] << 8));
        g_sens_from_eeprom = true;
    } else {
        g_sens_dist[PHONE_PEPS_SENS_IDX_NEAR]     = PHONE_PEPS_DEFAULT_SENS_NEAR_CM;
        g_sens_dist[PHONE_PEPS_SENS_IDX_STANDARD] = PHONE_PEPS_DEFAULT_SENS_STD_CM;
        g_sens_dist[PHONE_PEPS_SENS_IDX_FAR]      = PHONE_PEPS_DEFAULT_SENS_FAR_CM;
        g_sens_from_eeprom = false;
    }

    USER_LOG_INFO("[PHONE_PEPS] sens_dist: near=%u std=%u far=%u (cm)%s" USER_LOG_NL,
                  (unsigned)g_sens_dist[0], (unsigned)g_sens_dist[1], (unsigned)g_sens_dist[2],
                  g_sens_from_eeprom ? " (NVM)" : " (DEFAULT)");
}


/* ========== 内部: 无迟滞区域选择 (首次用) ================== */

static uint8_t zone_pick_no_hysteresis(uint16_t dist_cm, uint16_t sens_enter_cm)
{
    if (dist_cm <= g_th.in_enter_cm) {
        return (uint8_t)APP_PROTO_ZONE_IN_CAR;
    }
    if (dist_cm <= sens_enter_cm) {
        return (uint8_t)APP_PROTO_ZONE_OUTSIDE_UNLOCK;
    }
    if (dist_cm <= g_th.invalid_enter_cm) {
        return (uint8_t)APP_PROTO_ZONE_OUTSIDE_LOCK;
    }
    return (uint8_t)APP_PROTO_ZONE_PARKING_INVALID;
}


/* ========== 内部: 迟滞区域判定 (核心算法) ================== */

static uint8_t zone_with_hysteresis(uint8_t prev_zone, uint16_t dist_cm,
                                     bool zone_init_done)
{
    /* 走近解锁边界 = 当前灵敏度档位的标定距离 (已按 unlock_exit 迟滞钳制) */
    uint16_t sens_enter_cm = user_app_phone_peps_sens_dist_effective();

    if (!zone_init_done) {
        /* 首次确定: 无迟滞, 直接用进入阈值 */
        return zone_pick_no_hysteresis(dist_cm, sens_enter_cm);
    }

    switch (prev_zone) {

    case APP_PROTO_ZONE_IN_CAR:
        /* 只有走远到 > in_exit 才离开车内 */
        if (dist_cm > g_th.in_exit_cm) {
            return (uint8_t)APP_PROTO_ZONE_OUTSIDE_UNLOCK;
        }
        return (uint8_t)APP_PROTO_ZONE_IN_CAR;

    case APP_PROTO_ZONE_OUTSIDE_UNLOCK:
        /* 走近到 ≤ in_enter → 进入车内 */
        if (dist_cm <= g_th.in_enter_cm) {
            return (uint8_t)APP_PROTO_ZONE_IN_CAR;
        }
        /* 走远到 > unlock_exit → 进入闭锁区 */
        if (dist_cm > g_th.unlock_exit_cm) {
            return (uint8_t)APP_PROTO_ZONE_OUTSIDE_LOCK;
        }
        return (uint8_t)APP_PROTO_ZONE_OUTSIDE_UNLOCK;

    case APP_PROTO_ZONE_OUTSIDE_LOCK:
        /* 走近到 ≤ 当前档位灵敏度距离 → 进入解锁区 */
        if (dist_cm <= sens_enter_cm) {
            return (uint8_t)APP_PROTO_ZONE_OUTSIDE_UNLOCK;
        }
        /* 走远到 > invalid_enter → 进入无效区 */
        if (dist_cm > g_th.invalid_enter_cm) {
            return (uint8_t)APP_PROTO_ZONE_PARKING_INVALID;
        }
        return (uint8_t)APP_PROTO_ZONE_OUTSIDE_LOCK;

    case APP_PROTO_ZONE_PARKING_INVALID:
        /* 走近到 ≤ invalid_exit → 回到闭锁区 */
        if (dist_cm <= g_th.invalid_exit_cm) {
            return (uint8_t)APP_PROTO_ZONE_OUTSIDE_LOCK;
        }
        return (uint8_t)APP_PROTO_ZONE_PARKING_INVALID;

    default: /* DISCONNECTED_UNKNOWN */
        return zone_pick_no_hysteresis(dist_cm, sens_enter_cm);
    }
}


/* ========== 内部: 自动解闭锁命令检测 ====================== */

static uint8_t detect_auto_cmd(uint8_t prev_zone, uint8_t new_zone)
{
    /* 从闭锁/无效区 → 解锁区: 自动解锁 (闭锁改为蓝牙断开时触发, 见 process) */
    if ((prev_zone == (uint8_t)APP_PROTO_ZONE_OUTSIDE_LOCK
      || prev_zone == (uint8_t)APP_PROTO_ZONE_PARKING_INVALID)
     && (new_zone == (uint8_t)APP_PROTO_ZONE_OUTSIDE_UNLOCK
      || new_zone == (uint8_t)APP_PROTO_ZONE_IN_CAR)) {
        return (uint8_t)APP_PROTO_REMOTE_CMD_UNLOCK;
    }

    return 0;
}


/* ========== 内部: EEPROM 条目映射 ========================== */

static user_eeprom_item_t threshold_idx_to_eeprom_item(uint8_t index)
{
    switch (index) {
    case 0: /* fall through */
    case 1:  return EEPROM_PHONE_PEPS_ZONE_IN_TH;
    case 2: /* fall through */
    case 3:  return EEPROM_PHONE_PEPS_ZONE_UNLOCK_TH;
    case 4: /* fall through */
    case 5:  return EEPROM_PHONE_PEPS_ZONE_INVALID_TH;
    default: return EEPROM_PHONE_PEPS_ZONE_IN_TH;  /* 不应该到达 */
    }
}

/* 返回 index 在 4 字节条目中的偏移: 偶数 index→offset 0, 奇数 index→offset 2 */
static uint8_t threshold_idx_to_offset(uint8_t index)
{
    return (index & 1U) ? 2U : 0U;
}


/* ========== Public API =================================== */

void user_app_phone_peps_init(void)
{
    /* 加载阈值 (EEPROM 有效则用 EEPROM, 否则用默认值) */
    thresholds_load();

    /* 加载三档灵敏度走近解锁距离 (EEPROM 有效则用 EEPROM, 否则用默认值) */
    sens_dist_load();

    /* 初始化状态 */
    g_current_zone     = (uint8_t)APP_PROTO_ZONE_DISCONNECTED_UNKNOWN;
    g_pending_zone     = (uint8_t)APP_PROTO_ZONE_DISCONNECTED_UNKNOWN;
    g_debounce_cnt     = 0;
    g_zone_init_done   = false;
    g_distance_m       = 0.0f;
    g_distance_valid   = false;
    g_auto_cmd_pending = 0;
    g_cmd_cooldown_ms  = 0;
    g_disconnect_since_ms = 0;
    g_authorized_gate_last = -1;
    g_range_fresh_last = -1;

    USER_LOG_INFO("[PHONE_PEPS] init done (debounce=%d cooldown=%ums)" USER_LOG_NL,
                  PHONE_PEPS_DEBOUNCE_COUNT, (int)PHONE_PEPS_AUTO_CMD_COOLDOWN_MS);
}


void user_app_phone_peps_process(void)
{
    bool     passive_on;
    bool     authorized_link;
    bool     authorized_disconnected;
    bool     dist_valid;
    uint16_t dist_cm;
    uint8_t  raw_zone;
    uint8_t  lock_state;
    bool     silent;
    uint8_t  door_status;

    /* CR008-009/010: 区域判断和自动落锁共享授权 Passive L4 事实。 */
    passive_on    = phone_comm_is_passive_enabled();
    authorized_link = phone_comm_current_link_is_authorized_passive();
    authorized_disconnected =
        phone_comm_consume_authorized_passive_disconnect();
    dist_valid    = phone_rang_is_valid();
    lock_state = phone_comm_get_vehicle_lock_state();
    silent = phone_comm_is_silent();
    door_status = vehicle_state_get_door_status();

    if ((int8_t)(authorized_link ? 1 : 0) != g_authorized_gate_last) {
        g_authorized_gate_last = (int8_t)(authorized_link ? 1 : 0);
        if (authorized_link) {
            /* 禁止复用同一连接在 L1/L2 阶段提前采集的RSSI；授权门控开放后
             * 必须等待本连接在L4阶段产生新的测距样本。 */
            phone_rang_reset();
            dist_valid = false;
        }
        USER_LOG_INFO("[PHONE_PEPS] CR008-009 gate=%s passive=%s bond=0x%02X sec=L%u"
                      USER_LOG_NL,
                      authorized_link ? "OPEN" : "CLOSED",
                      passive_on ? "ON" : "OFF",
                      (unsigned)phone_comm_current_bonding_handle_get(),
                      (unsigned)phone_comm_current_security_mode_get() + 1U);
    }

    /* ---- 非授权 Passive L4 链路: 输出未知, 重置所有区域状态 ---- */
    if (!authorized_link) {
        g_range_fresh_last = -1;
        g_current_zone     = (uint8_t)APP_PROTO_ZONE_DISCONNECTED_UNKNOWN;
        g_pending_zone     = (uint8_t)APP_PROTO_ZONE_DISCONNECTED_UNKNOWN;
        g_debounce_cnt     = 0;
        g_zone_init_done   = false;
        g_distance_valid   = false;
        g_distance_m       = 0.0f;
        g_auto_cmd_pending = 0;
        g_cmd_cooldown_ms  = 0;

        /* CR008-010: 只有断开前确认的授权 Passive L4 链路才能启动计时。
         * 未授权连接既不能启动，也不能取消已有的授权断连计时。 */
        if (!passive_on) {
            g_disconnect_since_ms = 0U;
        } else {
            uint64_t now_ms = sl_sleeptimer_tick_to_ms(
                                sl_sleeptimer_get_tick_count64());

            /* CR009-001: 断连闭锁门控 — 非静默且门/尾门全关才允许启动 */
            if (authorized_disconnected
                && g_disconnect_since_ms == 0U
                && lock_state != (uint8_t)PHONE_LOCK_STATE_LOCKED
                && silent == false
                && door_status == 0U) {
                g_disconnect_since_ms = (uint32_t)now_ms;
                USER_LOG_INFO("[PHONE_PEPS] CR008-010 authorized disconnect timer start"
                              USER_LOG_NL);
            } else if (authorized_disconnected) {
                USER_LOG_INFO("[PHONE_PEPS] CR009-001 disconnect lock skipped" USER_LOG_NL);
            }

            if (g_disconnect_since_ms != 0U) {
                /* CR009-001: 静默开始或门/尾门打开也取消计时 */
                if (lock_state == (uint8_t)PHONE_LOCK_STATE_LOCKED
                    || silent == true
                    || door_status != 0U) {
                    USER_LOG_INFO("[PHONE_PEPS] CR008-010 authorized disconnect timer cancel: condition changed"
                                  USER_LOG_NL);
                    g_disconnect_since_ms = 0U;
                } else if (((uint32_t)now_ms - g_disconnect_since_ms)
                           >= PHONE_PEPS_DISCONNECT_LOCK_SUSTAIN_MS) {
                    g_auto_cmd_pending = (uint8_t)APP_PROTO_REMOTE_CMD_LOCK;
                    g_disconnect_since_ms = 0U;
                    USER_LOG_INFO("[PHONE_PEPS] AUTO LOCK (authorized link disconnected %ums)"
                                  USER_LOG_NL,
                                  (unsigned)PHONE_PEPS_DISCONNECT_LOCK_SUSTAIN_MS);
                }
            }
        }
        return;
    }

    /* 只有授权 Passive L4 链路恢复才能取消待执行的自动闭锁。 */
    if (g_disconnect_since_ms != 0U) {
        USER_LOG_INFO("[PHONE_PEPS] CR008-010 authorized reconnect → lock timer cancel"
                      USER_LOG_NL);
        g_disconnect_since_ms = 0U;
    }

    if ((int8_t)(dist_valid ? 1 : 0) != g_range_fresh_last) {
        g_range_fresh_last = (int8_t)(dist_valid ? 1 : 0);
        USER_LOG_INFO("[PHONE_PEPS] CR008-009 range=%s timeout=%ums"
                      USER_LOG_NL,
                      dist_valid ? "FRESH" : "STALE",
                      (unsigned)PHONE_RANG_FRESH_TIMEOUT_MS);
    }

    /* ---- 当前连接尚无新鲜距离或已超时: 保持 zone, 不触发任何命令 ---- */
    if (!dist_valid) {
        g_distance_valid = false;
        return;
    }

    /* ---- 获取卡尔曼滤波距离 (phone_rang 已做 Kalman, 无需二次 EMA) ---- */
    {
        float dist_m_f;
        if (!phone_rang_get_distance(&dist_m_f)) {
            return;
        }
        g_distance_m = dist_m_f;
        dist_cm = (uint16_t)(dist_m_f * 100.0f + 0.5f);
    }
    g_distance_valid = true;

    /* ---- 迟滞区域判定 ---- */
    raw_zone = zone_with_hysteresis(g_current_zone, dist_cm, g_zone_init_done);

    /* ---- 计数去抖 ---- */
    if (raw_zone == g_pending_zone) {
        g_debounce_cnt++;
    } else {
        g_pending_zone = raw_zone;
        g_debounce_cnt = 1;
    }

    /* ---- 确认区域切换 ---- */
    if (g_debounce_cnt >= PHONE_PEPS_DEBOUNCE_COUNT) {

        if (g_pending_zone != g_current_zone) {

            USER_LOG_INFO("[PHONE_PEPS] %s -> %s (dist=%u cm)" USER_LOG_NL,
                          g_zone_names[g_current_zone],
                          g_zone_names[g_pending_zone],
                          (unsigned)(g_distance_m * 100.0f + 0.5f));

            /* 自动解闭锁命令检测 (带冷却) */
            {
                uint64_t now_ms = sl_sleeptimer_tick_to_ms(
                                    sl_sleeptimer_get_tick_count64());
                if ((uint32_t)now_ms >= g_cmd_cooldown_ms) {
                    uint8_t cmd = detect_auto_cmd(g_current_zone, g_pending_zone);
                    /* V1.2: 自动解锁前置 — 当前已解锁则不重复解锁 */
                    if (cmd == (uint8_t)APP_PROTO_REMOTE_CMD_UNLOCK
                        && lock_state == (uint8_t)PHONE_LOCK_STATE_UNLOCKED) {
                        cmd = 0;
                        USER_LOG_INFO("[PHONE_PEPS] AUTO UNLOCK skipped: already unlocked" USER_LOG_NL);
                    }
                    /* CR009-001: 静默期阻止自动解锁 */
                    if (cmd == (uint8_t)APP_PROTO_REMOTE_CMD_UNLOCK
                        && silent == true) {
                        cmd = 0;
                        USER_LOG_INFO("[PHONE_PEPS] CR009-001 AUTO UNLOCK skipped: silent" USER_LOG_NL);
                    }
                    /* V1.2: 自动解锁消耗额度, 额度耗尽则阻止自动解锁 */
                    if (cmd == (uint8_t)APP_PROTO_REMOTE_CMD_UNLOCK
                        && !phone_comm_consume_passive_quota()) {
                        cmd = 0;
                        USER_LOG_INFO("[PHONE_PEPS] AUTO UNLOCK blocked: quota exhausted" USER_LOG_NL);
                    }
                    if (cmd != 0) {
                        g_auto_cmd_pending = cmd;
                        g_cmd_cooldown_ms  = (uint32_t)now_ms
                                           + PHONE_PEPS_AUTO_CMD_COOLDOWN_MS;

                        USER_LOG_INFO("[PHONE_PEPS] AUTO UNLOCK (cooldown %ums)" USER_LOG_NL,
                                      PHONE_PEPS_AUTO_CMD_COOLDOWN_MS);
                    }
                }
            }

            g_current_zone = g_pending_zone;
        }

        if (!g_zone_init_done) {
            g_zone_init_done = true;
            USER_LOG_INFO("[PHONE_PEPS] first zone=%s (dist=%u cm)" USER_LOG_NL,
                          g_zone_names[g_current_zone],
                          (unsigned)(g_distance_m * 100.0f + 0.5f));
        }
    }
}


uint8_t user_app_phone_peps_get_zone(void)
{
    return g_current_zone;
}


uint8_t user_app_phone_peps_get_auto_cmd(void)
{
    uint8_t cmd = g_auto_cmd_pending;
    g_auto_cmd_pending = 0;
    return cmd;
}


float user_app_phone_peps_get_distance(void)
{
    return g_distance_m;
}


bool user_app_phone_peps_is_distance_valid(void)
{
    return g_distance_valid;
}


void user_app_phone_peps_on_disconnected(void)
{
    g_current_zone     = (uint8_t)APP_PROTO_ZONE_DISCONNECTED_UNKNOWN;
    g_pending_zone     = (uint8_t)APP_PROTO_ZONE_DISCONNECTED_UNKNOWN;
    g_debounce_cnt     = 0;
    g_zone_init_done   = false;
    g_distance_valid   = false;
    g_distance_m       = 0.0f;
    g_auto_cmd_pending = 0;
    g_cmd_cooldown_ms  = 0;

    USER_LOG_DEBUG("[PHONE_PEPS] disconnected" USER_LOG_NL);
}


/* ========== 阈值管理 API ================================== */

void user_app_phone_peps_threshold_set(uint8_t index, uint16_t value_cm)
{
    /* 1. 更新 RAM */
    switch (index) {
    case PHONE_PEPS_TH_IDX_IN_ENTER:        g_th.in_enter_cm       = value_cm; break;
    case PHONE_PEPS_TH_IDX_IN_EXIT:         g_th.in_exit_cm        = value_cm; break;
    case PHONE_PEPS_TH_IDX_UNLOCK_ENTER:    g_th.unlock_enter_cm   = value_cm; break;
    case PHONE_PEPS_TH_IDX_UNLOCK_EXIT:     g_th.unlock_exit_cm    = value_cm; break;
    case PHONE_PEPS_TH_IDX_INVALID_ENTER:   g_th.invalid_enter_cm  = value_cm; break;
    case PHONE_PEPS_TH_IDX_INVALID_EXIT:    g_th.invalid_exit_cm   = value_cm; break;
    default: return;
    }

    /* 2. 写入 EEPROM: 读出所在条目 → 修改 → 写回 */
    {
        user_eeprom_item_t item   = threshold_idx_to_eeprom_item(index);
        uint8_t            offset = threshold_idx_to_offset(index);
        uint8_t            buf[4];

        /* 先读取当前条目内容 (如果有效), 否则用零填充 */
        if (user_eeprom_is_valid(item)) {
            (void)user_eeprom_read(item, buf, sizeof(buf));
        } else {
            memset(buf, 0, sizeof(buf));
        }

        /* 修改对应字段 (小端序) */
        buf[offset]     = (uint8_t)(value_cm & 0xFFU);
        buf[offset + 1] = (uint8_t)((value_cm >> 8) & 0xFFU);

        /* 异步写回 (不关心回调) */
        (void)user_eeprom_write(item, buf, sizeof(buf), NULL);

        /* 标记对应对为来自 EEPROM */
        g_th_from_eeprom[index / 2U] = true;
    }
}


uint16_t user_app_phone_peps_threshold_get(uint8_t index)
{
    switch (index) {
    case PHONE_PEPS_TH_IDX_IN_ENTER:        return g_th.in_enter_cm;
    case PHONE_PEPS_TH_IDX_IN_EXIT:         return g_th.in_exit_cm;
    case PHONE_PEPS_TH_IDX_UNLOCK_ENTER:    return g_th.unlock_enter_cm;
    case PHONE_PEPS_TH_IDX_UNLOCK_EXIT:     return g_th.unlock_exit_cm;
    case PHONE_PEPS_TH_IDX_INVALID_ENTER:   return g_th.invalid_enter_cm;
    case PHONE_PEPS_TH_IDX_INVALID_EXIT:    return g_th.invalid_exit_cm;
    default: return 0;
    }
}


bool user_app_phone_peps_threshold_is_from_eeprom(uint8_t index)
{
    if (index > PHONE_PEPS_TH_IDX_INVALID_EXIT) return false;
    return g_th_from_eeprom[index / 2U];
}


void user_app_phone_peps_threshold_reset(void)
{
    /* 重置 RAM */
    g_th.in_enter_cm      = PHONE_PEPS_DEFAULT_IN_ENTER_CM;
    g_th.in_exit_cm       = PHONE_PEPS_DEFAULT_IN_EXIT_CM;
    g_th.unlock_enter_cm  = PHONE_PEPS_DEFAULT_UNLOCK_ENTER_CM;
    g_th.unlock_exit_cm   = PHONE_PEPS_DEFAULT_UNLOCK_EXIT_CM;
    g_th.invalid_enter_cm = PHONE_PEPS_DEFAULT_INVALID_ENTER_CM;
    g_th.invalid_exit_cm  = PHONE_PEPS_DEFAULT_INVALID_EXIT_CM;

    /* 清除 EEPROM */
    (void)user_eeprom_delete(EEPROM_PHONE_PEPS_ZONE_IN_TH);
    (void)user_eeprom_delete(EEPROM_PHONE_PEPS_ZONE_UNLOCK_TH);
    (void)user_eeprom_delete(EEPROM_PHONE_PEPS_ZONE_INVALID_TH);

    g_th_from_eeprom[0] = false;
    g_th_from_eeprom[1] = false;
    g_th_from_eeprom[2] = false;

    USER_LOG_INFO("[PHONE_PEPS] thresholds reset to defaults" USER_LOG_NL);
}


void user_app_phone_peps_threshold_reload(void)
{
    thresholds_load();
    USER_LOG_INFO("[PHONE_PEPS] thresholds reloaded from EEPROM" USER_LOG_NL);
}


/* ========== 三档灵敏度走近解锁距离管理 API ==================== */

void user_app_phone_peps_sens_dist_set(uint8_t level, uint16_t value_cm)
{
    uint16_t idx = sens_dist_level_to_index(level);
    uint8_t  buf[6];

    /* 1. 更新 RAM */
    g_sens_dist[idx] = value_cm;

    /* 2. 写入 EEPROM: 读出所在条目 → 修改 → 写回 (小端) */
    if (user_eeprom_is_valid(EEPROM_PHONE_PEPS_SENS_ENTER_TH)) {
        (void)user_eeprom_read(EEPROM_PHONE_PEPS_SENS_ENTER_TH, buf, sizeof(buf));
    } else {
        memset(buf, 0, sizeof(buf));
    }
    buf[idx * 2U]      = (uint8_t)(value_cm & 0xFFU);
    buf[idx * 2U + 1U] = (uint8_t)((value_cm >> 8) & 0xFFU);

    /* 异步写回 (不关心回调) */
    (void)user_eeprom_write(EEPROM_PHONE_PEPS_SENS_ENTER_TH, buf, sizeof(buf), NULL);

    g_sens_from_eeprom = true;
}

uint16_t user_app_phone_peps_sens_dist_get(uint8_t level)
{
    return g_sens_dist[sens_dist_level_to_index(level)];
}

void user_app_phone_peps_sens_dist_reset(void)
{
    g_sens_dist[PHONE_PEPS_SENS_IDX_NEAR]     = PHONE_PEPS_DEFAULT_SENS_NEAR_CM;
    g_sens_dist[PHONE_PEPS_SENS_IDX_STANDARD] = PHONE_PEPS_DEFAULT_SENS_STD_CM;
    g_sens_dist[PHONE_PEPS_SENS_IDX_FAR]      = PHONE_PEPS_DEFAULT_SENS_FAR_CM;

    /* 清除 EEPROM */
    (void)user_eeprom_delete(EEPROM_PHONE_PEPS_SENS_ENTER_TH);

    g_sens_from_eeprom = false;

    USER_LOG_INFO("[PHONE_PEPS] sens_dist reset to defaults (near=%u std=%u far=%u cm)" USER_LOG_NL,
                  (unsigned)g_sens_dist[0], (unsigned)g_sens_dist[1], (unsigned)g_sens_dist[2]);
}

bool user_app_phone_peps_sens_dist_is_from_eeprom(void)
{
    return g_sens_from_eeprom;
}

uint16_t user_app_phone_peps_sens_dist_effective(void)
{
    uint8_t  level = phone_comm_get_passive_sensitivity();
    uint16_t idx   = sens_dist_level_to_index(level);
    uint16_t v     = g_sens_dist[idx];
    uint16_t limit = g_th.unlock_exit_cm;

    /* 钳制: 走近边界恒 < unlock_exit, 杜绝迟滞翻转振荡 */
    if (limit > PHONE_PEPS_SENS_HYST_GUARD_CM) {
        limit -= PHONE_PEPS_SENS_HYST_GUARD_CM;
    } else {
        limit = 10U;
    }
    if (v > limit) {
        v = limit;
    }
    return v;
}
