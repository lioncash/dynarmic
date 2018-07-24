/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

#include <array>
#include <vector>

#include <unicorn/unicorn.h>

#include "common/common_types.h"
#include "../testenv.h"

class Unicorn final {
public:
    static constexpr size_t num_gprs = 31;
    using RegisterArray = std::array<u64, num_gprs>;
    using RegisterPtrArray = std::array<RegisterArray::pointer, num_gprs>;
    using RegisterConstPtrArray = std::array<RegisterArray::const_pointer, num_gprs>;

    static constexpr size_t num_vecs = 32;
    using VectorArray = std::array<Vector, num_vecs>;
    using VectorPtrArray = std::array<VectorArray::pointer, num_vecs>;
    using VectorConstPtrArray = std::array<VectorArray::const_pointer, num_vecs>;

    explicit Unicorn(TestEnv& testenv);
    ~Unicorn();

    void Run();

    u64 GetSP() const;
    void SetSP(u64 value);

    u64 GetPC() const;
    void SetPC(u64 value);

    RegisterArray GetRegisters() const;
    void SetRegisters(const RegisterArray& value);

    VectorArray GetVectors() const;
    void SetVectors(const VectorArray& value);

    u32 GetFpcr() const;
    void SetFpcr(u32 value);

    u32 GetFpsr() const;
    void SetFpsr(u32 value);

    u32 GetPstate() const;
    void SetPstate(u32 value);

    void ClearPageCache();

    void DumpMemoryInformation();

private:
    static void InterruptHook(uc_engine* uc, u32 interrupt, void* user_data);
    static bool UnmappedMemoryHook(uc_engine* uc, uc_mem_type type, u64 addr, int size, u64 value, void* user_data);
    static bool MemoryWriteHook(uc_engine* uc, uc_mem_type type, u64 addr, int size, u64 value, void* user_data);

    struct Page {
        u64 address;
        std::array<u8, 4096> data;
    };

    TestEnv& testenv;
    uc_engine* uc{};
    uc_hook intr_hook{};
    uc_hook mem_invalid_hook{};
    uc_hook mem_write_prot_hook{};

    std::vector<std::unique_ptr<Page>> pages;
};
