#include "DrvParamStore.h"
#include "BoardConfig.h"
#include "stm32f10x_flash.h"
#include <string.h>

#define DRV_PARAM_STORE_MAGIC          0x44495046u
#define DRV_PARAM_STORE_VERSION        1u

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t length;
    uint32_t crc32;
} DrvParamStoreHeader_t;

static uint32_t drv_param_store_crc32(const uint8_t *data, uint16_t length)
{
    uint32_t crc = 0xFFFFFFFFu;
    uint16_t i;
    uint8_t bit;

    if (data == 0) {
        return 0u;
    }

    for (i = 0u; i < length; i++) {
        crc ^= (uint32_t)data[i];
        for (bit = 0u; bit < 8u; bit++) {
            if ((crc & 1u) != 0u) {
                crc = (crc >> 1) ^ 0xEDB88320u;
            } else {
                crc >>= 1;
            }
        }
    }

    return crc ^ 0xFFFFFFFFu;
}

static uint8_t drv_param_store_program_bytes(uint32_t address, const uint8_t *data, uint16_t length)
{
    uint16_t i = 0u;

    while (i < length) {
        uint16_t halfWord = data[i];

        if ((i + 1u) < length) {
            halfWord |= (uint16_t)((uint16_t)data[i + 1u] << 8);
        } else {
            halfWord |= 0xFF00u;
        }

        if (FLASH_ProgramHalfWord(address, halfWord) != FLASH_COMPLETE) {
            return 0u;
        }

        address += 2u;
        i = (uint16_t)(i + 2u);
    }

    return 1u;
}

uint8_t DrvParamStore_Load(void *payload, uint16_t payloadSize)
{
    const DrvParamStoreHeader_t *header;
    const uint8_t *payloadPtr;
    uint32_t crc;

    if ((payload == 0) || (payloadSize == 0u)) {
        return 0u;
    }

    header = (const DrvParamStoreHeader_t *)BOARD_PARAM_STORE_ADDRESS;
    if ((header->magic != DRV_PARAM_STORE_MAGIC) ||
        (header->version != DRV_PARAM_STORE_VERSION) ||
        (header->length != payloadSize) ||
        (header->length > (BOARD_PARAM_STORE_PAGE_SIZE - sizeof(DrvParamStoreHeader_t)))) {
        return 0u;
    }

    payloadPtr = (const uint8_t *)(BOARD_PARAM_STORE_ADDRESS + sizeof(DrvParamStoreHeader_t));
    crc = drv_param_store_crc32(payloadPtr, header->length);
    if (crc != header->crc32) {
        return 0u;
    }

    memcpy(payload, payloadPtr, payloadSize);
    return 1u;
}

uint8_t DrvParamStore_Save(const void *payload, uint16_t payloadSize)
{
    DrvParamStoreHeader_t header;
    FLASH_Status flashStatus;
    uint8_t ok = 0u;

    if ((payload == 0) || (payloadSize == 0u) ||
        (payloadSize > (BOARD_PARAM_STORE_PAGE_SIZE - sizeof(DrvParamStoreHeader_t)))) {
        return 0u;
    }

    header.magic = DRV_PARAM_STORE_MAGIC;
    header.version = DRV_PARAM_STORE_VERSION;
    header.length = payloadSize;
    header.crc32 = drv_param_store_crc32((const uint8_t *)payload, payloadSize);

    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);

    flashStatus = FLASH_ErasePage(BOARD_PARAM_STORE_ADDRESS);
    if (flashStatus == FLASH_COMPLETE) {
        ok = drv_param_store_program_bytes(BOARD_PARAM_STORE_ADDRESS,
                                           (const uint8_t *)&header,
                                           (uint16_t)sizeof(header));
        if (ok != 0u) {
            ok = drv_param_store_program_bytes(BOARD_PARAM_STORE_ADDRESS + sizeof(header),
                                               (const uint8_t *)payload,
                                               payloadSize);
        }
    }

    FLASH_Lock();
    return ok;
}
