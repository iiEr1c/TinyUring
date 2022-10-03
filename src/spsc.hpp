#pragma once
#include <atomic>
#include <cstdlib>
#include <memory>

constexpr std::size_t hardware_destructive_interference_size = 64;

template <typename T> struct spsc {
  explicit spsc(uint32_t size)
      : m_size{size}, m_records{static_cast<T *>(
                          std::malloc(sizeof(T) * size))},
        m_readIndex{0}, m_writeIndex{0} {
    if (m_records == nullptr) [[unlikely]] {
      std::abort();
    }
  }

  /**
   * @brief Destroy the spsc object
   * only one thread can be doing this
   *
   */
  ~spsc() {
    if (!std::is_trivially_destructible_v<T>) {
      uint32_t startIndex = m_readIndex;
      uint32_t endIndex = m_writeIndex;
      while (startIndex != endIndex) {
        m_records[startIndex].~T();
        if (++startIndex == m_size) {
          startIndex = 0;
        }
      }
    }

    std::free(m_records);
  }

  /* produce */
  template <typename... Args> bool write(Args &&...recordArgs) {
    const auto currentWrite = m_writeIndex.load(std::memory_order_relaxed);
    auto nextRecord = currentWrite + 1;
    if (nextRecord == m_size) [[unlikely]] {
      nextRecord = 0;
    }

    /* 只要nextRecord不等于m_readIndex即可, 当队列满了的时候, nextRecord ==
     * m_readIndex */
    if (nextRecord != m_readIndex.load(std::memory_order_acquire)) [[likely]] {
      ::new (&m_records[currentWrite]) T(std::forward<Args>(recordArgs)...);
      m_writeIndex.store(nextRecord, std::memory_order_release);
      return true;
    }

    /* queue is full */
    return false;
  }

  bool read(T &record) {
    const auto currentRead = m_readIndex.load(std::memory_order_relaxed);
    if (currentRead == m_writeIndex.load(std::memory_order_acquire)) {
      /* queue is empty */
      return false;
    }

    auto nextRecord = currentRead + 1;
    if (nextRecord == m_size) [[unlikely]] {
      nextRecord = 0;
    }

    record = std::move(m_records[currentRead]);
    m_records[currentRead].~T();
    m_readIndex.store(nextRecord, std::memory_order_release);
    return true;
  }

  // * If called by consumer, then true size may be more (because producer may
  //   be adding items concurrently).
  // * If called by producer, then true size may be less (because consumer may
  //   be removing items concurrently).
  // * It is undefined to call this from any other thread.
  size_t sizeGuess() const {
    int ret = m_writeIndex.load(std::memory_order_acquire) -
              m_readIndex.load(std::memory_order_acquire);
    if (ret < 0) {
      ret += m_size;
    }
    return ret;
  }

  spsc(const spsc &) = delete;

  spsc &operator=(const spsc &) = delete;

private:
  char pad0[hardware_destructive_interference_size];
  const uint32_t m_size;
  T *const m_records;

  alignas(
      hardware_destructive_interference_size) std::atomic<uint32_t> m_readIndex;
  alignas(hardware_destructive_interference_size)
      std::atomic<uint32_t> m_writeIndex;
  char pad1[hardware_destructive_interference_size -
            sizeof(std::atomic<uint32_t>)];
};
