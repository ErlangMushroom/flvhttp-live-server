#pragma once

#include "utils.hh"
#include "HttpMaster.hh"

struct HttpRevPubState {
};

using HttpRevPubBroker = caf::stateful_actor<HttpRevPubState, broker>;
behavior HttpRevPublish(HttpRevPubBroker* self,
                        connection_handle hdl,
                        const std::string& upstream);
