#include "utils.hh"
#include "HttpRevPublish.hh"
#include "HttpPublish.hh"
#include "HttpSubscribe.hh"

enum ShutDownReason : uint8_t {
  UPSTREAM_NETWORK_DOWN,
  NO_MORE_SUBSCIRBER,
};

behavior HttpRevPublish(HttpRevPubBroker* self,
                        connection_handle hdl,
                        const std::string& path,
                        const actor_addr& addr) {
  self->set_down_handler([=](const down_msg& msg) {
    printf("down_msg(%p)\n", self);
    if (--self->state.nsubs == 0) {
      self->delayed_send(self,
                         std::chrono::seconds(5),
                         delay_shut_atom::value,
                         self->state.gen,
                         NO_MORE_SUBSCIRBER);
    }
  });

  std::stringstream ss;
  ss << "GET " << path << " HTTP/1.1\r\n"
      << "User-Agent: Mozilla/5.0 (Windows NT 6.1; WOW64)\r\n"
      << "Host: \r\n"
      << "Accept: */*\r\n"
      << "Connection: keep-alive\r\n"
      << "\r\n";
  auto http_req = ss.str();
  self->state.master = addr;
  self->state.resp.reset(new HttpResp);
  self->state.resp->status = HttpResp::HEADER;
  self->write(hdl, http_req.length(), http_req.c_str());
  self->configure_read(hdl, receive_policy::at_least(8));
  self->flush(hdl);
  return {
    [=](const new_data_msg& msg) {
      //printf("new_data_msg(%p)\n", self);
      auto& state = self->state;
      auto& resp = state.resp;
      if (resp->status == HttpResp::HEADER) {
        int res = resp->response.parse(msg.buf);
        if (res > 0) {
          return;
        } else if (res < 0) {
          cout << "Might get an invalid request!" << endl;
          self->close(msg.handle);
          self->quit();
          return;
        }

        auto status = resp->response.getStatusCode();
        if (status != 200) {
          cout << "Bad http response: " << status << endl;
          self->close(msg.handle);
          self->quit();
          return;
        }

        auto residue = resp->response.getBody();
        state.flv_parser.parse(residue);
        resp->status = HttpResp::BODY;
      } else if (resp->status == HttpResp::BODY) {
        state.flv_parser.parse(msg.buf);
      }
    },

    [=](register_atom, const actor& subscriber) {
      printf("register_atom(%p)\n", self);
      self->state.nsubs++;
      self->monitor(subscriber);
      self->link_to(subscriber);
    },

    [=](resync_atom, const actor_addr& subscriber) {
      printf("resync_atom(%p)\n", self);
      if (self->state.resp->status != HttpResp::BODY) {
        self->send(actor_cast<actor>(subscriber),
                   eagain_atom::value);
        return;
      }
      auto& cache = self->state.cache;
      FlvPacketList* pkts = new FlvPacketList;
      FlvPacketCache::ErrorCode err = FlvPacketCache::OK;
      auto dcrs = cache.getDCR();
      pkts->splice(pkts->end(), std::move(dcrs));
      for (ssize_t id = -1;
           PACKET_IS_GOOD(err);) {
        FlvPacket out;
        err = cache.getNext(id, out);
        if (PACKET_IS_GOOD(err)) {
          id = out.id;
          pkts->emplace_back(std::move(out));
        }
      }
      self->send(actor_cast<actor>(subscriber),
                 read_resp_atom::value,
                 (int64_t)pkts,
                 true);
    },

    [=](read_some_atom, int64_t old, const actor_addr& subscriber) {
      //printf("read_some_atom(%p)\n", self);
      ssize_t oldId = -1;
      FlvPacketList* pkts = (FlvPacketList*)old;
      for (auto& pkt : *pkts) {
        pkt.payload->release();
        oldId = pkt.id;
      }
      delete pkts;

      FlvPacketCache::ErrorCode err = FlvPacketCache::OK;
      pkts = new FlvPacketList;
      auto& cache = self->state.cache;
      for (ssize_t id = oldId;
           PACKET_IS_GOOD(err);) {
        FlvPacket out;
        err = cache.getNext(id, out);
        if (PACKET_IS_GOOD(err)) {
          id = out.id;
          pkts->emplace_back(std::move(out));
        }
      }
      self->send(actor_cast<actor>(subscriber),
                 read_resp_atom::value,
                 (int64_t)pkts,
                 false);
    },

    [=](reclaim_atom, int64_t old) {
      printf("reclaim_atom(%p)\n", self);
      FlvPacketList* pkts = (FlvPacketList*)old;
      for (auto& pkt : *pkts) {
        pkt.payload->release();
      }
      delete pkts;
    },

    [=](const connection_closed_msg& msg) {
      printf("connection_closed_msg(%p)\n", self);
      self->delayed_send(self,
                         std::chrono::seconds(3),
                         delay_shut_atom::value,
                         self->state.gen,
                         UPSTREAM_NETWORK_DOWN);
    },

    [=](delay_shut_atom, int gen, ShutDownReason reason) {
      printf("delay_shut_atom(%p, %d)\n", self, gen);
      switch (reason) {
        case UPSTREAM_NETWORK_DOWN: {
          if (self->state.gen == gen) {
            self->quit();
          }
          break;
        }
        case NO_MORE_SUBSCIRBER: {
          if (self->state.nsubs == 0) {
            self->quit();
          }
          break;
        }
        default: break;
      }
    }
  };
}
