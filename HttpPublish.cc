#include "HttpPublish.hh"

behavior HttpPublish(HttpPubBroker* self,
//behavior HttpPublish(HttpPubActor::stateful_pointer<HttpPubActor> self,
                     connection_handle hdl,
                     const std::vector<char>& residue) {
  self->write(hdl, arraySize(http_ok), http_ok);
  self->state.parser.parse(residue);
  return {
    [=](const new_data_msg& msg) {
      self->configure_read(msg.handle, receive_policy::at_least(1024));
      self->state.parser.parse(msg.buf);
      /*
      cout << "in publish :\n";
      for (auto& i : msg.buf) {
        cout << i;
      }
      cout << endl;
      */
    },

    [=](register_atom, const actor& subscriber) {
      cout << "register_atom :\n";
      self->monitor(subscriber);
      self->link_to(subscriber);
    },

    [=](resync_atom) {
      cout << "resync_atom :\n";
    },

    [=](const connection_closed_msg& msg) {
      self->quit();
    }
  };
}
