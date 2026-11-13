/**
 ******************************************************************************
 * @file    bsp_uart.c
 * @brief   UART板级驱动实现 — UART1/UART3 DMA循环接收 + IDLE判帧 + UART2调试口
 ******************************************************************************
 */
#include "module_cfg.h"

#include "bsp_uart.h"
#include "ring_buffer.h"
#include "main.h"
#include "usart.h"
#include "dma.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ==========================================================================
 *  K230 视觉通信口驱动（现绑定在 USART2 PA2/PA3，DMA1_Stream5 环形接收 + IDLE判帧）
 * ========================================================================== */
#if BSP_UART1_ENABLE

extern UART_HandleTypeDef huart2;
extern DMA_HandleTypeDef  hdma_usart2_rx;

static uint8_t           s_uart1_dma_rx_buf[BSP_UART1_RX_DMA_BUF_SIZE];
static uint8_t           s_uart1_ring_buf[BSP_UART1_RX_RING_BUF_SIZE];
static ring_buffer_t     s_uart1_rx_ring;
static volatile uint32_t s_uart1_dma_last_pos;

static void BSP_UART1_StartRxDma(void)
{
    HAL_UART_Receive_DMA(&huart2, s_uart1_dma_rx_buf, BSP_UART1_RX_DMA_BUF_SIZE);
    __HAL_UART_ENABLE_IT(&huart2, UART_IT_IDLE);
}

void BSP_UART1_Init(void)
{
    RB_Init(&s_uart1_rx_ring, s_uart1_ring_buf, sizeof(s_uart1_ring_buf));
    s_uart1_dma_last_pos = 0U;
    BSP_UART1_StartRxDma();
}

void BSP_UART1_RxIdleHandler(void)
{
    uint32_t pos   = BSP_UART1_RX_DMA_BUF_SIZE - __HAL_DMA_GET_COUNTER(&hdma_usart2_rx);
    uint32_t start = s_uart1_dma_last_pos;
    uint32_t len;
    uint32_t first;

    if (pos >= start) {
        len = pos - start;
    } else {
        len = BSP_UART1_RX_DMA_BUF_SIZE - start + pos;
    }
    s_uart1_dma_last_pos = pos;

    if (len == 0U) {
        return;
    }

    first = BSP_UART1_RX_DMA_BUF_SIZE - start;
    if (len <= first) {
        RB_Write(&s_uart1_rx_ring, &s_uart1_dma_rx_buf[start], len);
    } else {
        RB_Write(&s_uart1_rx_ring, &s_uart1_dma_rx_buf[start], first);
        RB_Write(&s_uart1_rx_ring, &s_uart1_dma_rx_buf[0], len - first);
    }
}

uint32_t BSP_UART1_Available(void)
{
    return RB_Used(&s_uart1_rx_ring);
}

uint32_t BSP_UART1_Read(uint8_t *buf, uint32_t len)
{
    return RB_Read(&s_uart1_rx_ring, buf, len);
}

void BSP_UART1_Send(const uint8_t *buf, uint32_t len)
{
    if ((buf == NULL) || (len == 0U)) {
        return;
    }
    HAL_UART_Transmit(&huart2, (uint8_t *)buf, (uint16_t)len, 100U);
}

void BSP_UART1_SendString(const char *str)
{
    if (str == NULL) {
        return;
    }
    BSP_UART1_Send((const uint8_t *)str, (uint32_t)strlen(str));
}

