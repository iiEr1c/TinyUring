#pragma once
#include <coroutine>
#include <cstdlib>
#include <iostream>
#include <type_traits>
#include <utility>

template <typename return_type = void> class task;

template <typename return_type> struct promise {
  using task_type = task<return_type>;
  using coroutine_handle = std::coroutine_handle<promise<return_type>>;
  auto get_return_object() noexcept -> task_type;

  auto initial_suspend() noexcept { return std::suspend_always{}; }

  friend struct final_awaitable;
  struct final_awaitable {
    std::coroutine_handle<> m_release_detached;

    auto await_ready() noexcept -> bool { return false; }
    auto await_resume() noexcept -> void {}

    template <typename promise_type>
    auto await_suspend(std::coroutine_handle<promise_type> coro) noexcept
        -> std::coroutine_handle<> {
      auto &promise = coro.promise();
      if (promise.m_coroutination != nullptr) {
        return promise.m_coroutination;
      } else {
        /* 只有detach时才会设置 m_release_detached */
        if (m_release_detached) {
          m_release_detached.destroy();
        }
        return std::noop_coroutine();
      }
    }
  };

  auto final_suspend() noexcept { return final_awaitable{m_release_detached}; }

  auto unhandled_exception() -> void { std::abort(); }

  auto set_continuation(std::coroutine_handle<> continuation) noexcept -> void {
    m_coroutination = continuation;
  }

  auto set_detached_task(std::coroutine_handle<> h) noexcept -> void {
    this->m_release_detached = h;
  }

  auto return_value(return_type value) noexcept -> void {
    m_return_value = std::move(value);
  }

  auto result() const & -> const return_type & { return m_return_value; }

  auto result() && -> return_type && { return std::move(m_return_value); }

private:
  std::coroutine_handle<> m_coroutination{nullptr};    /* callee */
  std::coroutine_handle<> m_release_detached{nullptr}; /* 实验性 */
  return_type m_return_value;
};

template <> struct promise<void> {
  using task_type = task<void>;
  using coroutine_handle = std::coroutine_handle<promise<void>>;
  auto get_return_object() noexcept -> task_type;

  auto initial_suspend() noexcept { return std::suspend_always{}; }

  friend struct final_awaitable;
  struct final_awaitable {

    std::coroutine_handle<> m_release_detached;

    auto await_ready() noexcept -> bool { return false; }
    auto await_resume() noexcept -> void {}

    template <typename promise_type>
    auto await_suspend(std::coroutine_handle<promise_type> coro) noexcept
        -> std::coroutine_handle<> {
      auto &promise = coro.promise();
      if (promise.m_coroutination != nullptr) {
        return promise.m_coroutination;
      } else {
        /* 只有detach时才会设置 m_release_detached */
        if (m_release_detached) {
          m_release_detached.destroy();
        }
        return std::noop_coroutine();
      }
    }
  };

  auto final_suspend() noexcept { return final_awaitable{m_release_detached}; }

  auto unhandled_exception() -> void { std::abort(); }

  auto set_continuation(std::coroutine_handle<> continuation) noexcept -> void {
    m_coroutination = continuation;
  }

  auto set_detached_task(std::coroutine_handle<> h) noexcept -> void {
    this->m_release_detached = h;
  }

  auto return_void() noexcept -> void {}

  auto result() -> void {}

private:
  std::coroutine_handle<> m_coroutination{nullptr};    /* callee */
  std::coroutine_handle<> m_release_detached{nullptr}; /* 实验性 */
};

template <typename return_type> class task {
public:
  using task_type = task<return_type>;
  using promise_type = promise<return_type>;
  using coroutine_handle = std::coroutine_handle<promise_type>;

  task() noexcept : m_coroutine{nullptr} {}

  explicit task(coroutine_handle handle) : m_coroutine{handle} {}

  task(const task &) = delete;
  task(task &&other) noexcept
      : m_coroutine(std::exchange(other.m_coroutine, nullptr)) {}

  auto operator=(const task &) -> task & = delete;

  auto operator=(task &&other) noexcept -> task & {
    if (std::addressof(other) != this) {
      if (m_coroutine != nullptr) {
        m_coroutine.destroy();
      }

      m_coroutine = std::exchange(other.m_coroutine, nullptr);
    }

    return *this;
  }

  /* todo copy/move construct/assign */
  ~task() {
    /* 在非detach时释放 */
    if (!m_detached) {
      if (m_coroutine) {
        m_coroutine.destroy();
      }
    }
  }

  struct awaitable_base {
    std::coroutine_handle<promise_type> m_coroutine{nullptr};

    auto await_ready() const noexcept -> bool {
      return !m_coroutine || m_coroutine.done();
    }

    auto await_suspend(std::coroutine_handle<> await_coroutine) noexcept
        -> std::coroutine_handle<> {
      m_coroutine.promise().set_continuation(await_coroutine);
      return m_coroutine; /* 恢复自己 */
    }
  };

  auto operator co_await() const &noexcept {

    struct awaitable : public awaitable_base {
      /* 操纵的是原promise的数据 */
      auto await_resume() -> decltype(auto) {
        if constexpr (std::is_same_v<void, return_type>) {
          this->m_coroutine.promise().result();
          return;
        } else {
          return this->m_coroutine.promise().result();
        }
      }
    };

    return awaitable{m_coroutine};
  }

  auto operator co_await() const &&noexcept {

    struct awaitable : public awaitable_base {
      /* 操纵的是原promise的数据 */
      auto await_resume() -> decltype(auto) {
        if constexpr (std::is_same_v<void, return_type>) {
          this->m_coroutine.promise().result();
          return;
        } else {
          return std::move(this->m_coroutine.promise()).result();
        }
      }
    };

    return awaitable{m_coroutine};
  }

  auto resume() -> bool {
    if (!m_coroutine.done()) {
      m_coroutine.resume();
    }
    return !m_coroutine.done();
  }

  auto get_promise() & -> promise_type & { return m_coroutine.promise(); }

  auto get_promise() && -> promise_type && {
    return std::move(m_coroutine.promise());
  }

  auto handle() -> coroutine_handle { return m_coroutine; }

  /*
   * 实验性
   * 使用detach一定要确保该协程执行到结束, 即执行到co_return处
   * 触发final_suspend
   */
  auto detach() -> void {
    m_coroutine.promise().set_detached_task(m_coroutine);
    m_detached = true;
    m_coroutine.resume(); /* 恢复自己 */
  }

private:
  coroutine_handle m_coroutine{nullptr};
  bool m_detached{false};
};

template <typename return_type>
inline auto promise<return_type>::get_return_object() noexcept
    -> task<return_type> {
  return task<return_type>{coroutine_handle::from_promise(*this)};
}

inline auto promise<void>::get_return_object() noexcept -> task<> {
  return task<>{coroutine_handle::from_promise(*this)};
}
