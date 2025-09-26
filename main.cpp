
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

#define USECONSOLE 1

#ifdef USECONSOLE
#include "CommandInterpreter.hpp"
#endif

MessageIdTracker messageIdTracker;
MessageIdTracker messageIdTrackerTelemetry;
NodeNameMap nodeNameMap;

std::atomic<bool> running(true);

MeshMqttClient localClient;
MeshMqttClient mainClient;
NodeDb nodeDb("nodes.db");
TelegramPoster telegramPoster;


#ifdef USECONSOLE
// --- Command Callback Functions ---
void cmd_help(const std::string& parameters) {
    safe_printf("Available commands:\n");
    safe_printf("  help                         - Shows this help message\n");
    safe_printf("  send <node_id_hex> <message> - Sends a text message\n");
    safe_printf("  send4 <node_id_hex> <message> - Sends a text message\n");
    safe_printf("  send8 <node_id_hex> <message> - Sends a text message\n");
    safe_printf("  nodeinfo                     - Send my nodeinfo\n");
    safe_printf("  exit                         - Exits the application\n");
}

void cmd_send_int(const std::string& parameters, int freq = 0) {
    size_t first_space = parameters.find(' ');
    if (first_space == std::string::npos || first_space == parameters.length() - 1) {
        safe_printf("Usage: send <node_id_hex> <message>\n");
        return;
    }

    std::string nodeId_str = parameters.substr(0, first_space);
    std::string message = parameters.substr(first_space + 1);

    try {
        uint32_t nodeId = std::stoul(nodeId_str, nullptr, 16);
        safe_printf("Sending message '%s' to node 0x%08x via local client...\n", message.c_str(), nodeId);
        // This is where you call your actual send function
        // localClient.sendTextMessage(message, nodeId);
        std::string rt = "msh/EU_868/HU";
        if (freq == 868 || freq == 0) localClient.sendMeshtasticMsg(nodeId, message, rt, 7);
        rt = "msh/EU_433/HU";
        if (freq == 433 || freq == 0) localClient.sendMeshtasticMsg(nodeId, message, rt, 7);
    } catch (const std::exception& e) {
        safe_printf("Invalid node ID. Please use hex format (e.g., aabbccdd).\n");
    }
}

void cmd_send(const std::string& parameters) {
    cmd_send_int(parameters, 0);
}

void cmd_send4(const std::string& parameters) {
    cmd_send_int(parameters, 433);
}

void cmd_send8(const std::string& parameters) {
    cmd_send_int(parameters, 868);
}

void cmd_sendlowhop(const std::string& parameters) {
    size_t first_space = parameters.find(' ');
    if (first_space == std::string::npos || first_space == parameters.length() - 1) {
        safe_printf("Usage: send <node_id_hex> <message>\n");
        return;
    }

    std::string nodeId_str = parameters.substr(0, first_space);
    std::string message = parameters.substr(first_space + 1);

    try {
        uint32_t nodeId = std::stoul(nodeId_str, nullptr, 16);
        safe_printf("Sending message '%s' to node 0x%08x via local client...\n", message.c_str(), nodeId);
        // This is where you call your actual send function
        // localClient.sendTextMessage(message, nodeId);
        std::string rt = "msh/EU_868/HU";
        localClient.sendMeshtasticMsg(nodeId, message, rt, 1);
        rt = "msh/EU_433/HU";
        localClient.sendMeshtasticMsg(nodeId, message, rt, 1);
    } catch (const std::exception& e) {
        safe_printf("Invalid node ID. Please use hex format (e.g., aabbccdd).\n");
    }
}

void cmd_nodeinfo(const std::string& parameters) {
    safe_printf("Sending node info...\n");
    // Here you would gather the node info and send it
    std::string shortname = "INFO";
    std::string longname = "Hungarian Info Node";
    std::string rootTopic = "msh/EU_868/HU";
    localClient.sendMeshtasticNodeinfo(0xabbababa, shortname, longname, rootTopic);
    rootTopic = "msh/EU_433/HU";
    localClient.sendMeshtasticNodeinfo(0xabbababa, shortname, longname, rootTopic);
    rootTopic = "msh/EU_868";
    mainClient.sendMeshtasticNodeinfo(0xabbababa, shortname, longname, rootTopic);
}

