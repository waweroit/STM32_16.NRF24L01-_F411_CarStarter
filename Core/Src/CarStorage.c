/* Two-sector append-only journal: first 8 KiB of external W25Q64.
 * Each snapshot is CRC protected, committed last. The previous sector remains
 * intact until a verified newer record exists in the other sector. */
#include "CarStorage.h"
#include "CarConfig.h"
#include <string.h>
#define MAGIC 0x43535431u
#define COMMIT 0x434F4D54u
#define RECORD_SIZE 64u
typedef struct {
    uint32_t magic, version, sequence;
    CarStoredData data;
    uint32_t reserved[5];
    uint32_t crc, commit;
} Record;
_Static_assert(sizeof(Record) == RECORD_SIZE, "Storage layout");
static W25Q_HandleTypeDef *device;
static CarStoredData current;
static uint32_t sequence, latestAddress;
static bool ready, haveRecord;
static uint32_t Crc(const uint8_t *p, uint32_t n)
{
    uint32_t crc = ~0u;
    while (n--) {
        crc ^= *p++;
        for (unsigned i = 0; i < 8; ++i) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}
static bool Empty(const Record *r)
{
    const uint8_t *p = (const uint8_t *)r;
    for (unsigned i = 0; i < sizeof(*r); ++i) if (p[i] != 0xFFu) return false;
    return true;
}
static bool Valid(const Record *r)
{
    return r->magic == MAGIC && r->version == 1u && r->commit == COMMIT &&
           r->sequence != 0u && r->crc == Crc((const uint8_t *)r, sizeof(*r)-8u) &&
           r->data.stopSeconds >= CAR_MIN_STOP_SECONDS && r->data.stopSeconds <= CAR_MAX_STOP_SECONDS &&
           r->data.bootCounter <= 0xFFFFFFu && r->data.rootInitialized <= 1u;
}
bool CarStorage_Init(W25Q_HandleTypeDef *flash)
{
    device = flash; ready = false; haveRecord = false; sequence = 0u;
    bool allEmpty = true;
    for (uint32_t addr = 0; addr < 2u*W25Q64_SECTOR_SIZE; addr += RECORD_SIZE) {
        Record r;
        if (!W25Q_Read(device, addr, (uint8_t *)&r, sizeof(r))) return false;
        if (!Empty(&r)) allEmpty = false;
        if (Valid(&r) && (!haveRecord || r.sequence > sequence)) {
            sequence = r.sequence; current = r.data; latestAddress = addr; haveRecord = true;
        }
    }
    /* Never silently reset boot/replay counters on corrupt, nonblank media. */
    if (!haveRecord && !allEmpty) return false;
    if (!haveRecord) {
        memset(&current, 0, sizeof(current));
        current.stopSeconds = CAR_DEFAULT_STOP_SECONDS;
    }
    ready = true;
    return true;
}
const CarStoredData *CarStorage_Get(void) { return &current; }
bool CarStorage_Ready(void) { return ready; }
bool CarStorage_Save(const CarStoredData *data)
{
    if (!ready || !data || sequence == UINT32_MAX) return false;
    uint32_t sector = haveRecord ? latestAddress & ~(W25Q64_SECTOR_SIZE-1u) : 0u;
    uint32_t addr = haveRecord ? latestAddress + RECORD_SIZE : 0u;
    Record r;
    /* Skip torn records; never program over non-erased bytes. */
    while (addr < sector + W25Q64_SECTOR_SIZE) {
        if (!W25Q_Read(device, addr, (uint8_t *)&r, sizeof(r))) goto fail;
        if (Empty(&r)) break;
        addr += RECORD_SIZE;
    }
    if (addr >= sector + W25Q64_SECTOR_SIZE) {
        addr = sector ^ W25Q64_SECTOR_SIZE;
        if (!W25Q_EraseSector(device, addr)) goto fail;
    }
    memset(&r, 0, sizeof(r));
    r.magic = MAGIC; r.version = 1u; r.sequence = sequence + 1u; r.data = *data;
    r.crc = Crc((const uint8_t *)&r, sizeof(r)-8u); r.commit = COMMIT;
    if (!Valid(&r) || !W25Q_Write(device, addr, (const uint8_t *)&r, sizeof(r)-4u) ||
        !W25Q_Write(device, addr+sizeof(r)-4u, (const uint8_t *)&r.commit, 4u)) goto fail;
    Record check;
    if (!W25Q_Read(device, addr, (uint8_t *)&check, sizeof(check)) || memcmp(&check, &r, sizeof(r))) goto fail;
    current = *data; sequence = r.sequence; latestAddress = addr; haveRecord = true;
    return true;
fail:
    ready = false;
    return false;
}
