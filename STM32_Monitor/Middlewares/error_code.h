#ifndef __ERROR_CODE_H
#define __ERROR_CODE_H
#include "stm32f10x.h"

/*
 * 文件职责：
 * 1. 声明系统统一错误码。
 * 2. 覆盖传感器超时、I2C 错误、WiFi 连接失败、上传失败、OLED 错误等场景。
 * 3. 供日志、OLED 状态页和看门狗策略使用。
 *
 * 当前状态：
 * - 已按 App、Drivers、Middlewares、BSP 分层维护主要模块状态码。
 */


/* ************** APP_LAYER_ERROR_CODE ************** */
/*
 * @brief   appsensor.* 状态码。
 * @note    AppSensor_Status_t:
 *          - APP_SENSOR_OK: 正常
 *          - APP_SENSOR_ERROR_PARAM: 参数错误
 *          - APP_SENSOR_ERROR_INIT: 传感器初始化错误
 *          - APP_SENSOR_ERROR_READ： 传感器数据读取错误
 */
typedef enum
{
    APP_SENSOR_OK = 0,
    APP_SENSOR_ERROR_PARAM,
    APP_SENSOR_ERROR_INIT,
    APP_SENSOR_ERROR_READ
} AppSensor_Status_t;

/*
 * @brief   app_detect.* 状态码。
 * @note    AppDetect_Status_t:
 *          - APP_DETECT_OK: 正常
 *          - APP_DETECT_ERROR_PARAM: 参数错误
 *          - APP_DETECT_ERROR_SENSOR_INVALID: 传感器数据无效
 */
typedef enum
{
    APP_DETECT_OK = 0,
    APP_DETECT_ERROR_PARAM,
    APP_DETECT_ERROR_SENSOR_INVALID
} AppDetect_Status_t;

/*
 * @brief   app_display.* 状态码。
 * @note    AppDisplay_Status_t:
 *          - APP_DISPLAY_OK: 正常。
 *          - APP_DISPLAY_ERROR_PARAM: 参数错误。
 *          - APP_DISPLAY_ERROR_OLED: OLED 底层初始化或通信错误。
 *          - APP_DISPLAY_ERROR_I2C_MUTEX: 获取I2C mutex失败。
 */
typedef enum
{
    APP_DISPLAY_OK = 0,
    APP_DISPLAY_ERROR_PARAM,
    APP_DISPLAY_ERROR_OLED,
    APP_DISPLAY_ERROR_I2C_MUTEX
} AppDisplay_Status_t;

/*
 * @brief   app_protocol.* 状态码。
 * @note    AppProtocol_Status_t:
 *          - APP_PROTOCOL_OK: 正常
 *          - APP_PROTOCOL_ERROR_PARAM: 参数错误
 *          - APP_PROTOCOL_ERROR_BUFFER_SMALL: 数据缓存过小
 *          - APP_PROTOCOL_ERROR_DATA_INVALID: 传感器数据无效
 */
typedef enum
{
    APP_PROTOCOL_OK = 0,
    APP_PROTOCOL_ERROR_PARAM,
    APP_PROTOCOL_ERROR_BUFFER_SMALL,
    APP_PROTOCOL_ERROR_DATA_INVALID
} AppProtocol_Status_t;


/*
 * @brief   app_upload.* 状态码。
 * @note    AppUpload_Status_t:
 *          - APP_UPLOAD_OK: 正常
 *          - APP_UPLOAD_ERROR_PARAM: 参数错误
 *          - APP_UPLOAD_ERROR_INIT: 初始化错误
 *          - APP_UPLOAD_ERROR_WIFI_CONNECT: WIFI连接失败
 *          - APP_UPLOAD_ERROR_TCP_CONNECT: TCP连接失败
 *          - APP_UPLOAD_ERROR_PROTOCOL: 数据打包失败
 *          - APP_UPLOAD_ERROR_ATK_ESP01: 数据发送失败
 *          - APP_UPLOAD_ERROR_ESP01_BUSY: ESP01正被其他任务占用，本轮上传跳过
 */
typedef enum
{
    APP_UPLOAD_OK = 0,
    APP_UPLOAD_ERROR_PARAM,
    APP_UPLOAD_ERROR_INIT,
    APP_UPLOAD_ERROR_WIFI_CONNECT,
    APP_UPLOAD_ERROR_TCP_CONNECT,
    APP_UPLOAD_ERROR_PROTOCOL,
    APP_UPLOAD_ERROR_ATK_ESP01,
    APP_UPLOAD_ERROR_ESP01_BUSY
} AppUpload_Status_t;

