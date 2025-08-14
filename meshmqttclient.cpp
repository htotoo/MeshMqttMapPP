#include "meshmqttclient.hpp"

MeshMqttClient::MeshMqttClient() {
    mbedtls_aes_init(&aes_ctx);
}

MeshMqttClient::~MeshMqttClient() {
    mbedtls_aes_free(&aes_ctx);
    MQTTClient_disconnect(client, TIMEOUT);
    MQTTClient_destroy(&client);
}

bool MeshMqttClient::init() {
    int rc;

    if ((rc = MQTTClient_create(&client, ADDRESS, CLIENTID,
                                MQTTCLIENT_PERSISTENCE_NONE, NULL)) != MQTTCLIENT_SUCCESS) {
        printf("Failed to connect. Reason: %d\n", rc);
        return false;
    }

    if ((rc = MQTTClient_setCallbacks(client, (void*)this, connectionLost, messageArrived, NULL) != MQTTCLIENT_SUCCESS)) {
        printf("Failed to set callbacks. Reason: %d\n", rc);
        return false;
    }

    conn_opts = MQTTClient_connectOptions_initializer;
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;
    conn_opts.username = "meshdev";
    conn_opts.password = "large4cats";
    return true;
}

void MeshMqttClient::loop() {
    int rc;
    if (!MQTTClient_isConnected(client)) {
        printf("Try to connect...\n");

        // Ha a csatlakozás nem sikerül, várunk és újrapróbáljuk
        if (MQTTClient_connect(client, &conn_opts) != MQTTCLIENT_SUCCESS) {
            fprintf(stderr, "Connection failed. Retry in %d seconds.\n", RECONNECT_DELAY);
            sleep(RECONNECT_DELAY);
            return;  // Vissza a ciklus elejére
        }

        printf("MQTT connect ok!\n");

        // Sikeres csatlakozás után újra fel kell iratkozni a témakörökre!
        printf("Subscribe to topics...\n");
        if ((rc = MQTTClient_subscribeMany(client, topicCount, topicList, qosList)) != MQTTCLIENT_SUCCESS) {
            fprintf(stderr, "Failed to resubscribe, error code: %d\n", rc);
            // Nem lépünk ki, a ciklus újrapróbálja a kapcsolatot bontani és újrakötni
            MQTTClient_disconnect(client, TIMEOUT);
        } else {
            printf("Successful resubscription.\n\n");
        }
    }
}

bool MeshMqttClient::aes_decrypt_meshtastic_payload(const uint8_t* key, uint16_t keySize, uint32_t packet_id, uint32_t from_node, const uint8_t* encrypted_in, uint8_t* decrypted_out, size_t len) {
    int ret = mbedtls_aes_setkey_enc(&aes_ctx, key, keySize);
    if (ret != 0) {
        printf("mbedtls_aes_setkey_enc failed with error: -0x%04x", -ret);
        mbedtls_aes_free(&aes_ctx);
        return false;
    }
    uint8_t nonce[16];
    uint8_t stream_block[16];
    size_t nc_off = 0;
    memset(nonce, 0, 16);
    memcpy(nonce, &packet_id, sizeof(uint32_t));
    memcpy(nonce + 8, &from_node, sizeof(uint32_t));
    ret = mbedtls_aes_crypt_ctr(&aes_ctx, len, &nc_off, nonce, stream_block, encrypted_in, decrypted_out);
    if (ret != 0) {
        printf("mbedtls_aes_crypt_ctr failed with error: -0x%04x", -ret);
        mbedtls_aes_free(&aes_ctx);
        return false;
    }
    return true;
}

void MeshMqttClient::connectionLost(void* context, char* cause) {
    printf("\n### Disconnected ###\nReason: %s\n", cause ? cause : "UNK");
}

bool MeshMqttClient::pb_decode_from_bytes(const uint8_t* srcbuf, size_t srcbufsize, const pb_msgdesc_t* fields, void* dest_struct) {
    pb_istream_t stream = pb_istream_from_buffer(srcbuf, srcbufsize);
    if (!pb_decode(&stream, fields, dest_struct)) {
        printf("Can't decode protobuf reason='%s', pb_msgdesc %p", PB_GET_ERROR(&stream), fields);
        return false;
    } else {
        return true;
    }
}

