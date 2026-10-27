#include "activity/comment_feed_activity.hpp"
#include "activity/stream_detail_activity.hpp"

#include "activity/stream_feed_activity.hpp"
#include "newpipe/download_manager.hpp"
#include "newpipe/i18n.hpp"
#include "newpipe/image_loader.hpp"
#include "newpipe/settings_store.hpp"
#include "newpipe/youtube_resolver.hpp"
#include "newpipe/library_store.hpp"
#include "newpipe/log.hpp"
#include "newpipe/playback_helper.hpp"
#include "newpipe/runtime.hpp"

StreamDetailActivity::StreamDetailActivity(newpipe::StreamItem item)
    : item_(std::move(item))
    , service_() {
}

void StreamDetailActivity::onContentAvailable() {
    this->registerAction(newpipe::tr("hints/back"), brls::BUTTON_B, [](brls::View*) {
        brls::Application::popActivity();
        return true;
    });

    this->loadDetail();
}

brls::Box* StreamDetailActivity::makeActionButton(
    const std::string& label,
    bool primary,
    std::function<void()> on_click) {
    auto* button = new brls::Box(brls::Axis::ROW);
    button->setFocusable(true);
    button->setPadding(9, 20, 9, 20);
    button->setMarginRight(12);
    button->setCornerRadius(20);
    button->setHighlightCornerRadius(22);
    button->setHideHighlightBackground(true);
    button->setBackgroundColor(primary ? nvgRGB(241, 241, 241) : nvgRGB(39, 39, 39));

    auto* text = new brls::Label();
    text->setFontSize(16);
    text->setSingleLine(true);
    text->setText(label);
    text->setTextColor(primary ? nvgRGB(15, 15, 15) : nvgRGB(241, 241, 241));
    button->addView(text);

    button->registerClickAction([on_click](brls::View*) {
        on_click();
        return true;
    });
    button->addGestureRecognizer(new brls::TapGestureRecognizer(button));
    return button;
}

// YouTube-style action pills: play, download and save first, navigation below.
void StreamDetailActivity::buildActions(bool has_channel, bool has_related) {
    if (!this->actionsBox || !this->actionsMoreBox) {
        return;
    }
    this->actionsBox->clearViews();
    this->actionsMoreBox->clearViews();

    auto* play = this->makeActionButton("\xE2\x96\xB6  " + newpipe::tr("detail/play_action"), true,
                                        [this]() { this->playStream(); });
    this->actionsBox->addView(play);
    this->actionsBox->addView(this->makeActionButton(newpipe::tr("detail/download_action"), false,
                                                     [this]() { this->startDownload(); }));
    const bool favorite = newpipe::LibraryStore::instance().is_favorite(this->item_.url);
    this->favoriteButton_ = this->makeActionButton(
        favorite ? newpipe::tr("detail/unfavorite_action") : newpipe::tr("detail/favorite_action"), false,
        [this]() { this->toggleFavorite(); });
    this->actionsBox->addView(this->favoriteButton_);

    if (has_channel) {
        this->actionsMoreBox->addView(this->makeActionButton(newpipe::tr("detail/channel_action"), false,
                                                             [this]() { this->openChannelFeed(); }));
    }
    if (has_related) {
        this->actionsMoreBox->addView(this->makeActionButton(newpipe::tr("detail/related_action"), false,
                                                             [this]() { this->openRelatedFeed(); }));
    }
    this->actionsMoreBox->addView(this->makeActionButton(newpipe::tr("detail/comments_action"), false,
                                                         [this]() { this->openComments(); }));
    if (this->item_.is_playlist || this->item_.url.find("list=") != std::string::npos) {
        this->actionsMoreBox->addView(this->makeActionButton(newpipe::tr("detail/playlist_action"), false,
                                                             [this]() { this->openPlaylistFeed(); }));
    }

    brls::Application::giveFocus(play);
}

