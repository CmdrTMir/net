#ifndef NET_HTTP_CLIENT_RESPONSE_H_
#define NET_HTTP_CLIENT_RESPONSE_H_

#include <map>
#include <string>

namespace net {
namespace http {
namespace client {

struct response {
  int status_code;
  std::map<std::string, std::string> headers;
  std::string body;
};

}  // namespace client
}  // namespace http
}  // namespace net

#endif  // NET_HTTP_CLIENT_RESPONSE_H_