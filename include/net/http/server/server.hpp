//
// server.hpp
// ~~~~~~~~~~
//
// Copyright (c) 2003-2013 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef HTTP_SERVER_HPP
#define HTTP_SERVER_HPP

#include <boost/asio.hpp>
#include <string>

#include "net/http/server/connection.hpp"
#include "net/http/server/connection_manager.hpp"
#include "net/http/server/request_handler.hpp"

namespace net {
namespace http {
namespace server {

/// The top-level class of the HTTP server.
class server
{
public:
  server(const server&) = delete;
  server& operator=(const server&) = delete;

  /// Construct the server to listen on the specified TCP address and port, and
  /// serve up files from the given directory.
  explicit server(
      boost::asio::io_service& io_service,
      std::string address, std::string port,
      request_handler request_handler);

  // Stops accepting new connections.
  void stop();

private:
  /// Perform an asynchronous accept operation.
  void do_accept();

  /// The io_service used to perform asynchronous operations.
  boost::asio::io_service& io_service_;

  /// The signal_set is used to register for process termination notifications.
  boost::asio::signal_set signals_;

  /// Acceptor used to listen for incoming connections.
  boost::asio::ip::tcp::acceptor acceptor_;

  /// The connection manager which owns all live connections.
  connection_manager connection_manager_;

  /// The next socket to be accepted.
  boost::asio::ip::tcp::socket socket_;

  /// The handler for all incoming requests.
  request_handler request_handler_;
};

} // namespace server
} // namespace http
} // namespace net

#endif // HTTP_SERVER_HPP
