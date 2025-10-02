#pragma once

#include "driver/gpio.h"
#include "mining.h"
#include "rom/gpio.h"
#include "asic.h"

class BM1366 : public Asic {
protected:
    virtual const uint8_t* getChipId();
    virtual uint32_t getDefaultVrFrequency();

    virtual uint8_t jobToAsicId(uint8_t job_id);
    virtual uint8_t asicToJobId(uint8_t asic_id);
    virtual uint8_t nonceToAsicNr(uint32_t nonce);

public:
    BM1366();
    virtual const char* getName() { return "BM1366"; };
    virtual uint8_t init(uint64_t frequency, uint16_t asic_count, uint32_t difficulty, uint32_t vrFrequency);
    virtual uint16_t getSmallCoreCount();
};