/*
 * @brief   app_scan.* 状态码。
 * @note    AppScan_Status_t:
 *          - APP_SCAN_OK: 正常
 *          - APP_SCAN_ERROR_PARAM: 参数错误
 *          - APP_SCAN_ERROR_INIT: 初始化错误
 *          - APP_SCAN_ERROR_SERVO: 舵机控制错误
 *          - APP_SCAN_ERROR_SENSOR_READ: 方向传感器读取错误
 */
typedef enum
{
    APP_SCAN_OK = 0,
    APP_SCAN_ERROR_PARAM,
    APP_SCAN_ERROR_INIT,
    APP_SCAN_ERROR_SERVO,
    APP_SCAN_ERROR_SENSOR_READ
} AppScan_Status_t;

/*
 * @brief   app_servo_control.* 状态码。
 * @note    AppServoControl_Status_t:
 *          - APP_SERVO_CONTROL_OK: 正常
 *          - APP_SERVO_CONTROL_ERROR_PARAM: 参数错误
 *          - APP_SERVO_CONTROL_ERROR_INIT: 编码器或舵机初始化失败
 *          - APP_SERVO_CONTROL_ERROR_ENCODER: 编码器读取或按键更新失败
 *          - APP_SERVO_CONTROL_ERROR_SERVO: 舵机角度设置失败
 */
typedef enum
{
    APP_SERVO_CONTROL_OK = 0,
    APP_SERVO_CONTROL_ERROR_PARAM,
    APP_SERVO_CONTROL_ERROR_INIT,
    APP_SERVO_CONTROL_ERROR_ENCODER,
    APP_SERVO_CONTROL_ERROR_SERVO
} AppServoControl_Status_t;

/*
 * @brief   app_input.* 状态码。
 * @note    AppInput_Status_t:
 *          - APP_INPUT_OK: 正常。
 *          - APP_INPUT_ERROR_INIT: 输入设备初始化失败。
 *          - APP_INPUT_ERROR_ENCODER: 编码器读取或按键更新失败。
 */
typedef enum
{
    APP_INPUT_OK = 0,
    APP_INPUT_ERROR_INIT,
    APP_INPUT_ERROR_ENCODER
} AppInput_Status_t;

/*
 * @brief   app_menu.* 状态码。
 * @note    AppMenu_Status_t:
 *          - APP_MENU_OK: 正常。
 *          - APP_MENU_ERROR_PARAM: 参数错误。
 */
typedef enum
{
    APP_MENU_OK = 0,
    APP_MENU_ERROR_PARAM
} AppMenu_Status_t;

/*
 * @brief   app_system.* 状态码。
 * @note    AppSystem_Status_t:
 *          - APP_SYSTEM_OK: 正常
 *          - APP_SYSTEM_ERROR_PARAM: 参数错误
 *          - APP_SYSTEM_ERROR_ENCODER: 编码器按键状态更新失败
 *          - APP_SYSTEM_ERROR_INIT: 系统初始化失败
 *          - APP_SYSTEM_ERROR_SCAN: 自动扫描流程失败
 *          - APP_SYSTEM_ERROR_SCAN_DISPLAY: 历史保留，显示已迁移到 DisplayTask
 *          - APP_SYSTEM_ERROR_MANUAL_SERVO_UPDATE: 手动模式舵机控制更新失败
 *          - APP_SYSTEM_ERROR_MANUAL_DISPLAY_UPDATE: 历史保留，显示已迁移到 DisplayTask
 *          - APP_SYSTEM_ERROR_MANUAL_SENSOR_UPDATE: 手动模式当前方向传感器采集或检测失败
 *          - APP_SYSTEM_ERROR_AUTO: 系统更新时自动模式执行失败
 *          - APP_SYSTEM_ERROR_MANUAL: 系统更新时手动模式执行失败
 *          - APP_SYSTEM_ERROR_UPLOAD: 历史保留，上传已迁移到 UploadTask
 */
