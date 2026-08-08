#include "app_sensor.h"
#include "dht11.h"
#include "hcsr04.h"
#include "light_sensor.h"
#include "amg8833.h"
#include "app_task.h"
#include "app_log.h"

/*
 * 文件职责：
 * 1. 实现传感器采集业务逻辑。
 * 2. 环境采集：DHT11 + 光敏传感器。
 * 3. 方向采集：HC-SR04 + AMG8833。
 * 4. 将各 Driver 的 Data_t 结果整理到 Env/Manual/Scan 等任务私有数据结构中。
 */

/*
 * @brief   将 app_sensor.* 状态码转换为可读字符串。
 * @param   status: AppSensor_Status_t 状态码。
 * @retval  const char *: 状态名称字符串。
 */
const char *AppSensorStatusName(AppSensor_Status_t status)
{
    switch(status)
    {
        case APP_SENSOR_OK:
            return "OK";
        case APP_SENSOR_ERROR_PARAM:
            return "PARAM";
        case APP_SENSOR_ERROR_INIT:
            return "INIT_ERR";
        case APP_SENSOR_ERROR_READ:
            return "READ_ERR";
        default:
            return "UNKNOWN";
    }
}

/**
  * @brief  环境类传感器初始化。
  * @retval AppSensor_Status_t:
  *         - APP_SENSOR_OK: 初始化成功。
  *         - APP_SENSOR_ERROR_INIT: DHT11 或光敏传感器初始化失败。
  */
AppSensor_Status_t App_Sensor_EnvInit(System_EnvSensorStatus_t *env_sensor_status)
{
    DHT11_Status_t dht11_status;
    LightSensor_Status_t lightsensor_status;

    if(env_sensor_status == 0)
    {
        return APP_SENSOR_ERROR_PARAM;
    }


    dht11_status = DHT11_Init();
    lightsensor_status = LightSensor_Init();

    env_sensor_status->dht11_status = dht11_status;
    env_sensor_status->lightsensor_status = lightsensor_status;

    if((dht11_status != DHT11_OK) || (lightsensor_status != LIGHT_SENSOR_OK))
    {
        return APP_SENSOR_ERROR_INIT;
    }

    return APP_SENSOR_OK;
}

/**
  * @brief  方向类传感器初始化。
  * @retval AppSensor_Status_t:
  *         - APP_SENSOR_OK: 初始化成功。
  *         - APP_SENSOR_ERROR_PARAM: 输入参数为空。
  *         - APP_SENSOR_ERROR_INIT: HC-SR04 或 AMG8833 初始化失败。
  */
AppSensor_Status_t App_Sensor_DirectionInit(System_DirectionSensorStatus_t *direction_sensor_status)
{
    HCSR04_Status_t hcsr04_status;
    AMG8833_Status_t amg8833_status;
    AppTask_Status_t amg8833_i2cmutex_status = APP_TASK_ERROR_MUTEX_TAKE;

    if(direction_sensor_status == 0)
    {
        return APP_SENSOR_ERROR_PARAM;
    }

    hcsr04_status = HCSR04_Init();
    direction_sensor_status->hcsr04_status = hcsr04_status;

    amg8833_i2cmutex_status = App_Task_TakeI2C(portMAX_DELAY);
    if(amg8833_i2cmutex_status == APP_TASK_OK)
    {
        amg8833_status = AMG8833_Init();
        amg8833_i2cmutex_status = App_Task_GiveI2C();
        if(amg8833_i2cmutex_status != APP_TASK_OK)
        {
            App_LogPrintf("[SENSOR] amg8833_release_mutex_error:%s(%d)\r\n", AppTaskStatusName(amg8833_i2cmutex_status), amg8833_i2cmutex_status);
        }
    }
    else
    {
        amg8833_status = AMG8833_ERROR_I2C;
        App_LogPrintf("[SENSOR] amg8833_take_mutex_error:%s(%d)\r\n", AppTaskStatusName(amg8833_i2cmutex_status), amg8833_i2cmutex_status);
    }
    direction_sensor_status->amg8833_status = amg8833_status;

    if((hcsr04_status != HCSR04_OK) || (amg8833_status != AMG8833_OK))
    {
        return APP_SENSOR_ERROR_INIT;
    }

    return APP_SENSOR_OK;
}

