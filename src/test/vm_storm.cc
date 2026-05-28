#include <Windows.h>
#include <intrin.h>
#include <thread>
#include <vector>
#include <stdio.h>

static volatile bool g_Run = true;

void StormThread(int id)
{
    int cpuInfo[4];

    unsigned long long counter = 0;

    while (g_Run)
    {
        // 高频 CPUID
        __cpuid(cpuInfo, 1);

        // 高频 timing
        auto t = __rdtsc();

        // 防止优化
        counter += cpuInfo[0] ^ t;

        // 再来一次
        __cpuid(cpuInfo, 0x40000000);

        counter ^= __rdtsc();
    }

    printf("thread %d done %llu\n", id, counter);
}

int main()
{
    SYSTEM_INFO si;
    GetSystemInfo(&si);

    int threads = si.dwNumberOfProcessors * 2;

    printf("starting %d threads\n", threads);

    std::vector<std::thread> workers;

    for (int i = 0; i < threads; i++)
    {
        workers.emplace_back(StormThread, i);
    }

    Sleep(30000);

    g_Run = false;

    for (auto& t : workers)
        t.join();

    return 0;
}