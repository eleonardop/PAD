// Mini framework de testes (sem dependências externas).
#pragma once

#include <cstdio>
#include <functional>
#include <string>
#include <vector>
#include <atomic>

namespace padtest {

struct TestCase { std::string name; std::function<void()> fn; };
std::vector<TestCase>& registry();
extern int g_failures;
extern std::string g_current;

struct Registrar { Registrar(const char* n, std::function<void()> f) { registry().push_back({n, std::move(f)}); } };

// Contagem de alocações no thread de áudio simulado (ver TestMain.cpp).
extern thread_local bool g_trackAllocations;
extern std::atomic<long> g_allocationCount;

void report(const char* fmt, ...);

} // namespace padtest

#define PAD_CONCAT2(a, b) a##b
#define PAD_CONCAT(a, b) PAD_CONCAT2(a, b)
#define TEST(name) \
    static void name(); \
    static padtest::Registrar PAD_CONCAT(reg_, name)(#name, name); \
    static void name()

#define CHECK(cond) do { if (!(cond)) { ++padtest::g_failures; \
    std::printf("  FALHOU [%s] %s:%d  %s\n", padtest::g_current.c_str(), __FILE__, __LINE__, #cond); } } while (0)

#define CHECK_MSG(cond, ...) do { if (!(cond)) { ++padtest::g_failures; \
    std::printf("  FALHOU [%s] %s:%d  %s -> ", padtest::g_current.c_str(), __FILE__, __LINE__, #cond); \
    std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)
