#include "HttpSubscribe.hh"
#include <chrono>
#include <arpa/inet.h>

#define SIZE_OF_TAG_HEADER 11

constexpr char http_flv[] = "HTTP/1.1 200 OK\r\n"
                            "Cache-Control: no-cache\r\n"
                            "Pragma: no-cache\r\n"
                            "Content-Type: video/x-flv\r\n"
                            "\r\n";

behavior HttpSubscribe(HttpSubBroker* self,
                       connection_handle hdl,
                       const std::vector<char>& residue) {
  self->state.handle = hdl;
  self->write(hdl, strlen(http_flv), http_flv);
  return {
    [=](const new_data_msg& msg) {
    },
  
    [=](sub_init_atom, const actor& publisher) {
      printf("sub_init_atom(%p)\n", self);
      self->state.publisher = publisher;
      self->send(self->state.publisher, 
                 resync_atom::value,
                 self->address());
    },

    [=](read_resp_atom, int64_t some, bool resync) {
      //printf("read_resp_atom(%p)\n", self);
      if (resync) {
        char hdr_with_size[] = {
          0x46, 0x4c, 0x56, 0x01, 0x04 | 0x01,
          0x00, 0x00, 0x00, 0x09, 0x00,
          0x00, 0x00, 0x00
        };
        self->write(self->state.handle,
                    arraySize(hdr_with_size),
                    hdr_with_size);
      }
      FlvPacketList* pkts = (FlvPacketList*)some;
      for (const auto& pkt : *pkts) {
        uint8_t type;
        if (pkt.type == VIDEO ||
            pkt.type == VIDEO_DCR) {
          type = 9;
        } else if (pkt.type == AUDIO ||
                   pkt.type == AUDIO_DCR) {
          type = 8;
        } else {
          continue;
        }

        uint32_t dts = pkt.dts;
        if (pkt.type != VIDEO_DCR &&
            pkt.type != AUDIO_DCR) {
          if (self->state.ts_base < 0) {
            self->state.ts_base = pkt.dts;
          }
          dts -= self->state.ts_base;
        }

        uint32_t size = pkt.payload->size();
        uint8_t htag[SIZE_OF_TAG_HEADER];
        memset(htag, 0, SIZE_OF_TAG_HEADER);
      	htag[0] = type;

        htag[1] = (size & 0x00ff0000) >> 16;
        htag[2] = (size & 0x0000ff00) >> 8;
        htag[3] = (size & 0x000000ff);

        htag[4] = (dts & 0x00ff0000) >> 16;
        htag[5] = (dts & 0x0000ff00) >> 8;
        htag[6] = (dts & 0x000000ff);
        htag[7] = (dts & 0xff000000) >> 24;

        self->write(self->state.handle,
                    sizeof(htag),
                    &htag);
        self->write(self->state.handle,
                    pkt.payload->size(),
                    pkt.payload->constBytes());
        uint32_t prev_tag_size = pkt.payload->size() + SIZE_OF_TAG_HEADER;
        prev_tag_size = htonl(prev_tag_size);
        self->write(self->state.handle,
                    sizeof(prev_tag_size),
                    &prev_tag_size);
        self->flush(self->state.handle);
      }
      self->delayed_send(self->state.publisher,
                         std::chrono::milliseconds(500),
                         read_some_atom::value,
                         (int64_t)pkts,
                         self->address());
    },

    [=](const connection_closed_msg& msg) {
      printf("connection_closed_msg(%p)\n", self);
      self->quit();
    }
  };
}