#pragma once

#include "utils.hh"
#include <boost/scope_exit.hpp>

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
/*
  struct FlvHeader {
    uint8_t signature[3]; // flv file starts with first 3 bytes "FLV"
    uint8_t version;      // version
    uint8_t video : 1;    // whether has video
    uint8_t       : 1;
    uint8_t audio : 1;    // whether has audio
    uint8_t       : 5;
    uint32_t offset;      // flv's header length(version 1 should be always 9)
  } __attribute__((packed));

  struct FLVTagHeader {
    uint8_t  type;
    uint8_t  size[3];
    uint8_t  ts[3];
    uint8_t  ts_ext;
    uint8_t  sid[3];
  } __attribute__((packed));

  struct AACData {
    uint8_t* data;
    uint32_t length;
  } __attribute__((packed));

  struct AAC {
    uint8_t type;
    union {
      AudioSpecificConfig asc;
      AACData dat;
    };
  } __attribute__((packed));

  struct FlvAudioTagData {
    uint8_t sound_typ  : 1;
    uint8_t sound_size : 1;
    uint8_t sound_rate : 2;
    uint8_t sound_fmt  : 4;
    union {
      AAC aac;
      uint8_t* other;
    };
  } __attribute__((packed));

  struct AVCVideoPacket {
    uint8_t  pkt_type;
    uint24_t composition_time;
    union {
      AVCDecorderConfigurationRecord avc_dcr;
      Nalu nalu;
      uint8_t* data;
    };
  } __attribute__((packed));

  struct FlvVideoTagData {
    uint8_t codec_id   : 4;
    uint8_t frame_type : 4;
    AVCVideoPacket pkt;
  } __attribute__((packed));

  struct FlvTagData {
    union {
      amf::AMFData script;
      FlvVideoTagData video;
      FlvAudioTagData audio;
    };
  } __attribute__((packed));
*/
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
/*
struct Packets : RefCounted<Packets> {
  std::list<FlvPakcet> packets;
  void emplace_back(FlvPacket pkt) {
    packets.emplace_back(std::move(pkt));
  }
  template <class F>
  void foreach(F&& fn) {
    for (auto& i : packets) {
      fn(i);
    }
  }
};
*/
using FlvPacketList = std::list<FlvPacket>;

struct HttpPubState {
  abstract_broker* self;
  FlvPacketCache cache {256};
  FlvParser parser {cache};
  int nsubs {0};
  int gen {0};
};

using register_atom = atom_constant<atom("sub_reg")>;
using resync_atom = atom_constant<atom("resync")>;
using read_some_atom = atom_constant<atom("read_some")>;
using delay_shut_atom = atom_constant<atom("delay_shut")>;


using HttpPubBroker = caf::stateful_actor<HttpPubState, broker>;
behavior HttpPublish(HttpPubBroker* self,
                     connection_handle hdl,
                     const std::vector<char>& residue);


