#pragma once
#include "acceptor.hpp"
#include "io_scheduler.hpp"
#include <iostream>

struct tcp_conn {
  char recvbuf[4096];
  char sendbuf[4096];
  int conn_fd;
};

struct tcp_server {
  explicit tcp_server(std::string host, uint16_t port)
      :

        m_acceptor(std::move(host), port), m_sched(4096) {}

  task<void> handle_conn(int connfd) {
    tcp_conn conn;
    conn.conn_fd = connfd;
    while (true) {
      int nrecv = co_await m_sched.add_read_request(conn.conn_fd, conn.recvbuf,
                                                    4096, 0);
      if (nrecv == 0) {
        // handle close
        ::close(conn.conn_fd);
        co_return;
      }
      /* todo:处理nsend < nrecv的情况 */
      int nsend = co_await m_sched.add_write_request(conn.conn_fd, conn.recvbuf,
                                                     nrecv, 0);
    }
  }

  task<void> handle_accept() {
    while (true) {
      sockaddr client_addr;
      socklen_t client_len = sizeof(sockaddr);
      int connfd = co_await m_sched.add_accept_request(
          m_acceptor.getListenFd(), &client_addr, &client_len);
      std::cout << "connfd = " << connfd << '\n';
      if (connfd < 0) [[unlikely]] {
        // log
        continue;
      } else {
        handle_conn(connfd).detach();
      }
    }
    co_return;
  }

  void start() {
    auto t = handle_accept();
    t.resume();
    m_sched.loop();
  }

private:
  acceptor m_acceptor;
  io_scheduler m_sched;
};