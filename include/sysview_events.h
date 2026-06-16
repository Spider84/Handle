#ifndef SYSVIEW_EVENTS_H
#define SYSVIEW_EVENTS_H

#ifdef DEBUG
#ifndef SEGGER_SYSVIEW_DISABLE
#include "SEGGER_SYSVIEW.h"
#endif
#endif

#define SYSVIEW_MODULE_MODBUS     0u
#define SYSVIEW_MODULE_USB        1u
#define SYSVIEW_MODULE_BUTTONS    2u
#define SYSVIEW_MODULE_LED        3u
#define SYSVIEW_MODULE_FLASHDB    4u

#define SYSVIEW_EVT_MODBUS_INIT       32u
#define SYSVIEW_EVT_MODBUS_POLL       33u
#define SYSVIEW_EVT_MODBUS_INPUT      34u
#define SYSVIEW_EVT_MODBUS_HOLDING    35u
#define SYSVIEW_EVT_MODBUS_DISCRETE   36u

#define SYSVIEW_EVT_USB_INIT          48u
#define SYSVIEW_EVT_USB_RXTX          49u
#define SYSVIEW_EVT_USB_SEND          50u
#define SYSVIEW_EVT_USB_RX_ENABLE     51u
#define SYSVIEW_EVT_USB_RX_DISABLE    52u

#define SYSVIEW_EVT_BUTTON_INIT       64u
#define SYSVIEW_EVT_BUTTON_REGISTER   65u
#define SYSVIEW_EVT_BUTTON_EVENT      66u

#define SYSVIEW_EVT_LED_INIT          80u
#define SYSVIEW_EVT_LED_UPDATE        81u
#define SYSVIEW_EVT_LED_SET_COLOR     82u
#define SYSVIEW_EVT_LED_BLINK         83u

#define SYSVIEW_EVT_FLASHDB_INIT      96u
#define SYSVIEW_EVT_FLASHDB_LOCK      97u
#define SYSVIEW_EVT_FLASHDB_UNLOCK    98u
#define SYSVIEW_EVT_FLASHDB_ITER      99u
#define SYSVIEW_EVT_FLASHDB_APPEND    100u

#define SYSVIEW_MODULE_EVENT(module, event)    (((module) << 8) | (event))

#if defined(DEBUG) && !defined(SEGGER_SYSVIEW_DISABLE)
#define SYSVIEW_RecordVoid(event)                 SEGGER_SYSVIEW_RecordVoid(event)
#define SYSVIEW_RecordU32(event, p0)              SEGGER_SYSVIEW_RecordU32(event, p0)
#define SYSVIEW_RecordU32x2(event, p0, p1)        SEGGER_SYSVIEW_RecordU32x2(event, p0, p1)
#define SYSVIEW_RecordU32x3(event, p0, p1, p2)    SEGGER_SYSVIEW_RecordU32x3(event, p0, p1, p2)
#define SYSVIEW_RecordU32x4(event, p0, p1, p2, p3) SEGGER_SYSVIEW_RecordU32x4(event, p0, p1, p2, p3)
#else
#define SYSVIEW_RecordVoid(event)                 ((void)0)
#define SYSVIEW_RecordU32(event, p0)              ((void)0)
#define SYSVIEW_RecordU32x2(event, p0, p1)        ((void)0)
#define SYSVIEW_RecordU32x3(event, p0, p1, p2)    ((void)0)
#define SYSVIEW_RecordU32x4(event, p0, p1, p2, p3) ((void)0)
#endif

#endif
