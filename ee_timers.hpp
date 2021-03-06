#pragma once

#include <cstdint>

constexpr uint32_t EE_CLOCK = 294912000;
constexpr uint32_t BUS_CLOCK = EE_CLOCK / 2;
constexpr uint32_t HBLANK_NTSC = 15734;
constexpr uint32_t HBLANK_PAL = 15625;

union TnMode
{
    uint32_t value;
    struct
    {
        uint32_t clock : 2;
        uint32_t gate_enable : 1;
        uint32_t gate_type : 1;
        uint32_t gate_mode : 2;
        uint32_t clear_when_cmp : 1;
        uint32_t enable : 1;
        uint32_t cmp_intr_enable : 1;
        uint32_t overflow_intr_enable : 1;
        uint32_t cmp_flag : 1;
        uint32_t overflow_flag : 1;
        uint32_t : 20;
    };
};

struct Timer
{
    uint32_t counter;
    TnMode mode;
    uint32_t compare;
    uint32_t hold;
    uint32_t ratio;
};

class INTC;
class Timers
{
public:
    Timers(INTC* intc);
    ~Timers() = default;

    void tick(uint32_t cycles);

    uint32_t read(uint32_t addr);
    void write(uint32_t addr, uint32_t data);

private:
    INTC* intc;
    Timer timers[4] = {};
};