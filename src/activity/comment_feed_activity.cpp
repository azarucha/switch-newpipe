#include "activity/comment_feed_activity.hpp"

#include "newpipe/i18n.hpp"

CommentFeedActivity::CommentFeedActivity(
    std::string title,
    std::vector<newpipe::CommentItem> items,
    std::string continuation)
    : title_(std::move(title))
    , items_(std::move(items))
    , continuation_(std::move(continuation)) {
}

void CommentFeedActivity::onContentAvailable() {
    this->registerAction(newpipe::tr("hints/back"), brls::BUTTON_B, [](brls::View*) {
        brls::Application::popActivity();
        return true;
    });
    this->registerAction(newpipe::tr("common/load_more"), brls::BUTTON_X, [this](brls::View*) {
        this->loadMore();
        return true;
    });

    if (this->titleLabel) {
        this->titleLabel->setText(this->title_.empty() ? newpipe::tr("comments/title") : this->title_);
    }
    this->updateSubtitle();
    this->buildList();
}

void CommentFeedActivity::updateSubtitle() {
    if (this->subtitleLabel) {
        this->subtitleLabel->setText(this->continuation_.empty()
                ? newpipe::tr("comments/subtitle")
                : newpipe::tr("comments/subtitle_more"));
    }
}

void CommentFeedActivity::loadMore() {
    if (this->continuation_.empty()) {
        brls::Application::notify(newpipe::tr("common/no_more"));
        return;
    }

    const auto page = this->service_.get_comments_continuation(this->continuation_);
    if (!page.has_value()) {
        brls::Application::notify(this->service_.error_message().empty()
                ? newpipe::tr("detail/comments_load_failed")
                : this->service_.error_message());
        return;
    }

    this->items_.insert(this->items_.end(), page->items.begin(), page->items.end());
    this->continuation_ = page->continuation;
    this->updateSubtitle();
    this->buildList();
}

void CommentFeedActivity::buildList() {
    if (!this->listBox) {
        return;
    }

    this->listBox->clearViews();
    for (const auto& item : this->items_) {
        auto* block = new brls::Box(brls::Axis::COLUMN);
        block->setMarginBottom(18);

        auto* author = new brls::Label();
        author->setFontSize(18);
        author->setTextColor(nvgRGB(240, 240, 240));
        std::string author_text = item.author_name;
        if (item.is_verified) {
            author_text += " · " + newpipe::tr("comments/verified");
        }
        if (!item.published_text.empty()) {
            if (!author_text.empty()) {
                author_text += " · ";
            }
            author_text += item.published_text;
        }
        author->setText(author_text.empty() ? newpipe::tr("comments/unknown_author") : author_text);
        block->addView(author);

        auto* body = new brls::Label();
        body->setFontSize(17);
        body->setLineHeight(26);
        body->setTextColor(nvgRGB(220, 220, 220));
        body->setText(item.body);
        block->addView(body);

        std::string meta_text;
        if (!item.like_count_text.empty()) {
            meta_text += newpipe::tr("comments/likes", item.like_count_text);
        }
        if (!item.reply_count_text.empty()) {
            if (!meta_text.empty()) {
                meta_text += " · ";
            }
            meta_text += newpipe::tr("comments/replies", item.reply_count_text);
        }
        if (!meta_text.empty()) {
            auto* meta = new brls::Label();
            meta->setFontSize(14);
            meta->setTextColor(nvgRGB(170, 170, 170));
            meta->setText(meta_text);
            block->addView(meta);
        }

        this->listBox->addView(block);
    }
}
