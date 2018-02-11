/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

#include <cstddef>
#include <tuple>

namespace Dynarmic::mp {

/// Used to provide information about an arbitrary function.
template <typename Function>
struct FunctionInfo : public FunctionInfo<decltype(&Function::operator())>
{
};

/**
 * Partial specialization for function types.
 *
 * This is used as the supporting base for all other specializations.
 */
template <typename R, typename... Args>
struct FunctionInfo<R(Args...)>
{
    using return_type = R;
    static constexpr size_t args_count = sizeof...(Args);

    template <size_t ParameterIndex>
    struct Parameter
    {
        static_assert(args_count != 0 && ParameterIndex < args_count, "Non-existent function parameter index");
        using type = std::tuple_element_t<ParameterIndex, std::tuple<Args...>>;
    };

    using equivalent_function_type = R(Args...);
};

/// Partial specialization for function pointers
template <typename R, typename... Args>
struct FunctionInfo<R(*)(Args...)> : public FunctionInfo<R(Args...)>
{
};

/// Partial specialization for member function pointers.
template <typename C, typename R, typename... Args>
struct FunctionInfo<R(C::*)(Args...)> : public FunctionInfo<R(Args...)>
{
    using class_type = C;
};

/// Partial specialization for const member function pointers.
template <typename C, typename R, typename... Args>
struct FunctionInfo<R(C::*)(Args...) const> : public FunctionInfo<R(Args...)>
{
    using class_type = C;
};

/**
 * Helper template for retrieving the type of a function parameter.
 *
 * @tparam Function       An arbitrary function type.
 * @tparam ParameterIndex Zero-based index indicating which parameter to get the type of.
 */
template <typename Function, size_t ParameterIndex>
using parameter_type_t = typename FunctionInfo<Function>::template Parameter<ParameterIndex>::type;

/**
 * Helper template for retrieving the return type of a function.
 *
 * @tparam Function The function type to get the return type of.
 */
template <typename Function>
using return_type_t = typename FunctionInfo<Function>::return_type;

/**
 * Helper template for retrieving the class type of a member function.
 *
 * @tparam Function The function type to get the return type of.
 */
template <typename Function>
using class_type_t = typename FunctionInfo<Function>::class_type;

/**
 * Helper template for retrieving the equivalent function type of a member function or functor.
 *
 * @tparam Function The function type to get the return type of.
 */
template <typename Function>
using equivalent_function_type_t = typename FunctionInfo<Function>::equivalent_function_type;

} // namespace Dynarmic::mp
