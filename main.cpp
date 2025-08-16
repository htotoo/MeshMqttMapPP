
#include <signal.h>
#include <string>
#include <inttypes.h>
#include "mbedtls/aes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>  // sleep()
#include <atomic>    // std::atomic
#include <iostream>
#include <sqlite3.h>
#include "meshmqttclient.hpp"
#include "nodedb.hpp"
#include <unordered_set>
#include <mutex>
#include <deque>
#include "telegram.hpp"
#include "messageidtracker.hpp"
#include "nodenamemap.hpp"

#include "config.hpp"

MessageIdTracker messageIdTracker;
NodeNameMap nodeNameMap;

std::atomic<bool> running(true);

MeshMqttClient localClient;
MeshMqttClient mainClient;
NodeDb nodeDb("nodes.db");
TelegramPoster telegramPoster;

void m_on_message(MC_Header& header, MC_TextMessage& message) {
    if (messageIdTracker.check(header.packet_id)) {
        return;
    }
    printf("Message from node 0x%08" PRIx32 ": %s\n", header.srcnode, message.text.c_str());
    nodeDb.saveChatMessage(header.srcnode, message.chan, message.text);

    std::string telegramMessage = std::to_string(message.chan) + "# " + nodeNameMap.getNodeName(header.srcnode) + ":  " + message.text;
    printf("Telegram: %s\n", telegramMessage.c_str());
    telegramPoster.queueMessage(telegramMessage);
}

void m_on_position_message(MC_Header& header, MC_Position& position, bool needReply) {
    if (messageIdTracker.check(header.packet_id)) {
        return;
    }
    if (!position.has_latitude_i || !position.has_longitude_i) {
        return;
    }
    if (position.latitude_i == 0 && position.longitude_i == 0) {
        return;
    }
    printf("Position from node %s: Lat: %d, Lon: %d, Alt: %d, Speed: %d\n", nodeNameMap.getNodeName(header.srcnode).c_str(), position.latitude_i, position.longitude_i, position.altitude, position.ground_speed);
    nodeDb.setNodePosition(header.srcnode, position.latitude_i, position.longitude_i, position.altitude);
}

void m_on_node_info(MC_Header& header, MC_NodeInfo& nodeinfo, bool needReply) {
    if (messageIdTracker.check(header.packet_id)) {
        return;
    }
    printf("Node Info from node 0x%08" PRIx32 ": ID: %s, Short Name: %s, Long Name: %s\n", header.srcnode, nodeinfo.id, nodeinfo.short_name, nodeinfo.long_name);
    nodeDb.setNodeInfo(header.srcnode, nodeinfo.short_name, nodeinfo.long_name);
    nodeNameMap.setNodeName(header.srcnode, nodeinfo.short_name);
}

void m_on_waypoint_message(MC_Header& header, MC_Waypoint& waypoint) {
    if (messageIdTracker.check(header.packet_id)) {
        return;
    }
    printf("Waypoint from node 0x%08" PRIx32 ": Lat: %d, Lon: %d, Name: %s\n", header.srcnode, waypoint.latitude_i, waypoint.longitude_i, waypoint.name);
}

void m_on_telemetry_device(MC_Header& header, MC_Telemetry_Device& telemetry) {
    if (messageIdTracker.check(header.packet_id)) {
        return;
    }
    nodeDb.setNodeBattery(header.srcnode, telemetry.battery_level);
    printf("Telemetry Device from node 0x%08" PRIx32 ": Battery: %d, Uptime: %d, Voltage: %d, Channel Utilization: %d\n", header.srcnode, telemetry.battery_level, telemetry.uptime_seconds, telemetry.voltage, telemetry.channel_utilization);
}
void m_on_telemetry_environment(MC_Header& header, MC_Telemetry_Environment& telemetry) {
    if (messageIdTracker.check(header.packet_id)) {
        return;
    }
    printf("Telemetry Environment from node 0x%08" PRIx32 ": Temperature: %d, Humidity: %d, Pressure: %d, Lux: %d\n", header.srcnode, telemetry.temperature, telemetry.humidity, telemetry.pressure, telemetry.lux);
    nodeDb.setNodeTemperature(header.srcnode, telemetry.temperature);
}

void m_on_traceroute(MC_Header& header, MC_RouteDiscovery& route, bool for_me, bool is_reply, bool need_reply) {
    if (messageIdTracker.check(header.packet_id)) {
        return;
    }
    printf("Traceroute from node 0x%08" PRIx32 ": Route Count: %d\n", header.srcnode, route.route_count);
    // Print the route details if needed
    for (int i = 0; i < route.route_count; i++) {
        printf("Route[%d]: Node 0x%08" PRIx32 ", SNR: %d\n", i, route.route[i], route.snr_towards[i]);
    }
}

void handle_signal(int signal) {
    if (signal == SIGINT) {
        printf("\nCaught SIGINT, exiting...\n");
        running = false;
    }
}

int main(int argc, char* argv[]) {
    signal(SIGINT, handle_signal);
    telegramPoster.setApiToken(TELEGRAM_TOKEN);
    telegramPoster.setChatId(TELEGRAM_CHAT_ID);
    printf("Loading node names from database...\n");
    nodeDb.loadNodeNames(nodeNameMap);
    printf("Connecting to MQTT servers...\n");

    mainClient.set_address("tcp://mqtt.meshtastic.org:1883");
    localClient.setOnMessage(m_on_message);
    localClient.setOnPositionMessage(m_on_position_message);
    localClient.setOnWaypointMessage(m_on_waypoint_message);
    localClient.setOnNodeInfoMessage(m_on_node_info);
    localClient.setOnTelemetryDevice(m_on_telemetry_device);
    localClient.setOnTelemetryEnvironment(m_on_telemetry_environment);
    localClient.setOnTraceroute(m_on_traceroute);
    mainClient.setOnMessage(m_on_message);
    mainClient.setOnPositionMessage(m_on_position_message);
    mainClient.setOnWaypointMessage(m_on_waypoint_message);
    mainClient.setOnNodeInfoMessage(m_on_node_info);
    mainClient.setOnTelemetryDevice(m_on_telemetry_device);
    mainClient.setOnTelemetryEnvironment(m_on_telemetry_environment);
    mainClient.setOnTraceroute(m_on_traceroute);
    mainClient.init();
    localClient.init();
    while (running) {
        localClient.loop();
        mainClient.loop();
        try {
            telegramPoster.loop();
        } catch (const std::exception& e) {
            std::cerr << e.what() << '\n';
        }
        sleep(1);
    }

    return 0;
}