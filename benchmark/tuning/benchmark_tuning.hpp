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

#ifndef ROCRAND_BENCHMARK_TUNING_HPP_
#define ROCRAND_BENCHMARK_TUNING_HPP_

#include <tuple>
#include <type_traits>
#include <utility>

#include "benchmark/benchmark.h"
#include "hip/hip_runtime.h"
#include "rocrand/rocrand.h"

#include "rng/config_types.hpp"
#include "rng/cpp_utils.hpp"
#include "rng/distributions.hpp"

#include "benchmark_rocrand_utils.hpp"
#include "benchmark_tuning_setup.hpp"
#include "distribution_traits.hpp"

namespace benchmark_tuning
{

/// @brief Controls whether the specified type \ref T can be generated
/// by the specified \ref GeneratorTemplate.
/// @tparam T Type of the generated values.
template<class T, template<class> class GeneratorTemplate>
struct output_type_supported : public std::true_type
{};

using rocrand_host::detail::generator_config;

/// @brief ConfigProvider that always returns a config with the specified \ref Blocks and \ref Threads.
/// This can be used in place of \ref rocrand_host::detail::default_config_provider, which bases the
/// returned configuration on the current architecture.
/// @tparam Threads The number of threads in the kernel block.
/// @tparam Blocks The number of blocks in the kernel grid.
template<unsigned int Threads, unsigned int Blocks>
struct static_config_provider
{
    static constexpr inline generator_config static_config = {Threads, Blocks};

    template<class>
    constexpr generator_config device_config(const bool /*is_dynamic*/)
    {
        return static_config;
    }

    template<class>
    hipError_t host_config(const hipStream_t /*stream*/,
                           const rocrand_ordering /*ordering*/,
                           generator_config& config)
    {
        config = static_config;
        return hipSuccess;
    }
};

} // namespace benchmark_tuning

namespace benchmark_tuning
{

/// @brief Runs the googlebenchmark for the specified generator, output type and distribution.
/// @tparam T The generated value type.
/// @tparam Generator The type rocRAND generator to use for the RNG.
/// @tparam Distribution The rocRAND distribution to generate.
/// @param state Benchmarking state.
/// @param config Benchmark config, controlling e.g. the size of the generated random array.
template<class T, class Generator, class Distribution>
void run_benchmark(benchmark::State& state, const benchmark_config& config)
{
    const hipStream_t stream = 0;

    T* data;
    HIP_CHECK(hipMalloc(&data, config.size * sizeof(T)));

    Generator generator;
    generator.set_stream(stream);

    const auto generate_func = [&]
    { return generator.generate(data, config.size, default_distribution<Distribution>{}(config)); };

    // Warm-up
    ROCRAND_CHECK(generate_func());
    HIP_CHECK(hipDeviceSynchronize());

    hipEvent_t start, stop;
    HIP_CHECK(hipEventCreate(&start));
    HIP_CHECK(hipEventCreate(&stop));
    for(auto _ : state)
    {
        HIP_CHECK(hipEventRecord(start, stream));
        ROCRAND_CHECK(generate_func());
        HIP_CHECK(hipEventRecord(stop, stream));
        HIP_CHECK(hipEventSynchronize(stop));

        float elapsed = 0.0f;
        HIP_CHECK(hipEventElapsedTime(&elapsed, start, stop));

        state.SetIterationTime(elapsed / 1000.f);
    }
    state.SetBytesProcessed(state.iterations() * config.size * sizeof(T));
    state.SetItemsProcessed(state.iterations() * config.size);

    HIP_CHECK(hipEventDestroy(stop));
    HIP_CHECK(hipEventDestroy(start));
    HIP_CHECK(hipFree(data));
}

/// @brief Helper class to instantiate all benchmarks with the specified \ref GeneratorTemplate.
template<template<class ConfigProvider> class GeneratorTemplate>
class generator_benchmark_factory
{
public:
    generator_benchmark_factory(const benchmark_config&                       config,
                                std::vector<benchmark::internal::Benchmark*>& benchmarks)
        : m_config(config), m_benchmarks(benchmarks)
    {}

