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
    explicit Unicorn(TestEnv& testenv);
    ~Unicorn();

    void Run();

    u64 GetSP() const;
    void SetSP(u64 value);

    u64 GetPC() const;
    void SetPC(u64 value);

    std::array<u64, 31> GetRegisters() const;
    void SetRegisters(const std::array<u64, 31>& value);

    using Vector = std::array<u64, 2>;
    static_assert(sizeof(Vector) == sizeof(u64) * 2);

    std::array<Vector, 32> GetVectors() const;
    void SetVectors(const std::array<Vector, 32>& value);

    u32 GetFpcr() const;
    void SetFpcr(u32 value);

    u32 GetPstate() const;
    void SetPstate(u32 value);

    void ClearPageCache();

    void DumpMemoryInformation();

private:
    static void InterruptHook(uc_engine* uc, u32 interrupt, void* user_data);
    static bool UnmappedMemoryHook(uc_engine* uc, uc_mem_type type, u64 addr, int size, u64 value, void* user_data);

    struct Page {
        u64 address;
        std::array<u8, 4096> data;
    };

    TestEnv& testenv;
    uc_engine* uc{};
    uc_hook intr_hook{};
    uc_hook mem_invalid_hook{};

    std::vector<Page*> pages;
};
