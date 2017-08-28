#pragma once

#include "utils.hh"
#include "HttpPublish.hh"

struct HttpSubState {
  abstract_broker* self;
  actor publisher;
  connection_handle handle;
  int64_t ts_base {-1};
};

using sub_init_atom = atom_constant<atom("sub_init")>;
using read_resp_atom = atom_constant<atom("read_resp")>;
using eagain_atom = atom_constant<atom("eagain")>;

using HttpSubBroker = caf::stateful_actor<HttpSubState, broker>;
behavior HttpSubscribe(HttpSubBroker* self,
                       connection_handle hdl,
                       const std::vector<char>& residue);