void StreamDetailActivity::loadDetail() {
    if (this->spinner) {
        this->spinner->setVisibility(brls::Visibility::VISIBLE);
    }

    const auto detail = this->service_.get_stream_detail(this->item_.url);
    if (detail.has_value()) {
        // Keep list fields the detail request does not return.
        const std::string thumbnail_url = this->item_.thumbnail_url;
        const std::string duration = this->item_.duration_text;
        const std::string views = this->item_.view_count_text;
        const std::string published = this->item_.published_text;
        this->item_ = detail->item;
        if (this->item_.thumbnail_url.empty()) {
            this->item_.thumbnail_url = thumbnail_url;
        }
        if (this->item_.duration_text.empty()) {
            this->item_.duration_text = duration;
        }
        if (this->item_.view_count_text.empty()) {
            this->item_.view_count_text = views;
        }
        if (this->item_.published_text.empty()) {
            this->item_.published_text = published;
        }
    }

    if (this->titleLabel) {
        this->titleLabel->setText(
            this->item_.title.empty() ? newpipe::tr("detail/default_title") : this->item_.title);
    }
    if (this->channelLabel) {
        std::string channel = this->item_.channel_name;
        if (detail.has_value() && !detail->channel_subscriber_count_text.empty()) {
            channel += "  \xE2\x80\xA2  " + detail->channel_subscriber_count_text;
        }
        this->channelLabel->setText(channel);
    }

    std::string meta = this->item_.view_count_text;
    if (!this->item_.published_text.empty()) {
        meta += meta.empty() ? this->item_.published_text : "  \xE2\x80\xA2  " + this->item_.published_text;
    }
    if (this->metaLabel) {
        this->metaLabel->setText(meta);
    }
    if (this->badge && this->badgeLabel) {
        this->badgeLabel->setText(this->item_.duration_text);
        this->badge->setVisibility(this->item_.duration_text.empty() ? brls::Visibility::GONE
                                                                     : brls::Visibility::VISIBLE);
    }
    if (this->thumbnail && !this->item_.thumbnail_url.empty()) {
        newpipe::ImageLoader::instance().load(this->item_.thumbnail_url, this->thumbnail);
    }

    // The description box does not scroll; keep it to what fits.
    std::string body;
    if (detail.has_value() && !detail->description.empty()) {
        body = detail->description;
    }
    if (body.empty()) {
        body = newpipe::tr("detail/no_description");
    }
    if (body.size() > 900) {
        size_t cut = 900;
        while (cut > 0 && (static_cast<unsigned char>(body[cut]) & 0xC0) == 0x80) {
            --cut;
        }
        body = body.substr(0, cut) + "\xE2\x80\xA6";
    }
    if (this->bodyLabel) {
        this->bodyLabel->setText(body);
    }

    const bool has_channel = !this->item_.channel_id.empty() || !this->item_.channel_url.empty();
    const bool has_related = detail.has_value() && !detail->related_items.empty();
    this->buildActions(has_channel, has_related);

    if (this->spinner) {
        this->spinner->setVisibility(brls::Visibility::GONE);
    }
}

void StreamDetailActivity::updateFavoriteAction() {
    if (!this->favoriteButton_ || this->favoriteButton_->getChildren().empty()) {
        return;
    }
    auto* label = dynamic_cast<brls::Label*>(this->favoriteButton_->getChildren().front());
    if (label) {
        const bool favorite = newpipe::LibraryStore::instance().is_favorite(this->item_.url);
        label->setText(favorite ? newpipe::tr("detail/unfavorite_action") : newpipe::tr("detail/favorite_action"));
    }
}

void StreamDetailActivity::playStream() {
    const auto detail = this->service_.get_stream_detail(this->item_.url);
    const auto request = newpipe::build_playback_request(this->item_, detail);
    if (!request.has_value()) {
        brls::Application::notify(newpipe::tr("detail/playback_url_failed"));
        return;
    }

    const newpipe::StreamItem history_item = detail.has_value() ? detail->item : this->item_;
    std::string ignored_error;
    newpipe::LibraryStore::instance().add_history(history_item, &ignored_error);
    newpipe::queue_playback(*request);
    brls::Application::quit();
}

