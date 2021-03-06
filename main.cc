#include <iostream>
#include <chrono>

#include "HttpMaster.hh"

class config : public actor_system_config {
public:
  uint16_t port {0};
  std::string up_stream_url;

  config() {
    opt_group{custom_options_, "publish"}
      .add(port,          "port,p",     "set hdl server port")
      .add(up_stream_url, "upstream,u", "define an upstream to pull stream");
  }
};

void caf_main(actor_system& system, const config& cfg) {
  auto server_actor =
    system.middleman().spawn_server(HttpMaster,
                                    cfg.port,
                                    cfg.up_stream_url);
  if (!server_actor) {
    cerr << "cannot spawn server: "
         << system.render(server_actor.error()) << endl;
    return;
  }
}

CAF_MAIN(io::middleman)
