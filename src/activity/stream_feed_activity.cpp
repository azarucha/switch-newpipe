#include "activity/stream_feed_activity.hpp"

#include "activity/stream_detail_activity.hpp"
#include "newpipe/i18n.hpp"
#include "newpipe/library_store.hpp"
#include "newpipe/log.hpp"
#include "newpipe/playback_helper.hpp"
#include "newpipe/runtime.hpp"
#include "view/stream_card.hpp"

namespace {
constexpr size_t kGridColumns = 3;
}

StreamFeedActivity::StreamFeedActivity(
    std::string title,
    std::vector<newpipe::StreamItem> items,
    std::string continuation)
    : title_(std::move(title))
    , items_(std::move(items))
    , continuation_(std::move(continuation))
    , service_() {
}

void StreamFeedActivity::onContentAvailable() {
    brls::delay(500, [this]() { interactionReady_.store(true); });
    this->registerAction(newpipe::tr("hints/back"), brls::BUTTON_B, [](brls::View*) {
        brls::Application::popActivity();
        return true;
    });
    this->registerAction(newpipe::tr("common/load_more"), brls::BUTTON_X, [this](brls::View*) {
        this->loadMore();
        return true;
    });

    if (this->statusLabel) {
        this->statusLabel->setText(this->title_);
    }
    this->updateSubtitle();
    this->buildGrid();
}

void StreamFeedActivity::updateSubtitle() {
    if (this->subtitleLabel) {
        this->subtitleLabel->setText(this->continuation_.empty()
                ? newpipe::tr("feed/subtitle")
                : newpipe::tr("feed/subtitle_more"));
    }
}

void StreamFeedActivity::loadMore() {
    if (this->continuation_.empty()) {
        brls::Application::notify(newpipe::tr("common/no_more"));
        return;
    }

    const auto feed = this->service_.get_playlist_continuation(this->continuation_);
    if (!feed.has_value()) {
        brls::Application::notify(this->service_.error_message().empty()
                ? newpipe::tr("detail/playlist_load_failed")
                : this->service_.error_message());
        return;
    }

    const size_t first_new = this->items_.size();
    this->items_.insert(this->items_.end(), feed->items.begin(), feed->items.end());
    this->continuation_ = feed->continuation;
    this->updateSubtitle();
    // Rebuilding deletes the focused card, which leaves borealis without focus;
    // hand it to the first newly loaded card.
    this->buildGrid();
    const auto& rows = this->gridBox->getChildren();
    const size_t row = first_new / kGridColumns;
    auto* row_box = row < rows.size() ? dynamic_cast<brls::Box*>(rows[row]) : nullptr;
    if (row_box) {
        const auto& cards = row_box->getChildren();
        const size_t column = first_new % kGridColumns;
        if (column < cards.size()) {
            brls::Application::giveFocus(cards[column]);
        }
    }
}

bool StreamFeedActivity::allowInitialInput() const {
    return interactionReady_.load();
}

void StreamFeedActivity::buildGrid() {
    if (!this->gridBox) {
        return;
    }

    this->gridBox->clearViews();
    for (size_t i = 0; i < this->items_.size(); i += kGridColumns) {
        auto* row = new brls::Box(brls::Axis::ROW);
        row->setMarginBottom(22);

        for (size_t j = i; j < i + kGridColumns && j < this->items_.size(); j++) {
            auto* card = new StreamCard();
            card->setData(this->items_[j]);
            const size_t idx = j;
            card->registerClickAction([this, idx](brls::View*) {
                this->playStream(this->items_[idx]);
                return true;
            });
            card->registerAction(newpipe::tr("common/info"), brls::ControllerButton::BUTTON_Y, [this, idx](brls::View*) {
                this->openStream(this->items_[idx]);
                return true;
            });
            card->addGestureRecognizer(new brls::TapGestureRecognizer(card));
            row->addView(card);
        }

        this->gridBox->addView(row);
    }
}

void StreamFeedActivity::playStream(const newpipe::StreamItem& item) {
    if (!allowInitialInput()) {
        return;
    }
    const auto detail = this->service_.get_stream_detail(item.url);
    const auto request = newpipe::build_playback_request(item, detail);
    if (!request.has_value()) {
        this->openStream(item);
        return;
    }

    const newpipe::StreamItem history_item = detail.has_value() ? detail->item : item;
    std::string ignored_error;
    newpipe::LibraryStore::instance().add_history(history_item, &ignored_error);
    newpipe::logf("feed_activity: queue playback url=%s", request->url.c_str());
    newpipe::queue_playback(*request);
    brls::Application::quit();
}

void StreamFeedActivity::openStream(const newpipe::StreamItem& item) {
    if (!allowInitialInput()) {
        return;
    }
    brls::Application::pushActivity(new StreamDetailActivity(item));
}
