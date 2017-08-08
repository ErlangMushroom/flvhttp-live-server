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
      //anon_send(self->state.publisher, resync_atom::value, self->address());
      self->send(self->state.publisher, resync_atom::value, self->address());
    },

    [=](read_resp_atom) {
      cout << "read_resp_atom :\n";
    },

    [=](const connection_closed_msg& msg) {
      self->quit();
    }
  };
}