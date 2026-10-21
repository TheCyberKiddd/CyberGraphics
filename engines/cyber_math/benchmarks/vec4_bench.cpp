#include <cyber/math/simd.h>
#include <cyber/math/vec4.h>

#include <chrono>
#include <cstddef>
#include <cstdio>

namespace
{

volatile cyber::math::f32 g_sink = 0.0f;

[[nodiscard]] cyber::math::Vec4 ScalarAdd(const cyber::math::Vec4& lhs,
                                          const cyber::math::Vec4& rhs) noexcept
{
    return cyber::math::Vec4{lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z, lhs.w + rhs.w};
}

template <typename Function>
[[nodiscard]] double Measure(Function&& function) noexcept
{
    const auto start = std::chrono::steady_clock::now();
    function();
    const auto end = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::nano>(end - start).count();
}

}  // namespace

int main()
{
    using namespace cyber::math;

    constexpr std::size_t iterations = 20'000'000;
    const Vec4 increment{1.0f, 2.0f, 3.0f, 4.0f};

    Vec4 simdAccumulator{};
    const double simdNanoseconds = Measure(
        [&]()
        {
            for (std::size_t index = 0; index < iterations; ++index)
            {
                simdAccumulator = simdAccumulator + increment;
            }
            g_sink = simdAccumulator.x + simdAccumulator.y + simdAccumulator.z + simdAccumulator.w;
        });

    Vec4 scalarAccumulator{};
    const double scalarNanoseconds = Measure(
        [&]()
        {
            for (std::size_t index = 0; index < iterations; ++index)
            {
                scalarAccumulator = ScalarAdd(scalarAccumulator, increment);
            }
            g_sink = scalarAccumulator.x + scalarAccumulator.y + scalarAccumulator.z +
                     scalarAccumulator.w;
        });

    const char* backend = ActiveSimdBackend() == SimdBackend::Sse2 ? "sse2" : "scalar";
    std::printf("backend=%s iterations=%zu\n", backend, iterations);
    std::printf("vec4_add_simd:   %.2f ns/op (%.3f ms)\n",
                simdNanoseconds / static_cast<double>(iterations), simdNanoseconds / 1.0e6);
    std::printf("vec4_add_scalar: %.2f ns/op (%.3f ms)\n",
                scalarNanoseconds / static_cast<double>(iterations), scalarNanoseconds / 1.0e6);
    return 0;
}
