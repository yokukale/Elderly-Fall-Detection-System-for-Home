/**
 ******************************************************************************
 * @file    bsp_uart.h
 * @brief   UART板级驱动头文件 — UART1(K230)/UART3(ESP-01S) DMA环形接收+IDLE判帧 + UART2调试口
 *
 *  数据通路职责（本层负责前两级）：
 *    1. DMA环形缓冲：HAL_UART_Receive_DMA 循环模式，硬件自动收数
 *    2. IDLE判帧：USARTx 总线空闲中断判定"一帧结束"，将 DMA 环形缓冲
 *       中新到的字节搬入软件环形缓冲（ring_buffer_t），ISR内不阻塞
 *    3. UART2（PA2/PA3）：专用于调试日志与 printf 重定向，避免干扰 UART1(K230)
 ******************************************************************************
 */
#ifndef BSP_UART_H
#define BSP_UART_H

#include <stdint.h>

/* ========== UART1 缓冲配置（K230 视觉通信口，PA9=TX / PA10=RX） ========== */
#define BSP_UART1_RX_DMA_BUF_SIZE   256U  /*!< DMA接收环形缓冲大小（字节） */
#define BSP_UART1_RX_RING_BUF_SIZE  512U  /*!< 软件环形缓冲大小（字节） */

/* ========== UART3 缓冲配置（ESP-01S WiFi通信口，PB10=TX / PB11=RX） ========== */
#define BSP_UART3_RX_DMA_BUF_SIZE   256U  /*!< UART3 DMA接收环形缓冲大小（字节） */
#define BSP_UART3_RX_RING_BUF_SIZE  512U  /*!< UART3 软件环形缓冲大小（字节） */

/* ==================== UART1 API (K230) ==================== */
void     BSP_UART1_Init(void);
void     BSP_UART1_RxIdleHandler(void);
uint32_t BSP_UART1_Available(void);
uint32_t BSP_UART1_Read(uint8_t *buf, uint32_t len);
void     BSP_UART1_Send(const uint8_t *buf, uint32_t len);
void     BSP_UART1_SendString(const char *str);
void     BSP_UART1_Printf(const char *fmt, ...);

/* ==================== UART2 API (Debug) ==================== */
void     BSP_UART2_Send(const uint8_t *buf, uint32_t len);
void     BSP_UART2_SendString(const char *str);
void     BSP_UART2_Printf(const char *fmt, ...);

/* ==================== UART3 API (ESP-01S) ==================== */
void     BSP_UART3_Init(void);
void     BSP_UART3_RxIdleHandler(void);
uint32_t BSP_UART3_Available(void);
uint32_t BSP_UART3_Read(uint8_t *buf, uint32_t len);
void     BSP_UART3_Send(const uint8_t *buf, uint32_t len);
void     BSP_UART3_SendString(const char *str);
void     BSP_UART3_Printf(const char *fmt, ...);

#endif /* BSP_UART_H */
