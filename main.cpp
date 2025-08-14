
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

std::atomic<bool> running(true);

MeshMqttClient localClient;
NodeDb nodeDb("nodes.db");

void m_on_message(MC_Header& header, MC_TextMessage& message) {
    printf("Message from node 0x%08" PRIx32 ": %s\n", header.srcnode, message.text.c_str());
}

void m_on_position_message(MC_Header& header, MC_Position& position, bool needReply) {
    printf("Position from node 0x%08" PRIx32 ": Lat: %d, Lon: %d, Alt: %d, Speed: %d\n", header.srcnode, position.latitude_i, position.longitude_i, position.altitude, position.ground_speed);
    nodeDb.setNodePosition(header.srcnode, position.latitude_i, position.longitude_i, position.altitude);
}

void m_on_node_info(MC_Header& header, MC_NodeInfo& nodeinfo, bool needReply) {
    printf("Node Info from node 0x%08" PRIx32 ": ID: %s, Short Name: %s, Long Name: %s\n", header.srcnode, nodeinfo.id, nodeinfo.short_name, nodeinfo.long_name);
    nodeDb.setNodeInfo(header.srcnode, nodeinfo.short_name, nodeinfo.long_name);
}

void m_on_waypoint_message(MC_Header& header, MC_Waypoint& waypoint) {
    printf("Waypoint from node 0x%08" PRIx32 ": Lat: %d, Lon: %d, Name: %s\n", header.srcnode, waypoint.latitude_i, waypoint.longitude_i, waypoint.name);
}

void m_on_telemetry_device(MC_Header& header, MC_Telemetry_Device& telemetry) {
    printf("Telemetry Device from node 0x%08" PRIx32 ": Battery: %d, Uptime: %d, Voltage: %d, Channel Utilization: %d\n", header.srcnode, telemetry.battery_level, telemetry.uptime_seconds, telemetry.voltage, telemetry.channel_utilization);
}
void m_on_telemetry_environment(MC_Header& header, MC_Telemetry_Environment& telemetry) {
    printf("Telemetry Environment from node 0x%08" PRIx32 ": Temperature: %d, Humidity: %d, Pressure: %d, Lux: %d\n", header.srcnode, telemetry.temperature, telemetry.humidity, telemetry.pressure, telemetry.lux);
}

void m_on_traceroute(MC_Header& header, MC_RouteDiscovery& route, bool for_me, bool is_reply, bool need_reply) {
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

    localClient.setOnMessage(m_on_message);
    localClient.setOnPositionMessage(m_on_position_message);
    localClient.setOnWaypointMessage(m_on_waypoint_message);
    localClient.setOnNodeInfoMessage(m_on_node_info);
    localClient.setOnTelemetryDevice(m_on_telemetry_device);
    localClient.setOnTelemetryEnvironment(m_on_telemetry_environment);
    localClient.setOnTraceroute(m_on_traceroute);

    localClient.init();
    while (running) {
        localClient.loop();
        sleep(1);
    }

    return 0;
}