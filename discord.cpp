#include "discord.hpp"
#include <iostream>

std::string DiscordBot::escape_json(const std::string& s) {
    std::string escaped;
    // Reserve space to avoid frequent reallocations.
    // This is a heuristic; we know the new string will be
    // at least as long as the original.
    escaped.reserve(s.length());

    // A lookup table is faster than conditional logic for hex conversion
    static const char* hex_chars = "0123456789abcdef";

    for (unsigned char c : s) {
        switch (c) {
            case '"':
                escaped.append("\\\"");
                break;
            case '\\':
                escaped.append("\\\\");
                break;
            case '\b':
                escaped.append("\\b");
                break;
            case '\f':
                escaped.append("\\f");
                break;
            case '\n':
                escaped.append("\\n");
                break;
            case '\r':
                escaped.append("\\r");
                break;
            case '\t':
                escaped.append("\\t");
                break;
            default:
                if (c >= 0x00 && c <= 0x1F) {
                    // This is a control character. Escape as \u00XX
                    escaped.append("\\u00");
                    escaped.push_back(hex_chars[(c >> 4) & 0x0F]);
                    escaped.push_back(hex_chars[c & 0x0F]);
                } else {
                    // This is a standard printable character.
                    escaped.push_back(c);
                }
        }
    }
    return escaped;
}

void DiscordBot::sendQueuedMessage() {
    std::lock_guard<std::mutex> lock(mtx);
    if (messageQueue.empty()) {
        return;
    }

    std::string message = escape_json(messageQueue.front());
    messageQueue.pop();

    std::string json_payload = R"({"content": ")" + message + R"("})";

    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "Error initializing curl!" << std::endl;
        return;
    }
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, webhook_url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_payload.c_str());
    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        // If the request failed, print the error
        std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
    } else {
        // Get the HTTP response code from Discord
        long response_code;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
        // Discord returns 204 No Content on success
        if (response_code == 204) {
            std::cout << "Webhook sent successfully!" << std::endl;
        } else {
            std::cout << "Discord returned HTTP code: " << response_code << std::endl;
        }
    }
    curl_slist_free_all(headers);  // Free the header list
    curl_easy_cleanup(curl);       // Free the easy handle
}

void DiscordBot::loop() {
    sendQueuedMessage();
}

void DiscordBot::queueMessage(const std::string& message) {
    std::lock_guard<std::mutex> lock(mtx);
    if (messageQueue.size() >= 100) return;
    messageQueue.push(message);
}