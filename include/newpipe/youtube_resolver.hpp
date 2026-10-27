#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "newpipe/http_client.hpp"
#include "newpipe/throttling_decrypter.hpp"

namespace newpipe {

enum class PlaybackQualityMode {
    // BEST auto-selects by console state: docked -> 1080p, handheld -> 720p.
    BEST = 0,
    HD_1080 = 1,
    HD_720 = 2,
    LOW_320 = 3,
};

// Resolves a quality mode to a target video height. BEST inspects the Switch
// operation mode (docked -> 1080p, handheld -> 720p); on host it targets 1080p.
int preferred_height_for_quality(PlaybackQualityMode mode);

struct CaptionTrack {
    std::string language;
    std::string name;
    std::string url;  // WebVTT
};

struct ResolvedPlayback {
    std::string stream_url;
    std::string referer;
    std::string http_header_fields;
    std::string quality_label;
    std::string audio_language;
    int hls_bitrate = 0;
    std::string playlist_body;
    std::string external_audio_url;
    std::string external_audio_playlist_body;
    std::string fallback_stream_url;
    std::string fallback_referer;
    std::string fallback_http_header_fields;
    std::string fallback_quality_label;
    std::string fallback_external_audio_url;
    bool use_ump = false;
    bool is_live = false;
    std::vector<CaptionTrack> captions;
};

// Segment lists of one HLS variant, for downloading a video to the SD card.
// Video and audio are separate MPEG-TS streams.
struct DownloadSource {
    int height = 0;
    std::string audio_language;
    std::vector<std::string> video_segments;
    std::vector<std::string> audio_segments;
};

using ResolverStatusCallback = std::function<void(const std::string&, const std::string&)>;

class YouTubeResolver {
public:
    explicit YouTubeResolver(HttpClient* client = nullptr);

    std::optional<ResolvedPlayback> resolve(
        const std::string& url,
        std::string& error_message,
        ResolverStatusCallback on_status = {});

    // Overrides the quality from settings for this resolver only.
    void set_quality_override(PlaybackQualityMode mode) { quality_override_ = mode; }

    std::optional<DownloadSource> resolve_download(
        const std::string& url,
        int preferred_height,
        std::string& error_message);

    static bool is_youtube_url(const std::string& url);
    static std::optional<std::string> extract_video_id(const std::string& url);

    ThrottlingDecrypter& throttle_decrypter() { return throttle_decrypter_; }
    void apply_throttle_transform(ResolvedPlayback& playback);

private:
    std::optional<ResolvedPlayback> resolve_internal(
        const std::string& url,
        std::string& error_message,
        ResolverStatusCallback on_status);

    HttpsHttpClient owned_client_;
    HttpClient* client_ = nullptr;
    ThrottlingDecrypter throttle_decrypter_;
    std::optional<PlaybackQualityMode> quality_override_;
};

}  // namespace newpipe
