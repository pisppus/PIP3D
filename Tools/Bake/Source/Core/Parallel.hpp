#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

namespace pip3D
{
    namespace Bake
    {
        template <typename Body>
        inline void parallelFor(uint32_t count, uint32_t threadCount, Body &&body)
        {
            if (count == 0)
                return;
            if (threadCount == 0)
                threadCount = std::max(1u, std::thread::hardware_concurrency());
            if (threadCount <= 1)
            {
                body(0, count);
                return;
            }

            if (count < threadCount * 16u)
                threadCount = std::max(1u, (count + 15u) / 16u);
            threadCount = std::min(threadCount, count);
            std::atomic<uint32_t> next(0);
            std::vector<std::thread> threads;
            threads.reserve(threadCount);
            for (uint32_t t = 0; t < threadCount; ++t)
                threads.emplace_back([&body, &next, count]()
                                     {
                    while (true) {
                        const uint32_t begin = next.fetch_add(32, std::memory_order_relaxed);
                        if (begin >= count) return;
                        const uint32_t end = std::min(count, begin + 32);
                        body(begin, end);
                    } });
            for (auto &th : threads)
                th.join();
        }
    }
}
