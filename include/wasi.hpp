#pragma once

#include <cstdint>
#include <memory>


namespace wasi
{

using u8  = uint8_t;
using u32 = uint32_t;
using u64 = uint64_t;

using i8  = int8_t;
using i32 = int32_t;
using i64 = int64_t;


template <typename T>
using u_ptr = std::unique_ptr<T>;

template <typename T>
using s_ptr = std::shared_ptr<T>;

template <typename T>
using w_ptr = std::weak_ptr<T>;


}
