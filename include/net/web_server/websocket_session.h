#pragma once

#include "boost/beast/core.hpp"
#include "boost/beast/http.hpp"
#include "boost/beast/ssl.hpp"
#include "boost/beast/version.hpp"
#include "boost/beast/websocket.hpp"

#include <memory>
#include <queue>
#include <string>
#include <utility>

#include "net/web_server/fail.h"
#include "net/web_server/web_server.h"

namespace net {

template <class Derived>
struct websocket_session : public ws_session {
  using send_cb_t = std::function<void(boost::system::error_code, size_t)>;

  websocket_session(web_server::ws_msg_cb_t& ws_msg_cb,
                    web_server::ws_open_cb_t& ws_open_cb,
                    web_server::ws_close_cb_t& ws_close_cb)
      : ws_msg_cb_(ws_msg_cb),
        ws_open_cb_(ws_open_cb),
        ws_close_cb_(ws_close_cb) {}

  ~websocket_session() {
    if (ws_close_cb_) {
      ws_close_cb_(this);
    }
  }

  // Start the asynchronous operation
  template <class Body, class Allocator>
  void run(boost::beast::http::request<
           Body, boost::beast::http::basic_fields<Allocator>>
               req) {
    // Accept the WebSocket upgrade request
    do_accept(std::move(req));
  }

  void send(std::string&& msg, ws_msg_type type, send_cb_t cb) override {
    send_queue_.emplace(std::move(msg), type, cb);
    send_next();
  }

  void send(std::string const& msg, ws_msg_type type, send_cb_t cb) override {
    send_queue_.emplace(msg, type, cb);
    send_next();
  }

private:
  // Start the asynchronous operation
  template <class Body, class Allocator>
  void do_accept(boost::beast::http::request<
                 Body, boost::beast::http::basic_fields<Allocator>>
                     req) {
    // Set suggested timeout settings for the websocket
    derived().ws().set_option(
        boost::beast::websocket::stream_base::timeout::suggested(
            boost::beast::role_type::server));

    // Set a decorator to change the Server of the handshake
    derived().ws().set_option(boost::beast::websocket::stream_base::decorator(
        [](boost::beast::websocket::response_type& res) {
          res.set(boost::beast::http::field::server,
                  std::string(BOOST_BEAST_VERSION_STRING));
        }));

    // Accept the websocket handshake
    derived().ws().async_accept(
        req, boost::beast::bind_front_handler(&websocket_session::on_accept,
                                              derived().shared_from_this()));
  }

  void on_accept(boost::beast::error_code ec) {
    if (ec) {
      return fail(ec, "accept");
    }

    if (ws_open_cb_) {
      boost::asio::post(derived().ws().get_executor(),
                        [&, self = derived().shared_from_this()] {
                          ws_open_cb_(self, derived().is_ssl());
                        });
    }

    // Read a message
    do_read();
  }

  void do_read() {
    // Read a message into our buffer
    derived().ws().async_read(buffer_, boost::beast::bind_front_handler(
                                           &websocket_session::on_read,
                                           derived().shared_from_this()));
  }

  void on_read(boost::beast::error_code ec, std::size_t bytes_transferred) {
    boost::ignore_unused(bytes_transferred);

    // This indicates that the websocket_session was closed
    if (ec == boost::beast::websocket::error::closed) return;

    if (ec) {
      return fail(ec, "read");
    }

    if (ws_msg_cb_) {
      ws_msg_cb_(
          derived().shared_from_this(),
          boost::beast::buffers_to_string(buffer_.data()),
          derived().ws().got_text() ? ws_msg_type::TEXT : ws_msg_type::BINARY);
    }

    buffer_.consume(buffer_.size());
    do_read();
  }

  void on_write(boost::beast::error_code ec, std::size_t bytes_transferred) {
    boost::ignore_unused(bytes_transferred);

    if (ec) {
      return fail(ec, "write");
    }

    // Clear the buffer
    buffer_.consume(buffer_.size());

    // Do another read
    do_read();
  }

