#pragma once

#include "caf/all.hpp"
#include "caf/io/all.hpp"
#include "http-parser/http_parser.h"
#include <boost/functional/hash.hpp>
#include <boost/scope_exit.hpp>
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

constexpr char http_error[] = "HTTP/1.1 503 Service Unavailable\r\n"
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

class UrlParser {
public:
  UrlParser(const std::string& url)
    : _url(url) {
    http_parser_url_init(&_parser);
  }

  int parse() {
    int res = http_parser_parse_url(_url.data(),
                                    _url.size(),
                                    0, &_parser);
    if (res != 0) {
      return res;
    }

#define SETUP_FIELD(m, f) \
  if (_parser.field_set & (1 << (m))) { \
    (f) = std::string( \
      _url.data() + _parser.field_data[m].off, _parser.field_data[m].len); \
  }
  SETUP_FIELD(UF_SCHEMA,   _schema);
  SETUP_FIELD(UF_HOST,     _host);
  SETUP_FIELD(UF_PORT,     _port);
  SETUP_FIELD(UF_PATH,     _path);
  SETUP_FIELD(UF_QUERY,    _query);
  SETUP_FIELD(UF_FRAGMENT, _fragment);
  SETUP_FIELD(UF_USERINFO, _userinfo);
#undef SETUP_FIELD

    return 0;
  }

  const std::string& getSchema() const {
    return _schema;
  }

  const std::string& getHost() const {
    return _host;
  }

  const std::string& getPort() const {
    return _port;
  }

  const std::string& getPath() const {
    return _path;
  }

  const std::string& getQuery() const {
    return _query;
  }

  const std::string& getFragment() const {
    return _fragment;
  }

  const std::string& getUserInfo() const {
    return _userinfo;
  }

private:
  http_parser_url _parser;
  std::string _url;
  std::string _schema;
  std::string _host;
  std::string _port;
  std::string _path;
  std::string _query;
  std::string _fragment;
  std::string _userinfo;
};

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

  ~Request() = default;

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

    int res = 0;
    UrlParser parser(self->_url);
    if ((res = parser.parse()) != 0) {
      return res;
    }

    self->_path = parser.getPath();
    self->_fragment = parser.getFragment();
    self->_query = parser.getQuery();

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
  } _status {NONE};
};

class Response {
public:
  using Headers =
    std::unordered_map<std::string, std::string,
                       detail::hash,
                       detail::equal>;

  Response() {
    http_parser_settings_init(&_settings);
    _settings.on_message_begin = &Response::on_message_begin;
    _settings.on_url = &Response::on_url;
    _settings.on_header_field = &Response::on_header_field;
    _settings.on_header_value = &Response::on_header_value;
    _settings.on_headers_complete = &Response::on_headers_complete;
    _settings.on_body = &Response::on_body;
    _settings.on_message_complete = &Response::on_message_complete;
    _settings.on_status = &Response::on_status;
    _settings.on_chunk_complete = &Response::on_chunk_complete;
    _settings.on_chunk_header = &Response::on_chunk_header;
    http_parser_init(&_parser, HTTP_RESPONSE);
    _parser.data = this;
  }

  ~Response() = default;

  int parse(const std::vector<char>& msg) {
    size_t n = http_parser_execute(&_parser, &_settings,
                                   msg.data(), msg.size());
    if (n != msg.size()) {
      if (_status == HEADER_FINISH) {
        std::copy(msg.data() + n + 1,
                  msg.data() + msg.size(),
                  std::back_inserter(_body));
        return 0;
      } else {
        return -1;
      }
    }
    
    return _status == HEADER_FINISH ? 0 : 1;
  }

  int getStatusCode() const {
    return _status_code;
  }

  const std::vector<char>& getBody() const {
    return _body;
  }

  const std::string& getField(const std::string& key) const {
    return _headers.at(key);
  }

protected:
  static int on_message_begin(http_parser* p) {
    Response* self = static_cast<Response*>(p->data);
    self->_status_code = 0;
    self->_status = HEADER_FIELD;
    self->_field.clear();
    self->_value.clear();
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
    return 0;
  }

  static int on_header_field(http_parser* p,
                             const char* data,
                             size_t length) {
    Response* self = static_cast<Response*>(p->data);
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
    Response* self = static_cast<Response*>(p->data);
    self->_value.append(data, length);
    self->_status = HEADER_VALUE;
    return 0;
  }

  static int on_headers_complete(http_parser* p) {
    Response* self = static_cast<Response*>(p->data);
    self->_status_code = self->_parser.status_code;
    self->_headers[self->_field] = self->_value;
    self->_field.clear();
    self->_value.clear();
    self->_status = HEADER_FINISH;
  }

  static int on_body(http_parser* p,
                     const char* data,
                     size_t length) {
    Response* self = static_cast<Response*>(p->data);
    std::copy(data, data + length, std::back_inserter(self->_body));
    self->_status = HEADER_FINISH;
    return 0;
  }

  static int on_message_complete(http_parser* p) {
    return 0;
  }