void StreamDetailActivity::openChannelFeed() {
    const auto feed = this->service_.get_channel_feed(this->item_);
    if (!feed.has_value()) {
        brls::Application::notify(
            this->service_.error_message().empty() ? newpipe::tr("detail/channel_load_failed")
                                                   : this->service_.error_message());
        return;
    }

    brls::Application::pushActivity(new StreamFeedActivity(feed->kiosk.title, feed->items));
}

void StreamDetailActivity::openRelatedFeed() {
    const auto feed = this->service_.get_related_feed(this->item_);
    if (!feed.has_value()) {
        brls::Application::notify(
            this->service_.error_message().empty() ? newpipe::tr("detail/related_load_failed")
                                                   : this->service_.error_message());
        return;
    }

    brls::Application::pushActivity(new StreamFeedActivity(feed->kiosk.title, feed->items));
}

void StreamDetailActivity::openExtrasMenu() {
    auto* dialog = new brls::Dialog(newpipe::tr("detail/extras_title"));

    if (this->item_.url.find("list=") != std::string::npos) {
        dialog->addButton(newpipe::tr("detail/playlist_action"), [this, dialog]() {
            dialog->close();
            this->openPlaylistFeed();
        });
    }

    dialog->addButton(newpipe::tr("detail/comments_action"), [this, dialog]() {
        dialog->close();
        this->openComments();
    });
    dialog->addButton(newpipe::tr("detail/download_action"), [this, dialog]() {
        dialog->close();
        this->startDownload();
    });
    dialog->setCancelable(true);
    dialog->open();
}

void StreamDetailActivity::startDownload() {
    newpipe::StreamItem item = this->item_;
    if (item.id.empty()) {
        item.id = newpipe::YouTubeResolver::extract_video_id(item.url).value_or("");
    }

    const auto quality = newpipe::SettingsStore::instance().settings().playback_quality;
    std::string error;
    if (!newpipe::DownloadManager::instance().enqueue(
            item, newpipe::preferred_height_for_quality(quality), &error)) {
        brls::Application::notify(error.empty() ? newpipe::tr("detail/download_failed") : error);
        return;
    }
    brls::Application::notify(newpipe::tr("detail/download_started"));
}

void StreamDetailActivity::openPlaylistFeed() {
    const auto feed = this->item_.is_playlist ? this->service_.get_full_playlist(this->item_.id)
                                              : this->service_.get_playlist_feed(this->item_);
    if (!feed.has_value()) {
        brls::Application::notify(
            this->service_.error_message().empty() ? newpipe::tr("detail/playlist_load_failed")
                                                   : this->service_.error_message());
        return;
    }

    std::string title = feed->kiosk.title;
    if (title.empty() || title == "Playlist") {
        title = newpipe::tr("detail/playlist_action");
    }

    brls::Application::pushActivity(new StreamFeedActivity(title, feed->items, feed->continuation));
}

void StreamDetailActivity::openComments() {
    const auto page = this->service_.get_comments(this->item_);
    if (!page.has_value()) {
        brls::Application::notify(
            this->service_.error_message().empty() ? newpipe::tr("detail/comments_load_failed")
                                                   : this->service_.error_message());
        return;
    }

    std::string title = page->title;
    if (title.empty() || title == "Comments") {
        title = newpipe::tr("comments/title");
    }

    brls::Application::pushActivity(new CommentFeedActivity(title, page->items, page->continuation));
}

void StreamDetailActivity::toggleFavorite() {
    bool favorite = false;
    std::string error;
    if (!newpipe::LibraryStore::instance().toggle_favorite(this->item_, &favorite, &error)) {
        brls::Application::notify(error.empty() ? newpipe::tr("detail/favorite_save_failed") : error);
        return;
    }

    this->updateFavoriteAction();
    brls::Application::notify(
        favorite ? newpipe::tr("detail/favorite_added")
                 : newpipe::tr("detail/favorite_removed"));
}
