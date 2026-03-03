/*
 * File: FirmwareVersion.h
 * Purpose: Declares firmware version and build stamp macros.
 */
#ifndef FIRMWARE_VERSION_H
#define FIRMWARE_VERSION_H

// Manually bump this when you want an obvious runtime marker after flashing.
#define FIRMWARE_VERSION "fw-30s-pause-deltatime-v1"

// Compile-time stamp helps verify the exact binary that is running.
#define FIRMWARE_BUILD_STAMP __DATE__ " " __TIME__

#endif
