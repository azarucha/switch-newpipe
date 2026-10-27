#pragma once

#include <mutex>
#include <string>

#include "newpipe/youtube_resolver.hpp"

namespace newpipe {

struct AppSettings {
    std::string language = "auto";
    std::string startup_tab = "home";
    std::string home_kiosk = "recommended";
    PlaybackQualityMode playback_quality = PlaybackQualityMode::BEST;
    bool hide_short_videos = false;
};

std::string default_settings_store_path();

// Language/region sent to YouTube (hl/gl), following the app language.
struct ContentLocale {
    std::string hl;
    std::string gl;
};

// "auto" follows the console language; main() passes it in once borealis knows it.
void set_system_locale(const std::string& locale);
ContentLocale content_locale();

class SettingsStore {
public:
    static SettingsStore& instance();

    bool load(std::string* error_message = nullptr);
    bool reload(std::string* error_message = nullptr);
    AppSettings settings();

    bool update_language(const std::string& language, std::string* error_message = nullptr);
    bool update_startup_tab(const std::string& startup_tab, std::string* error_message = nullptr);
    bool update_home_kiosk(const std::string& home_kiosk, std::string* error_message = nullptr);
    bool update_playback_quality(
        PlaybackQualityMode playback_quality,
        std::string* error_message = nullptr);
    bool update_hide_short_videos(bool hide_short_videos, std::string* error_message = nullptr);
    bool reset(std::string* error_message = nullptr);

private:
    SettingsStore() = default;

    bool ensure_loaded(std::string* error_message);
    bool persist(std::string* error_message);

    mutable std::mutex mutex_;
    bool loaded_ = false;
    AppSettings settings_;
};

}  // namespace newpipe
