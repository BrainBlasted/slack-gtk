#include "message_text_view.h"
#include <iostream>
#include <regex>
#include "channels_store.h"
#include "emoji_loader.h"
#include "users_store.h"

MessageTextView::MessageTextView(team& team,
                                 Glib::RefPtr<Gio::Settings> settings)
    : team_(team), settings_(settings), raw_text_(), is_message_(false) {
  signal_event_after().connect(
      sigc::mem_fun(*this, &MessageTextView::on_event_after));
}

MessageTextView::~MessageTextView() {
}

static void create_tags(Glib::RefPtr<Gtk::TextBuffer> buffer) {
  auto tag = buffer->create_tag("info_message");
  tag->property_foreground() = "gray";
}

static Gtk::TextBuffer::iterator insert_user_link(
    Glib::RefPtr<Gtk::TextBuffer> buffer, Gtk::TextBuffer::iterator iter,
    const std::string& user_id, const std::string& user_name) {
  Glib::RefPtr<Gtk::TextTag> tag = buffer->create_tag();
  tag->property_foreground() = "blue";
  tag->property_underline() = Pango::UNDERLINE_SINGLE;
  tag->set_data("user_id", g_strdup(user_id.c_str()), g_free);
  return buffer->insert_with_tag(iter, user_name, tag);
}

static Gtk::TextBuffer::iterator insert_channel_link(
    Glib::RefPtr<Gtk::TextBuffer> buffer, Gtk::TextBuffer::iterator iter,
    const std::string& channel_id, const std::string& channel_name) {
  Glib::RefPtr<Gtk::TextTag> tag = buffer->create_tag();
  tag->property_foreground() = "blue";
  tag->property_underline() = Pango::UNDERLINE_SINGLE;
  tag->set_data("channel_id", g_strdup(channel_id.c_str()), g_free);
  return buffer->insert_with_tag(iter, channel_name, tag);
}

static Gtk::TextBuffer::iterator insert_url_link(
    Glib::RefPtr<Gtk::TextBuffer> buffer, Gtk::TextBuffer::iterator iter,
    const std::string& url, const std::string& link_name) {
  Glib::RefPtr<Gtk::TextTag> tag = buffer->create_tag();
  tag->property_foreground() = "blue";
  tag->property_underline() = Pango::UNDERLINE_SINGLE;
  tag->set_data("url", g_strdup(url.c_str()), g_free);
  return buffer->insert_with_tag(iter, link_name, tag);
}

Gtk::TextBuffer::iterator MessageTextView::insert_hyperlink(
    Glib::RefPtr<Gtk::TextBuffer> buffer, Gtk::TextBuffer::iterator iter,
    const std::string& linker) {
  std::regex url_re("^(.+)\\|(.+)$");
  std::smatch match;

  if (std::regex_match(linker, match, url_re)) {
    const std::string& left = match[1];
    const std::string& right = match[2];
    switch (left[0]) {
      case '@':
        iter = insert_user_link(buffer, iter, left.substr(1, left.size() - 1),
                                "@" + right);
        break;
      case '#':
        iter = insert_channel_link(
            buffer, iter, left.substr(1, left.size() - 1), "#" + right);
        break;
      default:
        iter = insert_url_link(buffer, iter, left, right);
        break;
    }
  } else {
    switch (linker[0]) {
      case '@': {
        const std::string user_id = linker.substr(1, linker.size() - 1);
        const boost::optional<user> o_user = team_.users_store_->find(user_id);
        if (o_user) {
          iter =
              insert_user_link(buffer, iter, user_id, "@" + o_user.get().name);
        } else {
          std::cerr << "[MessageTextView] cannot find linked user " << linker
                    << std::endl;
          iter = insert_user_link(buffer, iter, linker, linker);
        }
      } break;
      case '#': {
        const std::string channel_id = linker.substr(1, linker.size());
        const boost::optional<channel> o_channel =
            team_.channels_store_->find(channel_id);
        if (o_channel) {
          iter = insert_channel_link(buffer, iter, channel_id,
                                     "#" + o_channel.get().name);
        } else {
          std::cerr << "[MessageTextView] cannot find linked channel " << linker
                    << std::endl;
          iter = insert_channel_link(buffer, iter, linker, linker);
        }
      } break;
      default:
        iter = insert_url_link(buffer, iter, linker, linker);
        break;
    }
  }
  return iter;
}

