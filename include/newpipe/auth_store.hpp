#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "newpipe/http_client.hpp"

namespace newpipe {

struct AuthSession {
    std::string cookie_header;
    std::string sapisid;
    std::string source_label;
    std::string source_path;
    std::string display_name;
    std::string oauth_access_token;
    std::string oauth_refresh_token;
    long long oauth_expires_at = 0;

    bool has_cookie() const { return !cookie_header.empty() && !sapisid.empty(); }
    bool has_oauth() const { return !oauth_refresh_token.empty(); }
    bool authenticated() const { return has_cookie() || has_oauth(); }
};

// YouTube TV device-code login: show user_code, the user confirms it at
// verification_url on another device, then poll with device_code.
struct DeviceLoginCode {
    std::string device_code;
    std::string user_code;
    std::string verification_url;
    int interval_seconds = 5;
    int expires_in_seconds = 1800;
};

enum class DeviceLoginStatus { Pending, Success, Failed };

std::string default_auth_import_path();
std::string default_auth_session_path();

class AuthStore {
public:
    static AuthStore& instance();

    bool load(std::string* error_message = nullptr);
    bool reload(std::string* error_message = nullptr);

    AuthSession session() const;
    bool has_session() const;

    bool update_from_cookie_header(
        const std::string& cookie_header,
        const std::string& source_label,
        std::string* error_message = nullptr);
    bool import_from_file(
        const std::string& file_path = {},
        std::string* error_message = nullptr);
    bool clear(std::string* error_message = nullptr);

    bool has_oauth_session() const;
    std::optional<DeviceLoginCode> start_device_login(
        HttpClient& client,
        std::string* error_message = nullptr);
    DeviceLoginStatus poll_device_login(
        HttpClient& client,
        const std::string& device_code,
        std::string* error_message = nullptr);
    // Returns a valid bearer token, refreshing it when it is about to expire.
    std::optional<std::string> oauth_access_token(
        HttpClient& client,
        std::string* error_message = nullptr);

    std::vector<HttpHeader> build_youtube_headers(
        const std::string& origin,
        const std::string& referer,
        std::string* error_message = nullptr) const;

private:
    AuthStore() = default;

    bool persist_session(std::string* error_message);

    mutable std::mutex mutex_;
    bool loaded_ = false;
    AuthSession session_;
};

}  // namespace newpipe