int16_t MeshMqttClient::try_decode_root_packet(const uint8_t* srcbuf, size_t srcbufsize, const pb_msgdesc_t* fields, void* dest_struct, size_t dest_struct_size, MC_Header& header) {
    uint8_t decrypted_data[srcbufsize] = {0};
    memset(dest_struct, 0, dest_struct_size);
    // 1st.
    if (aes_decrypt_meshtastic_payload(default_l1_key, sizeof(default_l1_key) * 8, header.packet_id, header.srcnode, srcbuf, decrypted_data, srcbufsize)) {
        if (pb_decode_from_bytes(decrypted_data, srcbufsize, fields, dest_struct)) return 254;
    }
    memset(dest_struct, 0, dest_struct_size);
    if (aes_decrypt_meshtastic_payload(default_chan_key, sizeof(default_chan_key) * 8, header.packet_id, header.srcnode, srcbuf, decrypted_data, srcbufsize)) {
        if (pb_decode_from_bytes(decrypted_data, srcbufsize, fields, dest_struct)) return 0;
    }

    if (header.chan_hash == 0 && header.dstnode != 0xffffffff) {
        // todo pki decrypt
        printf("can't decode priv packet");
        return -1;
    }
    // todo iterate chan keys

    printf("can't decode packet");
    return -1;
}

void MeshMqttClient::intOnMessage(MC_Header& header, MC_TextMessage& message) {
    if (onMessage) {
        onMessage(header, message);
    };
}

void MeshMqttClient::intOnNodeInfo(MC_Header& header, MC_NodeInfo& nodeinfo, bool want_reply) {
    if (onNodeInfo) {
        onNodeInfo(header, nodeinfo, false);
    };
}

void MeshMqttClient::intOnWaypointMessage(MC_Header& header, MC_Waypoint& waypoint) {
    if (onWaypointMessage) {
        onWaypointMessage(header, waypoint);
    };
}

void MeshMqttClient::intOnTelemetryDevice(MC_Header& header, MC_Telemetry_Device& telemetry) {
    if (onTelemetryDevice) {
        onTelemetryDevice(header, telemetry);
    };
}

void MeshMqttClient::intOnTelemetryEnvironment(MC_Header& header, MC_Telemetry_Environment& telemetry) {
    if (onTelemetryEnvironment) {
        onTelemetryEnvironment(header, telemetry);
    };
}

void MeshMqttClient::intOnTraceroute(MC_Header& header, MC_RouteDiscovery& route_discovery) {
    if (onTraceroute) {
        onTraceroute(header, route_discovery, false, header.request_id == 0, false);
    }
}

void MeshMqttClient::intOnPositionMessage(MC_Header& header, MC_Position& position, bool want_reply) {
    if (onPositionMessage) {
        onPositionMessage(header, position, false);
    };
}

int MeshMqttClient::messageArrived(void* context, char* topicName, int topicLen, MQTTClient_message* message) {
    /*unsigned char* payload = static_cast<unsigned char*>(message->payload);
    for (int i = 0; i < message->payloadlen; i++) {
        printf("%02X ", payload[i]);
    }*/
    MeshMqttClient* client = static_cast<MeshMqttClient*>(context);
    client->ProcessPacket(static_cast<uint8_t*>(message->payload), message->payloadlen);

    MQTTClient_freeMessage(&message);
    MQTTClient_free(topicName);
    return 1;
}