static std::vector<std::pair<std::string, std::string>> tokenize_emojis(
    const std::vector<std::pair<std::string, std::string>>& tokens) {
  const std::regex emoji_re(":([a-zA-Z0-9_]+):");

  std::vector<std::pair<std::string, std::string>> ret;

  for (const auto& tag_and_text : tokens) {
    const std::string& tag = tag_and_text.first;
    const std::string& text = tag_and_text.second;
    std::sregex_iterator re_it(text.begin(), text.end(), emoji_re), re_end;
    if (re_it == re_end) {
      ret.push_back(tag_and_text);
      continue;
    }

    std::size_t pos = 0;
    for (; re_it != re_end; ++re_it) {
      ret.emplace_back(
          std::make_pair(tag, text.substr(pos, re_it->position() - pos)));
      const std::string& emoji = (*re_it)[1].str();
      std::string lower_emoji;
      std::transform(emoji.begin(), emoji.end(),
                     std::back_inserter(lower_emoji),
                     [](char c) { return std::tolower(c); });
      ret.emplace_back(std::make_pair("emoji_" + tag, lower_emoji));
      pos = re_it->position() + re_it->length();
    }
    if (pos != text.size()) {
      ret.emplace_back(std::make_pair(tag, text.substr(pos)));
    }
  }

  return ret;
}

static void replace_all(std::string& text, const std::string& sub,
                        const std::string& replacement) {
  for (std::string::size_type pos = text.find(sub); pos != std::string::npos;
       pos = text.find(sub, pos + 1)) {
    text.replace(pos, sub.size(), replacement);
  }
}

static void replace_entity_references(
    std::vector<std::pair<std::string, std::string>>& tokens) {
  for (auto& tag_and_text : tokens) {
    std::string& text = tag_and_text.second;
    // https://api.slack.com/docs/formatting#how_to_escape_characters
    replace_all(text, "&amp;", "&");
    replace_all(text, "&lt;", "<");
    replace_all(text, "&gt;", ">");
  }
}

Gtk::TextBuffer::iterator MessageTextView::insert_markdown_text(
    Glib::RefPtr<Gtk::TextBuffer> buffer, Gtk::TextBuffer::iterator iter,
    const std::string& text, bool is_message) {
  // TODO: format markdown-like text
  std::vector<std::pair<std::string, std::string>> tokens(
      1, std::make_pair(is_message ? "" : "info_message", text));
  tokens = tokenize_emojis(tokens);
  replace_entity_references(tokens);

  for (const auto& tag_and_text : tokens) {
    const std::string& tag = tag_and_text.first;
    const std::string& text = tag_and_text.second;
    if (text.empty()) {
      continue;
    }
    if (tag.empty()) {
      iter = buffer->insert(iter, text);
    } else if (tag.compare(0, 6, "emoji_") == 0) {
      Glib::RefPtr<Gdk::Pixbuf> emoji = team_.emoji_loader_->find(text);
      if (emoji) {
        const auto size = settings_->get_uint("emoji-size");
        iter = buffer->insert_pixbuf(
            iter, emoji->scale_simple(size, size, Gdk::INTERP_BILINEAR));
      } else {
        std::cerr << "[MessageTextView] cannot find emoji " << text
                  << std::endl;
        const std::string fallback_tag = tag.substr(6);
        if (fallback_tag.empty()) {
          iter = buffer->insert(iter, ":" + text + ":");
        } else {
          iter = buffer->insert_with_tag(iter, ":" + text + ":", tag.substr(6));
        }
      }
    } else {
      iter = buffer->insert_with_tag(iter, text, tag);
    }
  }
  return iter;
}

void MessageTextView::set_text(const std::string& text, bool is_message) {
  raw_text_ = text;
  is_message_ = is_message;
  redraw_message();
}

std::string MessageTextView::get_text() const {
  return get_buffer()->get_text();
}

