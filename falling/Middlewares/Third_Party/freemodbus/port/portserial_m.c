/*
 * FreeModbus Port: 主站串口驱动 (USART2 + RS485 DE=PB0)
 *
 * 发送机制（中断驱动，非阻塞）：同从站，见 portserial.c 注释
 */
#include "port.h"
#include "mb.h"
#include "mb_m.h"
#include "mbport.h"

#if MB_MASTER_RTU_ENABLED > 0 || MB_MASTER_ASCII_ENABLED > 0

static UCHAR master_rx_byte;
static UCHAR master_tx_byte;  /* 必须静态：中断发送期间保持有效 */

BOOL xMBMasterPortSerialInit(UCHAR ucPort, ULONG ulBaudRate, UCHAR ucDataBits, eMBParity eParity)
{
    MB_MASTER_RX_MODE();
    return TRUE;
}

void vMBMasterPortSerialEnable(BOOL xRxEnable, BOOL xTxEnable)
{
    if (xTxEnable) {
        MB_MASTER_TX_MODE();  /* RS485 DE 高 = 发送 */
        pxMBMasterFrameCBTransmitterEmpty();  /* 触发首字节 */
    } else {
        uint32_t tc_wait = 0U;
        while ((__HAL_UART_GET_FLAG(&huart2, UART_FLAG_TC) == RESET) && (tc_wait < 50000U)) { tc_wait++; }
    }
    if (xRxEnable) {
        MB_MASTER_RX_MODE();  /* RS485 DE 低 = 接收 */
        HAL_UART_Receive_IT(&huart2, &master_rx_byte, 1);
    } else {
        HAL_UART_AbortReceive_IT(&huart2);
    }
}

void vMBMasterPortClose(void) { }

/* 错误恢复：清标志 + 重启接收（由 HAL_UART_ErrorCallback 调用） */
void MB_Master_RestartRx(void)
{
    __HAL_UART_CLEAR_OREFLAG(&huart2);
    __HAL_UART_CLEAR_FEFLAG(&huart2);
    __HAL_UART_CLEAR_NEFLAG(&huart2);
    HAL_UART_Receive_IT(&huart2, &master_rx_byte, 1);
}

BOOL xMBMasterPortSerialPutByte(CHAR ucByte)
{
    HAL_StatusTypeDef st;
    master_tx_byte = (UCHAR)ucByte;
    st = HAL_UART_Transmit_IT(&huart2, &master_tx_byte, 1);
    if (st != HAL_OK) {
        HAL_UART_Transmit(&huart2, &master_tx_byte, 1, 100);
        {
            uint32_t tc_wait2 = 0U;
            while ((__HAL_UART_GET_FLAG(&huart2, UART_FLAG_TC) == RESET) && (tc_wait2 < 50000U)) { tc_wait2++; }
        }
        pxMBMasterFrameCBTransmitterEmpty();
    }
    return TRUE;
}

BOOL xMBMasterPortSerialGetByte(CHAR *pucByte)
{
    *pucByte = (CHAR)master_rx_byte;
    return TRUE;
}

/* 主站接收中断 — 在 HAL_UART_RxCpltCallback 中分发 */
void MB_Master_RxCpltHandler(void)
{
    pxMBMasterFrameCBByteReceived();
    HAL_UART_Receive_IT(&huart2, &master_rx_byte, 1);
}

/* 主站发送完成中断 — 在 HAL_UART_TxCpltCallback 中分发 */
void MB_Master_TxCpltHandler(void)
{
    pxMBMasterFrameCBTransmitterEmpty();
}

#endif /* MB_MASTER_RTU_ENABLED || MB_MASTER_ASCII_ENABLED */
