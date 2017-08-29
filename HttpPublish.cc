#include "HttpPublish.hh"
#include "HttpSubscribe.hh"

behavior HttpPublish(HttpPubBroker* self,
                     connection_handle hdl,
                     const std::vector<char>& residue) {
  self->write(hdl, strlen(http_ok), http_ok);
  self->state.parser.parse(residue);
  self->set_down_handler([=](const down_msg& msg) {
    printf("down_msg(%p)\n", self);
    self->state.nsubs--;
  });

  return {
    [=](const new_data_msg& msg) {
      self->configure_read(msg.handle, receive_policy::at_least(1024));
      self->state.parser.parse(msg.buf);
    },

    [=](register_atom, const actor& subscriber) {
      printf("register_atom(%p)\n", self);
      self->state.nsubs++;
      self->monitor(subscriber);
      self->link_to(subscriber);
    },

    [=](resync_atom, const actor_addr& subscriber) {
      printf("resync_atom(%p)\n", self);
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
                         self->state.gen);
    },

    [=](delay_shut_atom, int gen) {
      printf("delay_shut_atom(%p, %d)\n", self, gen);
      if (self->state.gen == gen) {
        self->quit();
      }
    }
  };
}
