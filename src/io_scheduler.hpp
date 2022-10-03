#pragma once
#include "poll_info.hpp"
#include "task.hpp"
#include "work_thread.hpp"
#include <atomic>
#include <cstring>
#include <iostream>
#include <liburing.h>
#include <vector>

#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>

class io_scheduler {
public:
  friend class schedule_awaiter;
  struct schedule_awaiter {
    friend class io_scheduler;
    explicit schedule_awaiter(io_scheduler *io_scheduler)
        : m_io_scheduler(io_scheduler) {}

    auto await_ready() noexcept -> bool { return false; }
    auto await_resume() noexcept -> void {}
    auto await_suspend(std::coroutine_handle<> handle) noexcept -> void {
      m_io_scheduler->m_tasks.emplace_back(handle);
    }

  private:
    io_scheduler *m_io_scheduler;
  };

public:
  io_scheduler(unsigned int entries) : m_work_threads(8) {
    io_uring_params params{};
    if (io_uring_queue_init_params(entries, &m_uring, &params) < 0)
        [[unlikely]] {
      std::cout << "io_uring_queue_init_params...\n";
      std::abort();
    }
    if (!(params.features & IORING_FEAT_FAST_POLL)) [[unlikely]] {
      std::cout << "IORING_FEAT_FAST_POLL...\n";
      std::abort();
    }
  }
  ~io_scheduler() { io_uring_queue_exit(&m_uring); }

  io_scheduler(const io_scheduler &) = delete;
  io_scheduler(io_scheduler &&) = delete;
  io_scheduler &operator=(const io_scheduler &) = delete;
  io_scheduler &operator=(io_scheduler &&) = delete;

  auto schedule() -> schedule_awaiter { return schedule_awaiter{this}; }

  auto add_accept_request(int listenfd, sockaddr *client_addr,
                          socklen_t *client_len) -> task<int> {
    poll_info pi;
    io_uring_sqe *sqe = io_uring_get_sqe(&m_uring);
    io_uring_prep_accept(sqe, listenfd, client_addr, client_len, 0);
    io_uring_sqe_set_data(sqe, &pi);
    co_return co_await pi; /* 返回acceptfd */
  }

  auto add_read_request(int connfd, char *buf, size_t len, size_t offset)
      -> task<int> {
    poll_info pi;
    iovec vec{.iov_base = buf, .iov_len = len};
    io_uring_sqe *sqe = io_uring_get_sqe(&m_uring);
    io_uring_prep_readv(sqe, connfd, &vec, 1, offset);
    io_uring_sqe_set_data(sqe, &pi);
    co_return co_await pi;
  }

  auto add_write_request(int connfd, char *buf, size_t len, size_t offset)
      -> task<int> {
    poll_info pi;
    iovec vec{.iov_base = buf, .iov_len = len};
    io_uring_sqe *sqe = io_uring_get_sqe(&m_uring);
    io_uring_prep_writev(sqe, connfd, &vec, 1, offset);
    io_uring_sqe_set_data(sqe, &pi);
    co_return co_await pi;
  }

  void loop();

  bool addFdToWorkThread(int fd) {
    return m_work_threads.get_work_thread()->addFd(fd);
  }

private:
  io_uring m_uring;
  std::atomic_flag Running{true};
  std::vector<std::coroutine_handle<>> m_tasks;
  work_thread_pool m_work_threads;
};

void io_scheduler::loop() {
  while (Running.test()) {
    /* 至少有一个完成的任务block */
    io_uring_submit_and_wait(&m_uring, 1);
    // io_uring_submit(&m_uring);
    io_uring_cqe *cqe;
    unsigned head;
    unsigned count = 0;
    io_uring_for_each_cqe(&m_uring, head, cqe) {
      ++count;
      if (cqe->res == -ENOBUFS) [[unlikely]] {
        std::abort();
      }
      auto *ptr = reinterpret_cast<poll_info *>(cqe->user_data);
      ptr->m_res = cqe->res;
      ptr->m_awaiting_coroutine.resume();
    }
    io_uring_cq_advance(&m_uring, count);

    /* 处理m_tasks的任务 */
    std::vector<std::coroutine_handle<>> tasks;
    tasks.swap(m_tasks);
    for (const auto &t : tasks) {
      t.resume();
    }
  }
}