#ifndef SLACK_GTK_CHANNEL_WINDOW_H
#define SLACK_GTK_CHANNEL_WINDOW_H

#include <glibmm/property.h>
#include <gtkmm/box.h>
#include <gtkmm/listbox.h>
#include <gtkmm/scrolledwindow.h>
#include <json/json.h>
#include "api_client.h"
#include "channel.h"
#include "icon_loader.h"
#include "message_entry.h"
#include "users_store.h"

class MessageRow;

class ChannelWindow : public Gtk::Box {
 public:
  ChannelWindow(const api_client& api_client, const users_store& users_store,
                icon_loader& icon_loader, const channel& chan);

  const std::string& id() const;
  const std::string& name() const;
  sigc::signal<void, const std::string&> channel_link_signal();

  Glib::PropertyProxy<int> property_unread_count();
  int unread_count() const;

  void mark_as_read(const std::string& ts);

  void on_message_signal(const Json::Value& payload);
  MessageRow* append_message(const Json::Value& payload);
  void on_channels_history(const boost::optional<Json::Value>& result);
  void on_channel_link_clicked(const std::string& channel_id);
  void on_channel_visible();

 private:
  void send_notification(const MessageRow* row);

  Gtk::ScrolledWindow messages_scrolled_window_;
  Gtk::ListBox messages_list_box_;
  MessageEntry message_entry_;

  Glib::Property<int> unread_count_;

  std::string id_;
  std::string name_;
  api_client api_client_;
  const users_store& users_store_;
  icon_loader& icon_loader_;

  sigc::signal<void, const std::string&> channel_link_signal_;
};

#endif
