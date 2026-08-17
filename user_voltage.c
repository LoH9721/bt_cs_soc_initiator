/*******************************************************
 * Name    :user_voltage.c
 * Function:电池电压测量 —— IADC 单次采样 VBAT/4
 * Note    :AVDD/4 → 内部 1.21V 参考 → 0.5x 增益 → 12bit
 *         换算: mv = (raw * 1210 * 4) / (4095 * gain)
 *         0.5x 增益时: mv = raw * 1210 * 4 / (4095 * 0.5)
 *                       = raw * 9680 / 4095 ≈ raw * 2.364
 *         滤波: 8 次滑动平均
*******************************************************/
#include "user_voltage.h"
#include "em_device.h"
#include "em_cmu.h"
#include "em_iadc.h"

#define FILTER_WINDOW       8
#define VOLT_LOW_MV          2700    /* 3.3V rail: <2.7V = 欠压 */
#define VOLT_HIGH_MV         4000    /* 3.3V rail: >4.0V = 过压 */

static IADC_Result_t  s_result;
static uint16_t       s_filterBuf[FILTER_WINDOW];
static uint8_t        s_filterIdx;
static uint8_t        s_filterFull;
static uint16_t       s_voltageMV;
static uint8_t        s_measureCnt;
static bool            s_dataReady;

/*******************************************************
 * Name    :UserVoltage_Init
 * Function:初始化 IADC 用于 VBAT 测量
*******************************************************/
void UserVoltage_Init(void)
{
    uint8_t i;

    /* 使能 IADC0 时钟 */
    CMU_ClockEnable(cmuClock_IADC0, true);

    /* IADC 初始化参数 */
    IADC_Init_t init = IADC_INIT_DEFAULT;
    init.warmup       = iadcWarmupNormal;
    init.srcClkPrescale = IADC_calcSrcClkPrescale(IADC0, 4000000, 0);

    /* 配置: Normal 模式, OSR=32, 1.21V 参考, 0.5x 增益 */
    IADC_Config_t config = IADC_CONFIG_DEFAULT;
    config.adcMode     = iadcCfgModeNormal;
    config.osrHighSpeed = iadcCfgOsrHighSpeed32x;
    config.analogGain  = iadcCfgAnalogGain0P5x;
    config.reference   = iadcCfgReferenceInt1V2;
    config.twosComplement = false;

    IADC_AllConfigs_t allConfigs = { config, config };
    IADC_init(IADC0, &init, &allConfigs);

    /* 单次转换: VBAT/4 对 GND */
    IADC_InitSingle_t singleInit = IADC_INITSINGLE_DEFAULT;
    singleInit.triggerAction = iadcTriggerActionOnce;
    singleInit.dataValidLevel = iadcFifoCfgDvl1;
    singleInit.alignment = iadcAlignRight12;
    singleInit.showId = false;

    IADC_SingleInput_t input = IADC_SINGLEINPUT_DEFAULT;
    input.negInput = iadcNegInputGnd;
    input.posInput = iadcPosInputAvdd;
    input.configId = 0;

    IADC_initSingle(IADC0, &singleInit, &input);

    /* 滤波初始化 */
    for (i = 0; i < FILTER_WINDOW; i++)
    {
        s_filterBuf[i] = 0;
    }
    s_filterIdx  = 0;
    s_filterFull = 0;
    s_voltageMV  = 0;
    s_measureCnt = 0;
    s_dataReady  = false;
}

/*******************************************************
 * Name    :UserVoltage_MeasureTrigger
 * Function:每 1ms 调用，每 50ms 触发一次 ADC 并读取结果
*******************************************************/
void UserVoltage_MeasureTrigger(void)
{
    uint32_t sum = 0;
    uint8_t  i;

    /* 每 50 次 tick (~50ms) 触发一次测量 */
    if (++s_measureCnt < 50)
    {
        return;
    }
    s_measureCnt = 0;

    /* 启动单次转换 */
    IADC_command(IADC0, iadcCmdStartSingle);

    /* 等待数据就绪（简单轮询，单次转换很快完成） */
    uint32_t timeout = 100000;
    while ((IADC0->STATUS & IADC_STATUS_SINGLEFIFODV) == 0)
    {
        if (--timeout == 0) return;
    }

    /* 读取结果 */
    s_result = IADC_readSingleResult(IADC0);

    /* 滑动平均滤波 */
    s_filterBuf[s_filterIdx] = s_result.data;
    if (++s_filterIdx >= FILTER_WINDOW)
    {
        s_filterIdx = 0;
        s_filterFull = 1;
    }

    sum = 0;
    for (i = 0; i < FILTER_WINDOW; i++)
    {
        sum += s_filterBuf[i];
    }
    if (s_filterFull == 0)
    {
        sum = sum / s_filterIdx;
    }
    else
    {
        sum = sum / FILTER_WINDOW;
    }

    /* 换算电压 (mV): raw * Vref * 4 / 4095 / gain
     * Vref=1210mV, gain=0.5 → raw * 1210 * 4 * 2 / 4095
     */
    s_voltageMV = (uint16_t)(((uint32_t)sum * 1210 * 4 * 2) / 4095);
    s_dataReady = true;
}

/*******************************************************
 * Name    :UserVoltage_GetMV
 * Function:获取最近一次滤波后的电压值 (mV)
*******************************************************/
uint16_t UserVoltage_GetMV(void)
{
    return s_voltageMV;
}

/*******************************************************
 * Name    :UserVoltage_IsLowHighStatus
 * Function:电压异常（<6V 或 >18V）返回 true
*******************************************************/
bool UserVoltage_IsLowHighStatus(void)
{
    if (!s_dataReady)
    {
        return false;  /* 首次测量未完成，不报异常 */
    }
    return (s_voltageMV < VOLT_LOW_MV) || (s_voltageMV > VOLT_HIGH_MV);
}
