/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

#include <boost/variant.hpp>

#include "common/common_types.h"
#include "frontend/ir/cond.h"
#include "frontend/ir/location_descriptor.h"

namespace Dynarmic {
namespace IR {
namespace Term {

struct Invalid {};

/**
 * This terminal instruction calls the interpreter, starting at `next`.
 * The interpreter must interpret exactly one instruction.
 */
struct Interpret {
    explicit Interpret(const LocationDescriptor& next_) : next(next_) {}
    LocationDescriptor next; ///< Location at which interpretation starts.
};

/**
 * This terminal instruction returns control to the dispatcher.
 * The dispatcher will use the value in R15 to determine what comes next.
 */
struct ReturnToDispatch {};

/**
 * This terminal instruction jumps to the basic block described by `next` if we have enough
 * cycles remaining. If we do not have enough cycles remaining, we return to the
 * dispatcher, which will return control to the host.
 */
struct LinkBlock {
    explicit LinkBlock(const LocationDescriptor& next_) : next(next_) {}
    LocationDescriptor next; ///< Location descriptor for next block.
};

/**
 * This terminal instruction jumps to the basic block described by `next` unconditionally.
 * This is an optimization and MUST only be emitted when this is guaranteed not to result
 * in hanging, even in the face of other optimizations. (In practice, this means that only
 * forward jumps to short-ish blocks would use this instruction.)
 * A backend that doesn't support this optimization may choose to implement this exactly
 * as LinkBlock.
 */
struct LinkBlockFast {
    explicit LinkBlockFast(const LocationDescriptor& next_) : next(next_) {}
    LocationDescriptor next; ///< Location descriptor for next block.
};

/**
 * This terminal instruction checks the top of the Return Stack Buffer against R15.
 * If RSB lookup fails, control is returned to the dispatcher.
 * This is an optimization for faster function calls. A backend that doesn't support
 * this optimization or doesn't have a RSB may choose to implement this exactly as
 * ReturnToDispatch.
 */
struct PopRSBHint {};

struct If;
struct CheckHalt;
/// A Terminal is the terminal instruction in a MicroBlock.
using Terminal = boost::variant<
        Invalid,
        Interpret,
        ReturnToDispatch,
        LinkBlock,
        LinkBlockFast,
        PopRSBHint,
        boost::recursive_wrapper<If>,
        boost::recursive_wrapper<CheckHalt>
>;

/**
 * This terminal instruction conditionally executes one terminal or another depending
 * on the run-time state of the ARM flags.
 */
struct If {
    If(Cond if_, Terminal then_, Terminal else_) : if_(if_), then_(then_), else_(else_) {}
    Cond if_;
    Terminal then_;
    Terminal else_;
};

/**
 * This terminal instruction checks if a halt was requested. If it wasn't, else_ is
 * executed.
 */
struct CheckHalt {
    explicit CheckHalt(Terminal else_) : else_(else_) {}
    Terminal else_;
};

} // namespace Term

using Term::Terminal;

} // namespace IR
} // namespace Dynarmic
