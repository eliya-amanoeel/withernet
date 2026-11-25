idf_component_register(SRCS "main.c" "web.c")

# One build per dongle, picked with -DBOARD=<board>:
#   nibe               192.168.68.2
#   checkwatt          192.168.68.3
#   checkwatt-badwifi  rollback test only: the Checkwatt build with a wrong
#                      Wi-Fi password, so it rolls back after 2 minutes.
#                      Never leave it installed.
# Management IPs: .2-.9 is the block for network devices (see handoff.md).
# ADMIN_PASS_BOARD picks the dongle's own admin password from secrets.h.
if(BOARD STREQUAL "nibe")
    set(board_name "nibe")
    set(mgmt_ip "192.168.68.2")
    set(admin_pass ADMIN_PASS_NIBE)
    set(icon_color "2e9d4f") # favicon: green
elseif(BOARD STREQUAL "checkwatt" OR BOARD STREQUAL "checkwatt-badwifi")
    set(board_name "checkwatt")
    set(mgmt_ip "192.168.68.3")
    set(admin_pass ADMIN_PASS_CHECKWATT)
    set(icon_color "2f6fde") # favicon: blue
else()
    message(FATAL_ERROR "BOARD not set or unknown ('${BOARD}'): build with "
        "idf.py -B build/<board> -DBOARD=<nibe|checkwatt> build")
endif()

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    "MGMT_IP=\"${mgmt_ip}\""
    "BOARD_NAME=\"${board_name}\""
    "ICON_COLOR=\"${icon_color}\""
    "ADMIN_PASS_BOARD=${admin_pass}")
if(BOARD STREQUAL "checkwatt-badwifi")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE TEST_BAD_WIFI)
endif()
