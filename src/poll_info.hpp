#pragma once
#include <coroutine>

struct poll_info {
  std::coroutine_handle<> m_awaiting_coroutine;
  int m_res;
  struct poll_awaiter {
    poll_info &m_pi;
    explicit poll_awaiter(poll_info &pi) noexcept : m_pi(pi) {}
    auto await_ready() noexcept -> bool { return false; }
    auto await_resume() noexcept -> int { return m_pi.m_res; }
    auto await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept
        -> void {
      m_pi.m_awaiting_coroutine = awaiting_coroutine;
    }
  };

  auto operator co_await() noexcept -> poll_awaiter {
    return poll_awaiter{*this};
  }
};