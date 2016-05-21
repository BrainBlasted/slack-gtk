#ifndef SLACK_GTK_MAIN_WINDOW_H
#define SLACK_GTK_MAIN_WINDOW_H

#include <gtkmm/applicationwindow.h>
#include <gtkmm/box.h>
#include <gtkmm/stack.h>
#include <gtkmm/stacksidebar.h>
#include "api_client.h"
#include "icon_loader.h"
#include "rtm_client.h"
#include "users_store.h"

class MainWindow : public Gtk::ApplicationWindow {
 public:
  MainWindow(const api_client& api_client, const Json::Value& json);
  virtual ~MainWindow();

 private:
  void on_hello_signal(const Json::Value& payload);
  void on_reconnect_url_signal(const Json::Value& payload);
  void on_presence_change_signal(const Json::Value& payload);
  void on_pref_change_signal(const Json::Value& payload);
  void on_message_signal(const Json::Value& payload);
  void on_channel_marked_signal(const Json::Value& payload);

  void on_channel_link_clicked(const std::string& channel_id);

  void append_message(const std::string& text);

  Gtk::Box box_;
  Gtk::StackSidebar channels_sidebar_;
  Gtk::Stack channels_stack_;

  rtm_client rtm_client_;
  users_store users_store_;
  icon_loader icon_loader_;
};
#endif
