#ifndef TELEGRAM_HPP
#define TELEGRAM_HPP

#include <string>
#include <curl/curl.h>
#include <mutex>
#include <queue>

class TelegramPoster {
   public:
    void queueMessage(const std::string& message);
    void setApiToken(const std::string& token) { apiToken = token; }
    void setChatId(const std::string& id) { chatId = id; }
    void loop();

   private:
    void sendQueuedMessage();
    std::string apiToken;
    std::string chatId;
    std::mutex mtx;
    std::queue<std::string> messageQueue;
};
#endif  // TELEGRAM_HPP