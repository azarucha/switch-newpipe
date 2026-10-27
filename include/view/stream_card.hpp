#pragma once

#include <borealis.hpp>

#include "newpipe/models.hpp"

// YouTube-style video card: 16:9 thumbnail with a duration badge, a two-line
// title and the channel / views / age lines underneath.
class StreamCard : public brls::Box {
public:
    // Fixed pixel sizes: percentage widths do not resolve inside the
    // auto-sized grid rows on the Switch.
    static constexpr float kTabWidth = 350.0f;   // tabs next to the sidebar
    static constexpr float kFullWidth = 384.0f;  // full-screen feeds

    explicit StreamCard(float width = kTabWidth);

    void setData(const newpipe::StreamItem& item);

private:
    BRLS_BIND(brls::Box, thumbBox, "stream/thumb_box");
    BRLS_BIND(brls::Image, thumbnail, "stream/thumbnail");
    BRLS_BIND(brls::Box, badge, "stream/badge");
    BRLS_BIND(brls::Label, badgeLabel, "stream/badge_label");
    BRLS_BIND(brls::Label, titleLabel, "stream/title");
    BRLS_BIND(brls::Label, channelLabel, "stream/channel");
    BRLS_BIND(brls::Label, metaLabel, "stream/meta");
};