bool MessageTextView::on_motion_notify_event(GdkEventMotion* event) {
  int buffer_x, buffer_y;

  window_to_buffer_coords(Gtk::TEXT_WINDOW_WIDGET, event->x, event->y, buffer_x,
                          buffer_y);

  Gtk::TextBuffer::iterator iter;
  get_iter_at_location(iter, buffer_x, buffer_y);
  bool hovering = false;
  for (Glib::RefPtr<Gtk::TextTag> tag : iter.get_tags()) {
    if (tag->get_data("user_id") != nullptr ||
        tag->get_data("channel_id") != nullptr ||
        tag->get_data("url") != nullptr) {
      hovering = true;
      break;
    }
  }
  Glib::RefPtr<Gdk::Window> window = get_window(Gtk::TEXT_WINDOW_TEXT);
  Glib::RefPtr<const Gdk::Cursor> cursor = window->get_cursor();
  if (hovering) {
    if (!pointer_cursor_) {
      pointer_cursor_ = Gdk::Cursor::create(window->get_display(), "pointer");
    }
    window->set_cursor(pointer_cursor_);
  } else {
    if (!default_cursor_) {
      default_cursor_ = Gdk::Cursor::create(window->get_display(), "default");
    }
    window->set_cursor(default_cursor_);
  }

  return false;
}

void MessageTextView::on_event_after(GdkEvent* event) {
  if (event->type != GDK_BUTTON_RELEASE) {
    return;
  }
  const GdkEventButton* button_event =
      reinterpret_cast<decltype(button_event)>(event);
  if (button_event->button != GDK_BUTTON_PRIMARY) {
    return;
  }

  /* we shouldn't follow a link if the user has selected something */
  Gtk::TextBuffer::iterator start, end;
  if (get_buffer()->get_selection_bounds(start, end)) {
    return;
  }

  int buffer_x, buffer_y;
  window_to_buffer_coords(Gtk::TEXT_WINDOW_WIDGET, button_event->x,
                          button_event->y, buffer_x, buffer_y);
  Gtk::TextBuffer::iterator iter;
  get_iter_at_location(iter, buffer_x, buffer_y);
  for (Glib::RefPtr<Gtk::TextTag> tag : iter.get_tags()) {
    void* data;
    if ((data = tag->get_data("user_id")) != nullptr) {
      signal_user_link_clicked_.emit(
          std::string(static_cast<const char*>(data)));
      break;
    } else if ((data = tag->get_data("channel_id")) != nullptr) {
      signal_channel_link_clicked_.emit(
          std::string(static_cast<const char*>(data)));
      break;
    } else if ((data = tag->get_data("url")) != nullptr) {
      GError* error = nullptr;
      const gchar* url = static_cast<decltype(url)>(data);
      if (!gtk_show_uri(get_screen()->gobj(), url, gtk_get_current_event_time(),
                        &error)) {
        g_warning("Unable to open link %s: %s", url, error->message);
        g_error_free(error);
      }
      break;
    }
  }
}

sigc::signal<void, const std::string&>
MessageTextView::signal_user_link_clicked() {
  return signal_user_link_clicked_;
}
sigc::signal<void, const std::string&>
MessageTextView::signal_channel_link_clicked() {
  return signal_channel_link_clicked_;
}

void MessageTextView::redraw_message() {
  Glib::RefPtr<Gtk::TextBuffer> buffer = Gtk::TextBuffer::create();
  create_tags(buffer);
  Gtk::TextBuffer::iterator iter = buffer->get_iter_at_offset(0);

  std::regex markup_re("<([^<>]*)>");
  std::sregex_iterator re_it(raw_text_.begin(), raw_text_.end(), markup_re),
      re_end;

  std::size_t pos = 0;

  for (; re_it != re_end; ++re_it) {
    iter = insert_markdown_text(buffer, iter,
                                raw_text_.substr(pos, re_it->position() - pos),
                                is_message_);
    iter = insert_hyperlink(buffer, iter, (*re_it)[1].str());
    pos = re_it->position() + re_it->length();
  }
  iter = insert_markdown_text(buffer, iter, raw_text_.substr(pos), is_message_);

  set_buffer(buffer);
}
