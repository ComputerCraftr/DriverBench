#ifndef DRIVERBENCH_VK_SELECTION_DIAGNOSTICS_H
#define DRIVERBENCH_VK_SELECTION_DIAGNOSTICS_H

#include "vk_internal.h"

void db_vk_log_physical_devices(const DeviceSelectionState *selection);
void db_vk_log_execution_plan(const DeviceSelectionState *selection);

#endif
