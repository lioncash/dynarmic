/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

// Load Unicorn DLL once on Windows using RAII
#ifdef _WIN32
#include <unicorn/arm64.h>
#include <unicorn_dynload.h>

#include "common/assert.h"

static struct LoadDll {
public:
    LoadDll() {
        ASSERT(uc_dyn_load(NULL, 0));
    }
    ~LoadDll() {
        ASSERT(uc_dyn_free());
    }
    static LoadDll g_load_dll;
} load_dll;
#endif
