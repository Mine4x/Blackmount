#ifndef ACPI_H
#define ACPI_H

#include <stdint.h>
#include <stddef.h>

#define SLP_EN (1 << 13)
#define S5_SLEEP_TYPE 0x05

#include "acpi.h"
#include <stdint.h>

// ACPI 1.0
struct RSDP_t {
    char Signature[8];
    uint8_t Checksum;
    char OEMID[6];
    uint8_t Revision;
    uint32_t RsdtAddress;
} __attribute__((packed));

// ACPI 2.0+
struct XSDP_t {
    char Signature[8];
    uint8_t Checksum;
    char OEMID[6];
    uint8_t Revision;
    uint32_t RsdtAddress;

    uint32_t Length;
    uint64_t XsdtAddress;
    uint8_t ExtendedChecksum;
    uint8_t reserved[3];
} __attribute__((packed));

struct ACPISDTHeader {
    char Signature[4];
    uint32_t Length;
    uint8_t Revision;
    uint8_t Checksum;
    char OEMID[6];
    char OEMTableID[8];
    uint32_t OEMRevision;
    uint32_t CreatorID;
    uint32_t CreatorRevision;
} __attribute__((packed));

struct FADT_t {
    struct ACPISDTHeader header;
    uint32_t FirmwareCtrl;
    uint32_t Dsdt;
    uint8_t reserved;
    uint8_t PreferredPMProfile;
    uint16_t SciInt;
    uint32_t SmiCmd;
    uint8_t AcpiEnable;
    uint8_t AcpiDisable;
    uint8_t S4BiosReq;
    uint8_t PstateCnt;
    uint32_t PM1aEvtBlk;
    uint32_t PM1bEvtBlk;
    uint32_t PM1aCntBlk;
    uint32_t PM1bCntBlk;
    uint32_t PM2CntBlk;
    uint32_t PMTmrBlk;
    uint32_t Gpe0Blk;
    uint32_t Gpe1Blk;
    uint8_t PM1EvtLen;
    uint8_t PM1CntLen;
    uint8_t PM2CntLen;
    uint8_t PMTmLen;
    uint8_t Gpe0BlkLen;
    uint8_t Gpe1BlkLen;
    uint8_t Gpe1Base;
    uint8_t CstCnt;
    uint16_t PLvl2Lat;
    uint16_t PLvl3Lat;
    uint16_t FlushSize;
    uint16_t FlushStride;
    uint8_t DutyOffset;
    uint8_t DutyWidth;
    uint8_t DayAlrm;
    uint8_t MonAlrm;
    uint8_t Century;
    uint16_t IapcBootArch;
    uint8_t Reserved2;
    uint32_t Flags;
} __attribute__((packed));

void acpi_init();
void* acpi_find_table(const char* signature);
void acpi_shutdown();

#endif
