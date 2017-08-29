/**
 * Copyright (C) 2017 Maolin Liu <liu.matthews@gmail.com>.
 * All Rights Reserved.
 */

#include "HttpMaster.hh"
#include "HttpSubscribe.hh"
#include "HttpRevPublish.hh"

behavior HttpMaster(HttpMasterBroker* self,
                    const std::string& up_stream_url) {
  self->state.upstream = up_stream_url;
  self->set_down_handler([=](const down_msg& msg) {
    printf("down_msg(%p)\n", self);
    auto act = actor_cast<actor>(msg.source);
    auto it = self->state.publishers.right.find(act);
    if (it != std::end(self->state.publishers.right)) {
      self->state.publishers.right.erase(it);
    }
  });

  return {
    [=](const new_connection_msg& msg) {
      cout << "new_connection_msg " << msg.handle.id() << endl;
      self->configure_read(msg.handle, receive_policy::at_most(1024));

      auto it = self->state.procs.find(msg.handle);
      if (it == std::end(self->state.procs)) {
        cout << "create new http context" << endl;
        std::shared_ptr<HttpReqContext> ctx(new HttpReqContext);
        ctx->status = HttpReqContext::HEADER;
        self->state.procs[msg.handle] = ctx;
      }
    },

    [=](const new_data_msg& msg) {
      auto& state = self->state;
      auto ctx = state.procs[msg.handle];
      if (!ctx) {
        cout << "Cannot find handle!" << endl;
        return;
      }

      if (ctx->status == HttpReqContext::HEADER) {
        int res = ctx->request.parse(msg.buf);
        if (res > 0) {
          return;
        }

        state.procs.erase(state.procs.find(msg.handle));

        if (res < 0) {
          cout << "Might get an invalid request!" << endl;
          self->write(msg.handle, strlen(http_error), http_error);
          self->flush(msg.handle);
          self->close(msg.handle);
          return;
        }

        auto method = ctx->request.getMethod();
        auto path = ctx->request.getPath();
        auto query = ctx->request.getQuery();
        if (method == HTTP_GET) {
          cout << "HTTP_GET " << path << "\n";
          auto it = state.publishers.left.find(path);
          if (it != std::end(state.publishers.left)) {
            auto worker = self->fork(HttpSubscribe, msg.handle, ctx->request.getBody());
            //self->monitor(worker);
            self->link_to(worker);
            anon_send(it->second, register_atom::value, worker);
            anon_send(worker, sub_init_atom::value, it->second);
          } else if (!state.upstream.empty()) {
            http::UrlParser parser(state.upstream);
            if (parser.parse() != 0) {
              cout << "Upstream url parse failed!" << endl;
              self->write(msg.handle, strlen(http_error), http_error);
              self->flush(msg.handle);
              self->close(msg.handle);
              return;
            }
            auto host = parser.getHost();
            auto port = std::stol(parser.getPort());
            std::string res_path = path;
            if (!query.empty()) {
              res_path += "?" + query;
            }
            auto client =
              self->parent().spawn_client(HttpRevPublish,
                                          host,
                                          port,
                                          res_path,
                                          self->address());
            if (client) {
              self->monitor(*client);
              self->link_to(*client);
              state.publishers.insert(HttpMasterState::PublisherMap::value_type(path, *client));

              auto worker = self->fork(HttpSubscribe, msg.handle, ctx->request.getBody());
              //self->monitor(worker);
              self->link_to(worker);
              anon_send(*client, register_atom::value, worker);
              anon_send(worker, sub_init_atom::value, *client);
            } else {
              self->write(msg.handle, strlen(http_error), http_error);
              self->flush(msg.handle);
              self->close(msg.handle);
            }
          } else {
            self->write(msg.handle, strlen(http_error), http_error);
            self->flush(msg.handle);
            self->close(msg.handle);
          }
        } else if (method == HTTP_POST) {
          cout << "HTTP_POST " << path << "\n";
          auto it = state.publishers.left.find(path);
          if (it == std::end(state.publishers.left)) {
            auto worker = self->fork(HttpPublish, msg.handle, ctx->request.getBody());
            state.publishers.insert(HttpMasterState::PublisherMap::value_type(path, worker));
            self->monitor(worker);
            self->link_to(worker);
          } else {
            self->write(msg.handle, strlen(http_error), http_error);
            self->flush(msg.handle);
            self->close(msg.handle);
          }
        }
        ctx->status = HttpReqContext::BODY;
      }
    },

    [=](const connection_closed_msg& msg) {
      auto& procs = self->state.procs;
      procs.erase(procs.find(msg.handle));
    }
  };
}


