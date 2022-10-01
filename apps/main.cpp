#include "acceptor.hpp"
#include "tcp_server.hpp"

int main() {
  tcp_server server("127.0.0.1", 8888);
  server.start();
}