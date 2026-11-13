/**
 ******************************************************************************
 * @file    module_cfg.h
 * @brief   条件编译模块开关配置 — 统一管控各层模块的启用/禁用
 *  使用说明：
 *    - 每个源文件顶部包含本头文件，用 #if 包裹全部实现
 *    - 禁用模块不生成任何代码，节省 Flash 和 RAM
 ******************************************************************************
 */
#ifndef MODULE_CFG_H
#define MODULE_CFG_H

/* ========== BSP 板级支持包模块开关 ========== */
#define BSP_UART1_ENABLE        1   /* K230视觉通信口驱动（现绑定在USART2 PA2/PA3，DMA环形接收 + IDLE判帧） */
#define BSP_UART2_DEBUG_ENABLE  1   /* 调试日志输出与printf重定向驱动 */
#define BSP_DEBUG_USE_UART1     1   /* 1=调试日志走USART1(PA9/PA10 -> 板载USB1 CH340)，K230走USART2(PA2/PA3) */
#define BSP_UART3_ENABLE        1   /* UART3驱动（DMA环形接收 + IDLE判帧，PB10/PB11，ESP-01S通信口） */
#define BSP_ESP8266_ENABLE      1   /* ESP-01S WiFi驱动（AT指令，依赖BSP_UART3_ENABLE） */
#define BSP_BEEP_ENABLE         1   /* 蜂鸣器驱动总开关（PA5） */
#define BSP_BEEP_PWM_ENABLE     0   /* 1=TIM2_CH1 PWM音乐旋律(PA5 AF1); 0=GPIO高低电平节奏(纯有源蜂鸣器) */
#define BSP_IWDG_ENABLE         1   /* 独立看门狗IWDG驱动（LSI时钟，3s超时复位） */

/* ========== 平台抽象层（PLAT）模块开关 ========== */
#define PLAT_OBJ_ENABLE         1   /* 对象模型（plat_obj.h）：plat_dev_t/plat_svc_t + 统一生命周期 */
#define PLAT_DEVMGR_ENABLE      1   /* 设备管理器（plat_devmgr） */
#define PLAT_SVCMGR_ENABLE      1   /* 服务管理器（plat_svcmgr） */
#define PLAT_CONN_ENABLE        1   /* 连接器抽象（plat_conn） */

/* ========== 服务层（SVC）模块开关 ========== */
#define SVC_CONN_ENABLE         1   /* conn 连接管理服务（状态机 + 上云通道解耦） */

/* ========== Protocol 协议层模块开关 ========== */
#define PROTOCOL_FALL_ENABLE    1   /* K230 跌倒检测文本帧解析状态机（$...# 与可选 $F,...*XOR） */

/* ========== APP 应用层模块开关 ========== */
#define APP_TASKS_ENABLE        1   /* FreeRTOS任务与对象管理 */
#define APP_FALL_ENABLE         1   /* 跌倒判决业务模块（滑动窗口防抖 + 报警保持 + 冷却 + 掉线检测） */
#define APP_ALARM_ENABLE        1   /* 本地报警执行模块（PA5蜂鸣器控制） */
#define APP_MQTT_ENABLE         1   /* MQTT上云任务（ESP-01S AT -> Paho MQTT -> 华为云IoTDA） */
#define APP_WATCHDOG_ENABLE     1   /* 多任务心跳检测 + IWDG喂狗 */

/* ========== 依赖检查 ========== */
#if BSP_ESP8266_ENABLE && !BSP_UART3_ENABLE
#error "BSP_ESP8266_ENABLE 依赖 BSP_UART3_ENABLE=1"
#endif

#if PLAT_DEVMGR_ENABLE && !PLAT_OBJ_ENABLE
#error "PLAT_DEVMGR_ENABLE 依赖 PLAT_OBJ_ENABLE=1"
#endif

#if PLAT_SVCMGR_ENABLE && !PLAT_OBJ_ENABLE
#error "PLAT_SVCMGR_ENABLE 依赖 PLAT_OBJ_ENABLE=1"
#endif

#if SVC_CONN_ENABLE && !(PLAT_SVCMGR_ENABLE && PLAT_CONN_ENABLE)
#error "SVC_CONN_ENABLE 依赖 PLAT_SVCMGR_ENABLE=1 与 PLAT_CONN_ENABLE=1"
#endif

#if APP_FALL_ENABLE && !PROTOCOL_FALL_ENABLE
#error "APP_FALL_ENABLE 依赖 PROTOCOL_FALL_ENABLE=1"
#endif

#if APP_MQTT_ENABLE && !(APP_TASKS_ENABLE && BSP_ESP8266_ENABLE)
#error "APP_MQTT_ENABLE 依赖 APP_TASKS_ENABLE=1 与 BSP_ESP8266_ENABLE=1"
#endif

#endif /* MODULE_CFG_H */