void cmd_exit(const std::string& parameters) {
    safe_printf("Exiting...\n");
    running = false;  // This will cause the main loop to terminate
}

#endif

void m_on_message(MC_Header& header, MC_TextMessage& message) {
    if (messageIdTracker.check(header.packet_id)) {
        return;
    }
    safe_printf("Message from node 0x%08" PRIx32 ": %s\n", header.srcnode, message.text.c_str());
    if (message.text.find("seq ", 0) == 0) {
        // return;
    }

    nodeDb.saveChatMessage(header.srcnode, message.chan, message.text, header.freq);

    std::string telegramMessage = std::to_string(message.chan) + "@" + std::to_string(header.freq) + "# " + nodeNameMap.getNodeName(header.srcnode) + ":  " + message.text;
    safe_printf("Telegram: %s\n", telegramMessage.c_str());
    telegramPoster.queueMessage(telegramMessage);
}

void m_on_position_message(MC_Header& header, MC_Position& position, bool needReply) {
    if (messageIdTrackerTelemetry.check(header.packet_id)) {
        return;
    }
    if (!position.has_latitude_i || !position.has_longitude_i) {
        return;
    }
    if (position.latitude_i == 0 && position.longitude_i == 0) {
        return;
    }
    safe_printf("Position from node %s: Lat: %d, Lon: %d, Alt: %d, Speed: %d\n", nodeNameMap.getNodeName(header.srcnode).c_str(), position.latitude_i, position.longitude_i, position.altitude, position.ground_speed);
    nodeDb.setNodePosition(header.srcnode, position.latitude_i, position.longitude_i, position.altitude);
}

void m_on_node_info(MC_Header& header, MC_NodeInfo& nodeinfo, bool needReply) {
    if (messageIdTrackerTelemetry.check(header.packet_id)) {
        return;
    }
    safe_printf("Node Info from node 0x%08" PRIx32 ": ID: %s, Short Name: %s, Long Name: %s\n", header.srcnode, nodeinfo.id, nodeinfo.short_name, nodeinfo.long_name);
    nodeDb.setNodeInfo(header.srcnode, nodeinfo.short_name, nodeinfo.long_name, header.freq, nodeinfo.role);
    nodeNameMap.setNodeName(header.srcnode, nodeinfo.short_name);
}

void m_on_waypoint_message(MC_Header& header, MC_Waypoint& waypoint) {
    if (messageIdTrackerTelemetry.check(header.packet_id)) {
        return;
    }
    safe_printf("Waypoint from node 0x%08" PRIx32 ": Lat: %d, Lon: %d, Name: %s\n", header.srcnode, waypoint.latitude_i, waypoint.longitude_i, waypoint.name);
}

void m_on_telemetry_device(MC_Header& header, MC_Telemetry_Device& telemetry) {
    if (messageIdTrackerTelemetry.check(header.packet_id)) {
        return;
    }
    nodeDb.setNodeTelemetryDevice(header.srcnode, telemetry.battery_level, telemetry.voltage, telemetry.has_uptime_seconds ? telemetry.uptime_seconds : 0);
    safe_printf("Telemetry Device from node 0x%08" PRIx32 ": Battery: %d, Uptime: %d, Voltage: %d, Channel Utilization: %d\n", header.srcnode, telemetry.battery_level, telemetry.uptime_seconds, telemetry.voltage, telemetry.channel_utilization);
}
void m_on_telemetry_environment(MC_Header& header, MC_Telemetry_Environment& telemetry) {
    if (messageIdTrackerTelemetry.check(header.packet_id)) {
        return;
    }
    safe_printf("Telemetry Environment from node 0x%08" PRIx32 ": Temperature: %d, Humidity: %d, Pressure: %d, Lux: %d\n", header.srcnode, telemetry.temperature, telemetry.humidity, telemetry.pressure, telemetry.lux);
    nodeDb.setNodeTemperature(header.srcnode, telemetry.temperature);
}

