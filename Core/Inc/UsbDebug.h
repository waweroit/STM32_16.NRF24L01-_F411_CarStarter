#ifndef USB_DEBUG_H
#define USB_DEBUG_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define USB_DEBUG_MESSAGE_SIZE 256u
#define USB_DEBUG_QUEUE_DEPTH   4u

typedef struct
{
    uint32_t queuedCount;
    uint32_t sentCount;
    uint32_t droppedCount;
} UsbDebugStats_t;

extern volatile UsbDebugStats_t gUsbDebugStats;

bool UsbDebug_CreateOsObjects(void);
void UsbDebug_Process(void);
void Debug(const char *msg);
void DebugPrintf(const char *format, ...);
void DebugMessage(const char *direction,
                  uint8_t sourceId,
                  uint8_t destinationId,
                  uint8_t messageType,
                  const uint8_t *payload,
                  uint16_t payloadLength);

#ifdef __cplusplus
}
#endif

#endif /* USB_DEBUG_H */
