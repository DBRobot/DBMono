cmake_minimum_required(VERSION 3.16)
project(DBcan C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(TRANSPORT_CORE_INC   ${CMAKE_CURRENT_SOURCE_DIR}/core/inc)
set(TRANSPORT_PORT_INC   ${CMAKE_CURRENT_SOURCE_DIR}/transports/inc)

# libsocketcan — vendored submodule, no CMakeLists.txt of its own
add_library(socketcan STATIC libs/libsocketcan/src/libsocketcan.c)
target_include_directories(socketcan PUBLIC libs/libsocketcan/include)
target_include_directories(socketcan PRIVATE libs/libsocketcan/src)
# Suppress the bundled can_netlink.h — use the system <linux/can/netlink.h> instead.
# The bundled copy has guard _CAN_NETLINK_H; the system copy uses _UAPI_CAN_NETLINK_H.
# Pre-defining the bundled guard prevents the duplicate struct definitions.
target_compile_definitions(socketcan PUBLIC _CAN_NETLINK_H)

# SocketCAN transport — Linux only
add_library(dbcan_socketcan STATIC
    transports/src/socketcan_transport.c
)
target_include_directories(dbcan_socketcan PUBLIC
    ${TRANSPORT_CORE_INC}
    ${TRANSPORT_PORT_INC}
)
target_compile_definitions(dbcan_socketcan PUBLIC TRANSPORT_PORT_SOCKETCAN)
target_link_libraries(dbcan_socketcan PUBLIC socketcan)
