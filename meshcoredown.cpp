#include "meshcoredown.hpp"
#include "parson.h"
#include <iostream>

MeshCoreDown::MeshCoreDown() {
    cnt = 0;
    last_check_time = static_cast<uint64_t>(time(nullptr));
}

void MeshCoreDown::loop() {
    cnt++;
    if (cnt % 10 == 0) {
        checkNew();
    }
}

void MeshCoreDown::checkNew() {
    uint64_t current_time = static_cast<uint64_t>(time(nullptr));
    std::string url = MCMAPURL "/api/v1/all/?after_ms=" + std::to_string(last_check_time * 1000);
    last_check_time = current_time;
    std::string response;

    CURL* curl = curl_easy_init();
    if (!curl) return;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "meshlogger/1.0");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, +[](char* ptr, size_t size, size_t nmemb, void* userdata) -> size_t {
            auto* out = static_cast<std::string*>(userdata);
            out->append(ptr, size * nmemb);
            return size * nmemb; });
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode rc = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);
    if (rc != CURLE_OK) {
        // This is the real error!
        fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(rc));
    }
    if (rc != CURLE_OK || http_code < 200 || http_code >= 300) return;

    try {
        JSON_Value* root_value = json_parse_string((const char*)response.c_str());
        if (json_value_get_type(root_value) != JSONObject) {
            json_value_free(root_value);
            return;
        }
        JSON_Object* root_object = json_value_get_object(root_value);
        if (!root_object) {
            json_value_free(root_value);
            return;
        }
        // get channel_messages object
        JSON_Object* channel_messages = json_object_get_object(root_object, "channel_messages");
        if (!channel_messages) {
            json_value_free(root_value);
            return;
        }
        // get objects array
        JSON_Array* messages_array = json_object_get_array(channel_messages, "objects");
        if (!messages_array) {
            json_value_free(root_value);
            return;
        }
        std::string lastmsg = "";
        size_t message_count = json_array_get_count(messages_array);
        for (size_t i = 0; i < message_count; i++) {
            JSON_Object* message_object = json_array_get_object(messages_array, i);
            if (!message_object) continue;
            const char* message = json_object_get_string(message_object, "message");
            if (!message) continue;
            const char* node_name = json_object_get_string(message_object, "name");
            if (!node_name) node_name = "unknown";
            int channel_id = static_cast<int>(json_object_get_number(message_object, "channel_id"));
            if (channel_id == 3) {
                std::string discord_msg = std::string(node_name) + ": " + std::string(message);
                if (discord_msg != lastmsg) {
                    lastmsg = discord_msg;
                    discordBot.queueMessage(discord_msg);
                    discordBot.loop();
                }
            };
        }
        json_value_free(root_value);
    } catch (const std::exception& e) {
        // parsing failed
    }
}