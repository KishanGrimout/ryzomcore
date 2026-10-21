FIND_PACKAGE(ZLIB REQUIRED)
FIND_PACKAGE(PolyVox MODULE REQUIRED)
FIND_PACKAGE(ANL MODULE REQUIRED)

INCLUDE_DIRECTORIES(${POLYVOX_INCLUDE_DIR})
INCLUDE_DIRECTORIES(${ANL_INCLUDE_DIR})

# Add NeL sevenzip include directory for LZMA compression headers
FIND_PACKAGE(NeL REQUIRED COMPONENTS sevenzip)
INCLUDE_DIRECTORIES(${NeL_sevenzip_INCLUDE_DIRS})

# Add local seven_zip include directory for wrapper
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/seven_zip)

FILE(GLOB SRC *.cpp)
FILE(GLOB SRC_TIME time_weather_season/*.cpp)
SET(SRC_DYNWORLD
 ${CMAKE_CURRENT_SOURCE_DIR}/dynworld/environment_definition.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/dynworld/map_sector.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/dynworld/material_definition.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/dynworld/material_library.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/dynworld/biome_manager.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/dynworld/map.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/dynworld/map_deferred_command_manager.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/dynworld/marching_controller.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/dynworld/polyvox_defs.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/dynworld/disk_file_db.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/dynworld/map_cache_manager.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/dynworld/map_server.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/dynworld/master_pager.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/dynworld/slave_pager.cpp
)

# Seven zip wrapper for LZMA compression
FILE(GLOB SRC_SEVENZIP ${CMAKE_CURRENT_SOURCE_DIR}/seven_zip/*.cpp)
SET(SRC_DYNWORLD_MAPGEN
	${CMAKE_CURRENT_SOURCE_DIR}/dynworld/mapgen/noise.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/dynworld/mapgen/mapgen_params.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/dynworld/mapgen/cavegen.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/dynworld/mapgen/mapgen.cpp
)

FILE(GLOB PRIV_H *.h)
SET(PRIV_DYNWORLD_H
	${CMAKE_CURRENT_SOURCE_DIR}/dynworld/voxel.h
	${CMAKE_CURRENT_SOURCE_DIR}/dynworld/environment_definition.h
	${CMAKE_CURRENT_SOURCE_DIR}/dynworld/map_sector.h
	${CMAKE_CURRENT_SOURCE_DIR}/dynworld/material_definition.h
	${CMAKE_CURRENT_SOURCE_DIR}/dynworld/material_library.h
	${CMAKE_CURRENT_SOURCE_DIR}/dynworld/biome_manager.h
	${CMAKE_CURRENT_SOURCE_DIR}/dynworld/map.h
	${CMAKE_CURRENT_SOURCE_DIR}/dynworld/map_deferred_command.h
	${CMAKE_CURRENT_SOURCE_DIR}/dynworld/map_deferred_command_manager.h
	${CMAKE_CURRENT_SOURCE_DIR}/dynworld/cast_collector.h
	${CMAKE_CURRENT_SOURCE_DIR}/dynworld/marching_controller.h
	${CMAKE_CURRENT_SOURCE_DIR}/dynworld/region_mesh_interface.h
	${CMAKE_CURRENT_SOURCE_DIR}/dynworld/polyvox_defs.h
	${CMAKE_CURRENT_SOURCE_DIR}/dynworld/disk_file_db.h
	${CMAKE_CURRENT_SOURCE_DIR}/dynworld/map_cache_manager.h
	${CMAKE_CURRENT_SOURCE_DIR}/dynworld/map_server.h
	${CMAKE_CURRENT_SOURCE_DIR}/dynworld/master_pager.h
	${CMAKE_CURRENT_SOURCE_DIR}/dynworld/slave_pager.h
)

# Seven zip wrapper headers
FILE(GLOB PRIV_SEVENZIP_H ${CMAKE_CURRENT_SOURCE_DIR}/seven_zip/*.h)
SET(PRIV_DYNWORLD_MAPGEN_H
	${CMAKE_CURRENT_SOURCE_DIR}/dynworld/mapgen/noise.h
	${CMAKE_CURRENT_SOURCE_DIR}/dynworld/mapgen/mapgen_params.h
	${CMAKE_CURRENT_SOURCE_DIR}/dynworld/mapgen/cavegen.h
	${CMAKE_CURRENT_SOURCE_DIR}/dynworld/mapgen/mapgen.h
)
FILE(GLOB PRIV_TIME_H time_weather_season/*.h)

SET(HEADER ${PRIV_H} ${PRIV_DYNWORLD_H} ${PRIV_DYNWORLD_MAPGEN_H} ${PRIV_SEVENZIP_H})

FILE(GLOB R2 
	dms.h dms.cpp
	scenario.h scenario.cpp
	user_connection_mgr.h user_connection_mgr.cpp
	object.h object.cpp
	server_animation_module.h server_animation_module.cpp
	server_admin_module.h server_admin_module.cpp
	server_edition_module.h server_edition_module.cpp
	string_mgr_module.h string_mgr_module.cpp
	scenario_entry_points.h scenario_entry_points.cpp
	small_string_manager.h small_string_manager.cpp
	ai_wrapper.h ai_wrapper.cpp
	r2_*.h r2_*.cpp
	ring_*.h ring_*.cpp)

LIST(REMOVE_ITEM SRC R2)
LIST(REMOVE_ITEM HEADER R2)

SOURCE_GROUP("" FILES ${SRC} ${HEADER})
SOURCE_GROUP("dynworld" FILES ${SRC_DYNWORLD} ${PRIV_DYNWORLD_H})
SOURCE_GROUP("dynworld/mapgen" FILES ${SRC_DYNWORLD_MAPGEN} ${PRIV_DYNWORLD_MAPGEN_H})
SOURCE_GROUP("seven_zip" FILES ${SRC_SEVENZIP} ${PRIV_SEVENZIP_H})
SOURCE_GROUP("time_weather_season" FILES ${SRC_TIME} ${PRIV_TIME_H})
SOURCE_GROUP("R2" FILES ${R2})

# Filter out the source files not actually compiled.
LIST(REMOVE_ITEM SRC ${CMAKE_CURRENT_SOURCE_DIR}/enum_template.cpp)
LIST(REMOVE_ITEM HEADER ${CMAKE_CURRENT_SOURCE_DIR}/enum_template.h)

NL_TARGET_LIB(ryzom_gameshare ${HEADER} ${PRIV_TIME_H} ${SRC} ${SRC_TIME} ${SRC_DYNWORLD} ${SRC_DYNWORLD_MAPGEN} ${SRC_SEVENZIP} ${R2})
add_library(Ryzom::gameshare ALIAS ryzom_gameshare)

if (WITH_RYZOM_XSLT)
	add_module_interface(XML_SOURCES
			character_sync_itf.xml
			r2_modules_itf.xml
			r2_share_itf.xml
			ring_session_manager_itf.xml
			welcome_service_itf.xml
	)
endif ()

TARGET_INCLUDE_DIRECTORIES(ryzom_gameshare PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

TARGET_LINK_LIBRARIES(ryzom_gameshare
		PUBLIC
		NeL::misc
		NeL::net
		NeL::ligo
		NeL::georges
		NeL::sevenzip
		PRIVATE
		ZLIB::ZLIB
)

NL_DEFAULT_PROPS(ryzom_gameshare "Ryzom, Library: Game Share")
NL_ADD_RUNTIME_FLAGS(ryzom_gameshare)
NL_ADD_LIB_SUFFIX(ryzom_gameshare)

IF(WITH_PCH)
  target_precompile_headers(ryzom_gameshare PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/stdpch.h)
ENDIF()

IF(WITH_INSTALL_LIBRARIES OR NOT WITH_STATIC)
  INSTALL(TARGETS ryzom_gameshare LIBRARY DESTINATION ${RYZOM_LIB_PREFIX} ARCHIVE DESTINATION ${RYZOM_LIB_PREFIX} COMPONENT libraries)
ENDIF()

IF(WITH_INSTALL_LIBRARIES)
  INSTALL(FILES ${PRIV_H} DESTINATION include/ryzom/game_share COMPONENT headers)
  INSTALL(FILES ${PRIV_DYNWORLD_H} DESTINATION include/ryzom/game_share/dynworld COMPONENT headers)
  INSTALL(FILES ${PRIV_DYNWORLD_MAPGEN_H} DESTINATION include/ryzom/game_share/dynworld/mapgen COMPONENT headers)
  INSTALL(FILES ${PRIV_SEVENZIP_H} DESTINATION include/ryzom/game_share/seven_zip COMPONENT headers)
  INSTALL(FILES ${PRIV_TIME_H} DESTINATION include/ryzom/game_share/time_weather_season COMPONENT headers)
ENDIF()
