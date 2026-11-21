#include "TestFramework.h"
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <new>

namespace padtest {
std::vector<TestCase>& registry() { static std::vector<TestCase> r; return r; }
int g_failures = 0;
std::string g_current;
thread_local bool g_trackAllocations = false;
std::atomic<long> g_allocationCount{0};

void report(const char* fmt, ...) {
    std::printf("    ");
    va_list ap;
    va_start(ap, fmt);
    std::vprintf(fmt, ap);
    va_end(ap);
    std::printf("\n");
}
} // namespace padtest

// Substitui o operador new global: qualquer alocação feita enquanto o "callback"
// está sendo executado nos testes é contada (o requisito é zero).
void* operator new(std::size_t n) {
    if (padtest::g_trackAllocations) padtest::g_allocationCount.fetch_add(1);
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) {
    if (padtest::g_trackAllocations) padtest::g_allocationCount.fetch_add(1);
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

int main(int argc, char** argv) {
    // Uso: pad_tests [filtro] | pad_tests --rapido (pula o teste de 10 minutos, p/ sanitizers)
    const bool quick = argc > 1 && std::strcmp(argv[1], "--rapido") == 0;
    const char* filter = (argc > 1 && !quick) ? argv[1] : nullptr;
    int run = 0;
    const auto t0 = std::chrono::steady_clock::now();
    for (auto& t : padtest::registry()) {
        if (filter && std::strstr(t.name.c_str(), filter) == nullptr) continue;
        if (quick && std::strstr(t.name.c_str(), "prolongada") != nullptr) continue;
        padtest::g_current = t.name;
        const int before = padtest::g_failures;
        const auto s = std::chrono::steady_clock::now();
        std::printf("[ TESTE ] %s\n", t.name.c_str());
        std::fflush(stdout);
        t.fn();
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - s).count();
        std::printf("[ %s ] %s (%.0f ms)\n", padtest::g_failures == before ? " OK " : "FALHA", t.name.c_str(), ms);
        ++run;
    }
    const double total = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("\n%d testes executados em %.1f s, %d verificações falharam.\n", run, total, padtest::g_failures);
    return padtest::g_failures == 0 ? 0 : 1;
}
