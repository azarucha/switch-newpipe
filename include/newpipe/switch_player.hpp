#pragma once

#include <optional>
#include <string>

namespace newpipe {

struct PlaybackRequest {
    std::string title;
    std::string url;
    std::string referer;
    std::string http_header_fields;
    // Set when the player restarts itself, e.g. after a quality change.
    double start_seconds = 0.0;
    int quality_override = -1;  // PlaybackQualityMode, -1 = use settings
};

// When the user switches quality mid-video the player stops and fills
// restart_request; the caller should run the player again with it.
bool run_switch_player(
    const PlaybackRequest& request,
    std::string& error,
    std::optional<PlaybackRequest>* restart_request = nullptr);

}  // namespace newpipe