typedef enum
{
    APP_SYSTEM_OK = 0,
    APP_SYSTEM_ERROR_PARAM,
    APP_SYSTEM_ERROR_ENCODER,
    APP_SYSTEM_ERROR_INIT,
    APP_SYSTEM_ERROR_SCAN,
    APP_SYSTEM_ERROR_SCAN_DISPLAY,          /* 已迁移：显示由 DisplayTask 处理 */
    APP_SYSTEM_ERROR_MANUAL_SERVO_UPDATE,
    APP_SYSTEM_ERROR_MANUAL_DISPLAY_UPDATE, /* 已迁移：显示由 DisplayTask 处理 */
    APP_SYSTEM_ERROR_MANUAL_SENSOR_UPDATE,
    APP_SYSTEM_ERROR_AUTO,
    APP_SYSTEM_ERROR_MANUAL,
    APP_SYSTEM_ERROR_UPLOAD                 /* 已迁移：上传由 UploadTask 处理 */
} AppSystem_Status_t;

/*
 * @brief   app_task.* 状态码。
 * @note    AppTask_Status_t:
 *          - APP_TASK_OK: 正常
 *          - APP_TASK_ERROR_QUEUE_INIT: 队列初始化失败
 *          - APP_TASK_ERROR_QUEUE_WRITE: 写队列失败
 *          - APP_TASK_ERROR_QUEUE_READ: 读队列失败
 *          - APP_TASK_ERROR_MUTEX_INIT: 互斥量初始化失败
 *          - APP_TASK_ERROR_MUTEX_TAKE: 获取互斥量失败
 *          - APP_TASK_ERROR_MUTEX_GIVE: 释放互斥量失败
 */
typedef enum
{
    APP_TASK_OK = 0,
    APP_TASK_ERROR_PARAM,
    APP_TASK_ERROR_QUEUE_INIT,
    APP_TASK_ERROR_QUEUE_WRITE,
    APP_TASK_ERROR_QUEUE_READ,
    APP_TASK_ERROR_MUTEX_INIT,
    APP_TASK_ERROR_MUTEX_TAKE,
    APP_TASK_ERROR_MUTEX_GIVE
}AppTask_Status_t;

/* ************** DRIVERS_LAYER_ERROR_CODE ************** */
/*
 * @brief   光敏传感器light_sensor.* 状态码。
 * @note    LightSensor_Status_t:
 *          - LIGHT_SENSOR_OK: 正常
 *          - LIGHT_SENSOR_ERROR_PARAM: 参数错误
 *          - LIGHT_SENSOR_ERROR_ADC: ADC 错误
 */
typedef enum
{
    LIGHT_SENSOR_OK = 0,
    LIGHT_SENSOR_ERROR_PARAM,
    LIGHT_SENSOR_ERROR_ADC
} LightSensor_Status_t;

/*
 * @brief   温湿度传感器.* 状态码。
 * @note    DHT11_Status_t:
 *          - DHT11_OK: 正常
 *          - DHT11_ERROR_PARAM: 参数错误
 *          - DHT11_ERROR_TIMEOUT: 超时错误
 *          - DHT11_ERROR_START_SIGNAL: 起始信号错误
 *          - DHT11_ERROR_READ_BIT: 读取位错误
 *          - DHT11_ERROR_CHECKSUM: 校验错误
 */
typedef enum
{
    DHT11_OK = 0,
    DHT11_ERROR_PARAM,
    DHT11_ERROR_TIMEOUT,
    DHT11_ERROR_START_SIGNAL,
    DHT11_ERROR_READ_BIT,
    DHT11_ERROR_CHECKSUM
} DHT11_Status_t;

/*
 * @brief   OLED_Status_t: OLED 驱动状态码。
 * @note    OLED_Status_t:
 *          - OLED_OK: 正常
 *          - OLED_ERROR_PARAM: 参数错误
 *          - OLED_ERROR_I2C: I2C 通信错误
 *          - OLED_ERROR_INIT: 初始化错误
 */
typedef enum
{
    OLED_OK = 0,
    OLED_ERROR_PARAM,
    OLED_ERROR_I2C,
    OLED_ERROR_INIT
} OLED_Status_t;

/*
 * @brief   HCSR04_Status_t: HCSR04 驱动状态码。
 * @note    HCSR04_Status_t:
 *          - HCSR04_OK: 正常
 *          - HCSR04_ERROR_PARAM: 参数错误
 *          - HCSR04_ERROR_TIMEOUT: 超时
 *          - HCSR04_ERROR_NO_ECHO: 等待捕获失败
 *          - HCSR04_ERROR_TIMER: 底层定时器错误
 *          - HCSR04_ERROR_RANGE: 测距结果超出有效范围
 */
typedef enum
{
    HCSR04_OK = 0,
    HCSR04_ERROR_PARAM,
    HCSR04_ERROR_TIMEOUT,
    HCSR04_ERROR_NO_ECHO,
    HCSR04_ERROR_TIMER,
    HCSR04_ERROR_RANGE
} HCSR04_Status_t;

