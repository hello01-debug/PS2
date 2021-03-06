#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <iop/iop_cop0.hpp>

class Bus;

struct IOP_ICacheLine
{
    bool valid;
    uint32_t tag;
};

class IOP
{
private:
    Bus* bus;
    IOP_Cop0 cop0;
    uint32_t PC;
    uint32_t LO, HI;

    IOP_ICacheLine icache[256];

    uint32_t new_PC;
    uint32_t cache_control;
    int branch_delay;
    bool can_disassemble;
    bool will_branch;
    bool wait_for_IRQ;

    int muldiv_delay;
    int cycles_to_run;

    FILE* disasm_log;

    uint32_t translate_addr(uint32_t addr);
public:
    uint32_t gpr[32];
    IOP(Bus* bus);
    ~IOP()
    {
        fclose(disasm_log);
    }
    static const char* REG(int id);

    void reset();
    void run(int cycles);
    void halt();
    void unhalt();
    void print_state();
    void set_disassembly(bool dis);
    void set_muldiv_delay(int delay);

    void jp(uint32_t addr);
    void branch(bool condition, int32_t offset);

    void handle_exception(uint32_t addr, uint8_t cause);
    void syscall_exception();
    void interrupt_check(bool i_pass);
    void interrupt();
    void rfe();

    void mfc(int cop_id, int cop_reg, int reg);
    void mtc(int cop_id, int cop_reg, int reg);
    void mtc();

    uint32_t get_PC();
    uint32_t get_gpr(int index);
    uint32_t get_LO();
    uint32_t get_HI();
    void set_PC(uint32_t value);
    void set_gpr(int index, uint32_t value);
    void set_LO(uint32_t value);
    void set_HI(uint32_t value);

    uint8_t read8(uint32_t addr);
    uint16_t read16(uint32_t addr);
    uint32_t read32(uint32_t addr);
    uint32_t read_instr(uint32_t addr);
    void write8(uint32_t addr, uint8_t data);
    void write16(uint32_t addr, uint16_t data);
    void write32(uint32_t addr, uint32_t data);
};

inline void IOP::halt()
{
    wait_for_IRQ = true;
}

inline void IOP::unhalt()
{
    wait_for_IRQ = false;
}

inline uint32_t IOP::get_PC()
{
    return PC;
}

inline uint32_t IOP::get_gpr(int index)
{
    return gpr[index];
}

inline uint32_t IOP::get_LO()
{
    if (muldiv_delay)
    {
        cycles_to_run -= muldiv_delay;
        muldiv_delay = 0;
    }
    return LO;
}

inline uint32_t IOP::get_HI()
{
    if (muldiv_delay)
    {
        cycles_to_run -= muldiv_delay;
        muldiv_delay = 0;
    }
    return HI;
}

inline void IOP::set_PC(uint32_t value)
{
    PC = value;
}

inline void IOP::set_gpr(int index, uint32_t value)
{
    if (index)
        gpr[index] = value;
}

inline void IOP::set_LO(uint32_t value)
{
    LO = value;
}

inline void IOP::set_HI(uint32_t value)
{
    HI = value;
}

inline void IOP::set_muldiv_delay(int delay)
{
    if (muldiv_delay)
        cycles_to_run -= muldiv_delay;
    muldiv_delay = delay;
}