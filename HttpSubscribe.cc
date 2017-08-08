#include "HttpSubscribe.hh"

behavior HttpSubscribe(HttpSubBroker* self,
                       connection_handle hdl,
                       const std::vector<char>& residue) {
  self->write(hdl, arraySize(http_ok), http_ok);
  return {
    [=](const new_data_msg& msg) {
      //self->configure_read(msg.handle, receive_policy::at_least(1024));
    },
  
    [=](sub_init_atom, const actor& publisher) {
      self->state.publisher = publisher;
      //self->request(self->state.publisher, resync_atom::value);
    },
/*
    [=](read_resp_atom, const std::list<FlvPacket>& pkt) {
      self->request(self->state.publisher, read_atom);
    },
*/
    [=](const connection_closed_msg& msg) {
      self->quit();
    }
  };
}