/*
 * @brief   AMG8833_Status_t: AMG8833 驱动状态码。
 * @note    AMG8833_Status_t:
 *          - AMG8833_OK: 正常
 *          - AMG8833_ERROR_PARAM: 参数错误
 *          - AMG8833_ERROR_I2C: I2C 通信失败
 *          - AMG8833_ERROR_DEVICE_NOT_READY: 设备无响应
 */
typedef enum
{
    AMG8833_OK = 0,
    AMG8833_ERROR_PARAM,
    AMG8833_ERROR_I2C,
    AMG8833_ERROR_DEVICE_NOT_READY,
} AMG8833_Status_t;

/*
 * @brief   ATK_ESP01_Status_t: ATK_ESP01 驱动状态码。
 * @note    ATK_ESP01_Status_t:
 *          - ATK_ESP01_OK: 正常
 *          - ATK_ESP01_ERROR_PARAM: 参数错误
 *          - ATK_ESP01_ERROR_AT: 其他错误
 *          - ATK_ESP01_ERROR_TIMEOUT: 等待超时
 *          - ATK_ESP01_ERROR_RESPONSE: 收到 ERROR 等错误响应
 *          - ATK_ESP01_ERROR_OVERFLOW: 响应缓存溢出
 *          - ATK_ESP01_ERROR_USART: 串口错误
 */
typedef enum
{
    ATK_ESP01_OK = 0,
    ATK_ESP01_ERROR_PARAM,
    ATK_ESP01_ERROR_AT,
    ATK_ESP01_ERROR_TIMEOUT,
    ATK_ESP01_ERROR_RESPONSE,
    ATK_ESP01_ERROR_OVERFLOW,
    ATK_ESP01_ERROR_USART
} ATK_ESP01_Status_t;

/*
 * @brief   Servo_Status_t: SG90 舵机驱动状态码。
 * @note    Servo_Status_t:
 *          - SERVO_OK: 正常
 *          - SERVO_ERROR_PARAM: 参数错误
 *          - SERVO_ERROR_PWM: 底层 PWM 初始化或设置失败
 */
typedef enum
{
    SERVO_OK = 0,
    SERVO_ERROR_PARAM,
    SERVO_ERROR_PWM
} Servo_Status_t;

/*
 * @brief   Encoder_Status_t: 旋转编码器驱动状态码。
 * @note    Encoder_Status_t:
 *          - ENCODER_OK: 正常
 *          - ENCODER_ERROR_PARAM: 参数错误
 *          - ENCODER_ERROR_TIMER: 底层 TIM3 编码器模式错误
 *          - ENCODER_ERROR_BUTTON: 编码器按键初始化或读取错误
 *          - ENCODER_ERROR_NOT_INIT: 编码器尚未初始化
 */
typedef enum
{
    ENCODER_OK = 0,
    ENCODER_ERROR_PARAM,
    ENCODER_ERROR_TIMER,
    ENCODER_ERROR_BUTTON,
    ENCODER_ERROR_NOT_INIT
} Encoder_Status_t;

/* ************** Middlewares_LAYER_ERROR_CODE ************** */
/*
 * @brief   ring_buffer.* 状态码。
 * @note    RingBuffer_Status_t:
 *          - RING_BUFFER_OK: 正常
 *          - RING_BUFFER_ERROR_PARAM: 参数错误
 *          - RING_BUFFER_ERROR_EMPTY: 缓冲区为空，无数据可读
 *          - RING_BUFFER_ERROR_FULL: 缓冲区已满，本次数据被丢弃
 */
typedef enum
{
    RING_BUFFER_OK = 0,
    RING_BUFFER_ERROR_PARAM,
    RING_BUFFER_ERROR_EMPTY,
    RING_BUFFER_ERROR_FULL
} RingBuffer_Status_t;

/*
 * @brief   at_parser.* 状态码。
 * @note    AT_Parser_Status_t:
 *          - AT_PARSER_OK: 正常, 匹配到期望响应。
 *          - AT_PARSER_ERROR_PARAM: 参数错误。
 *          - AT_PARSER_ERROR_TIMEOUT: 等待响应超时。
 *          - AT_PARSER_ERROR_OVERFLOW: 响应缓存溢出。
 *          - AT_PARSER_ERROR_RESPONSE: 收到 ERROR 等错误响应。
 *          - AT_PARSER_ERROR_USART: 底层 USART 发送失败。
 */
