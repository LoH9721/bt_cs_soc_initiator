/***************************************************************************//**
 * @file    TCAN_Driver.h
 * @brief   底层CAN驱动统一接口 (基于 TCAN 方案.md 规范)
 * @note    所有接口使用 TCAN_ 前缀命名
 ******************************************************************************/
#ifndef _TCAN_DRIVER_H_
#define _TCAN_DRIVER_H_

#include <stdint.h>

/* ===================================================================
 *  初始化与控制
 * =================================================================== */

/// 初始化 CAN 控制器硬件，配置波特率、工作模式、滤波器和邮箱
extern void TCAN_Init(void);

/// 使能 CAN 控制器，启动 CAN 通信（离开初始化模式进入正常工作模式）
extern void TCAN_Enable(void);

/// 禁止/关闭 CAN 控制器，停止 CAN 通信（进入初始化模式）
extern void TCAN_Disable(void);

/// 打开 CAN 收发器电源/使能，使 CAN 总线物理层进入正常工作状态
extern void TCAN_TransceiverOn(void);

/// 关闭 CAN 收发器，断开 CAN 总线物理连接
extern void TCAN_TransceiverOff(void);

/// 打开 CAN 中断使能（接收中断、发送中断、错误中断等）
extern void TCAN_OpenInterrupt(void);

/// 关闭 CAN 中断使能，停止所有 CAN 中断上报
extern void TCAN_CloseInterrupt(void);

/* ===================================================================
 *  发送管理
 * =================================================================== */

/// 发送一帧 CAN 报文到 CAN 总线上
/// @param id     CAN 报文 ID（11位标准帧: 0x000~0x7FF）
/// @param p_buff 指向发送数据缓冲区的指针
/// @param len    发送数据长度 (0~8 字节)
extern void    TCAN_SendAFrame(uint16_t id, uint8_t* p_buff, uint8_t len);

/// 获取上一次 CAN 帧发送的结果状态
/// @return 0=发送成功/空闲, 1=发送失败/发送中
extern uint8_t TCAN_GetSendResult(void);

/// 清空 CAN 控制器的发送邮箱/缓冲区，取消待发送的报文
extern void    TCAN_ClearMailbox(void);

/* ===================================================================
 *  接收状态管理
 * =================================================================== */

/// 检测接收缓冲区中是否有新接收到的 CAN 报文
/// @return 0=无新数据, 非0=有新数据待读取
extern uint8_t TCAN_CheckIfReceive(void);

/// 清除"接收完成"标志位，准备接收下一帧
extern void    TCAN_ClearReceivedFlag(void);

/// 从接收队列中读取一帧 CAN 报文数据
/// @param[out] p_id   接收到帧的 CAN ID
/// @param[out] p_buff 数据缓冲区
/// @param[out] p_len  实际数据长度
/// @return 1=成功读取, 0=无数据
extern uint8_t TCAN_ReadReceivedFrame(uint16_t* p_id, uint8_t* p_buff,
                                       uint8_t* p_len);

/* ===================================================================
 *  Bus-Off 错误管理
 * =================================================================== */

/// 获取是否处于 Bus-Off 状态的标志
/// @return 0=未进入Bus-Off, 1=已进入Bus-Off
extern uint8_t TCAN_GetBusOffFlag(void);

/// 获取 Bus-Off 事件累计发生次数
extern uint8_t TCAN_GetBusOffCounter(void);

/// 清除 Bus-Off 状态标志，尝试从 Bus-Off 状态恢复
extern void    TCAN_ClearBusOffFlag(void);

/// 清除/重置 Bus-Off 事件累计计数器
extern void    TCAN_ClearBusOffCounter(void);

/* ===================================================================
 *  No-Ack 错误管理
 * =================================================================== */

/// 获取是否检测到 No-Ack 错误（发送无应答）的标志
/// @return 0=未发生, 1=发生了No-Ack错误
extern uint8_t TCAN_GetNoAckFlag(void);

/// 获取 No-Ack 错误累计发生次数
extern uint8_t TCAN_GetNoAckCounter(void);

/// 清除 No-Ack 错误标志
extern void    TCAN_ClearNoAckFlag(void);

/// 清除/重置 No-Ack 事件累计计数器
extern void    TCAN_ClearNoAckCounter(void);

/* ===================================================================
 *  主循环轮询
 * =================================================================== */

/// 主循环周期轮询 (TX 超时检测、错误恢复等)
/// 需在 app_process_action() 中周期性调用
extern void    TCAN_PollMain(void);

/* ===================================================================
 *  应用层验证测试 (验证完成后删除调用)
 * =================================================================== */

/// 简单的 CAN 收发验证测试函数
/// 在 app_init() 和 app_process_action() 中调用
extern void    TCAN_Test(void);

#endif  // _TCAN_DRIVER_H_
