/**
 * Copyright (C) 2017 Maolin Liu <liu.matthews@gmail.com>.
 * All Rights Reserved.
 */

#pragma once

#include "utils.hh"

#define PACKET_IS_GOOD(e) ((e) == FlvPacketCache::ErrorCode::OK ||\
                           (e) == FlvPacketCache::ErrorCode::SKIP)

struct HttpPubState {
  FlvPacketCache cache {256};
  FlvParser parser {cache};
  int nsubs {0};
  int gen {0};
};

using register_atom = atom_constant<atom("sub_reg")>;
using resync_atom = atom_constant<atom("resync")>;
using read_some_atom = atom_constant<atom("read_some")>;
using delay_shut_atom = atom_constant<atom("delay_shut")>;
using reclaim_atom = atom_constant<atom("reclaim")>;

using HttpPubBroker = caf::stateful_actor<HttpPubState, broker>;
behavior HttpPublish(HttpPubBroker* self,
                     connection_handle hdl,
                     const std::vector<char>& residue);


