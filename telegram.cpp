#include "telegram.hpp"

void TelegramPoster::sendQueuedMessage() {
    std::lock_guard<std::mutex> lock(mtx);
    if (!messageQueue.empty()) {
        std::string message = messageQueue.front();
        messageQueue.pop();
        // Send the message using libcurl
    }
}

void TelegramPoster::loop() {
    sendQueuedMessage();
}

void TelegramPoster::queueMessage(const std::string& message) {
    std::lock_guard<std::mutex> lock(mtx);
    messageQueue.push(message);
}