  static int on_status(http_parser* p,
                       const char* data,
                       size_t length) {
    return 0;
  }

private:
  Headers _headers {};
  std::vector<char> _body;
  int _status_code;

  http_parser _parser;
  http_parser_settings _settings;

  std::string _field;
  std::string _value;

  enum {
    NONE, HEADER_FIELD, HEADER_VALUE, HEADER_FINISH
  } _status {NONE};
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
};

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
    if (pos > (static_cast<size_t>(_end - _data) * 8)) {
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


enum packet_t : uint8_t {
  NONE,
  VIDEO,
  AUDIO,
  VIDEO_DCR,
  AUDIO_DCR,
  SCRIPT
};

struct DCR {
  //stream_t type;
  union {
    byte_t* videoDCR;
    byte_t* audioDCR;
  };
};

struct FlvPacket {
  ssize_t  id {-1};
  packet_t type {NONE};
  int64_t  dts {-1LL};
  int      key {0};
  byte_t*  payload {nullptr};
};

struct InitPacket : public FlvPacket {
  byte_t* videoDCR {nullptr};
  byte_t* audioDCR {nullptr};
};

class FlvPacketCache {
public:
  enum ErrorCode : int8_t {
    ERROR = -1,
    OK,
    SKIP,
    AGAIN
  };

  enum Mode : uint8_t {
    NORMAL = 0,
    SKIP_B,
    SKIP_B_WITH_AUDIO,
    I_ONLY,
    I_ONLY_WITH_AUDIO
  };

  static constexpr ssize_t INVALID = -1;

  FlvPacketCache(size_t total)
    : _maxSize(total) {
  }

  ~FlvPacketCache() {
    std::for_each(_packets.begin(),
                  _packets.end(),
                  [](const FlvPacket& pkt) {
      if (pkt.payload) { 
        pkt.payload->release();
      }
    });

    if (_videoDCR.payload) {
      _videoDCR.payload->release();
    }
    if (_audioDCR.payload) {
      _audioDCR.payload->release();
    }
  }

  ssize_t append(FlvPacket& pkt) {
    ssize_t id = _curId;
    pkt.id = _curId;

    if (pkt.type == VIDEO_DCR) {
      printf("video dcr\n");
      if (_videoDCR.payload) {
        _videoDCR.payload->release();
      }
      _videoDCR = pkt;
      return id;
    } else if (pkt.type == AUDIO_DCR) {
      printf("audio dcr\n");
      if (_audioDCR.payload) {
        _audioDCR.payload->release();
      }
      _audioDCR = pkt;
      return id;
    }

    _packets.push_back(pkt);
    _curId++;

    if (id - _bottom > _maxSize) {
      auto pkt = _packets.front();
      pkt.payload->release();
      _packets.pop_front();
      _bottom ++;
    }

    if (pkt.type == VIDEO && pkt.key) {
      _keyIds.push_back(id);
      if (_keyIds.front() < _bottom) {
        _keyIds.pop_front();
      }
    }
    return id;
  }

  std::list<FlvPacket> getDCR() const {
    std::list<FlvPacket> res;
    if (_videoDCR.payload) {
      _videoDCR.payload->acquire();
      res.push_back(_videoDCR);
    }
    if (_audioDCR.payload) {
      _audioDCR.payload->acquire();
      res.push_back(_audioDCR);
    }
    return res;
  }

  ErrorCode getNext(ssize_t id,
                    FlvPacket& out,
                    Mode mode = Mode::NORMAL) const {
    BOOST_SCOPE_EXIT(&out) {
      if (out.payload) {
        out.payload->acquire();
      }
    } BOOST_SCOPE_EXIT_END

    switch (mode) {
      case Mode::I_ONLY: {
        auto it = std::find_if(_keyIds.begin(),
                               _keyIds.end(),
                               [=] (size_t k) {
                    return id < k;
                  });
        if (it != std::end(_keyIds)) {
          out = _packets[*it - _bottom];
          return ErrorCode::OK;
        }
        return ErrorCode::AGAIN;
      }
      case Mode::NORMAL: {
        ssize_t next = id + 1;
        if (next >= _curId) {
          return ErrorCode::AGAIN;
        } else if (next < _bottom) {
          if (!_keyIds.empty()) {
            out = _packets[_keyIds.back() - _bottom];
            return ErrorCode::SKIP;
          }
          return ErrorCode::ERROR;
        }
        out = _packets[next - _bottom];
        return ErrorCode::OK;
      }
    default:
      return ErrorCode::ERROR;
    }
    return ErrorCode::OK;
  }

private:
  std::deque<FlvPacket> _packets;
  std::deque<ssize_t> _keyIds;
  FlvPacket _videoDCR;
  FlvPacket _audioDCR;
  const size_t _maxSize;
  ssize_t _curId {0};
  ssize_t _bottom {0};
};

class FlvParser {
public:
  enum Status {
    HEADER, PREV_TAG_SIZE, TAG_HEADER, TAG_DATA
  };

  enum : ssize_t {
    FLV_HEADER_SIZE = 9,
    FLV_PREV_TAG_SIZE = 4,
    FLV_TAG_HEADER_SIZE = 11
  };

  enum TagHeaderType : uint8_t {
    TAG_AUDIO  = 0x08,
    TAG_VIDEO  = 0x09,
    TAG_SCRIPT = 0x12
  };

  enum CodecID : uint8_t {
    CODECID_JPEG    = 0x01,
    CODECID_H263    = 0x02,
    CODECID_SCREEN  = 0x03,
    CODECID_VP6     = 0x04,
    CODECID_VP6A    = 0x05,
    CODECID_SCREEN2 = 0x06,
    CODECID_H264    = 0x07
  };

  enum AVCPacketType : uint8_t {
    SEQUENCE_HEADER = 0x00,
    NALU            = 0x01,
    END_OF_SEQUENCE = 0x02,
  };

  enum : uint8_t { KEY_FRAME = 0x01 };

  FlvParser(FlvPacketCache& cache)
    : _cache(cache)
    , _status(HEADER) {
      _remain.reserve(FLV_HEADER_SIZE);
  }

  int parse(const std::vector<char>& data) {
    return parse(&data[0], data.size());
  }

  int parse(const char* data, size_t size) {
    const char* cur = data;
    const char* end = data + size;
    for (; cur < end;) {
      switch (_status) {
        case HEADER: {
          ssize_t more = FLV_HEADER_SIZE - _remain.size();
          if (cur + more >= end) {
            std::copy(cur, end, std::back_inserter(_remain));
            return 1;
          } else {
            std::copy(cur, cur + more,
                      std::back_inserter(_remain));
            cur += more;
          }

          Bitstream bs(_remain);
          uint32_t offset;
          bool audio, video;
                   bs.skip(24);
                   bs.skip(8);
                   bs.skip(5);
          audio  = bs.read(1);
                   bs.skip(1);
          video  = bs.read(1);
          offset = bs.read(32);
          _remain.clear();
          _remain.reserve(4);
          _status = PREV_TAG_SIZE;
          break;
        }
        case PREV_TAG_SIZE: {
          ssize_t more = FLV_PREV_TAG_SIZE - _remain.size();
          if (cur + more >= end) {
            std::copy(cur, end, std::back_inserter(_remain));
            return 1;
          } else {
            std::copy(cur, cur + more,
                      std::back_inserter(_remain));
            cur += more;
          }

          Bitstream bs(_remain);
          bs.read(32);
          _remain.clear();
          _status = TAG_HEADER;
          break;
        }
        case TAG_HEADER: {
          ssize_t more = FLV_TAG_HEADER_SIZE - _remain.size();
          if (cur + more >= end) {
            std::copy(cur, end, std::back_inserter(_remain));
            return 1;
          } else {
            std::copy(cur, cur + more,
                      std::back_inserter(_remain));
            cur += more;
          }

          Bitstream bs(_remain);
          uint8_t  type = bs.read(8);
          uint32_t size = bs.read(24);
          uint32_t dts  = bs.read(24);
                   dts |= (uint32_t)bs.read(8) << 24;
                          bs.skip(24);
          _remain.clear();
          _tagsize = size;
          _cursize = 0;

          _packet.type =
            type == TAG_AUDIO ?
              AUDIO : type == TAG_VIDEO ?
                VIDEO : _packet.type;
          _packet.dts = dts;
          _packet.payload = byte_t::create(size);

          _status = TAG_DATA;
          break;
        }
        case TAG_DATA: {
          ssize_t more = (ssize_t)_tagsize - _cursize;
          assert(more >= 0);
          if (more == 0) {
            if (_packet.type == AUDIO) {
              Bitstream bs((uint8_t*)_packet.payload, 2);
                       bs.read(8);
              int pt = bs.read(8);
              if (pt == SEQUENCE_HEADER) {
                _packet.type = AUDIO_DCR;
              }
              //printf("audio frame(%lu) siz(%zu) type(%d)\n",
              //  _packet.dts, _packet.payload->size(), pt);
              _cache.append(_packet);
            } else if (_packet.type == VIDEO) {
              Bitstream bs((uint8_t*)_packet.payload, 6);
              int type = bs.read(4);
              int id   = bs.read(4);
              int pt   = bs.read(8);
              int cts  = bs.read(24);

              _packet.key = (type == KEY_FRAME);
              if (pt == SEQUENCE_HEADER) {
                _packet.type = VIDEO_DCR;
              }
              //printf("video frame(%lu) size(%zu) id(%d) type(%d) pt(%d)\n",
              //  _packet.dts, _packet.payload->size(), id, type, pt);
              _cache.append(_packet);
            } else {
              _packet.payload->release();
            }
            _status = PREV_TAG_SIZE;
            break;
          } else if (more >= end - cur) {
            more = end - cur;
          }

          memcpy(_packet.payload + _cursize, cur, more);
          _cursize += more;
          cur += more;
          break;
        }
        default: break;
      }
    }
  }

private:
  FlvPacketCache& _cache;
  Status _status;
  std::vector<uint8_t> _remain;
  size_t _tagsize;
  size_t _cursize;
  FlvPacket _packet;
};

using FlvPacketList = std::list<FlvPacket>;
