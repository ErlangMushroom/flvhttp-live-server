#include "HttpPublish.hh"
#include "HttpSubscribe.hh"

behavior HttpPublish(HttpPubBroker* self,
                     connection_handle hdl,
                     const std::vector<char>& residue) {
  self->write(hdl, arraySize(http_ok), http_ok);
  self->state.parser.parse(residue);
  return {
    [=](const new_data_msg& msg) {
      self->configure_read(msg.handle, receive_policy::at_least(1024));
      self->state.parser.parse(msg.buf);
    },

    [=](register_atom, const actor& subscriber) {
      cout << "register_atom :\n";
      self->monitor(subscriber);
      self->link_to(subscriber);
    },

    [=](resync_atom, const actor_addr& subscriber) {
      cout << "resync_atom :\n";
      self->send(actor_cast<actor>(subscriber), read_resp_atom::value);
    },

    [=](const connection_closed_msg& msg) {
      self->quit();
    }
  };
}
