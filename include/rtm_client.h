#ifndef SLACK_GTK_RTM_CLIENT_H
#define SLACK_GTK_RTM_CLIENT_H

#include <json/json.h>
#include <libsoup/soup-session.h>
#include <sigc++/sigc++.h>

class rtm_client {
 public:
  rtm_client(const Json::Value& rtm_start);
  rtm_client(const rtm_client& other) = delete;
  ~rtm_client();

  void start();

  typedef sigc::signal<void, Json::Value> message_signal_type;
  message_signal_type hello_signal();
  message_signal_type reconnect_url_signal();
  message_signal_type presence_change_signal();
  message_signal_type pref_change_signal();
  message_signal_type message_signal();
  message_signal_type channel_marked_signal();
  message_signal_type channel_joined_signal();
  message_signal_type channel_left_signal();
  message_signal_type user_typing_signal();
  message_signal_type emoji_changed_signal();

 private:
  static void session_connect_callback(GObject* source, GAsyncResult* result,
                                       gpointer user_data);
  static void closed_callback(SoupWebsocketConnection* connection,
                              gpointer user_data);
  static void closing_callback(SoupWebsocketConnection* connection,
                               gpointer user_data);
  static void error_callback(SoupWebsocketConnection* connection, GError* error,
                             gpointer user_data);
  static void message_callback(SoupWebsocketConnection* connection, gint type,
                               GBytes* message, gpointer user_data);

  void on_session_connect(SoupSession* session, GAsyncResult* result);
  void on_closed();
  void on_closing();
  void on_error(GError* error);
  void on_message(SoupWebsocketDataType type, GBytes* message);

  void handle_payload(const char* payload, size_t size);

  std::string url_;
  SoupSession* session_;
  SoupWebsocketConnection* connection_;

  message_signal_type hello_signal_, reconnect_url_signal_,
      presence_change_signal_, pref_change_signal_, message_signal_,
      channel_marked_signal_, channel_joined_signal_, channel_left_signal_,
      user_typing_signal_, emoji_changed_signal_;
};

#endif
