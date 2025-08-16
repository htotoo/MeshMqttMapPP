#ifndef MESHMQTTCLIENT_HPP
#define MESHMQTTCLIENT_HPP

#include <string>
#include <inttypes.h>
#include "mbedtls/aes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>  // sleep()
#include <atomic>    // std::atomic
#include <vector>
#include "MQTTClient.h"
#include "MeshasticCompactStructs.hpp"

#include "pb.h"
#include "pb_decode.h"
#include "pb_encode.h"
#include "unishox2.h"
#include "meshtastic/remote_hardware.pb.h"
#include "meshtastic/telemetry.pb.h"
#include "meshtastic/mqtt.pb.h"

#define CLIENTID "MeshLoggerClient"
#define QOS 1
#define TIMEOUT 10000L
#define RECONNECT_DELAY 5  // Várakozási idő másodpercben újracsatlakozás előtt

class MeshMqttClient {
   public:
    MeshMqttClient();
    ~MeshMqttClient();
    bool init();
    void loop();
    void set_address(const std::string& address) {
        this->address = address;
    }
    void set_user_pass(const std::string& user, const std::string& pass) {
        this->user = user;
        this->pass = pass;
    }
    using OnMessageCallback = void (*)(MC_Header& header, MC_TextMessage& message);
    using OnPositionMessageCallback = void (*)(MC_Header& header, MC_Position& position, bool needReply);
    using OnNodeInfoCallback = void (*)(MC_Header& header, MC_NodeInfo& nodeinfo, bool needReply);
    using OnWaypointMessageCallback = void (*)(MC_Header& header, MC_Waypoint& waypoint);
    using OnTelemetryDeviceCallback = void (*)(MC_Header& header, MC_Telemetry_Device& telemetry);
    using OnTelemetryEnvironmentCallback = void (*)(MC_Header& header, MC_Telemetry_Environment& telemetry);
    using OnTracerouteCallback = void (*)(MC_Header& header, MC_RouteDiscovery& route, bool for_me, bool is_reply, bool need_reply);
    using OnRaw = void (*)(const uint8_t* data, size_t len);
    void setOnWaypointMessage(OnWaypointMessageCallback cb) {
        onWaypointMessage = cb;
    }
    void setOnNodeInfoMessage(OnNodeInfoCallback cb) {
        onNodeInfo = cb;
    }
    void setOnPositionMessage(OnPositionMessageCallback cb) {
        onPositionMessage = cb;
    }
    void setOnMessage(OnMessageCallback cb) {
        onMessage = cb;
    }
    void setOnTelemetryDevice(OnTelemetryDeviceCallback cb) {
        onTelemetryDevice = cb;
    }
    void setOnTelemetryEnvironment(OnTelemetryEnvironmentCallback cb) {
        onTelemetryEnvironment = cb;
    }
    void setOnTraceroute(OnTracerouteCallback cb) {
        onTraceroute = cb;
    }
    void setOnRaw(OnRaw cb) {
        onRaw = cb;
    }

    void addTopic(std::string topic) { topicList.push_back(topic); }

   private:
    MQTTClient client;
    mbedtls_aes_context aes_ctx;

    static int messageArrived(void* context, char* topicName, int topicLen, MQTTClient_message* message);
    static void connectionLost(void* context, char* cause);
    bool aes_decrypt_meshtastic_payload(const uint8_t* key, uint16_t keySize, uint32_t packet_id, uint32_t from_node, const uint8_t* encrypted_in, uint8_t* decrypted_out, size_t len);
    bool pb_decode_from_bytes(const uint8_t* srcbuf, size_t srcbufsize, const pb_msgdesc_t* fields, void* dest_struct);
    int16_t try_decode_root_packet(const uint8_t* srcbuf, size_t srcbufsize, const pb_msgdesc_t* fields, void* dest_struct, size_t dest_struct_size, MC_Header& header);
    void intOnNodeInfo(MC_Header& header, MC_NodeInfo& nodeinfo, bool want_reply);
    void intOnMessage(MC_Header& header, MC_TextMessage& message);
    void intOnWaypointMessage(MC_Header& header, MC_Waypoint& waypoint);
    void intOnTelemetryDevice(MC_Header& header, MC_Telemetry_Device& telemetry);
    void intOnTelemetryEnvironment(MC_Header& header, MC_Telemetry_Environment& telemetry);
    void intOnTraceroute(MC_Header& header, MC_RouteDiscovery& route_discovery);
    void intOnPositionMessage(MC_Header& header, MC_Position& position, bool want_reply);

    int16_t ProcessPacket(uint8_t* data, int len, uint16_t freq);
    // Callback function pointers
    OnMessageCallback onMessage = nullptr;  // Function pointer for onMessage callback
    OnPositionMessageCallback onPositionMessage = nullptr;
    OnNodeInfoCallback onNodeInfo = nullptr;
    OnWaypointMessageCallback onWaypointMessage = nullptr;
    OnTelemetryDeviceCallback onTelemetryDevice = nullptr;
    OnTelemetryEnvironmentCallback onTelemetryEnvironment = nullptr;
    OnTracerouteCallback onTraceroute = nullptr;
    OnRaw onRaw = nullptr;

    const uint8_t default_l1_key[16] =
        {0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29, 0x07, 0x59,
         0xf0, 0xbc, 0xff, 0xab, 0xcf, 0x4e, 0x69, 0x01};  // default aes128 key for L1 encryption
    const uint8_t default_chan_key[32] = {
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};  // default channel key

    std::string address = "tcp://127.0.0.1:1883";
    std::string user = "meshdev";
    std::string pass = "large4cats";
    MQTTClient_connectOptions conn_opts;
    std::vector<std::string> topicList = {"msh/EU_433/HU/2/e/#", "msh/EU_868/HU/2/e/#"};
};

#endif  // MESHMQTTCLIENT_HPP