#pragma once
#include <arpa/inet.h>
#include <iostream>
#include <netinet/in.h>
#include <string>
#include <unistd.h>

struct acceptor {
  acceptor(std::string host, uint16_t port);

  ~acceptor();

  inline int getListenFd() noexcept { return m_listen_fd; }

private:
  sockaddr_in m_host_addr;
  int m_listen_fd;
};