#pragma once

#ifdef __cplusplus
#include "PlatformDefines.h"
#else
#include <stdint.h>
uint32_t millis_c_shim(void);
#define millis millis_c_shim
#endif
