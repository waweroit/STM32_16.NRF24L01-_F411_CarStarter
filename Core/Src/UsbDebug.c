#include "UsbDebug.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "cmsis_os.h"
#include "usbd_cdc_if.h"

extern USBD_HandleTypeDef hUsbDeviceFS;

typedef struct
{
    uint16_t length;
    uint8_t data[USB_DEBUG_MESSAGE_SIZE];
} UsbDebugMessage_t;

static osMessageQueueId_t usbDebugQueueHandle = NULL;
static uint8_t usbDebugTxBuffer[USB_DEBUG_MESSAGE_SIZE];

volatile UsbDebugStats_t gUsbDebugStats = {0u, 0u, 0u};

bool UsbDebug_CreateOsObjects(void)
{
    if (usbDebugQueueHandle != NULL)
    {
        return true;
    }

    usbDebugQueueHandle = osMessageQueueNew(USB_DEBUG_QUEUE_DEPTH,
                                            sizeof(UsbDebugMessage_t),
                                            NULL);
    return usbDebugQueueHandle != NULL;
}

void Debug(const char *msg)
{
    UsbDebugMessage_t message;
    size_t length;

    if (msg == NULL || usbDebugQueueHandle == NULL)
    {
        ++gUsbDebugStats.droppedCount;
        return;
    }

    length = strlen(msg);
    if (length == 0u)
    {
        return;
    }
    if (length > USB_DEBUG_MESSAGE_SIZE)
    {
        length = USB_DEBUG_MESSAGE_SIZE;
    }

    message.length = (uint16_t)length;
    memcpy(message.data, msg, length);

    /* Logging must never block a real-time task. */
    if (osMessageQueuePut(usbDebugQueueHandle, &message, 0u, 0u) == osOK)
    {
        ++gUsbDebugStats.queuedCount;
    }
    else
    {
        ++gUsbDebugStats.droppedCount;
    }
}

void DebugPrintf(const char *format, ...)
{
    char buffer[USB_DEBUG_MESSAGE_SIZE];
    va_list args;
    int written;

    if (format == NULL)
    {
        return;
    }

    va_start(args, format);
    written = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    if (written <= 0)
    {
        return;
    }

    buffer[sizeof(buffer) - 1u] = '\0';
    Debug(buffer);
}

void DebugMessage(const char *direction,
                  uint8_t sourceId,
                  uint8_t destinationId,
                  uint8_t messageType,
                  const uint8_t *payload,
                  uint16_t payloadLength)
{
    char buffer[USB_DEBUG_MESSAGE_SIZE];
    size_t used;
    uint16_t i;
    int written;

    if (direction == NULL)
    {
        direction = "MSG";
    }

    written = snprintf(buffer,
                       sizeof(buffer),
                       "[%s] SRC=0x%02X DST=0x%02X TYPE=0x%02X LEN=%u DATA=\"",
                       direction,
                       sourceId,
                       destinationId,
                       messageType,
                       (unsigned int)payloadLength);
    if (written <= 0)
    {
        return;
    }

    used = (size_t)written;
    if (used >= sizeof(buffer))
    {
        buffer[sizeof(buffer) - 1u] = '\0';
        Debug(buffer);
        return;
    }

    for (i = 0u; i < payloadLength && used < (sizeof(buffer) - 4u); ++i)
    {
        uint8_t value = payload != NULL ? payload[i] : 0u;
        buffer[used++] = isprint((int)value) ? (char)value : '.';
    }

    buffer[used++] = '"';
    buffer[used++] = '\r';
    buffer[used++] = '\n';
    buffer[used] = '\0';
    Debug(buffer);
}

void UsbDebug_Process(void)
{
    USBD_CDC_HandleTypeDef *hcdc;
    UsbDebugMessage_t message;

    if (usbDebugQueueHandle == NULL ||
        hUsbDeviceFS.dev_state != USBD_STATE_CONFIGURED)
    {
        return;
    }

    hcdc = (USBD_CDC_HandleTypeDef *)hUsbDeviceFS.pClassData;
    if (hcdc == NULL || hcdc->TxState != 0u)
    {
        return;
    }

    if (osMessageQueueGet(usbDebugQueueHandle, &message, NULL, 0u) != osOK)
    {
        return;
    }

    memcpy(usbDebugTxBuffer, message.data, message.length);
    if (CDC_Transmit_FS(usbDebugTxBuffer, message.length) == USBD_OK)
    {
        ++gUsbDebugStats.sentCount;
    }
    else
    {
        ++gUsbDebugStats.droppedCount;
    }
}
