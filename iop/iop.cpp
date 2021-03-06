#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iop/iop.hpp>
#include <iop/iop_interpreter.hpp>

#include <Bus.hpp>
#include <iop/disassembler.hpp>

IOP::IOP(Bus* bus)
: bus(bus)
{
    disasm_log = fopen("disassembly_iop.log", "w");
}

const char* IOP::REG(int id)
{
    static const char* names[] =
    {
        "zero", "at", "v0", "v1",
        "a0", "a1", "a2", "a3",
        "t0", "t1", "t2", "t3",
        "t4", "t5", "t6", "t7",
        "s0", "s1", "s2", "s3",
        "s4", "s5", "s6", "s7",
        "t8", "t9", "k0", "k1",
        "gp", "sp", "fp", "ra"
    };
    return names[id];
}

void IOP::reset()
{
    cop0.reset();
    PC = 0xBFC00000;
    memset(icache, 0, sizeof(icache));
    gpr[0] = 0;
    branch_delay = 0;
    will_branch = false;
    can_disassemble = false;
    wait_for_IRQ = false;
    muldiv_delay = 0;
    cycles_to_run = 0;
}

uint32_t IOP::translate_addr(uint32_t addr)
{
    //KSEG0
    if (addr >= 0x80000000 && addr < 0xA0000000)
        return addr - 0x80000000;
    //KSEG1
    if (addr >= 0xA0000000 && addr < 0xC0000000)
        return addr - 0xA0000000;

    //KUSEG, KSEG2
    return addr;
}

void IOP::run(int cycles)
{
    if (PC == 0x12C48 || PC == 0x1420C || PC == 0x1430C)
    {
        uint32_t ptr = gpr[5];
        uint32_t text_size = gpr[6];
        while (text_size)
        {
            auto c = (char)bus->iopRam[ptr & 0x1FFFFF];
            putc(c, stdout);

            ptr++;
            text_size--;
        }
    }
    if (!wait_for_IRQ)
    {
        cycles_to_run += cycles;
        while (cycles_to_run > 0)
        {
            cycles_to_run--;
            if (muldiv_delay > 0)
                muldiv_delay--;
            uint32_t instr = read_instr(PC);
            if (can_disassemble)
            {
                fprintf(disasm_log, "[IOP] [$%08X] $%08X - %s\n", PC, instr, EmotionDisasm::disasm_instr(instr, PC).c_str());
                fflush(disasm_log);
                //print_state();
            }
            IOP_Interpreter::interpret(*this, instr);

            PC += 4;

            if (will_branch)
            {
                if (!branch_delay)
                {
                    will_branch = false;
                    PC = new_PC;
                    if (PC & 0x3)
                    {
                        printf("[IOP] Invalid PC address $%08X!\n", PC);
                        exit(1);
                    }
                }
                else
                    branch_delay--;
            }
        }
    }
    else if (muldiv_delay)
        muldiv_delay--;

    if (cop0.status.IEc && (cop0.status.Im & cop0.cause.int_pending))
        interrupt();
}

void IOP::print_state()
{
    printf("pc:$%08X\n", PC);
    for (int i = 1; i < 32; i++)
    {
        printf("%s:$%08X", REG(i), get_gpr(i));
        if (i % 4 == 3)
            printf("\n");
        else
            printf("\t");
    }
    printf("lo:$%08X\thi:$%08X\n", LO, HI);
}

void IOP::set_disassembly(bool disasm)
{
    can_disassemble = disasm;
}

void IOP::jp(uint32_t addr)
{
    if (!will_branch)
    {
        new_PC = addr;
        will_branch = true;
        branch_delay = 1;
    }
}

void IOP::branch(bool condition, int32_t offset)
{
    if (condition)
        jp(PC + offset + 4);
}