void m_on_traceroute(MC_Header& header, MC_RouteDiscovery& route, bool for_me, bool is_reply, bool need_reply) {
    // if (messageIdTracker.check(header.packet_id)) {
    //  return; //need to get the same multiple times, since the same message will get more and more data
    //}
    if (header.via_mqtt) {
        safe_printf("Skip bc mqtt\n");
        return;
    }
    // Print the route details if needed
    uint32_t n1 = (route.route_back_count > 0) ? header.dstnode : header.srcnode;
    safe_printf("Traceroute from node 0x%08" PRIx32 ": Route Count: %d\n", n1, route.route_count);
    for (int i = 0; i < route.route_count; i++) {
        safe_printf("Route[%d]: Node 0x%08" PRIx32 ", SNR: %d\n", i, route.route[i], route.snr_towards[i] / 4);
        safe_printf("0x%08" PRIx32 " -> 0x%08" PRIx32 "  : %d\n", n1, route.route[i], route.snr_towards[i] / 4);
        nodeDb.saveNodeSNR(n1, route.route[i], route.snr_towards[i] / 4);
        n1 = route.route[i];
    }
    if (route.route_back_count > 0) {
        nodeDb.saveNodeSNR(n1, header.srcnode, route.snr_towards[route.route_count + 1]);
    }
    n1 = (route.route_back_count > 0) ? header.srcnode : header.dstnode;
    for (int i = 0; i < route.route_back_count; i++) {
        safe_printf("Back[%d]: Node 0x%08" PRIx32 ", SNR: %d\n", i, route.route_back[i], route.snr_back[i] / 4);
        nodeDb.saveNodeSNR(n1, route.route_back[i], route.snr_back[i] / 4);
        safe_printf("0x%08" PRIx32 " -> 0x%08" PRIx32 "  : %d\n", n1, route.route_back[i], route.snr_back[i] / 4);
        n1 = route.route_back[i];
    }
}

void handle_signal(int signal) {
    if (signal == SIGINT) {
        safe_printf("\nCaught SIGINT, exiting...\n");
        running = false;
    }
}

int main(int argc, char* argv[]) {
    signal(SIGINT, handle_signal);

    #ifdef USECONSOLE
    CommandInterpreter interpreter;

    // Register the commands you want to support
    interpreter.subscribe("help", cmd_help);
    interpreter.subscribe("send", cmd_send);
    interpreter.subscribe("send4", cmd_send4);
    interpreter.subscribe("send8", cmd_send8);
    interpreter.subscribe("sendlowhop", cmd_sendlowhop);
    interpreter.subscribe("nodeinfo", cmd_nodeinfo);
    interpreter.subscribe("exit", cmd_exit);

    // Start listening for input in the background
    interpreter.start();
    #endif
    telegramPoster.setApiToken(TELEGRAM_TOKEN);
    telegramPoster.setChatId(TELEGRAM_CHAT_ID);
    safe_printf("Loading node names from database...\n");
    nodeDb.loadNodeNames(nodeNameMap);
    safe_printf("Connecting to MQTT servers...\n");

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
    uint32_t timer = 0;
    std::string globalad = "Hungarian mesh config: https://meshtastic.creativo.hu";
    while (running) {
        localClient.loop();
        mainClient.loop();
        try {
            telegramPoster.loop();
        } catch (const std::exception& e) {
            std::cerr << e.what() << '\n';
        }
        sleep(1);
        timer++;
        if (timer % 3600 == 0) {
            cmd_nodeinfo("");
        }
        if ((timer % (3600 * 4)) == 0) {
            std::string rt = "msh/EU_868";
            mainClient.sendMeshtasticMsg(0xabbababa, globalad, rt, 2);
        }
    }

    // Cleanly stop the command interpreter thread
    #ifdef USECONSOLE
    interpreter.stop();
    #endif
    return 0;
}