typedef enum
{
    AT_PARSER_OK = 0,
    AT_PARSER_ERROR_PARAM,
    AT_PARSER_ERROR_TIMEOUT,
    AT_PARSER_ERROR_OVERFLOW,
    AT_PARSER_ERROR_RESPONSE,
    AT_PARSER_ERROR_USART
} AT_Parser_Status_t;

/* ************** BSP_LAYER_ERROR_CODE ************** */
/*
 * @brief   BSP_ADC_Status_t: BSP_ADC 状态码。
 * @note    BSP_ADC_Status_t:
 *          - BSP_ADC_OK: 正常
 *          - BSP_ADC_ERROR_PARAM: 参数错误
 *          - BSP_ADC_ERROR_TIMEOUT: 超时错误
 *          - BSP_ADC_ERROR_CALIBRATION: 校准错误
 *          - BSP_ADC_ERROR_CONVERSION: 转换错误
 */
typedef enum
{
    BSP_ADC_OK = 0,
    BSP_ADC_ERROR_PARAM,
    BSP_ADC_ERROR_TIMEOUT,
    BSP_ADC_ERROR_CALIBRATION,
    BSP_ADC_ERROR_CONVERSION
} BSP_ADC_Status_t;

/*
    * @brief   BSP_I2C_Status_t: BSP_I2C 状态码。
    * @note    BSP_I2C_Status_t:
    *          - BSP_I2C_OK: 正常
    *          - BSP_I2C_ERROR_PARAM: 参数错误
    *          - BSP_I2C_ERROR_TIMEOUT: 超时错误
    *          - BSP_I2C_ERROR_NACK: 从机无 ACK
*/
typedef enum
{
    BSP_I2C_OK = 0,
    BSP_I2C_ERROR_PARAM,
    BSP_I2C_ERROR_TIMEOUT,
    BSP_I2C_ERROR_NACK,
    BSP_I2C_ERROR_REC_BYTE,
} BSP_I2C_Status_t;

/*
    * @brief   BSP_TIMER_Status_t: BSP_TIMER 状态码。
    * @note    BSP_TIMER_Status_t:
    *          - BSP_TIMER_OK: 正常
    *          - BSP_TIMER_ERROR_PARAM: 参数错误
    *          - BSP_TIMER_ERROR_TIMEOUT: 超时错误
    *          - BSP_TIMER_ERROR_CAPTURE: 未捕获到echo
    *          - BSP_TIMER_ERROR_NOT_INIT: 定时器功能尚未初始化
*/
typedef enum
{
    BSP_TIMER_OK = 0,
    BSP_TIMER_ERROR_PARAM,
    BSP_TIMER_ERROR_TIMEOUT,
    BSP_TIMER_ERROR_CAPTURE,
    BSP_TIMER_ERROR_NOT_INIT
} BSP_TIMER_Status_t;

/*
    * @brief   BSP_USART_Status_t: BSP_USART 状态码。
    * @note    BSP_USART_Status_t:
    *          - BSP_USART_OK: 正常
    *          - BSP_USART_ERROR_PARAM: 参数错误
    *          - BSP_USART_ERROR_TIMEOUT: 发送/接收超时
    *          - BSP_USART_ERROR_OVERFLOW: 接收溢出
    *          - BSP_USART_ERROR_EMPTY: 临时接收缓冲区空
*/
typedef enum
{
    BSP_USART_OK = 0,
    BSP_USART_ERROR_PARAM,
    BSP_USART_ERROR_TIMEOUT,
    BSP_USART_ERROR_OVERFLOW,
    BSP_USART_ERROR_EMPTY
} BSP_USART_Status_t;


/*
    * @brief   BSP_PWM_Status_t: BSP_PWM 状态码。
    * @note    BSP_PWM_Status_t:
    *          - BSP_PWM_OK: 正常
    *          - BSP_PWM_ERROR_PARAM: 参数错误
    *          - BSP_PWM_ERROR_RANGE: PWM 参数超出允许范围
    *          - BSP_PWM_ERROR_NOT_INIT: PWM 尚未初始化
*/
typedef enum
{
    BSP_PWM_OK = 0,
    BSP_PWM_ERROR_PARAM,
    BSP_PWM_ERROR_RANGE,
    BSP_PWM_ERROR_NOT_INIT
} BSP_PWM_Status_t;


#endif /* __ERROR_CODE_H */
