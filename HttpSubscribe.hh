#pragma once

#include "utils.hh"
#include "HttpPublish.hh"

struct HttpSubState {
  abstract_broker* self;
  actor publisher;
};

using HttpSubBroker = caf::stateful_actor<HttpSubState, broker>;
behavior HttpSubscribe(HttpSubBroker* self,
                       connection_handle hdl,
                       const std::vector<char>& residue);

using sub_init_atom = atom_constant<atom("sub_init")>;