/**
  * @brief  读取环境类传感器数据。
  * @param  env: 环境任务数据结构指针，函数会写入数据、状态和 valid 标志。
  * @retval AppSensor_Status_t:
  *         - APP_SENSOR_OK: DHT11 和光敏读取均成功。
  *         - APP_SENSOR_ERROR_PARAM: 输入参数为空。
  *         - APP_SENSOR_ERROR_READ: 至少一个环境传感器读取失败。
  * @note   具体失败原因保存到 env_sensor_status。
  */
AppSensor_Status_t App_Sensor_EnvUpdate(System_EnvTaskData_t *env)
{
    DHT11_Status_t dht11_read_status;
    LightSensor_Status_t lightsensor_read_status;

    if(env == 0)
    {
        return APP_SENSOR_ERROR_PARAM;
    }

    env->valid = 0;

    dht11_read_status = DHT11_Read(&env->env_data.dht11_data);
    env->env_sensor_status.dht11_status = dht11_read_status;

    lightsensor_read_status = LightSensor_Read(&env->env_data.lightsensor_data);
    env->env_sensor_status.lightsensor_status = lightsensor_read_status;

    if((dht11_read_status != DHT11_OK) || (lightsensor_read_status != LIGHT_SENSOR_OK))
    {
        return APP_SENSOR_ERROR_READ;
    }

    env->valid = 1;

    return APP_SENSOR_OK;
}

/**
  * @brief  读取方向类传感器数据。
  * @param  manual_data: 手动模式方向传感器数据指针。
  * @retval AppSensor_Status_t:
  *         - APP_SENSOR_OK: HC-SR04 和 AMG8833 读取均成功。
  *         - APP_SENSOR_ERROR_PARAM: 输入参数为空。
  *         - APP_SENSOR_ERROR_READ: 至少一个方向传感器读取失败。
  */
AppSensor_Status_t App_Sensor_DirectionUpdate_Manual(System_ManualTaskData_t *manual_data)
{
    HCSR04_Status_t hcsr04_read_status = HCSR04_ERROR_PARAM;
    AMG8833_Status_t amg8833_read_status = AMG8833_ERROR_PARAM;
    AppTask_Status_t amg8833_i2cmutex_status = APP_TASK_ERROR_MUTEX_TAKE;

    if(manual_data == 0)
    {
        return APP_SENSOR_ERROR_PARAM;
    }

    hcsr04_read_status = HCSR04_Read(&manual_data->distance.hcsr04_data);
    manual_data->direction_sensor_status.hcsr04_status = hcsr04_read_status;

    amg8833_i2cmutex_status = App_Task_TakeI2C(portMAX_DELAY);
    if(amg8833_i2cmutex_status == APP_TASK_OK)
    {
        amg8833_read_status = AMG8833_Read(&manual_data->thermal.amg8833_data);
        amg8833_i2cmutex_status = App_Task_GiveI2C();
        manual_data->direction_sensor_status.amg8833_status = amg8833_read_status;
        if(amg8833_i2cmutex_status != APP_TASK_OK)
        {
            App_LogPrintf("[SENSOR] amg8833_release_mutex_error:%s(%d)\r\n", AppTaskStatusName(amg8833_i2cmutex_status), amg8833_i2cmutex_status);
        }
    }
    else
    {
        amg8833_read_status = AMG8833_ERROR_I2C;
        manual_data->direction_sensor_status.amg8833_status = amg8833_read_status;
        App_LogPrintf("[SENSOR] amg8833_take_mutex_error:%s(%d)\r\n", AppTaskStatusName(amg8833_i2cmutex_status), amg8833_i2cmutex_status);
    }

    if(hcsr04_read_status != HCSR04_OK || amg8833_read_status != AMG8833_OK)
    {
        return APP_SENSOR_ERROR_READ;
    }

    return APP_SENSOR_OK;
}

