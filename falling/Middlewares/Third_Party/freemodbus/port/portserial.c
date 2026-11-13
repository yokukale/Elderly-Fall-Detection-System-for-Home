/*
 * FreeModbus Port: 从站串口驱动 (UART4 + RS485 DE=PB1)
 * 同时统一处理 UART2(主站) 和 UART4(从站) 的 HAL 回调
 *
 * 发送机制（中断驱动，非阻塞）：
 *   vMBPortSerialEnable(TX) -> 拉高DE -> 手动触发 pxMBFrameCBTransmitterEmpty()
 *     -> xMBRTUTransmitFSM() -> xMBPortSerialPutByte() -> HAL_UART_Transmit_IT(1字节)
 *   TC中断 -> HAL_UART_TxCpltCallback -> pxMBFrameCBTransmitterEmpty() -> 发下一字节
 *   发完最后一字节后，FSM调用 vMBPortSerialEnable(RX) -> 等TC -> 拉低DE -> 开收
 */
#include "port.h"
#include "mb.h"
#include "mbport.h"

#if MB_MASTER_RTU_ENABLED > 0 || MB_MASTER_ASCII_ENABLED > 0
extern void MB_Master_RxCpltHandler(void);
extern void MB_Master_TxCpltHandler(void);
#endif

static UCHAR slave_rx_byte;
static UCHAR slave_tx_byte;  /* 必须静态：中断发送期间保持有效 */

BOOL xMBPortSerialInit(UCHAR ucPort, ULONG ulBaudRate, UCHAR ucDataBits, eMBParity eParity)
{
    MB_SLAVE_RX_MODE();
    return TRUE;
}

void vMBPortSerialEnable(BOOL xRxEnable, BOOL xTxEnable)
{
    if (xTxEnable) {
        MB_SLAVE_TX_MODE();  /* RS485 DE 高 = 发送 */
        /* 手动触发首字节：FreeModbus FSM 会调用 xMBPortSerialPutByte()
         * （在临界区内调用，仅配置中断，中断在退出临界区后触发） */
        pxMBFrameCBTransmitterEmpty();
    } else {
        /* 等最后一字节完全移出移位寄存器，再释放 RS485 总线
         * （9600bps下1字节≈1.04ms，加超时防UART故障永久阻塞） */
        {
            uint32_t tc_wait = 0U;
            while ((__HAL_UART_GET_FLAG(&huart4, UART_FLAG_TC) == RESET) && (tc_wait < 50000U)) { tc_wait++; }
        }
    }
    if (xRxEnable) {
        MB_SLAVE_RX_MODE();  /* RS485 DE 低 = 接收 */
        HAL_UART_Receive_IT(&huart4, &slave_rx_byte, 1);
    } else {
        HAL_UART_AbortReceive_IT(&huart4);
    }
}

void vMBPortClose(void) { }

/* 错误恢复：清标志 + 重启接收（由 HAL_UART_ErrorCallback 调用） */
void MB_Slave_RestartRx(void)
{
    __HAL_UART_CLEAR_OREFLAG(&huart4);
    __HAL_UART_CLEAR_FEFLAG(&huart4);
    __HAL_UART_CLEAR_NEFLAG(&huart4);
    HAL_UART_Receive_IT(&huart4, &slave_rx_byte, 1);
}

BOOL xMBPortSerialPutByte(CHAR ucByte)
{
    HAL_StatusTypeDef st;
    slave_tx_byte = (UCHAR)ucByte;
    st = HAL_UART_Transmit_IT(&huart4, &slave_tx_byte, 1);
    if (st != HAL_OK) {
        /* 防御性回退：阻塞发送（正常情况下不应发生） */
        HAL_UART_Transmit(&huart4, &slave_tx_byte, 1, 100);
        {
            uint32_t tc_wait2 = 0U;
            while ((__HAL_UART_GET_FLAG(&huart4, UART_FLAG_TC) == RESET) && (tc_wait2 < 50000U)) { tc_wait2++; }
        }
        pxMBFrameCBTransmitterEmpty();
    }
    return TRUE;
}

BOOL xMBPortSerialGetByte(CHAR *pucByte)
{
    *pucByte = (CHAR)slave_rx_byte;
    return TRUE;
}

/* ===== 统一 HAL 回调分发 ===== */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == UART4) {
        /* 从站接收 */
        pxMBFrameCBByteReceived();
        HAL_UART_Receive_IT(&huart4, &slave_rx_byte, 1);
    }
#if MB_MASTER_RTU_ENABLED > 0 || MB_MASTER_ASCII_ENABLED > 0
    else if (huart->Instance == USART2) {
        MB_Master_RxCpltHandler();
    }
#endif
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == UART4) {
        /* 一字节发送完成，驱动 FreeModbus 发送状态机发下一字节
         * （RS485方向切换由 vMBPortSerialEnable 在发完最后一字节后处理） */
        pxMBFrameCBTransmitterEmpty();
    }
#if MB_MASTER_RTU_ENABLED > 0 || MB_MASTER_ASCII_ENABLED > 0
    else if (huart->Instance == USART2) {
        MB_Master_TxCpltHandler();
    }
#endif
}
