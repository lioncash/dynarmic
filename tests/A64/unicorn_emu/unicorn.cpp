/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "common/assert.h"
#include "unicorn.h"

#define CHECKED(expr)                                                                              \
    do {                                                                                           \
        if (auto cerr_ = (expr)) {                                                                 \
            ASSERT_MSG(false, "Call " #expr " failed with error: %u (%s)\n", cerr_,                \
                       uc_strerror(cerr_));                                                        \
        }                                                                                          \
    } while (0)

constexpr u64 BEGIN_ADDRESS = 0;
constexpr u64 END_ADDRESS = ~u64(0);

Unicorn::Unicorn(TestEnv& testenv) : testenv(testenv) {
    CHECKED(uc_open(UC_ARCH_ARM64, UC_MODE_ARM, &uc));
    u64 fpv = 3 << 20;
    CHECKED(uc_reg_write(uc, UC_ARM64_REG_CPACR_EL1, &fpv));
    CHECKED(uc_hook_add(uc, &intr_hook, UC_HOOK_INTR, (void*)InterruptHook, this, BEGIN_ADDRESS, END_ADDRESS));
    CHECKED(uc_hook_add(uc, &mem_invalid_hook, UC_HOOK_MEM_INVALID, (void*)UnmappedMemoryHook, this, BEGIN_ADDRESS, END_ADDRESS));
    CHECKED(uc_hook_add(uc, &mem_write_prot_hook, UC_HOOK_MEM_WRITE, (void*)MemoryWriteHook, this, BEGIN_ADDRESS, END_ADDRESS));
}

Unicorn::~Unicorn() {
    for (const auto& page : pages) {
        CHECKED(uc_mem_unmap(uc, page->address, 4096));
    }
    CHECKED(uc_hook_del(uc, intr_hook));
    CHECKED(uc_hook_del(uc, mem_invalid_hook));
    CHECKED(uc_close(uc));
}

void Unicorn::Run() {
    CHECKED(uc_emu_start(uc, GetPC(), END_ADDRESS, 0, testenv.ticks_left));
    testenv.ticks_left = 0;
}

u64 Unicorn::GetSP() const {
    u64 sp;
    CHECKED(uc_reg_read(uc, UC_ARM64_REG_SP, &sp));
    return sp;
}
void Unicorn::SetSP(u64 value) {
    CHECKED(uc_reg_write(uc, UC_ARM64_REG_SP, &value));
}

u64 Unicorn::GetPC() const {
    u64 pc;
    CHECKED(uc_reg_read(uc, UC_ARM64_REG_PC, &pc));
    return pc;
}

void Unicorn::SetPC(u64 value) {
    CHECKED(uc_reg_write(uc, UC_ARM64_REG_PC, &value));
}

constexpr size_t num_gprs = 31;
static const std::array<int, num_gprs> gpr_ids {
    UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2, UC_ARM64_REG_X3, UC_ARM64_REG_X4, UC_ARM64_REG_X5, UC_ARM64_REG_X6, UC_ARM64_REG_X7,
    UC_ARM64_REG_X8, UC_ARM64_REG_X9, UC_ARM64_REG_X10, UC_ARM64_REG_X11, UC_ARM64_REG_X12, UC_ARM64_REG_X13, UC_ARM64_REG_X14, UC_ARM64_REG_X15,
    UC_ARM64_REG_X16, UC_ARM64_REG_X17, UC_ARM64_REG_X18, UC_ARM64_REG_X19, UC_ARM64_REG_X20, UC_ARM64_REG_X21, UC_ARM64_REG_X22, UC_ARM64_REG_X23,
    UC_ARM64_REG_X24, UC_ARM64_REG_X25, UC_ARM64_REG_X26, UC_ARM64_REG_X27, UC_ARM64_REG_X28, UC_ARM64_REG_X29, UC_ARM64_REG_X30
};

std::array<u64, 31> Unicorn::GetRegisters() const {
    std::array<u64, num_gprs> regs;
    std::array<u64*, num_gprs> ptrs;
    for (size_t i = 0; i < num_gprs; ++i)
        ptrs[i] = &regs[i];

    CHECKED(uc_reg_read_batch(uc, const_cast<int*>(gpr_ids.data()), (void**)ptrs.data(), num_gprs));
    return regs;
}

void Unicorn::SetRegisters(const std::array<u64, 31>& value) {
    std::array<const u64*, num_gprs> ptrs;
    for (size_t i = 0; i < num_gprs; ++i)
        ptrs[i] = &value[i];

    CHECKED(uc_reg_write_batch(uc, const_cast<int*>(gpr_ids.data()), (void**)ptrs.data(), num_gprs));
}

using Vector = Unicorn::Vector;
constexpr size_t num_vecs = 32;
static const std::array<int, num_vecs> vec_ids {
    UC_ARM64_REG_Q0, UC_ARM64_REG_Q1, UC_ARM64_REG_Q2, UC_ARM64_REG_Q3, UC_ARM64_REG_Q4, UC_ARM64_REG_Q5, UC_ARM64_REG_Q6, UC_ARM64_REG_Q7,
    UC_ARM64_REG_Q8, UC_ARM64_REG_Q9, UC_ARM64_REG_Q10, UC_ARM64_REG_Q11, UC_ARM64_REG_Q12, UC_ARM64_REG_Q13, UC_ARM64_REG_Q14, UC_ARM64_REG_Q15,
    UC_ARM64_REG_Q16, UC_ARM64_REG_Q17, UC_ARM64_REG_Q18, UC_ARM64_REG_Q19, UC_ARM64_REG_Q20, UC_ARM64_REG_Q21, UC_ARM64_REG_Q22, UC_ARM64_REG_Q23,
    UC_ARM64_REG_Q24, UC_ARM64_REG_Q25, UC_ARM64_REG_Q26, UC_ARM64_REG_Q27, UC_ARM64_REG_Q28, UC_ARM64_REG_Q29, UC_ARM64_REG_Q30, UC_ARM64_REG_Q31
};

