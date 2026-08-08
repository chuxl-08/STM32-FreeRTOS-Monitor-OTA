#ifndef __APP_OTA_CONFIRM_H
#define __APP_OTA_CONFIRM_H

#include <stdint.h>
#include "app_task.h"

void AppOtaConfirm_TryConfirmFromMonitor(const AppTask_MonitorItem_t *items,
                                         uint32_t item_count);

#endif /* __APP_OTA_CONFIRM_H */
