#pragma once
#include "poll_info.hpp"
#include "spsc.hpp"
#include "task.hpp"
#include <iostream>
#include <liburing.h>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <type_traits>
#include <vector>

struct work_thread {
  /* io_uring队列大小, spsc队列大小 */
  explicit work_thread(unsigned int entries, unsigned int que_size)
      : m_thread{}, m_fds{que_size} {
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
  work_thread(const work_thread &) = delete;
  work_thread &operator=(const work_thread &) = delete;
  work_thread(work_thread &&) = delete;
  work_thread &operator=(work_thread &&) = delete;

  ~work_thread() {
    if (m_thread.joinable()) {
      m_thread.join();
    }
  }
  void start() {
    m_thread = std::thread([this] { this->loop(); });
  }
  bool addFd(int fd) { return m_fds.write(fd); }

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

  struct tcp_conn {
    char recvbuf[4096];
    char sendbuf[4096];
    int conn_fd;
  };

  task<void> handle_conn(int connfd) {
    tcp_conn conn;
    conn.conn_fd = connfd;
    while (true) {
      // std::cout << "handling fd " << connfd << '\n';
      int nrecv =
          co_await add_read_request(conn.conn_fd, conn.recvbuf, 4096, 0);
      // std::cout << "recv " << nrecv << "bytes\n";
      if (nrecv == 0) {
        // handle close
        ::close(conn.conn_fd);
        co_return;
      }
      /* todo:处理nsend < nrecv的情况 */
      int nsend =
          co_await add_write_request(conn.conn_fd, conn.recvbuf, nrecv, 0);
    }
  }

private:
  void loop() {
    while (true) {
      /* 处理fd & 循环iouring */
      size_t size = m_fds.sizeGuess();
      for (size_t i = 0; i < size; ++i) {
        int fd;
        if (m_fds.read(fd)) {
          // std::cout << "recv client fd = " << fd << '\n';
          handle_conn(fd).detach();
        }
      }
      io_uring_cqe *cqe;
      unsigned head;
      unsigned count = 0;
      // io_uring_submit_and_wait(&m_uring, 1);
      io_uring_submit(&m_uring);
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
    }
  }

  io_uring m_uring;
  std::thread m_thread;
  spsc<int> m_fds;
};

struct work_thread_pool {
  explicit work_thread_pool(int thread_num) : m_thread_num(thread_num) {
    for (int i = 0; i < m_thread_num; ++i) {
      m_work_threads.emplace_back(
          new work_thread(IOURING_QUEUE_SIZE, SPSC_QUEUE_SIZE));
      m_work_threads.back()->start();
    }
  }
  ~work_thread_pool() {
    for (int i = 0; i < m_thread_num; ++i) {
      delete m_work_threads[i];
    }
  }

  work_thread *get_work_thread() {
    static int cur = 0;
    ++cur;
    cur %= m_thread_num;
    return m_work_threads[cur];
  }

private:
  static constexpr unsigned int IOURING_QUEUE_SIZE = 8192;
  static constexpr unsigned int SPSC_QUEUE_SIZE = 8192;
  int m_thread_num;
  std::vector<work_thread *> m_work_threads;
};