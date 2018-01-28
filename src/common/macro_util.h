/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

#define CONCATENATE_TOKENS(x, y) CONCATENATE_TOKENS_IMPL(x, y)
#define CONCATENATE_TOKENS_IMPL(x, y) x ## y

#ifdef __COUNTER__
#define ANONYMOUS_VARIABLE(str) CONCATENATE_TOKENS(str, __COUNTER__)
#else
#define ANONYMOUS_VARIABLE(str) CONCATENATE_TOKENS(str, __LINE__)
#endif
