/**
 * Copyright (C) 2017 Maolin Liu <liu.matthews@gmail.com>.
 * All Rights Reserved.
 */

#pragma once

#include "utils.hh"
#include "HttpPublish.hh"
#include <boost/config.hpp>
#include <boost/bimap.hpp>

struct HttpReqContext {
  http::Request request;
  enum {
    NONE,
    HEADER,
    BODY,
    ERROR
  } status {NONE};
};

struct HttpMasterState {
  using PublisherMap =
    boost::bimap<std::string, actor>;
  using RequestProcMap =
    std::unordered_map<connection_handle, std::shared_ptr<HttpReqContext>>;

  std::string upstream;
  PublisherMap publishers;
  RequestProcMap procs;
};

using HttpMasterBroker = caf::stateful_actor<HttpMasterState, broker>;
behavior HttpMaster(HttpMasterBroker* self,
                    const std::string& up_stream_url);

