#include "main_window.h"
#include <gtkmm/stacksidebar.h>
#include <iostream>
#include "api_client.h"
#include "channels_store.h"
#include "emoji_loader.h"
#include "rtm_client.h"
#include "users_store.h"

MainWindow::MainWindow(std::shared_ptr<api_client> api_client,
                       const std::string& emoji_directory,
                       const Json::Value& json)
    : settings_(Gio::Settings::create("cc.wanko.slack-gtk")),
      team_(api_client, emoji_directory, json) {
  Gtk::Box* box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL));
  add(*box);

  Gtk::StackSidebar* channels_sidebar = Gtk::manage(new Gtk::StackSidebar());
  box->pack_start(*channels_sidebar, Gtk::PACK_SHRINK);
  box->pack_start(channels_stack_, Gtk::PACK_EXPAND_WIDGET);

  channels_sidebar->set_stack(channels_stack_);

  get_screen()->set_resolution(settings_->get_double("dpi"));

  team_.rtm_client_->hello_signal().connect(
      sigc::mem_fun(*this, &MainWindow::on_hello_signal));
  team_.rtm_client_->reconnect_url_signal().connect(
      sigc::mem_fun(*this, &MainWindow::on_reconnect_url_signal));
  team_.rtm_client_->presence_change_signal().connect(
      sigc::mem_fun(*this, &MainWindow::on_presence_change_signal));
  team_.rtm_client_->pref_change_signal().connect(
      sigc::mem_fun(*this, &MainWindow::on_pref_change_signal));
  team_.rtm_client_->message_signal().connect(
      sigc::mem_fun(*this, &MainWindow::on_message_signal));
  team_.rtm_client_->channel_marked_signal().connect(
      sigc::mem_fun(*this, &MainWindow::on_channel_marked_signal));
  team_.rtm_client_->channel_joined_signal().connect(
      sigc::mem_fun(*this, &MainWindow::on_channel_joined_signal));
  team_.rtm_client_->channel_left_signal().connect(
      sigc::mem_fun(*this, &MainWindow::on_channel_left_signal));
  team_.rtm_client_->user_typing_signal().connect(
      sigc::mem_fun(*this, &MainWindow::on_user_typing_signal));
  team_.rtm_client_->emoji_changed_signal().connect(
      sigc::mem_fun(*this, &MainWindow::on_emoji_changed_signal));

  channels_stack_.signal_add().connect(
      sigc::mem_fun(*this, &MainWindow::on_channel_added));
  channels_stack_.property_visible_child().signal_changed().connect(
      sigc::mem_fun(*this, &MainWindow::on_visible_channel_changed));
  for (const auto& p : team_.channels_store_->data()) {
    const channel& chan = p.second;
    if (chan.is_member) {
      add_channel_window(chan);
    }
  }

  request_update_emoji();

  team_.rtm_client_->start();
  show_all_children();
}

MainWindow::~MainWindow() {
}

void MainWindow::on_hello_signal(const Json::Value&) {
  append_message("RTM API started");
}

void MainWindow::on_reconnect_url_signal(const Json::Value& payload) {
  std::ostringstream oss;
  oss << "Receive reconnect_url=" << payload["url"].asString();
  append_message(oss.str());
}

void MainWindow::on_presence_change_signal(const Json::Value& payload) {
  std::ostringstream oss;
  auto ou = team_.users_store_->find(payload["user"].asString());
  if (ou) {
    oss << ou.get().name << " changed presence to " << payload["presence"];
    append_message(oss.str());
  } else {
    std::cerr << "[MainWindow] on_pref_change_signal: cannot find user "
              << payload["user"] << std::endl;
  }
}
void MainWindow::on_pref_change_signal(const Json::Value& payload) {
  std::ostringstream oss;
  oss << "Changed preference " << payload["name"] << ": " << payload["value"];
  append_message(oss.str());
}

void MainWindow::on_message_signal(const Json::Value& payload) {
  const std::string channel_id = payload["channel"].asString();
  Widget* widget = channels_stack_.get_child_by_name(channel_id);
  if (widget == nullptr) {
    std::cerr << "[MainWindow] on_message_signal: unknown channel: id="
              << channel_id << std::endl;
    std::cerr << payload << std::endl;
  } else {
    static_cast<ChannelWindow*>(widget)->on_message_signal(payload);
  }
}

void MainWindow::on_channel_marked_signal(const Json::Value& payload) {
  const std::string channel_id = payload["channel"].asString();
  Widget* widget = channels_stack_.get_child_by_name(channel_id);
  if (widget == nullptr) {
    std::cerr << "[MainWindow] on_channel_marked_signal: unknown channel: id="
              << channel_id << std::endl;
    std::cerr << payload << std::endl;
  } else {
    static_cast<ChannelWindow*>(widget)->on_channel_marked(payload);
  }
}

void MainWindow::append_message(const std::string& text) {
  std::cout << text << std::endl;
}

void MainWindow::on_channel_link_clicked(const std::string& channel_id) {
  Widget* widget = channels_stack_.get_child_by_name(channel_id);
  if (widget == nullptr) {
    const boost::optional<channel> result =
        team_.channels_store_->find(channel_id);
    if (result) {
      std::map<std::string, std::string> params;
      const std::string& name = result.get().name;
      params.emplace(std::make_pair("name", name));
      const boost::optional<Json::Value> join_result =
          team_.api_client_->post("channels.join", params);
      if (join_result) {
        const Json::Value& join_response = join_result.get();
        if (!join_response["ok"].asBool()) {
          std::cerr << "[MainWindow] on_channel_link_clicked: Failed to join #"
                    << name << ": " << join_response << std::endl;
        }
      } else {
        std::cerr << "[MainWindow] on_channel_link_clicked: Failed to join #"
                  << name << std::endl;
      }
    } else {
      std::cerr << "[MainWindow] on_channel_link_clicked: Unknown channel "
                << channel_id << std::endl;
    }
  } else {
    channels_stack_.set_visible_child(channel_id);
  }
}

