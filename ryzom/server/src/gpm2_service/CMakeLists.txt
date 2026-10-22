FILE(GLOB SRC *.cpp *.h)

# Part of the gpm sheets library (Ryzom::gpmsheets), compiled once and shared
# with sheets_packer_shard.
LIST(REMOVE_ITEM SRC
        ${CMAKE_CURRENT_SOURCE_DIR}/sheets.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/sheets.h)

ADD_EXECUTABLE(ryzom_gpm2_service WIN32 ${SRC})

# ${POLYVOX_INCLUDE_DIR} is propagated PUBLIC by Ryzom::gameshare (map_server.h chain)
TARGET_INCLUDE_DIRECTORIES(ryzom_gpm2_service PRIVATE ${RZ_SERVER_SRC_DIR} ${MYSQL_INCLUDE_DIR})

TARGET_LINK_LIBRARIES(ryzom_gpm2_service
        PRIVATE
        Ryzom::adminmodules
        Ryzom::gpmsheets
        Ryzom::gameshare
        Ryzom::servershare
        NeL::misc
        NeL::net
        NeL::pacs
        NeL::georges
        NeL::ligo
)

NL_DEFAULT_PROPS(ryzom_gpm2_service "Ryzom, Services: Global Position Manager Service (GPMS) - dynamic worlds")
NL_ADD_RUNTIME_FLAGS(ryzom_gpm2_service)

IF (WITH_PCH)
    target_precompile_headers(ryzom_gpm2_service PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/stdpch.h)
ENDIF ()

INSTALL(TARGETS ryzom_gpm2_service RUNTIME DESTINATION sbin COMPONENT services)
