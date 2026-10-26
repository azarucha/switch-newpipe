#pragma once

#include <string>
#include <vector>

#include <borealis.hpp>

#include "newpipe/models.hpp"
#include "newpipe/youtube_catalog_service.hpp"

class CommentFeedActivity : public brls::Activity {
public:
    CommentFeedActivity(
        std::string title,
        std::vector<newpipe::CommentItem> items,
        std::string continuation = {});

    CONTENT_FROM_XML_RES("activity/comment_feed.xml");

    void onContentAvailable() override;

private:
    void buildList();
    void loadMore();
    void updateSubtitle();

    std::string title_;
    std::vector<newpipe::CommentItem> items_;
    std::string continuation_;
    newpipe::YouTubeCatalogService service_;

    BRLS_BIND(brls::Label, titleLabel, "comments/title");
    BRLS_BIND(brls::Label, subtitleLabel, "comments/subtitle");
    BRLS_BIND(brls::Box, listBox, "comments/list");
};
