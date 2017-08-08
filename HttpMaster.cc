#include "HttpMaster.hh"
#include "HttpSubscribe.hh"
#include "HttpRevPublish.hh"

behavior HttpMaster(HttpMasterBroker* self,
                    const std::string& up_stream_url) {
  self->state.upstream = up_stream_url;
  return {
    [=](const new_connection_msg& msg) {
      self->configure_read(msg.handle, receive_policy::at_most(1024));
      
      auto it = self->state.procs.find(msg.handle);
      if (it == std::end(self->state.procs)) {
        std::shared_ptr<httpContext> ctx(new httpContext);
        ctx->status = httpContext::HEADER;
        self->state.procs.insert(std::make_pair(msg.handle, ctx));
      }
    },

    [=](const new_data_msg& msg) {
      auto& state = self->state;
      auto ctx = state.procs[msg.handle];

      if (ctx->status == httpContext::HEADER) {
        int res = ctx->request.parse(msg.buf);
        if (res < 0) {
          cout << "Might get an invalid request!" << endl;
          self->quit();
        } else if (res != 0) {
          return;
        }
        
        auto method = ctx->request.getMethod();
        auto path = ctx->request.getPath();
        if (method == HTTP_GET) {
          cout << "HTTP_GET " << path << "\n";
          auto it = state.publishers.find(path);
          if (it != std::end(state.publishers)) {
            cout << "find pub in master\n";
            auto worker = self->fork(HttpSubscribe, msg.handle, ctx->request.getBody());
            //send_as(worker, it->second, register_atom::value, worker);
            anon_send(it->second, register_atom::value, worker);
            send_as(it->second, worker, sub_init_atom::value, it->second);
          } else if (!state.upstream.empty()) {
            if (state.pendings.find(path) == std::end(state.pendings)) {
              state.pendings[path] = {msg.handle};
            } else {
              state.pendings[path].push_back(msg.handle);
            }
            //auto worker = self->fork(HttpRevPublish, msg.handle, box(self), state.upstream);
            //self->monitor(worker);
            //self->link_to(worker);
          } else {
            self->write(msg.handle, arraySize(http_error), http_error);
            self->quit();
          }
        } else if (method == HTTP_POST) {
          cout << "HTTP_POST " << path << "\n";
          auto it = state.publishers.find(path);
          if (it == std::end(state.publishers)) {
            auto worker = self->fork(HttpPublish, msg.handle, ctx->request.getBody());
            state.publishers[path] = worker;
            self->monitor(worker);
            self->link_to(worker);
          } else {
            self->write(msg.handle, arraySize(http_error), http_error);
            self->quit();
          }
        }
        ctx->status = httpContext::BODY;
      }
      //cout << method << " : " << path << endl;
    },

    [=](const connection_closed_msg& msg) {
      self->quit();
    }
  };
}
