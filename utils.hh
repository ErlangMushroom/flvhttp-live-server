#pragma once

#include "caf/all.hpp"
#include "caf/io/all.hpp"
#include "http-parser/http_parser.h"
#include <boost/functional/hash.hpp>
#include <vector>
#include <unordered_map>

using std::cout;
using std::cerr;
using std::endl;

using namespace caf;
using namespace caf::io;

constexpr char http_ok[] = "HTTP/1.1 200 OK\r\n"
                           "Connection: close\r\n"
                           "\r\n";

constexpr char http_error[] = "HTTP/1.1 404 Not Found\r\n"
                              "Connection: close\r\n"
                              "\r\n";

template <size_t N>
constexpr size_t arraySize(const char (&)[N]) {
  return N;
}

struct membuf : std::streambuf {
  membuf(std::vector<char> vec) {
    this->setg(vec.data(), vec.data(), vec.data() + vec.size());
  }
};
/*
template <class T>
struct Box {
  T* val;
};

template <class T>
Box<T> box(T* v) {
  return Box<T>{v};
}

template <class T>
const T* unbox(const Box<T>& b) {
  return b.val;
}
*/
namespace http {

namespace detail {
  //auto hash = [](const std::string& key) -> size_t {
  struct hash {
    size_t operator()(const std::string& key) const {
      size_t seed = 0;
      for (char c : key) {
        boost::hash_combine(seed, tolower(c));
      }
      return seed;
    }
  };

  //auto equal = [](const std::string& left,
  //                const std::string& right) -> bool {
  struct equal {
    bool operator()(const std::string& left,
                    const std::string& right) const {
      return std::equal(left.begin(), left.end(), right.begin(),
                        [](char l, char r) {
          return tolower(l) == tolower(r);
        });
    }
  };
}

class Request {
public:
  using Method = http_method;
  using Headers =
    std::unordered_map<std::string, std::string,
                       detail::hash,
                       detail::equal>;

  Request() {
    http_parser_settings_init(&_settings);
    _settings.on_message_begin = &Request::on_message_begin;
    _settings.on_url = &Request::on_url;
    _settings.on_header_field = &Request::on_header_field;
    _settings.on_header_value = &Request::on_header_value;
    _settings.on_headers_complete = &Request::on_headers_complete;
    _settings.on_body = &Request::on_body;
    _settings.on_message_complete = &Request::on_message_complete;
    _settings.on_chunk_complete = &Request::on_chunk_complete;
    _settings.on_chunk_header = &Request::on_chunk_header;
    http_parser_init(&_parser, HTTP_REQUEST);
    _parser.data = this;
  }

  int parse(const std::vector<char>& msg) {
    size_t n = http_parser_execute(&_parser, &_settings,
                                   msg.data(), msg.size());
    if (n != msg.size()) {
      return -1;
    }

    return _status == HEADER_FINISH ? 0 : 1;
  }

  Method getMethod() const {
    return _method;
  }

  const std::vector<char>& getBody() const {
    return _body;
  } 

  const std::string& getPath() const {
    return _path;
  }

  const std::string& getFragment() const {
    return _fragment;
  }

  const std::string& getQuery() const {
    return _query;
  }

  const std::string& getField(const std::string& key) const {
    return _headers.at(key);
  }

protected:
  static int on_message_begin(http_parser* p) {
    Request* self = static_cast<Request*>(p->data);
    self->_status = HEADER_FIELD;
    self->_field.clear();
    self->_value.clear();
    self->_query.clear();
    self->_url.clear();
    return 0;
  }

  static int on_chunk_complete(http_parser* p) {
    return 0;
  }

  static int on_chunk_header(http_parser* p) {
    return 0;
  }

  static int on_url(http_parser* p,
                    const char* data,
                    size_t length) {
    Request* self = static_cast<Request*>(p->data);
    self->_url.append(data, length);
    return 0;
  }

  static int on_header_field(http_parser* p,
                             const char* data,
                             size_t length) {
    Request* self = static_cast<Request*>(p->data);
    if (self->_status != HEADER_FIELD) {
      self->_headers[self->_field] = self->_value;
      self->_field.clear();
      self->_value.clear();
    }

    self->_field.append(data, length);
    self->_status = HEADER_FIELD;
    return 0;
  }

  static int on_header_value(http_parser* p,
                             const char* data,
                             size_t length) {
    Request* self = static_cast<Request*>(p->data);
    self->_value.append(data, length);
    self->_status = HEADER_VALUE;
    return 0;
  }

