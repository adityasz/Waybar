#include "AAppIconLabel.hpp"

#include <gdkmm/pixbuf.h>
#include <glibmm/fileutils.h>
#include <glibmm/keyfile.h>
#include <glibmm/miscutils.h>
#include <spdlog/spdlog.h>

#include <filesystem>
#include <optional>

#include "util/gtk_icon.hpp"

namespace waybar {

AAppIconLabel::AAppIconLabel(const Json::Value& config, const std::string& name,
                             const std::string& id, const std::string& format, uint16_t interval,
                             bool ellipsize, bool enable_click, bool enable_scroll)
    : AIconLabel(config, name, id, format, interval, ellipsize, enable_click, enable_scroll) {
  // Icon size
  if (config["icon-size"].isUInt()) {
    app_icon_size_ = config["icon-size"].asUInt();
  }
  image_.set_pixel_size(app_icon_size_);
}

std::string toLowerCase(const std::string& input) {
  std::string result = input;
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return result;
}

std::optional<std::string> getFileBySuffix(const std::string& dir, const std::string& suffix,
                                           bool check_lower_case) {
  if (!std::filesystem::exists(dir)) {
    return {};
  }
  for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
    if (entry.is_regular_file()) {
      std::string filename = entry.path().filename().string();
      if (filename.size() < suffix.size()) {
        continue;
      }
      if ((filename.compare(filename.size() - suffix.size(), suffix.size(), suffix) == 0) ||
          (check_lower_case && filename.compare(filename.size() - suffix.size(), suffix.size(),
                                                toLowerCase(suffix)) == 0)) {
        return entry.path().string();
      }
    }
  }

  return {};
}

std::optional<std::string> getFileBySuffix(const std::string& dir, const std::string& suffix) {
  return getFileBySuffix(dir, suffix, false);
}

std::optional<std::string> getDesktopFilePath(const std::string& app_identifier,
                                              const std::string& alternative_app_identifier) {
  if (app_identifier.empty()) {
    return {};
  }

  auto data_dirs = Glib::get_system_data_dirs();
  data_dirs.insert(data_dirs.begin(), Glib::get_user_data_dir());
  for (const auto& data_dir : data_dirs) {
    const auto data_app_dir = data_dir + "/applications/";
    auto desktop_file_suffix = app_identifier + ".desktop";
    // searching for file by suffix catches cases like terminal emulator "foot" where class is
    // "footclient" and desktop file is named "org.codeberg.dnkl.footclient.desktop"
    auto desktop_file_path = getFileBySuffix(data_app_dir, desktop_file_suffix, true);
    // "true" argument allows checking for lowercase - this catches cases where class name is
    // "LibreWolf" and desktop file is named "librewolf.desktop"
    if (desktop_file_path.has_value()) {
      return desktop_file_path;
    }
    if (!alternative_app_identifier.empty()) {
      desktop_file_suffix = alternative_app_identifier + ".desktop";
      desktop_file_path = getFileBySuffix(data_app_dir, desktop_file_suffix, true);
      if (desktop_file_path.has_value()) {
        return desktop_file_path;
      }
    }
  }
  return {};
}

struct AppInfo {
  std::optional<Glib::ustring> icon_name;
  std::optional<Glib::ustring> app_name;
};

AppInfo getAppInfo(const std::string& app_identifier,
                   const std::string& alternative_app_identifier) {
  AppInfo info;
  const auto desktop_file_path = getDesktopFilePath(app_identifier, alternative_app_identifier);
  if (!desktop_file_path.has_value()) {
    // Try some heuristics to find a matching icon

    if (DefaultGtkIconThemeWrapper::has_icon(app_identifier)) {
      info.icon_name = app_identifier;
      return info;
    }

    auto app_identifier_desktop = app_identifier + "-desktop";
    if (DefaultGtkIconThemeWrapper::has_icon(app_identifier_desktop)) {
      info.icon_name = app_identifier_desktop;
      return info;
    }

    auto first_space = app_identifier.find_first_of(' ');
    if (first_space != std::string::npos) {
      auto first_word = toLowerCase(app_identifier.substr(0, first_space));
      if (DefaultGtkIconThemeWrapper::has_icon(first_word)) {
        info.icon_name = first_word;
        return info;
      }
    }

    const auto first_dash = app_identifier.find_first_of('-');
    if (first_dash != std::string::npos) {
      auto first_word = toLowerCase(app_identifier.substr(0, first_dash));
      if (DefaultGtkIconThemeWrapper::has_icon(first_word)) {
        info.icon_name = first_word;
        return info;
      }
    }

    return info;
  }

  try {
    Glib::KeyFile desktop_file;
    desktop_file.load_from_file(desktop_file_path.value());
    info.icon_name = desktop_file.get_string("Desktop Entry", "Icon");
    info.app_name = desktop_file.get_string("Desktop Entry", "Name");
  } catch (Glib::FileError& error) {
    spdlog::warn("Error while loading desktop file {}: {}", desktop_file_path.value(),
                 error.what().c_str());
  } catch (Glib::KeyFileError& error) {
    spdlog::warn("Error while loading desktop file {}: {}", desktop_file_path.value(),
                 error.what().c_str());
  }
  return info;
}

std::optional<Glib::ustring> getIconName(const std::string& app_identifier,
                                         const std::string& alternative_app_identifier) {
  return getAppInfo(app_identifier, alternative_app_identifier).icon_name;
}

void AAppIconLabel::updateAppIconName(const std::string& app_identifier,
                                      const std::string& alternative_app_identifier) {
  if (!iconEnabled()) {
    return;
  }

  const auto app_info = getAppInfo(app_identifier, alternative_app_identifier);
  if (app_info.icon_name.has_value()) {
    app_icon_name_ = app_info.icon_name.value();
  } else {
    app_icon_name_ = "";
  }
  if (app_info.app_name.has_value()) {
    app_name_ = app_info.app_name.value();
  } else {
    app_name_ = "";
  }
  update_app_icon_ = true;
}

void AAppIconLabel::updateAppIcon() {
  if (update_app_icon_) {
    update_app_icon_ = false;
    if (app_icon_name_.empty()) {
      image_.set_visible(false);
    } else if (app_icon_name_.front() == '/') {
      int scaled_icon_size = app_icon_size_ * image_.get_scale_factor();
      auto pixbuf = Gdk::Pixbuf::create_from_file(app_icon_name_, scaled_icon_size, scaled_icon_size);
      image_.set(pixbuf);
      image_.set_visible(true);
    } else {
      image_.set_from_icon_name(app_icon_name_, Gtk::ICON_SIZE_INVALID);
      image_.set_visible(true);
    }
  }
}

auto AAppIconLabel::update() -> void {
  updateAppIcon();
  AIconLabel::update();
}

}  // namespace waybar
