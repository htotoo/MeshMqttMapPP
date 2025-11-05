#pragma once

#include <string>
#include <mutex>
#include <queue>
#include <curl/curl.h>

class DiscordBot {
   public:
    DiscordBot(std::string webhook) : webhook_url(webhook) {}
    ~DiscordBot() {}
    void queueMessage(const std::string& message);
    void loop();

   private:
    std::string escape_json(const std::string& s);
    void sendQueuedMessage();
    std::string apiToken;
    std::string chatId;
    std::mutex mtx;
    std::queue<std::string> messageQueue;
    std::string webhook_url = "";
};
