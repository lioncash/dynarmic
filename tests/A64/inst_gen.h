/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <cstring>
#include <vector>

#include "common/common_types.h"

struct InstructionGenerator final {
public:
    explicit InstructionGenerator(const char* format);

    u32 Generate() const;
    u32 Bits() const { return bits; }
    u32 Mask() const { return mask; }
    bool Match(u32 inst) const { return (inst & mask) == bits; }

    static void AddInvalidInstruction(const char* format) {
        invalid_instructions.emplace_back(InstructionGenerator{format});
    }

    static bool IsInvalidInstruction(u32 inst) {
        for (const auto& invalid : invalid_instructions)
            if (invalid.Match(inst))
                return true;
        return false;
    }

private:
    static std::vector<InstructionGenerator> invalid_instructions;

    u32 bits = 0;
    u32 mask = 0;
};