std::array<Vector, 32> Unicorn::GetVectors() const {
    std::array<Vector, num_vecs> vecs;
    std::array<Vector*, num_vecs> ptrs;
    for (size_t i = 0; i < num_vecs; ++i)
        ptrs[i] = &vecs[i];

    CHECKED(uc_reg_read_batch(uc, const_cast<int*>(vec_ids.data()), (void**)ptrs.data(), num_vecs));

    return vecs;
}

void Unicorn::SetVectors(const std::array<Vector, 32>& value) {
    std::array<const Vector*, num_vecs> ptrs;
    for (size_t i = 0; i < num_vecs; ++i)
        ptrs[i] = &value[i];

    CHECKED(uc_reg_write_batch(uc, const_cast<int*>(vec_ids.data()), (void**)ptrs.data(), num_vecs));
}

u32 Unicorn::GetFpcr() const {
    u32 fpcr;
    CHECKED(uc_reg_read(uc, UC_ARM64_REG_FPCR, &fpcr));
    return fpcr;
}

void Unicorn::SetFpcr(u32 value) {
    CHECKED(uc_reg_write(uc, UC_ARM64_REG_FPCR, &value));
}

u32 Unicorn::GetPstate() const {
    u32 pstate;
    CHECKED(uc_reg_read(uc, UC_ARM64_REG_NZCV, &pstate));
    return pstate;
}

void Unicorn::SetPstate(u32 value) {
    CHECKED(uc_reg_write(uc, UC_ARM64_REG_NZCV, &value));
}

void Unicorn::ClearPageCache() {
    pages.clear();
}

void Unicorn::DumpMemoryInformation() {
    uc_mem_region* regions;
    u32 count;
    CHECKED(uc_mem_regions(uc, &regions, &count));

    for (u32 i = 0; i < count; ++i) {
        printf("region: start 0x%016" PRIx64 " end 0x%016" PRIx64 " perms 0x%08x\n", regions[i].begin, regions[i].end, regions[i].perms);
    }

    CHECKED(uc_free(regions));
}

void Unicorn::InterruptHook(uc_engine* uc, u32 int_number, void* user_data) {
    auto* this_ = static_cast<Unicorn*>(user_data);

    u32 esr;
    CHECKED(uc_reg_read(uc, UC_ARM64_REG_ESR, &esr));

    auto ec = esr >> 26;
    auto iss = esr & 0xFFFFFF;

    switch (ec) {
    case 0x15: // SVC
        this_->testenv.CallSVC(iss);
        break;
    default:
        ASSERT_MSG(false, "Unhandled interrupt: int_number: %#x, esr: %#x", int_number, esr);
    }
}

bool Unicorn::UnmappedMemoryHook(uc_engine* uc, uc_mem_type /*type*/, u64 start_address, int size, u64 /*value*/, void* user_data) {
    auto* this_ = static_cast<Unicorn*>(user_data);

    const auto generate_page = [&](u64 base_address) {
        // printf("generate_page(%" PRIx64 ")\n", base_address);

        const u32 permissions = [&]() -> u32 {
            if (base_address < this_->testenv.code_mem.size() * 4)
                return UC_PROT_READ | UC_PROT_EXEC;
            return UC_PROT_READ;
        }();

        auto page = std::make_unique<Page>();
        page->address = base_address;
        for (size_t i = 0; i < page->data.size(); ++i)
            page->data[i] = this_->testenv.MemoryRead8(base_address + i);

        uc_err err = uc_mem_map_ptr(uc, base_address, page->data.size(), permissions, page->data.data());
        if (err == UC_ERR_MAP)
            return; // page already exists
        CHECKED(err);

        this_->pages.emplace_back(std::move(page));
    };

    const auto is_in_range = [](u64 addr, u64 start, u64 end) {
        if (start <= end)
            return addr >= start && addr <= end; // fffff[tttttt]fffff
        return addr >= start || addr <= end;     // ttttt]ffffff[ttttt
    };

    const u64 start_address_page = start_address & ~u64(0xFFF);
    const u64 end_address = start_address + size - 1;

    u64 current_address = start_address_page;
    do {
        generate_page(current_address);
        current_address += 0x1000;
    } while (is_in_range(current_address, start_address_page, end_address) && current_address != start_address_page);

    return true;
}

bool Unicorn::MemoryWriteHook(uc_engine* /*uc*/, uc_mem_type /*type*/, u64 start_address, int size, u64 value, void* user_data) {
    auto* this_ = static_cast<Unicorn*>(user_data);

    switch (size) {
    case 1:
        this_->testenv.MemoryWrite8(start_address, static_cast<u8>(value));
        break;
    case 2:
        this_->testenv.MemoryWrite16(start_address, static_cast<u16>(value));
        break;
    case 4:
        this_->testenv.MemoryWrite32(start_address, static_cast<u32>(value));
        break;
    case 8:
        this_->testenv.MemoryWrite64(start_address, value);
        break;
    default:
        UNREACHABLE();
    }

    return true;
}