  static int on_headers_complete(http_parser* p) {
    Request* self = static_cast<Request*>(p->data);
    self->_headers[self->_field] = self->_value;
    self->_method = (http_method)self->_parser.method;
    self->_keepAlive = http_should_keep_alive(&self->_parser) != 0;
    self->_field.clear();
    self->_value.clear();

    http_parser_url url;
    http_parser_url_init(&url);
    int parse_url =
      http_parser_parse_url(self->_url.data(), self->_url.size(), 0, &url);

    if (parse_url != 0) {
      return parse_url;
    }

    if (url.field_set & (1 << UF_PATH)) {
      self->_path = std::string(
          self->_url.data() + url.field_data[UF_PATH].off,
          url.field_data[UF_PATH].len);
    }

    if (url.field_set & (1 << UF_FRAGMENT)) {
      self->_fragment = std::string(
          self->_url.data() + url.field_data[UF_FRAGMENT].off,
          url.field_data[UF_FRAGMENT].len);
    }

    if (url.field_set & (1 << UF_QUERY)) {
      self->_query = std::string(
          self->_url.data() + url.field_data[UF_QUERY].off,
          url.field_data[UF_QUERY].len);
    }

    self->_status = HEADER_FINISH;
    return 0;
  }

  static int on_body(http_parser* p,
                     const char* data,
                     size_t length) {
    Request* self = static_cast<Request*>(p->data);
    std::copy(data, data + length, std::back_inserter(self->_body));
    self->_status = HEADER_FINISH;
    return 0;
  }

  static int on_message_complete(http_parser* p) {
    return 0;
  }

private:
  Method _method;
  int _keepAlive {0};
  Headers _headers {};
  std::vector<char> _body;

  http_parser _parser;
  http_parser_settings _settings;
  std::string _field;
  std::string _value;

  std::string _path;
  std::string _fragment;
  std::string _query;
  std::string _url;
  enum {
    NONE, HEADER_FIELD, HEADER_VALUE, HEADER_FINISH
  } _status;
};

}


#define OFFSET_OF(m, n) reinterpret_cast<size_t>(&(((m*)0)->*(&m::n)))

struct byte_t {
  const uint8_t* constBytes() const;
  size_t size() const;
  void release();
  void acquire();

  static byte_t* create(uint8_t* p,
                        size_t size);

  static byte_t* create(size_t size);

  //DISABLE_NEW_AND_DEREF(byte_t);
  //byte_t() = delete;
  //~byte_t() = delete;
  //operator *() = delete;
};
/*
struct acquire_t {
  void acquire();
};

class in_box_t : byte_t, acquire_t {

}
*/
class byte_impl_t {
  friend class byte_t;
private:
  int refcount;
  size_t capacity;
  byte_t data[0];

  byte_impl_t(uint8_t* p, size_t size)
    : refcount(1)
    , capacity(size) {
      if (p && size) {
        memcpy(data, p, size);
      }
  }

  byte_impl_t(size_t size)
    : refcount(1)
    , capacity(size) {
  }

  ~byte_impl_t() = delete;
  byte_impl_t(const byte_impl_t&) = delete;
  byte_impl_t(byte_impl_t&&) = delete;

  void* operator new(size_t size, size_t n) {
    void* res = malloc(size + n);
    if (!res) {
      throw std::bad_alloc();
    }
    return static_cast<uint8_t*>(res);
  }

  void operator delete(void* p) = delete;
};

class Bitstream {
private:
  const uint8_t* const _data;
  const uint8_t* const _end;
  const uint8_t* _curr;
  size_t _remain;

public:

  Bitstream(const uint8_t* data,
            size_t size)
    : _data(data)
    , _end(data + size)
    , _curr(_data)
    , _remain(8) {
  }

  template <class T>
  Bitstream(const std::vector<T>& data)
    : _data(&data[0])
    , _end(_data + data.size()*sizeof(T))
    , _curr(_data)
    , _remain(8) {
  }

  template <class T>
  Bitstream(const T& data)
    : _data(reinterpret_cast<const uint8_t*>(&data))
    , _end(_data + sizeof(T))
    , _curr(_data)
    , _remain(8) {
  }

  template <class T, size_t N>
  Bitstream(const T (&data)[N])
    : _data(reinterpret_cast<const uint8_t*>(data))
    , _end(_data + sizeof(T)*N)
    , _curr(_data)
    , _remain(8) {
  }

  Bitstream(const Bitstream&) = default;
  ~Bitstream() = default;

  uint64_t read(size_t n) {
    uint64_t r = 0;
    while (n > 0) {
      if (_curr >= _end) {
        assert(0);
      }

      unsigned int b = 8;
      if (b > n)
        b = n;
      if (b > _remain)
        b = _remain;

      unsigned int rshift = _remain - b;

      r <<= b;
      r  |= ((*_curr) >> rshift) & (0xff >> (8 - b));

      _remain -= b;
      if (0 == _remain) {
        _remain = 8;
        _curr += 1;
      }

      n -= b;
    }

    return r;
  }


  void set_bitpos(size_t pos) {
    if (pos >= (static_cast<size_t>(_end - _data) * 8)) {
      assert(0);
    }

    _curr = _data + (pos / 8);
    _remain = 8 - (pos % 8);
  }

  int get_bitpos() {
    return (_curr - _data) * 8 + 8 - _remain;
  }

  void skip(size_t n) {
    set_bitpos(get_bitpos() + n);
  }
};