void IOP::handle_exception(uint32_t addr, uint8_t cause)
{
    cop0.cause.code = cause;
    if (will_branch)
    {
        cop0.EPC = PC - 4;
        cop0.cause.bd = true;
    }
    else
    {
        cop0.EPC = PC;
        cop0.cause.bd = false;
    }
    cop0.status.IEo = cop0.status.IEp;
    cop0.status.IEp = cop0.status.IEc;
    cop0.status.IEc = false;

    //We do this to offset PC being incremented
    PC = addr - 4;
    branch_delay = 0;
    will_branch = false;
}

void IOP::syscall_exception()
{
    handle_exception(0x80000080, 0x08);
}

void IOP::interrupt_check(bool i_pass)
{
    if (i_pass)
    {
        printf("[IOP]: INT recieved\n");
        cop0.cause.int_pending |= 0x4;
    }
    else
        cop0.cause.int_pending &= ~0x4;
}

void IOP::interrupt()
{
    printf("[IOP] Processing interrupt!\n");
    handle_exception(0x80000084, 0x00);
    unhalt();
}

void IOP::mfc(int cop_id, int cop_reg, int reg)
{
    switch (cop_id)
    {
        case 0:
            set_gpr(reg, cop0.mfc(cop_reg));
            break;
        default:
            printf("\n[IOP] MFC: Unknown COP%d", cop_id);
            exit(1);
    }
}

void IOP::mtc(int cop_id, int cop_reg, int reg)
{
    uint32_t bark = get_gpr(reg);
    switch (cop_id)
    {
        case 0:
            cop0.mtc(cop_reg, bark);
            break;
        default:
            printf("\n[IOP] MTC: Unknown COP%d", cop_id);
            exit(1);
    }
}

void IOP::rfe()
{
    cop0.status.KUc = cop0.status.KUp;
    cop0.status.KUp = cop0.status.KUo;

    cop0.status.IEc = cop0.status.IEp;
    cop0.status.IEp = cop0.status.IEo;
    printf("[IOP] RFE!\n");
}

uint8_t IOP::read8(uint32_t addr)
{
    return bus->iop_read8(translate_addr(addr));
}

uint16_t IOP::read16(uint32_t addr)
{
    if (addr & 0x1)
    {
        printf("[IOP] Invalid read16 from $%08X\n", addr);
        exit(1);
    }
    return bus->iop_read16(translate_addr(addr));
}

uint32_t IOP::read32(uint32_t addr)
{
    if (addr & 0x3)
    {
        printf("[IOP] Invalid read32 from $%08X\n", addr);
        exit(1);
    }
    if (addr == 0xFFFE0130)
        return cache_control;
    return bus->iop_read32(translate_addr(addr));
}

uint32_t IOP::read_instr(uint32_t addr)
{
    if (addr >= 0xA0000000 || !(cache_control & (1 << 11)))
    {
        cycles_to_run -= 4;
        muldiv_delay = std::max(muldiv_delay - 4, 0);
    }

    return bus->iop_read32(addr & 0x1FFFFFFF);
}

void IOP::write8(uint32_t addr, uint8_t value)
{
    if (cop0.status.IsC)
        return;
    bus->iop_write8(translate_addr(addr), value);
}

void IOP::write16(uint32_t addr, uint16_t value)
{
    if (cop0.status.IsC)
        return;
    if (addr & 0x1)
    {
        printf("[IOP] Invalid write16 to $%08X!\n", addr);
        exit(1);
    }
    bus->iop_write16(translate_addr(addr), value);
}

void IOP::write32(uint32_t addr, uint32_t value)
{
    if (cop0.status.IsC)
    {
        icache[(addr >> 4) & 0xFF].valid = false;
        return;
    }
    if (addr & 0x3)
    {
        printf("[IOP] Invalid write32 to $%08X!\n", addr);
        exit(1);
    }
    if (addr = 0xFFFE0130)
        cache_control = value;
    bus->iop_write32(translate_addr(addr), value);
}