  void send_next() {
    if (send_active_ || send_queue_.empty()) {
      return;
    }

    std::string msg;
    ws_msg_type type;
    send_cb_t cb;
    std::tie(msg, type, cb) = send_queue_.front();
    send_queue_.pop();
    send_active_ = true;

    auto m = std::make_shared<std::string>(std::move(msg));
    derived().ws().text(type == ws_msg_type::TEXT);
    derived().ws().async_write(
        boost::asio::buffer(m->data(), m->size()),
        [m, cb, self = derived().shared_from_this()](
            boost::system::error_code const& ec, size_t bytes_transferred) {
          self->send_active_ = false;
          self->send_next();
          boost::asio::post(
              self->ws().get_executor(),
              [cb, ec, bytes_transferred]() { cb(ec, bytes_transferred); });
        });
  }

  // Access the derived class, this is part of
  // the Curiously Recurring Template Pattern idiom.
  Derived& derived() { return static_cast<Derived&>(*this); }

  boost::beast::flat_buffer buffer_;

  web_server::ws_msg_cb_t& ws_msg_cb_;
  web_server::ws_open_cb_t& ws_open_cb_;
  web_server::ws_close_cb_t& ws_close_cb_;

  std::queue<std::tuple<std::string, ws_msg_type, send_cb_t>> send_queue_;
  bool send_active_{false};
};

//------------------------------------------------------------------------------

// Handles a plain WebSocket connection
struct plain_websocket_session
    : public websocket_session<plain_websocket_session>,
      public std::enable_shared_from_this<plain_websocket_session> {
  // Create the session
  explicit plain_websocket_session(boost::beast::tcp_stream&& stream,
                                   web_server::ws_msg_cb_t& ws_msg_cb,
                                   web_server::ws_open_cb_t& ws_open_cb,
                                   web_server::ws_close_cb_t& ws_close_cb)
      : websocket_session<plain_websocket_session>(ws_msg_cb, ws_open_cb,
                                                   ws_close_cb),
        ws_(std::move(stream)) {}

  // Called by the base class
  boost::beast::websocket::stream<boost::beast::tcp_stream>& ws() {
    return ws_;
  }

  bool is_ssl() const { return false; }

private:
  boost::beast::websocket::stream<boost::beast::tcp_stream> ws_;
};

//------------------------------------------------------------------------------

// Handles an SSL WebSocket connection
struct ssl_websocket_session
    : public websocket_session<ssl_websocket_session>,
      public std::enable_shared_from_this<ssl_websocket_session> {
  // Create the ssl_websocket_session
  explicit ssl_websocket_session(
      boost::beast::ssl_stream<boost::beast::tcp_stream>&& stream,
      web_server::ws_msg_cb_t& ws_msg_cb, web_server::ws_open_cb_t& ws_open_cb,
      web_server::ws_close_cb_t& ws_close_cb)
      : websocket_session<ssl_websocket_session>(ws_msg_cb, ws_open_cb,
                                                 ws_close_cb),
        ws_(std::move(stream)) {}

  // Called by the base class
  boost::beast::websocket::stream<
      boost::beast::ssl_stream<boost::beast::tcp_stream>>&
  ws() {
    return ws_;
  }

  bool is_ssl() const { return true; }

private:
  boost::beast::websocket::stream<
      boost::beast::ssl_stream<boost::beast::tcp_stream>>
      ws_;
};

//------------------------------------------------------------------------------

template <class Body, class Allocator>
void make_websocket_session(
    boost::beast::tcp_stream stream,
    boost::beast::http::request<Body,
                                boost::beast::http::basic_fields<Allocator>>
        req,
    web_server::ws_msg_cb_t& ws_msg_cb, web_server::ws_open_cb_t& ws_open_cb,
    web_server::ws_close_cb_t& ws_close_cb) {
  std::make_shared<plain_websocket_session>(std::move(stream), ws_msg_cb,
                                            ws_open_cb, ws_close_cb)
      ->run(std::move(req));
}

template <class Body, class Allocator>
void make_websocket_session(
    boost::beast::ssl_stream<boost::beast::tcp_stream> stream,
    boost::beast::http::request<Body,
                                boost::beast::http::basic_fields<Allocator>>
        req,
    web_server::ws_msg_cb_t& ws_msg_cb, web_server::ws_open_cb_t& ws_open_cb,
    web_server::ws_close_cb_t& ws_close_cb) {
  std::make_shared<ssl_websocket_session>(std::move(stream), ws_msg_cb,
                                          ws_open_cb, ws_close_cb)
      ->run(std::move(req));
}

}  // namespace net
