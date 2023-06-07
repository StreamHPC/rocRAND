// Copyright (c) 2023 Advanced Micro Devices, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

/// \file benchmarked_generators.hpp
/// This header should contain the benchmarking setup for specific rocRAND generators.
/// E.g. specialization of the \c output_type_supported class template.
/// Additionally, an \c extern \c template declaration could be present for each
/// benchmarked generator, to offload compilation of the benchmarks to multiple translation
/// units.

#ifndef ROCRAND_BENCHMARK_TUNING_BENCHMARKED_GENERATORS_HPP_
#define ROCRAND_BENCHMARK_TUNING_BENCHMARKED_GENERATORS_HPP_

#include "benchmark_tuning.hpp"

template<class ConfigProvider>
class rocrand_xorwow_template;

namespace benchmark_tuning
{

template<>
struct output_type_supported<unsigned long long, rocrand_xorwow_template> : public std::false_type
{};

extern template void add_all_benchmarks_for_generator<rocrand_xorwow_template>(
    std::vector<benchmark::internal::Benchmark*>& benchmarks, const benchmark_config& config);

} // namespace benchmark_tuning

#endif