void BSP_UART1_Printf(const char *fmt, ...)
{
    char    buf[128];
    va_list args;
    int     n;

    va_start(args, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (n > 0) {
        if (n >= (int)sizeof(buf)) {
            n = (int)sizeof(buf) - 1;
        }
        BSP_UART1_Send((const uint8_t *)buf, (uint32_t)n);
    }
}

#endif /* BSP_UART1_ENABLE */

/* ==========================================================================
 *  UART2 驱动（调试打印口 + 标准库 printf 重定向）
 * ========================================================================== */
#if BSP_UART2_DEBUG_ENABLE

#if BSP_DEBUG_USE_UART1
extern UART_HandleTypeDef huart1;
#define BSP_DEBUG_UART_HANDLE   (&huart1)
#else
extern UART_HandleTypeDef huart2;
#define BSP_DEBUG_UART_HANDLE   (&huart2)
#endif

void BSP_UART2_Send(const uint8_t *buf, uint32_t len)
{
    if ((buf == NULL) || (len == 0U)) {
        return;
    }
    HAL_UART_Transmit(BSP_DEBUG_UART_HANDLE, (uint8_t *)buf, (uint16_t)len, 100U);
}

void BSP_UART2_SendString(const char *str)
{
    if (str == NULL) {
        return;
    }
    BSP_UART2_Send((const uint8_t *)str, (uint32_t)strlen(str));
}

void BSP_UART2_Printf(const char *fmt, ...)
{
    char    buf[192];
    va_list args;
    int     n;

    va_start(args, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (n > 0) {
        if (n >= (int)sizeof(buf)) {
            n = (int)sizeof(buf) - 1;
        }
        BSP_UART2_Send((const uint8_t *)buf, (uint32_t)n);
    }
}

int _write(int file, char *ptr, int len)
{
    if ((file == 1) || (file == 2)) {
        HAL_UART_Transmit(BSP_DEBUG_UART_HANDLE, (uint8_t *)ptr, (uint16_t)len, 100U);
        return len;
    }
    return -1;
}

#endif /* BSP_UART2_DEBUG_ENABLE */

/* ==========================================================================
 *  UART3 驱动（ESP-01S 通信口）
 * ========================================================================== */
#if BSP_UART3_ENABLE

extern UART_HandleTypeDef huart3;
extern DMA_HandleTypeDef  hdma_usart3_rx;

static uint8_t           s_uart3_dma_rx_buf[BSP_UART3_RX_DMA_BUF_SIZE];
static uint8_t           s_uart3_ring_buf[BSP_UART3_RX_RING_BUF_SIZE];
static ring_buffer_t     s_uart3_rx_ring;
static volatile uint32_t s_uart3_dma_last_pos;

static void BSP_UART3_StartRxDma(void)
{
    HAL_UART_Receive_DMA(&huart3, s_uart3_dma_rx_buf, BSP_UART3_RX_DMA_BUF_SIZE);
    __HAL_UART_ENABLE_IT(&huart3, UART_IT_IDLE);
}

void BSP_UART3_Init(void)
{
    RB_Init(&s_uart3_rx_ring, s_uart3_ring_buf, sizeof(s_uart3_ring_buf));
    s_uart3_dma_last_pos = 0U;
    BSP_UART3_StartRxDma();
}

void BSP_UART3_RxIdleHandler(void)
{
    uint32_t pos   = BSP_UART3_RX_DMA_BUF_SIZE - __HAL_DMA_GET_COUNTER(&hdma_usart3_rx);
    uint32_t start = s_uart3_dma_last_pos;
    uint32_t len;
    uint32_t first;

    if (pos >= start) {
        len = pos - start;
    } else {
        len = BSP_UART3_RX_DMA_BUF_SIZE - start + pos;
    }
    s_uart3_dma_last_pos = pos;

    if (len == 0U) {
        return;
    }

    first = BSP_UART3_RX_DMA_BUF_SIZE - start;
    if (len <= first) {
        RB_Write(&s_uart3_rx_ring, &s_uart3_dma_rx_buf[start], len);
    } else {
        RB_Write(&s_uart3_rx_ring, &s_uart3_dma_rx_buf[start], first);
        RB_Write(&s_uart3_rx_ring, &s_uart3_dma_rx_buf[0], len - first);
    }
}

uint32_t BSP_UART3_Available(void)
{
    return RB_Used(&s_uart3_rx_ring);
}

uint32_t BSP_UART3_Read(uint8_t *buf, uint32_t len)
{
    return RB_Read(&s_uart3_rx_ring, buf, len);
}

void BSP_UART3_Send(const uint8_t *buf, uint32_t len)
{
    if ((buf == NULL) || (len == 0U)) {
        return;
    }
    HAL_UART_Transmit(&huart3, (uint8_t *)buf, (uint16_t)len, 100U);
}

void BSP_UART3_SendString(const char *str)
{
    if (str == NULL) {
        return;
    }
    BSP_UART3_Send((const uint8_t *)str, (uint32_t)strlen(str));
}

void BSP_UART3_Printf(const char *fmt, ...)
{
    char    buf[128];
    va_list args;
    int     n;

    va_start(args, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (n > 0) {
        if (n >= (int)sizeof(buf)) {
            n = (int)sizeof(buf) - 1;
        }
        BSP_UART3_Send((const uint8_t *)buf, (uint32_t)n);
    }
}

#endif /* BSP_UART3_ENABLE */

/* ==========================================================================
 *  公共：UART错误回调（溢出/DMA错误自动恢复）
 * ========================================================================== */
#if BSP_UART1_ENABLE || BSP_UART3_ENABLE
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
#if BSP_UART1_ENABLE
    if (huart->Instance == USART2) {
        __HAL_UART_DISABLE_IT(&huart2, UART_IT_IDLE);
        RB_Reset(&s_uart1_rx_ring);
        s_uart1_dma_last_pos = 0U;
        BSP_UART1_StartRxDma();
    }
#endif
#if BSP_UART3_ENABLE
    if (huart->Instance == USART3) {
        __HAL_UART_DISABLE_IT(&huart3, UART_IT_IDLE);
        RB_Reset(&s_uart3_rx_ring);
        s_uart3_dma_last_pos = 0U;
        BSP_UART3_StartRxDma();
    }
#endif
}
#endif /* BSP_UART1_ENABLE || BSP_UART3_ENABLE */
