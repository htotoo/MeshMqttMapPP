# CMake minimum version requirement
cmake_minimum_required(VERSION 3.10)

# Define the project name and enable BOTH C and C++ languages
project(meshlogger C CXX)

# Set the C++ standard to C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Find Dependencies (Classic Method) ---

# Find Paho MQTT
find_path(PAHO_MQTT_INCLUDE_DIR NAMES MQTTClient.h)
find_library(PAHO_MQTT_LIBRARY NAMES paho-mqtt3c)
if(NOT PAHO_MQTT_INCLUDE_DIR OR NOT PAHO_MQTT_LIBRARY)
    message(FATAL_ERROR "Paho MQTT library or headers not found. Please ensure 'libpaho-mqtt-dev' is installed.")
endif()
# Find SQLite3
find_path(SQLITE3_INCLUDE_DIR NAMES sqlite3.h)
find_library(SQLITE3_LIBRARY NAMES sqlite3)

# Find the standard Threads library
find_package(Threads REQUIRED)

# Find MbedTLS components manually
find_path(MBEDTLS_INCLUDE_DIR NAMES mbedtls/ssl.h)
find_library(MBEDTLS_LIBRARY NAMES mbedtls)
find_library(MBEDX509_LIBRARY NAMES mbedx509)
find_library(MBEDCRYPTO_LIBRARY NAMES mbedcrypto)
if(NOT MBEDTLS_INCLUDE_DIR OR NOT MBEDTLS_LIBRARY OR NOT MBEDX509_LIBRARY OR NOT MBEDCRYPTO_LIBRARY)
    message(FATAL_ERROR "MbedTLS components not found. Please ensure 'libmbedtls-dev' is installed.")
endif()

# ADDED: Find libcurl
find_path(CURL_INCLUDE_DIR NAMES curl/curl.h)
find_library(CURL_LIBRARY NAMES curl)
if(NOT CURL_INCLUDE_DIR OR NOT CURL_LIBRARY)
    message(FATAL_ERROR "libcurl headers or library not found. Please ensure 'libcurl4-openssl-dev' or equivalent is installed.")
endif()


message(STATUS "Found Paho MQTT Library: ${PAHO_MQTT_LIBRARY}")
message(STATUS "Found Paho MQTT Include Dir: ${PAHO_MQTT_INCLUDE_DIR}")
message(STATUS "Found MbedTLS Include Dir: ${MBEDTLS_INCLUDE_DIR}")


# --- Gather Source Files ---

# Find all .cpp and .c files in the "meshtastic" subdirectory
file(GLOB MESHTASTIC_SOURCES
    "meshtastic/*.cpp"
    "meshtastic/*.c"
)

# Find all Nanopb C source files (pb_*.c) in the root directory
file(GLOB NANOPB_SOURCES "pb_*.c")

# --- Create the Executable ---

# Add the main executable and all discovered source files
add_executable(meshlogger
    main.cpp
    meshmqttclient.cpp
    nodedb.cpp
    main.cpp
    telegram.cpp
    unishox2.cpp
    ${MESHTASTIC_SOURCES}
    ${NANOPB_SOURCES}
)

# --- Link Libraries and Include Directories ---

target_include_directories(meshlogger PRIVATE
    # Add the project root directory
    "${CMAKE_CURRENT_SOURCE_DIR}"
    # NOTE: If pb.h is in a subfolder like 'nanopb', add it here:
    # "${CMAKE_CURRENT_SOURCE_DIR}/nanopb"
    "${PAHO_MQTT_INCLUDE_DIR}"
    "${MBEDTLS_INCLUDE_DIR}"
    "${SQLITE3_INCLUDE_DIR}"
    "${CURL_INCLUDE_DIR}" # ADDED
)

target_link_libraries(meshlogger PRIVATE
    "${PAHO_MQTT_LIBRARY}"
    Threads::Threads
    # Link the three MbedTLS libraries
    "${MBEDTLS_LIBRARY}"
    "${MBEDX509_LIBRARY}"
    "${MBEDCRYPTO_LIBRARY}"
    "${SQLITE3_LIBRARY}"
    "${CURL_LIBRARY}" # ADDED
)

# --- Optional: Install command ---
install(TARGETS meshlogger DESTINATION bin)

message(STATUS "CMake configuration for meshlogger is complete.")