int16_t MeshMqttClient::ProcessPacket(uint8_t* data, int len) {
    if (len > 0) {
        MC_Header header;  // for compatibility reason
        meshtastic_ServiceEnvelope serviceEnv;
        meshtastic_MeshPacket packet;
        meshtastic_Data decodedtmp;
        if (!pb_decode_from_bytes(data, len, &meshtastic_ServiceEnvelope_msg, &serviceEnv)) {
            printf("Service env decode failed\r\n");
            return -1;  // decoding failed
        }
        header.srcnode = serviceEnv.packet->from;
        header.dstnode = serviceEnv.packet->to;
        header.packet_id = serviceEnv.packet->id;
        header.hop_limit = serviceEnv.packet->hop_limit;
        header.hop_start = serviceEnv.packet->hop_start;
        header.chan_hash = serviceEnv.packet->channel;
        header.want_ack = serviceEnv.packet->want_ack;
        header.via_mqtt = serviceEnv.packet->via_mqtt;

        decodedtmp = serviceEnv.packet->decoded;  // copy the decoded data
        int16_t ret = 0;
        if (serviceEnv.packet->which_payload_variant == meshtastic_MeshPacket_encrypted_tag) {
            // printf("Encrypted packet received, size: %d\r\n", serviceEnv.packet->encrypted.size);
            //  decrypt the packet
            if (try_decode_root_packet(serviceEnv.packet->encrypted.bytes, serviceEnv.packet->encrypted.size, &meshtastic_Data_msg, &decodedtmp, sizeof(decodedtmp), header)) {
                // printf("Decrypted packet ok, size: %d", serviceEnv.packet->encrypted.size);
            } else {
                printf("Decryption failed, size: %d\r\n", serviceEnv.packet->encrypted.size);
                ret = -1;  // decryption failed
            }
        } else {
            printf("Unencrypted packet received, size: %d\r\n", serviceEnv.packet->encrypted.size);
            ret = -2;  // niy
        }

        if (ret >= 0) {
            // extract the want_response from bitfield
            decodedtmp.want_response = false;  // packet.want_response;
            /*printf("PortNum: %d  PacketId: %lu  Src: %lu\r\n", decodedtmp.portnum, header.packet_id, header.srcnode);
            printf("Want ack: %d\r\n", header.want_ack ? 1 : 0);
            printf("Want Response: %d\r\n", decodedtmp.want_response);
            printf("Request ID: %" PRIu32 "\r\n", decodedtmp.request_id);
            printf("Reply ID: %" PRIu32 "\r\n", decodedtmp.reply_id);*/
            header.request_id = decodedtmp.request_id;
            header.reply_id = decodedtmp.reply_id;
            // Process the decoded data as needed https://github.com/meshtastic/protobufs/blob/master/meshtastic/portnums.proto
            if (decodedtmp.portnum == 0) {
                // printf("Received an unknown packet\r\n");
            } else if (decodedtmp.portnum == 1) {
                // printf("Received a message packet\r\n");
                //  payload: utf8 text
                MC_TextMessage msg = {std::string(reinterpret_cast<const char*>(decodedtmp.payload.bytes), decodedtmp.payload.size), (uint8_t)ret, MC_MESSAGE_TYPE_TEXT};
                intOnMessage(header, msg);
            } else if (decodedtmp.portnum == 2) {
                // printf("Received a remote hardware packet\r\n");
                //  payload: protobuf HardwareMessage - NOT INTERESTED IN YET
                /*meshtastic_HardwareMessage hardware_msg = {};
                if (pb_decode_from_bytes(decodedtmp.payload.bytes, decodedtmp.payload.size, &meshtastic_HardwareMessage_msg, &hardware_msg)) {
                    printf("Hardware Message Type: %d", hardware_msg.type);
                    printf("GPIO Mask: 0x%016llX", hardware_msg.gpio_mask);
                    printf("GPIO Value: 0x%016llX", hardware_msg.gpio_value);
                } else {
                    printf("Failed to decode HardwareMessage");
                }*/
            } else if (decodedtmp.portnum == 3) {
                // payload: protobuf Position
                meshtastic_Position position_msg = {};
                if (pb_decode_from_bytes(decodedtmp.payload.bytes, decodedtmp.payload.size, &meshtastic_Position_msg, &position_msg)) {
                    MC_Position position = {.latitude_i = position_msg.latitude_i, .longitude_i = position_msg.longitude_i, .altitude = position_msg.altitude, .ground_speed = position_msg.ground_speed, .sats_in_view = position_msg.sats_in_view, .location_source = (uint8_t)position_msg.location_source, .has_latitude_i = position_msg.has_latitude_i, .has_longitude_i = position_msg.has_longitude_i, .has_altitude = position_msg.has_altitude, .has_ground_speed = position_msg.has_ground_speed};
                    intOnPositionMessage(header, position, decodedtmp.want_response);
                } else {
                    // printf("Failed to decode Position\r\n");
                }
            } else if (decodedtmp.portnum == 4) {
                // payload: protobuf User
                meshtastic_User user_msg = {};
                if (pb_decode_from_bytes(decodedtmp.payload.bytes, decodedtmp.payload.size, &meshtastic_User_msg, &user_msg)) {
                    MC_NodeInfo node_info;
                    node_info.node_id = header.srcnode;  // srcnode is the node ID
                    memcpy(node_info.id, user_msg.id, sizeof(node_info.id));
                    memcpy(node_info.short_name, user_msg.short_name, sizeof(node_info.short_name));
                    memcpy(node_info.long_name, user_msg.long_name, sizeof(node_info.long_name));
                    memcpy(node_info.macaddr, user_msg.macaddr, sizeof(node_info.macaddr));
                    memcpy(node_info.public_key, user_msg.public_key.bytes, sizeof(node_info.public_key));
                    node_info.public_key_size = user_msg.public_key.size;
                    node_info.role = user_msg.role;
                    node_info.hw_model = user_msg.hw_model;
                    intOnNodeInfo(header, node_info, decodedtmp.want_response);
                } else {
                    // printf("Failed to decode User\r\n");
                }
            } else if (decodedtmp.portnum == 5) {
                printf("Received a routing packet\r\n");
                // payload: protobuf Routing
                meshtastic_Routing routing_msg = {};  // todo process it. this is just a debug. or simply drop it.
                if (pb_decode_from_bytes(decodedtmp.payload.bytes, decodedtmp.payload.size, &meshtastic_Routing_msg, &routing_msg)) {
                    // printf("Routing reply count: %d\r\n", routing_msg.route_reply.route_count);
                } else {
                    // printf("Failed to decode Routing\r\n");
                }
            } else if (decodedtmp.portnum == 6) {
                // printf("Received an admin packet\r\n");
                //  payload: protobuf AdminMessage
                //  drop it, not interested in admin messages
            } else if (decodedtmp.portnum == 7) {
                // printf("Received a compressed text message packet\r\n");
                //  payload: utf8 text with Unishox2 Compression
                char uncompressed_data[256] = {0};
                size_t uncompressed_size = unishox2_decompress((const char*)&decodedtmp.payload.bytes, decodedtmp.payload.size, uncompressed_data, sizeof(uncompressed_data), USX_PSET_DFLT);
                MC_TextMessage msg = {std::string(reinterpret_cast<const char*>(uncompressed_data), uncompressed_size), (uint8_t)ret, MC_MESSAGE_TYPE_TEXT};
                intOnMessage(header, msg);
            } else if (decodedtmp.portnum == 8) {
                // printf("Received a waypoint packet\r\n");
                //  payload: protobuf Waypoint
                meshtastic_Waypoint waypoint_msg = {};  // todo store and callbacke
                if (pb_decode_from_bytes(decodedtmp.payload.bytes, decodedtmp.payload.size, &meshtastic_Waypoint_msg, &waypoint_msg)) {
                    MC_Waypoint waypoint;
                    waypoint.latitude_i = waypoint_msg.latitude_i;
                    waypoint.longitude_i = waypoint_msg.longitude_i;
                    memcpy(waypoint.name, waypoint_msg.name, sizeof(waypoint.name));
                    memcpy(waypoint.description, waypoint_msg.description, sizeof(waypoint.description));
                    waypoint.icon = waypoint_msg.icon;
                    waypoint.expire = waypoint_msg.expire;
                    waypoint.id = waypoint_msg.id;
                    waypoint.has_latitude_i = waypoint_msg.has_latitude_i;
                    waypoint.has_longitude_i = waypoint_msg.has_longitude_i;
                    intOnWaypointMessage(header, waypoint);
                } else {
                    // printf("Failed to decode Waypoint\r\n");
                }
            } else if (decodedtmp.portnum == 10) {
                // printf("Received a detection sensor packet\r\n");
                //  payload: utf8 text
                MC_TextMessage msg = {std::string(reinterpret_cast<const char*>(decodedtmp.payload.bytes), decodedtmp.payload.size), (uint8_t)ret, MC_MESSAGE_TYPE_DETECTOR_SENSOR};
                intOnMessage(header, msg);
            } else if (decodedtmp.portnum == 11) {
                // printf("Received an alert packet\r\n");
                //  payload: utf8 text
                MC_TextMessage msg = {std::string(reinterpret_cast<const char*>(decodedtmp.payload.bytes), decodedtmp.payload.size), (uint8_t)ret, MC_MESSAGE_TYPE_ALERT};
                intOnMessage(header, msg);
            } else if (decodedtmp.portnum == 12) {
                // printf("Received a key verification packet\r\n");
                //  payload: protobuf KeyVerification
                meshtastic_KeyVerification key_verification_msg = {};  // todo drop?
                if (pb_decode_from_bytes(decodedtmp.payload.bytes, decodedtmp.payload.size, &meshtastic_KeyVerification_msg, &key_verification_msg)) {
                    ;
                } else {
                    // printf("Failed to decode KeyVerification\r\n");
                }
            } else if (decodedtmp.portnum == 32) {
                // printf("Received a reply packet");
                //  payload: ASCII Plaintext //TODO determine the in/out part and send reply if needed
                MC_TextMessage msg = {std::string(reinterpret_cast<const char*>(decodedtmp.payload.bytes), decodedtmp.payload.size), (uint8_t)ret, MC_MESSAGE_TYPE_PING};
                intOnMessage(header, msg);
            } else if (decodedtmp.portnum == 34) {
                // printf("Received a paxcounter packet\r\n");
                // payload: protobuf DROP
            } else if (decodedtmp.portnum == 64) {
                // printf("Received a serial packet\r\n");
                // payload: uart rx/tx data
                MC_TextMessage msg = {std::string(reinterpret_cast<const char*>(decodedtmp.payload.bytes), decodedtmp.payload.size), (uint8_t)ret, MC_MESSAGE_TYPE_UART};
                intOnMessage(header, msg);
            } else if (decodedtmp.portnum == 65) {
                // printf("Received a STORE_FORWARD_APP  packet\r\n");
                //  payload: ?
            } else if (decodedtmp.portnum == 66) {
                // printf("Received a RANGE_TEST_APP  packet\r\n");
                //  payload: ascii text
                MC_TextMessage msg = {std::string(reinterpret_cast<const char*>(decodedtmp.payload.bytes), decodedtmp.payload.size), (uint8_t)ret, MC_MESSAGE_TYPE_RANGE_TEST};
                intOnMessage(header, msg);
            } else if (decodedtmp.portnum == 67) {
                // printf("Received a TELEMETRY_APP   packet\r\n");
                //  payload: Protobuf
                meshtastic_Telemetry telemetry_msg = {};  // todo store and callback
                if (pb_decode_from_bytes(decodedtmp.payload.bytes, decodedtmp.payload.size, &meshtastic_Telemetry_msg, &telemetry_msg)) {
                    // printf("Telemetry Time: %lu", telemetry_msg.time);
                    switch (telemetry_msg.which_variant) {
                        case meshtastic_Telemetry_device_metrics_tag:
                            MC_Telemetry_Device device_metrics;
                            device_metrics.battery_level = telemetry_msg.variant.device_metrics.battery_level;
                            device_metrics.uptime_seconds = telemetry_msg.variant.device_metrics.uptime_seconds;
                            device_metrics.voltage = telemetry_msg.variant.device_metrics.voltage;
                            device_metrics.channel_utilization = telemetry_msg.variant.device_metrics.channel_utilization;
                            device_metrics.has_battery_level = telemetry_msg.variant.device_metrics.has_battery_level;
                            device_metrics.has_uptime_seconds = telemetry_msg.variant.device_metrics.has_uptime_seconds;
                            device_metrics.has_voltage = telemetry_msg.variant.device_metrics.has_voltage;
                            device_metrics.has_channel_utilization = telemetry_msg.variant.device_metrics.has_channel_utilization;
                            intOnTelemetryDevice(header, device_metrics);
                            break;
                        case meshtastic_Telemetry_environment_metrics_tag:
                            MC_Telemetry_Environment environment_metrics;
                            environment_metrics.temperature = telemetry_msg.variant.environment_metrics.temperature;
                            environment_metrics.humidity = telemetry_msg.variant.environment_metrics.relative_humidity;
                            environment_metrics.pressure = telemetry_msg.variant.environment_metrics.barometric_pressure;
                            environment_metrics.lux = telemetry_msg.variant.environment_metrics.lux;
                            environment_metrics.has_temperature = telemetry_msg.variant.environment_metrics.has_temperature;
                            environment_metrics.has_humidity = telemetry_msg.variant.environment_metrics.has_relative_humidity;
                            environment_metrics.has_pressure = telemetry_msg.variant.environment_metrics.has_barometric_pressure;
                            environment_metrics.has_lux = telemetry_msg.variant.environment_metrics.has_lux;
                            intOnTelemetryEnvironment(header, environment_metrics);
                            break;
                        case meshtastic_Telemetry_air_quality_metrics_tag:
                            // printf("Air Quality Metrics: PM2.5: %lu ", telemetry_msg.variant.air_quality_metrics.pm25_standard);
                            // skipping, not interesting yet PR-s are welcome
                            break;
                        case meshtastic_Telemetry_power_metrics_tag:
                            // skipping, not interesting yet PR-s are welcome
                            break;
                        case meshtastic_Telemetry_local_stats_tag:
                            // skipping, not interesting yet PR-s are welcome
                            break;
                        case meshtastic_Telemetry_health_metrics_tag:
                            // printf("Health Metrics: Hearth BPM: %u, Temp: %f  So2: %u", telemetry_msg.variant.health_metrics.heart_bpm, telemetry_msg.variant.health_metrics.temperature, telemetry_msg.variant.health_metrics.spO2);
                            //  skipping, not interesting yet PR-s are welcome
                            break;
                        case meshtastic_Telemetry_host_metrics_tag:
                            // skipping, not interesting yet PR-s are welcome
                            break;
                    };
                } else {
                    // printf("Failed to decode Telemetry");
                }
            } else if (decodedtmp.portnum == 70) {
                // printf("Received a TRACEROUTE_APP    packet");
                //  payload: Protobuf RouteDiscovery
                meshtastic_RouteDiscovery route_discovery_msg = {};  // drop
                if (pb_decode_from_bytes(decodedtmp.payload.bytes, decodedtmp.payload.size, &meshtastic_RouteDiscovery_msg, &route_discovery_msg)) {
                    // printf("Route Discovery: Hop Count: %d", route_discovery_msg.route_count);
                    //  header.request_id ==0 --route back
                    MC_RouteDiscovery route_discovery;
                    route_discovery.route_count = route_discovery_msg.route_count;
                    route_discovery.snr_towards_count = route_discovery_msg.snr_towards_count;
                    route_discovery.route_back_count = route_discovery_msg.route_back_count;
                    route_discovery.snr_back_count = route_discovery_msg.snr_back_count;
                    memcpy(route_discovery.route, route_discovery_msg.route, sizeof(route_discovery.route));
                    memcpy(route_discovery.snr_towards, route_discovery_msg.snr_towards, sizeof(route_discovery.snr_towards));
                    memcpy(route_discovery.route_back, route_discovery_msg.route_back, sizeof(route_discovery.route_back));
                    memcpy(route_discovery.snr_back, route_discovery_msg.snr_back, sizeof(route_discovery.snr_back));
                    intOnTraceroute(header, route_discovery);
                } else {
                    // printf("Failed to decode RouteDiscovery");
                }
            } else if (decodedtmp.portnum == 71) {
                // printf("Received a NEIGHBORINFO_APP   packet");
                // payload: Protobuf ?
            } else {
                // printf("Received an unhandled portnum: %d", decodedtmp.portnum);
            }
            // if (header.want_ack && is_send_enabled && !is_in_stealth_mode && header.dstnode == my_nodeinfo.node_id) {
            //  send_ack(header);
            //}
        }
        // printf("\r\n");
        return ret;
    }
    return false;
}
