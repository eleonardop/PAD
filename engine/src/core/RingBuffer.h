// PAD Engine — fila SPSC (um produtor, um consumidor) sem trava e sem alocação após a criação.
//
// Usada para levar comandos (acorde, tonalidade, parar...) da interface ao callback de áudio.
// O consumidor (callback) nunca bloqueia. Se houver mais de um thread produtor no lado
// Android, ele deve serializar as escritas (ver PadEngine: mutex só do lado produtor).
#pragma once

#include <atomic>
#include <array>
#include <cstddef>
#include <type_traits>

namespace pad {

template <typename T, size_t Capacity>
class SpscRingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity precisa ser potência de 2");
    static_assert(std::is_trivially_copyable<T>::value, "T precisa ser trivialmente copiável");
public:
    bool push(const T& item) {
        const size_t w = write_.load(std::memory_order_relaxed);
        const size_t r = read_.load(std::memory_order_acquire);
        if (w - r >= Capacity) return false; // cheio
        buf_[w & (Capacity - 1)] = item;
        write_.store(w + 1, std::memory_order_release);
        return true;
    }
    bool pop(T& out) {
        const size_t r = read_.load(std::memory_order_relaxed);
        const size_t w = write_.load(std::memory_order_acquire);
        if (r == w) return false; // vazio
        out = buf_[r & (Capacity - 1)];
        read_.store(r + 1, std::memory_order_release);
        return true;
    }
    size_t size() const { return write_.load(std::memory_order_acquire) - read_.load(std::memory_order_acquire); }
    static constexpr size_t capacity() { return Capacity; }

private:
    alignas(64) std::atomic<size_t> write_{0};
    alignas(64) std::atomic<size_t> read_{0};
    alignas(64) std::array<T, Capacity> buf_{};
};

} // namespace pad
