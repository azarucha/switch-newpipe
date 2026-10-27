#include "view/stream_card.hpp"

#include <cmath>

#include "newpipe/image_loader.hpp"
#include "newpipe/i18n.hpp"

namespace {

// Cards have no line limit; cut long titles so they stay at about two lines.
std::string shorten_title(const std::string& title, size_t max_chars = 64) {
    size_t chars = 0;
    for (size_t i = 0; i < title.size(); ++i) {
        if ((static_cast<unsigned char>(title[i]) & 0xC0) != 0x80 && ++chars > max_chars) {
            return title.substr(0, i) + "\xE2\x80\xA6";
        }
    }
    return title;
}

}  // namespace

StreamCard::StreamCard(float width) {
    this->inflateFromXMLRes("xml/views/stream_card.xml");
    const float inner = width - 12.0f;  // 6 px padding on each side
    const float thumb_height = std::round(inner * 9.0f / 16.0f);
    this->setWidth(width);
    if (thumbBox) {
        thumbBox->setSize(brls::Size(inner, thumb_height));
    }
    if (thumbnail) {
        thumbnail->setSize(brls::Size(inner, thumb_height));
    }
    for (brls::Label* label : {titleLabel.getView(), channelLabel.getView(), metaLabel.getView()}) {
        if (label) {
            label->setWidth(inner);
        }
    }
}

void StreamCard::setData(const newpipe::StreamItem& item) {
    if (titleLabel) {
        titleLabel->setText(shorten_title(item.title));
    }
    if (channelLabel) {
        channelLabel->setText(item.channel_name);
        channelLabel->setVisibility(item.channel_name.empty() ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
    }
    if (metaLabel) {
        std::string meta = item.is_playlist ? std::string() : item.view_count_text;
        if (!item.published_text.empty() && !item.is_live) {
            meta += meta.empty() ? item.published_text : " \xE2\x80\xA2 " + item.published_text;
        }
        metaLabel->setText(meta);
        metaLabel->setVisibility(meta.empty() ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
    }
    if (badge && badgeLabel) {
        const std::string text = item.is_live ? newpipe::tr("stream/live_badge")
            : item.is_playlist                 ? item.view_count_text
                                               : item.duration_text;
        badgeLabel->setText(text);
        badge->setBackgroundColor(item.is_live ? nvgRGB(204, 0, 0) : nvgRGBA(0, 0, 0, 204));
        badge->setVisibility(text.empty() ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
    }
    if (thumbnail && !item.thumbnail_url.empty()) {
        newpipe::ImageLoader::instance().load(item.thumbnail_url, thumbnail);
    }
}
