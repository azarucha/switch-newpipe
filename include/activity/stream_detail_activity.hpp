#pragma once

#include <functional>
#include <string>

#include <borealis.hpp>

#include "newpipe/models.hpp"
#include "newpipe/youtube_catalog_service.hpp"

class StreamDetailActivity : public brls::Activity {
public:
    explicit StreamDetailActivity(newpipe::StreamItem item);

    CONTENT_FROM_XML_RES("activity/stream_detail.xml");

    void onContentAvailable() override;

private:
    void loadDetail();
    void updateFavoriteAction();
    void playStream();
    void openChannelFeed();
    void openRelatedFeed();
    void openExtrasMenu();
    void openPlaylistFeed();
    void openComments();
    void startDownload();
    void toggleFavorite();
    void buildActions(bool has_channel, bool has_related);
    brls::Box* makeActionButton(const std::string& label, bool primary, std::function<void()> on_click);

    newpipe::StreamItem item_;
    newpipe::YouTubeCatalogService service_;

    BRLS_BIND(brls::Image, thumbnail, "detail/thumbnail");
    BRLS_BIND(brls::Box, badge, "detail/badge");
    BRLS_BIND(brls::Label, badgeLabel, "detail/badge_label");
    BRLS_BIND(brls::Label, titleLabel, "detail/title");
    BRLS_BIND(brls::Label, channelLabel, "detail/channel");
    BRLS_BIND(brls::Label, metaLabel, "detail/meta");
    BRLS_BIND(brls::Box, actionsBox, "detail/actions");
    BRLS_BIND(brls::Box, actionsMoreBox, "detail/actions_more");
    BRLS_BIND(brls::Label, bodyLabel, "detail/body");
    brls::Box* favoriteButton_ = nullptr;
    BRLS_BIND(brls::ProgressSpinner, spinner, "detail/spinner");
};
