#include "UsbDebug.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "SecureTransport.h"
#include "usbd_cdc_if.h"

#define USB_DEBUG_MESSAGE_SIZE (96u + 2u * SECURE_TRANSPORT_MAX_MESSAGE_SIZE)
#define USB_DEBUG_QUEUE_DEPTH  8u

extern USBD_HandleTypeDef hUsbDeviceFS;

typedef struct
{
    uint16_t length;
    uint8_t data[USB_DEBUG_MESSAGE_SIZE];
} UsbDebugMessage_t;

static UsbDebugMessage_t queue[USB_DEBUG_QUEUE_DEPTH];
static uint8_t head, count;
static bool initialized;
static bool verbose;
static uint8_t usbDebugTxBuffer[USB_DEBUG_MESSAGE_SIZE];

volatile UsbDebugStats_t gUsbDebugStats = {0u, 0u, 0u};

bool UsbDebug_Init(void)
{
    head = count = 0u;
    initialized = true;
    verbose = false;
    return true;
}

void UsbDebug_SetVerbose(bool enabled) { verbose = enabled; }
bool UsbDebug_IsVerbose(void) { return verbose; }

void Debug(const char *msg)
{
    size_t length;

    if (msg == NULL || !initialized)
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

    /* Logging must never block a real-time task. */
    if (count < USB_DEBUG_QUEUE_DEPTH)
    {
        UsbDebugMessage_t *message = &queue[(head + count) % USB_DEBUG_QUEUE_DEPTH];
        message->length = (uint16_t)length;
        memcpy(message->data, msg, length);
        ++count;
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
    static const char hex[] = "0123456789ABCDEF";
    char buffer[USB_DEBUG_MESSAGE_SIZE];
    if (payloadLength > 0u && payload == NULL)
    {
        return;
    }

    int written = snprintf(buffer, sizeof(buffer),
                           "[%s] t=%lums SRC=0x%02X DST=0x%02X TYPE=0x%02X LEN=%u DATA=",
                           direction != NULL ? direction : "MSG", (unsigned long)HAL_GetTick(),
                           (unsigned)sourceId, (unsigned)destinationId, (unsigned)messageType,
                           (unsigned)payloadLength);
    if (written < 0 || (size_t)written + 2u * payloadLength + 3u > sizeof(buffer))
    {
        return;
    }

    size_t used = (size_t)written;
    for (uint16_t i = 0u; i < payloadLength; ++i)
    {
        buffer[used++] = hex[payload[i] >> 4];
        buffer[used++] = hex[payload[i] & 0x0Fu];
    }

    memcpy(&buffer[used], "\r\n", sizeof("\r\n"));
    Debug(buffer);
}

void UsbDebug_Process(void)
{
    USBD_CDC_HandleTypeDef *hcdc;
    if (!initialized || count == 0u ||
        hUsbDeviceFS.dev_state != USBD_STATE_CONFIGURED)
    {
        return;
    }

    hcdc = (USBD_CDC_HandleTypeDef *)hUsbDeviceFS.pClassData;
    if (hcdc == NULL || hcdc->TxState != 0u)
    {
        return;
    }

    const UsbDebugMessage_t *message = &queue[head];
    memcpy(usbDebugTxBuffer, message->data, message->length);

    /* A USB reset interrupt can clear pClassData while CDC starts the transfer. */
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    uint8_t status = USBD_BUSY;
    if (hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED && hUsbDeviceFS.pClassData != NULL)
    {
        status = CDC_Transmit_FS(usbDebugTxBuffer, message->length);
    }
    __set_PRIMASK(mask);
    if (status == USBD_BUSY)
    {
        return;
    }

    head = (head + 1u) % USB_DEBUG_QUEUE_DEPTH;
    --count;
    if (status == USBD_OK)
    {
        ++gUsbDebugStats.sentCount;
    }
    else
    {
        ++gUsbDebugStats.droppedCount;
    }
}