/**
  * @brief  读取方向类传感器数据。
  * @param  scanpoint_data: 扫描数据结构指针。
  * @param  direction_sensor_status: 方向类传感器状态指针。
  * @retval AppSensor_Status_t:
  *         - APP_SENSOR_OK: HC-SR04 和 AMG8833 读取均成功。
  *         - APP_SENSOR_ERROR_PARAM: 输入参数为空。
  *         - APP_SENSOR_ERROR_READ: 至少一个方向传感器读取失败。
  */
AppSensor_Status_t App_Sensor_DirectionUpdate_Scan(System_ScanPoint_t *scanpoint_data, System_DirectionSensorStatus_t *direction_sensor_status)
{
    HCSR04_Status_t hcsr04_read_status = HCSR04_ERROR_PARAM;
    AMG8833_Status_t amg8833_read_status = AMG8833_ERROR_PARAM;
    AppTask_Status_t amg8833_i2cmutex_status = APP_TASK_ERROR_PARAM;

    if(scanpoint_data == 0 || direction_sensor_status == 0)
    {
        return APP_SENSOR_ERROR_PARAM;
    }

    hcsr04_read_status = HCSR04_Read(&scanpoint_data->point_distance.hcsr04_data);
    direction_sensor_status->hcsr04_status = hcsr04_read_status;

    amg8833_i2cmutex_status = App_Task_TakeI2C(portMAX_DELAY);
    if(amg8833_i2cmutex_status == APP_TASK_OK)
    {
        amg8833_read_status = AMG8833_Read(&scanpoint_data->point_thermal.amg8833_data);
        amg8833_i2cmutex_status = App_Task_GiveI2C();
        direction_sensor_status->amg8833_status = amg8833_read_status;
        if(amg8833_i2cmutex_status != APP_TASK_OK)
        {
            App_LogPrintf("[SENSOR] amg8833_release_mutex_error:%s(%d)\r\n", AppTaskStatusName(amg8833_i2cmutex_status), amg8833_i2cmutex_status);
        }
    }
    else
    {
        amg8833_read_status = AMG8833_ERROR_I2C;
        direction_sensor_status->amg8833_status = amg8833_read_status;
        App_LogPrintf("[SENSOR] amg8833_take_mutex_error:%s(%d)\r\n", AppTaskStatusName(amg8833_i2cmutex_status), amg8833_i2cmutex_status);
    }

    if(hcsr04_read_status != HCSR04_OK || amg8833_read_status != AMG8833_OK)
    {
        return APP_SENSOR_ERROR_READ;
    }

    return APP_SENSOR_OK;
}

/**
  * @brief  全量传感器初始化。
  * @retval AppSensor_Status_t:
  *         - APP_SENSOR_OK: 全部初始化成功。
  *         - APP_SENSOR_ERROR_INIT: 至少一类传感器初始化失败。
  * @note   调用 App_Sensor_EnvInit() 和 App_Sensor_DirectionInit()。
  */
AppSensor_Status_t App_Sensor_Init(System_SensorStatus_t *sensor_status)
{
    AppSensor_Status_t env_status;
    AppSensor_Status_t direction_status;

    env_status = App_Sensor_EnvInit(&sensor_status->env_sensor_status);
    direction_status = App_Sensor_DirectionInit(&sensor_status->dir_sensor_status);

    if((env_status != APP_SENSOR_OK) || (direction_status != APP_SENSOR_OK))
    {
        return APP_SENSOR_ERROR_INIT;
    }

    return APP_SENSOR_OK;
}

