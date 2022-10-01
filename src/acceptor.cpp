#include "acceptor.hpp"

acceptor::acceptor(std::string host, uint16_t port)
    : m_host_addr{.sin_family = AF_INET,
                  .sin_port = ::htons(port),
                  .sin_addr = ::inet_addr(host.data()),
                  .sin_zero = 0} {
  m_listen_fd =
      ::socket(m_host_addr.sin_family, SOCK_STREAM | SOCK_CLOEXEC, IPPROTO_TCP);
  int optval = 1;
  ::setsockopt(m_listen_fd, SOL_SOCKET, SO_REUSEADDR, &optval,
               static_cast<socklen_t>(sizeof(optval)));
  ::setsockopt(m_listen_fd, SOL_SOCKET, SO_REUSEPORT, &optval,
               static_cast<socklen_t>(sizeof(optval)));
  int ret = ::bind(m_listen_fd, reinterpret_cast<sockaddr *>(&m_host_addr),
                   sizeof(sockaddr_in));
  ::listen(m_listen_fd, SOMAXCONN);
  std::cout << "accept fd = " << m_listen_fd << '\n';
}

acceptor::~acceptor() { ::close(m_listen_fd); }