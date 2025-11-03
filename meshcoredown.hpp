#pragma once

#include <cstdint>
#include "config.hpp"
#include "discord.hpp"

class MeshCoreDown {
   public:
    MeshCoreDown();
    ~MeshCoreDown() {};

    void loop();
    void checkNew();

   private:
    uint8_t cnt = 0;
    uint64_t last_check_time = 0;
    DiscordBot discordBot{DISCORD_MESHCORE};
    std::string lastmsg = "";
};