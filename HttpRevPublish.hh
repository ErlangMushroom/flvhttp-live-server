/**
 * Copyright (C) 2017 Maolin Liu <liu.matthews@gmail.com>.
 * All Rights Reserved.
 */

#pragma once

#include "utils.hh"
#include "HttpMaster.hh"

struct HttpResp {
  http::Response response;
  enum {
    NONE,
    HEADER,
    BODY,
    ERROR
  } status {NONE};
};

struct HttpRevPubState {
  std::unique_ptr<HttpResp> resp;
  FlvPacketCache cache {256};
  FlvParser flv_parser {cache};
  actor_addr master;
  int nsubs {0};
  int gen {0};
};

using HttpRevPubBroker = caf::stateful_actor<HttpRevPubState, broker>;
behavior HttpRevPublish(HttpRevPubBroker* self,
                        connection_handle hdl,
                        const std::string& path,
                        const actor_addr& addr);
