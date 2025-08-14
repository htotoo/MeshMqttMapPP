
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
std::atomic<bool> running(true);

MeshMqttClient localClient;
MeshMqttClient mainClient;
MeshMqttClient liamClient;
NodeDb nodeDb("nodes.db");

class MessageIdTracker {
   public:
    bool check(uint32_t msgid) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (msgids_.find(msgid) != msgids_.end()) {
            return true;
        }
        if (msgids_.size() >= 50) {
            // Remove the oldest inserted msgid
            msgids_order_.pop_front();
            msgids_.erase(msgids_order_.front());
        }
        msgids_order_.push_back(msgid);
        msgids_.insert(msgid);
        return false;
    }

   private:
    std::unordered_set<uint32_t> msgids_;
    std::deque<uint32_t> msgids_order_;
    std::mutex mutex_;
};

MessageIdTracker messageIdTracker;
void m_on_message(MC_Header& header, MC_TextMessage& message) {
    if (messageIdTracker.check(header.packet_id)) {
        return;
    }
    printf("Message from node 0x%08" PRIx32 ": %s\n", header.srcnode, message.text.c_str());
    nodeDb.saveChatMessage(header.srcnode, message.text);
}

void m_on_position_message(MC_Header& header, MC_Position& position, bool needReply) {
    if (messageIdTracker.check(header.packet_id)) {
        return;
    }
    printf("Position from node 0x%08" PRIx32 ": Lat: %d, Lon: %d, Alt: %d, Speed: %d\n", header.srcnode, position.latitude_i, position.longitude_i, position.altitude, position.ground_speed);
    nodeDb.setNodePosition(header.srcnode, position.latitude_i, position.longitude_i, position.altitude);
}

void m_on_node_info(MC_Header& header, MC_NodeInfo& nodeinfo, bool needReply) {
    if (messageIdTracker.check(header.packet_id)) {
        return;
    }
    printf("Node Info from node 0x%08" PRIx32 ": ID: %s, Short Name: %s, Long Name: %s\n", header.srcnode, nodeinfo.id, nodeinfo.short_name, nodeinfo.long_name);
    nodeDb.setNodeInfo(header.srcnode, nodeinfo.short_name, nodeinfo.long_name);
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

    mainClient.set_address("tcp://mqtt.meshtastic.org:1883");
    liamClient.set_address("tcp://mqtt.meshtastic.liamcottle.net:1883");
    liamClient.set_user_pass("uplink", "uplink");

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
    liamClient.setOnMessage(m_on_message);
    liamClient.setOnPositionMessage(m_on_position_message);
    liamClient.setOnWaypointMessage(m_on_waypoint_message);
    liamClient.setOnNodeInfoMessage(m_on_node_info);
    liamClient.setOnTelemetryDevice(m_on_telemetry_device);
    liamClient.setOnTelemetryEnvironment(m_on_telemetry_environment);
    liamClient.setOnTraceroute(m_on_traceroute);

    mainClient.init();
    localClient.init();
    liamClient.init();
    while (running) {
        localClient.loop();
        mainClient.loop();
        liamClient.loop();
        sleep(1);
    }

    return 0;
}