#pragma once

#include "utils.hh"
#include "HttpPublish.hh"

struct httpContext {
  http::Request request;
  enum {
    NONE,
    HEADER,
    BODY,
    ERROR
  } status {NONE};
};

struct HttpMasterState {
  using PendingConnectionMap =
    std::unordered_map<std::string, std::list<connection_handle>>;
  using PublisherMap =
    std::unordered_map<std::string, actor>;
  using RequestProcMap =
    std::unordered_map<connection_handle, std::shared_ptr<httpContext>>;

  abstract_broker* self;
  std::string upstream {};
  PendingConnectionMap pendings;
  PublisherMap publishers;
  RequestProcMap procs;
};

using HttpMasterBroker = caf::stateful_actor<HttpMasterState, broker>;
behavior HttpMaster(HttpMasterBroker* self,
                    const std::string& up_stream_url);

