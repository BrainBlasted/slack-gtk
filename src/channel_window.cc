#include "channel_window.h"
#include <iostream>
#include "message_row.h"

ChannelWindow::ChannelWindow(const api_client& api_client,
                             const users_store& users_store,
                             icon_loader& icon_loader,
                             const Json::Value& channel)
    : messages_scrolled_window_(),
      messages_list_box_(),
      message_entry_(api_client, channel["id"].asString()),

      id_(channel["id"].asString()),
      name_(channel["name"].asString()),
      api_client_(api_client),
      users_store_(users_store),
      icon_loader_(icon_loader) {
  set_orientation(Gtk::ORIENTATION_VERTICAL);
  pack_start(messages_scrolled_window_);
  pack_end(message_entry_, Gtk::PACK_SHRINK);

  messages_scrolled_window_.add(messages_list_box_);
  messages_scrolled_window_.set_policy(Gtk::POLICY_NEVER,
                                       Gtk::POLICY_AUTOMATIC);

  show_all_children();

  std::map<std::string, std::string> params;
  params.insert(std::make_pair("channel", id()));
  api_client_.queue_post("channels.history", params,
                         std::bind(&ChannelWindow::on_channels_history, this,
                                   std::placeholders::_1));
}

const std::string& ChannelWindow::id() const {
  return id_;
}
const std::string& ChannelWindow::name() const {
  return name_;
}

void ChannelWindow::on_message_signal(const Json::Value& payload) {
  auto row = Gtk::manage(new MessageRow(icon_loader_, users_store_, payload));
  messages_list_box_.append(*row);
  row->show();
}

void ChannelWindow::on_channels_history(
    const boost::optional<Json::Value>& result) {
  if (result) {
    std::vector<Json::Value> v;
    for (const Json::Value& message : result.get()["messages"]) {
      v.push_back(message);
    }
    for (auto it = v.crbegin(); it != v.crend(); ++it) {
      on_message_signal(*it);
    }
  } else {
    std::cerr << "[channel " << name()
              << "] failed to load history from channels.history API"
              << std::endl;
  }
}
