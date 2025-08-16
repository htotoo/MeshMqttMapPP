#include "telegram.hpp"
#include <iostream>
void TelegramPoster::sendQueuedMessage() {
    std::lock_guard<std::mutex> lock(mtx);
    if (messageQueue.empty()) {
        return;
    }

    std::string message = messageQueue.front();
    messageQueue.pop();

    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "Failed to initialize libcurl" << std::endl;
        return;
    }

    // Use a pointer for the escaped message that we will manage manually
    char* escaped_message_ptr = curl_easy_escape(curl, message.c_str(), message.length());
    if (!escaped_message_ptr) {
        std::cerr << "Failed to URL-encode message" << std::endl;
        curl_easy_cleanup(curl);
        return;
    }

    std::string post_fields = "chat_id=" + chatId + "&text=" + escaped_message_ptr;
    std::string url = "https://api.telegram.org/bot" + apiToken + "/sendMessage";

    // Set all libcurl options
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_fields.c_str());
    // Perform the request
    CURLcode res = curl_easy_perform(curl);

    // Check for errors and print response
    if (res != CURLE_OK) {
        fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
    } else {
        long response_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
        printf("HTTP response code: %ld\n", response_code);
    }

    // --- Cleanup at the very end ---
    curl_free(escaped_message_ptr);  // Free the memory from curl_easy_escape
    curl_easy_cleanup(curl);         // Clean up the curl handle
}

void TelegramPoster::loop() {
    sendQueuedMessage();
}

void TelegramPoster::queueMessage(const std::string& message) {
    std::lock_guard<std::mutex> lock(mtx);
    messageQueue.push(message);
}