#pragma once

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4324) // structure was padded due to alignment specifier
#endif

#include <atomic>
#include <array>
#include <cstddef>
#include <cstdint>

// Single-Producer Single-Consumer lock-free ring buffer
// Capacity must be a power of two.
template <typename T, std::size_t CapacityPow2>
class SpscRing {
  static_assert((CapacityPow2 & (CapacityPow2 - 1)) == 0, "Capacity must be power of two");

 public:
  SpscRing() : read_idx_(0), write_idx_(0), dropped_(0) {}

  bool try_push(T&& item) {
    const std::size_t w = write_idx_.load(std::memory_order_relaxed);
    std::size_t r = read_idx_.load(std::memory_order_acquire);
    if (w - r >= CapacityPow2) {
      // full: drop oldest (advance read)
      read_idx_.store(r + 1, std::memory_order_release);
      dropped_.fetch_add(1, std::memory_order_relaxed);
    }
    buf_[w & mask()] = std::move(item);
    write_idx_.store(w + 1, std::memory_order_release);
    return true;
  }

  bool try_pop(T& out) {
    const std::size_t r = read_idx_.load(std::memory_order_relaxed);
    const std::size_t w = write_idx_.load(std::memory_order_acquire);
    if (r == w) return false; // empty
    out = std::move(buf_[r & mask()]);
    read_idx_.store(r + 1, std::memory_order_release);
    return true;
  }

  std::size_t pop_all(std::vector<T>& out) {
    std::size_t count = 0;
    T item;
    while (try_pop(item)) { out.push_back(std::move(item)); ++count; }
    return count;
  }

  std::size_t size() const {
    const std::size_t r = read_idx_.load(std::memory_order_acquire);
    const std::size_t w = write_idx_.load(std::memory_order_acquire);
    return w - r;
  }

  std::size_t capacity() const { return CapacityPow2; }
  std::uint64_t dropped_count() const { return dropped_.load(std::memory_order_relaxed); }

 private:
  static constexpr std::size_t mask() { return CapacityPow2 - 1; }

  alignas(64) std::atomic<std::size_t> read_idx_;
  alignas(64) std::atomic<std::size_t> write_idx_;
  std::array<T, CapacityPow2> buf_{};
  std::atomic<std::uint64_t> dropped_;
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif
