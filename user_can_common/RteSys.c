/*******************************************************
 * Name    :RteSys.c
 * Function:RteSys stub implementation for BLE project
 *******************************************************/
#include "RteSys.h"
#include "em_device.h"

/* SysTick 1ms ISR — 驱动整个 CAN 栈的时间基准 */
extern void CanManage_TimerCtrl(void);
void SysTick_Handler(void)
{
    RteSys_Tick1ms();
    CanManage_TimerCtrl();
}

void RteSys_Init(void)
{
    /* 配置 SysTick 1ms 中断 — CAN 栈全部定时器依赖此 tick */
    if (SysTick_Config(SystemCoreClock / 1000U) != 0) {
        /* 1ms 周期超出 SysTick 24 位范围，降级处理 */
        (void)SysTick_Config(SystemCoreClock / 2000U);
    }
}

static uint32_t s_sysTimeMs   = 0;
static bool     s_boolSigs[8] = {false};
static uint8_t  s_u8Sigs[8]   = {0};
static bool     s_localSleep     = false;
static bool     s_manualSleepReq = false;  /* 串口 can_nm_sleep/wake 手动闩锁, 优先于自动判睡 */
static bool     s_canSleep       = false;
static bool     s_dtcDeactive    = false;

/* Called from SysTick or equivalent to advance time base */
void RteSys_Tick1ms(void)
{
    s_sysTimeMs++;
}

uint32_t RteSys_GetSysTimeMs(void)
{
    return s_sysTimeMs;
}

bool RteSys_GetBoolSig(uint8_t sigId)
{
    if (sigId < 8) return s_boolSigs[sigId];
    return false;
}

void RteSys_SetBoolSig(uint8_t sigId, bool val)
{
    if (sigId < 8) s_boolSigs[sigId] = val;
}

uint8_t RteSys_GetU8Sig(uint8_t sigId)
{
    if (sigId < 8) return s_u8Sigs[sigId];
    return 0;
}

void RteSys_SetU8Sig(uint8_t sigId, uint8_t val)
{
    if (sigId < 8) s_u8Sigs[sigId] = val;
}

bool RteSys_GetLocalSleepFlag(void)
{
    /* 手动闩锁优先: 串口 can_nm_sleep 强制休眠时不受自动判睡影响 */
    return (s_manualSleepReq || s_localSleep);
}

/* 自动判睡 (nm_sleep_process) 写入本地休眠请求 */
void RteSys_SetLocalSleepFlag(bool flag)
{
    s_localSleep = flag;
}

bool RteSys_GetCanSleepFlag(void)
{
    return s_canSleep;
}

void RteSys_SetCanSleepFlag(bool flag)
{
    s_canSleep = flag;
}

/* 串口 can_nm_sleep: 置位强制休眠闩锁 (自动判睡跳过) */
void RteSys_SetManualSleepReq(bool flag)
{
    s_manualSleepReq = flag;
}

bool RteSys_GetManualSleepReq(void)
{
    return s_manualSleepReq;
}

bool RteSys_GetDtcDeactiveFlag(void)
{
    return s_dtcDeactive;
}