void MainWindow::on_channel_joined_signal(const Json::Value& payload) {
  const std::string channel_id = payload["channel"]["id"].asString();
  const boost::optional<channel> result =
      team_.channels_store_->find(channel_id);
  if (result) {
    auto w = add_channel_window(result.get());
    channels_stack_.set_visible_child(*w);
  } else {
    std::cerr << "[MainWindow] on_channel_joined_signal: Unknown channel "
              << channel_id << std::endl;
  }
}

void MainWindow::on_channel_left_signal(const Json::Value& payload) {
  const std::string channel_id(payload["channel"].asString());

  Widget* widget = channels_stack_.get_child_by_name(channel_id);
  if (widget == nullptr) {
    std::cerr << "[MainWindow] on_channel_left_signal: Cannot find "
                 "ChannelWindow with id="
              << channel_id << std::endl;
  } else {
    channels_stack_.remove(*widget);
    delete widget;
  }
}

void MainWindow::on_user_typing_signal(const Json::Value& payload) {
  auto oc = team_.channels_store_->find(payload["channel"].asString());
  auto ou = team_.users_store_->find(payload["user"].asString());
  if (oc && ou) {
    const channel& c = oc.get();
    const user& u = ou.get();
    std::cout << u.name << " is typing on #" << c.name << std::endl;
  } else {
    if (!oc) {
      std::cerr << "[MainWindow] on_user_typing_signal: cannot find channel "
                << payload["channel"] << std::endl;
    }
    if (!ou) {
      std::cerr << "[MainWindow] on_user_typing_signal: cannot find user "
                << payload["user"] << std::endl;
    }
  }
}

void MainWindow::on_channel_added(Widget*) {
  std::map<std::string, Widget*> children;
  for (Widget* widget : channels_stack_.get_children()) {
    children.emplace(
        std::make_pair(static_cast<ChannelWindow*>(widget)->name(), widget));
  }
  int position = 0;
  for (auto p : children) {
    channels_stack_.child_property_position(*p.second).set_value(position);
    ++position;
  }
}

static std::string build_channel_title(const ChannelWindow& window) {
  std::ostringstream oss;
  oss << window.name() << " (" << window.unread_count() << ")";
  return oss.str();
}

ChannelWindow* MainWindow::add_channel_window(const channel& chan) {
  auto w = Gtk::manage(new ChannelWindow(team_, settings_, chan));
  w->channel_link_signal().connect(
      sigc::mem_fun(*this, &MainWindow::on_channel_link_clicked));
  w->property_unread_count().signal_changed().connect(sigc::bind(
      sigc::mem_fun(*this, &MainWindow::on_channel_unread_count_changed),
      chan.id));
  channels_stack_.add(*w, w->id(), build_channel_title(*w));
  return w;
}

void MainWindow::on_channel_unread_count_changed(
    const std::string& channel_id) {
  Widget* widget = channels_stack_.get_child_by_name(channel_id);
  if (widget == nullptr) {
    std::cerr
        << "[MainWindow] on_channel_unread_count_changed: unknown channel_id="
        << channel_id << std::endl;
    return;
  }

  ChannelWindow* window = static_cast<decltype(window)>(widget);

  channels_stack_.child_property_title(*window).set_value(
      build_channel_title(*window));
}

void MainWindow::on_visible_channel_changed() {
  Widget* widget = channels_stack_.get_visible_child();
  if (widget != nullptr) {
    static_cast<ChannelWindow*>(widget)->on_channel_visible();
  }
}

void MainWindow::request_update_emoji() {
  team_.api_client_->queue_post(
      "emoji.list", std::map<std::string, std::string>(),
      std::bind(&MainWindow::emoji_list_finished, this, std::placeholders::_1));
}

void MainWindow::emoji_list_finished(
    const boost::optional<Json::Value>& result) {
  if (result) {
    const Json::Value emojis = result.get()["emoji"];
    for (const std::string key : emojis.getMemberNames()) {
      const std::string val = emojis[key].asString();
      team_.emoji_loader_->add_custom_emoji(key, val);
    }
    redraw_messages();
  } else {
    std::cerr << "[MainWindow] failed to get custom emoji list" << std::endl;
  }
}

void MainWindow::on_emoji_changed_signal(const Json::Value& payload) {
  const std::string subtype = payload["subtype"].asString();
  if (subtype == "add") {
    const std::string name = payload["name"].asString();
    const std::string value = payload["value"].asString();
    team_.emoji_loader_->add_custom_emoji(name, value);
  } else if (subtype == "remove") {
    for (const auto& name : payload["names"]) {
      team_.emoji_loader_->remove_custom_emoji(name.asString());
    }
  } else {
    std::cerr << "[MainWindow] on_emoji_changed_signal: Unknown subtype: "
              << subtype << std::endl;
    return;
  }
  redraw_messages();
}

void MainWindow::redraw_messages() {
  for (Widget* widget : channels_stack_.get_children()) {
    static_cast<ChannelWindow*>(widget)->redraw_messages();
  }
}