    /// @brief Instantiate benchmarks with all supported distributions for the specified value type.
    /// @tparam T The generated value type.
    template<class T>
    void add_benchmarks()
    {
        if constexpr(!output_type_supported<T, GeneratorTemplate>::value)
        {
            // If the generator doesn't support the requested type, just return.
            return;
        }
        else if constexpr(std::is_integral_v<T>)
        {
            // The template signature of the usable uniform distribution is slightly different
            // in the unsigned long long case.
            using UniformDistribution
                = std::conditional_t<std::is_same_v<T, unsigned long long>,
                                     uniform_distribution<unsigned long long, unsigned long long>,
                                     uniform_distribution<T>>;

            add_benchmarks_impl<T, UniformDistribution>();

            if constexpr(std::is_same_v<T, unsigned int>)
            {
                // The poisson distribution is only supported for unsigned int.
                add_benchmarks_impl<T,
                                    rocrand_poisson_distribution<ROCRAND_DISCRETE_METHOD_ALIAS>>();
            }
        }
        else if constexpr(std::is_floating_point_v<T> || std::is_same_v<T, half>)
        {
            // float, double and half support these distributions only.
            add_benchmarks_impl<T, uniform_distribution<T>>();
            add_benchmarks_impl<T, normal_distribution<T>>();
            add_benchmarks_impl<T, log_normal_distribution<T>>();
        }
    }

private:
    benchmark_config                              m_config;
    std::vector<benchmark::internal::Benchmark*>& m_benchmarks;

    // This is an array of arrays, listing all {threads,blocks} pairs that run for the benchmark tuning.
    // The elements of the arrays can be controlled with CMake cache variables
    // BENCHMARK_TUNING_THREAD_OPTIONS and BENCHMARK_TUNING_BLOCK_OPTIONS
    static constexpr inline auto s_param_combinations
        = cpp_utils::numeric_combinations(thread_options, block_options);

    template<class Distribution, class StaticConfigProvider>
    static std::string get_benchmark_name()
    {
        using Generator                 = GeneratorTemplate<StaticConfigProvider>;
        const rocrand_rng_type rng_type = Generator{}.type();
        return engine_name(rng_type) + "_" + distribution_name<Distribution>{}() + "_t"
               + std::to_string(StaticConfigProvider::static_config.threads) + "_b"
               + std::to_string(StaticConfigProvider::static_config.blocks);
    }

    template<class T, class Distribution>
    void add_benchmarks_impl()
    {
        add_benchmarks_impl<T, Distribution>(
            std::make_index_sequence<s_param_combinations.size()>());
    }

    template<class T, class Distribution, std::size_t... Indices>
    void add_benchmarks_impl(std::index_sequence<Indices...>)
    {
        // Execute the following lambda for all configuration combinations
        ((
             [&]
             {
                 constexpr auto combination_idx     = Indices;
                 constexpr auto current_combination = s_param_combinations[combination_idx];
                 constexpr auto threads             = std::get<0>(current_combination);
                 constexpr auto blocks              = std::get<1>(current_combination);
                 constexpr auto grid_size           = threads * blocks;

                 // If the grid size is very small, it wouldn't make sense to run the benchmarks for it
                 // The threshold is controlled by CMake cache variable BENCHMARK_TUNING_MIN_GRID_SIZE
                 if constexpr(grid_size < min_benchmarked_grid_size)
                     return;

                 using ConfigProvider = static_config_provider<threads, blocks>;

                 const auto benchmark_name = get_benchmark_name<Distribution, ConfigProvider>();

                 // Append the benchmark to the list using the appropriate ConfigProvider.
                 // Note that captures must be by-value. This class instance won't live to see
                 // the execution of the benchmarks.
                 m_benchmarks.push_back(benchmark::RegisterBenchmark(
                     benchmark_name.c_str(),
                     [*this](auto& state) {
                         run_benchmark<T, GeneratorTemplate<ConfigProvider>, Distribution>(
                             state,
                             m_config);
                     }));
             }()),
         ...);
    }
};

/// @brief Instantiate all benchmarks for the specified \ref GeneratorTemplate.
/// @param benchmarks The list of benchmarks the new benchmarks are appended to.
/// @param config Benchmark config, controlling e.g. the size of the generated random array.
template<template<class ConfigProvider> class GeneratorTemplate>
void add_all_benchmarks_for_generator(std::vector<benchmark::internal::Benchmark*>& benchmarks,
                                      const benchmark_config&                       config)
{
    generator_benchmark_factory<GeneratorTemplate> benchmark_factory(config, benchmarks);

    benchmark_factory.template add_benchmarks<unsigned int>();
    benchmark_factory.template add_benchmarks<unsigned char>();
    benchmark_factory.template add_benchmarks<unsigned short>();
    benchmark_factory.template add_benchmarks<unsigned long long>();
    benchmark_factory.template add_benchmarks<float>();
    benchmark_factory.template add_benchmarks<half>();
    benchmark_factory.template add_benchmarks<double>();
}

} // namespace benchmark_tuning

#endif // ROCRAND_BENCHMARK_TUNING_HPP_
