add_test_executable(ryzom_server_test_module_interface
        test_module_interface.cpp
        test_module_itf.cpp
)

target_link_libraries(ryzom_server_test_module_interface
        PRIVATE
        NeL::misc
        NeL::net
)

add_test_executable(ryzom_game_share_scenario_text_parser
        scenario_text_parser_test.cpp
)

target_include_directories(ryzom_game_share_scenario_text_parser
        PRIVATE
        ${CMAKE_SOURCE_DIR}/ryzom/common/src
)

target_compile_definitions(ryzom_game_share_scenario_text_parser
        PRIVATE
        RYZOM_EXAMPLES_DIR="${CMAKE_SOURCE_DIR}/ryzom/client/examples"
)

target_link_libraries(ryzom_game_share_scenario_text_parser
        PRIVATE
        Ryzom::gameshare
        NeL::misc
)

add_test_executable(ryzom_dynworld_voxel_test
        test_dynworld_voxel.cpp
)

target_include_directories(ryzom_dynworld_voxel_test
        PRIVATE
        ${CMAKE_SOURCE_DIR}/ryzom/common/src
)

target_link_libraries(ryzom_dynworld_voxel_test
        PRIVATE
        NeL::misc
)

add_test_executable(ryzom_dynworld_noise_test
        test_dynworld_noise.cpp
)

target_include_directories(ryzom_dynworld_noise_test
        PRIVATE
        ${CMAKE_SOURCE_DIR}/ryzom/common/src
)

target_link_libraries(ryzom_dynworld_noise_test
        PRIVATE
        NeL::misc
        Ryzom::gameshare
)

add_test_executable(ryzom_dynworld_mapgen_params_test
        test_dynworld_mapgen_params.cpp
)

target_include_directories(ryzom_dynworld_mapgen_params_test
        PRIVATE
        ${CMAKE_SOURCE_DIR}/ryzom/common/src
)

target_link_libraries(ryzom_dynworld_mapgen_params_test
        PRIVATE
        NeL::misc
        Ryzom::gameshare
)

add_test_executable(ryzom_dynworld_environment_definition_test
        test_dynworld_environment_definition.cpp
)

target_include_directories(ryzom_dynworld_environment_definition_test
        PRIVATE
        ${CMAKE_SOURCE_DIR}/ryzom/common/src
)

target_link_libraries(ryzom_dynworld_environment_definition_test
        PRIVATE
        NeL::misc
        Ryzom::gameshare
)

add_test_executable(ryzom_dynworld_map_sector_test
        test_dynworld_map_sector.cpp
)

target_include_directories(ryzom_dynworld_map_sector_test
        PRIVATE
        ${CMAKE_SOURCE_DIR}/ryzom/common/src
        ${POLYVOX_INCLUDE_DIR}
)

target_link_libraries(ryzom_dynworld_map_sector_test
        PRIVATE
        NeL::misc
        Ryzom::gameshare
)

add_test_executable(ryzom_dynworld_deferred_command_test
        test_dynworld_deferred_command.cpp
)

target_include_directories(ryzom_dynworld_deferred_command_test
        PRIVATE
        ${CMAKE_SOURCE_DIR}/ryzom/common/src
        ${POLYVOX_INCLUDE_DIR}
)

target_link_libraries(ryzom_dynworld_deferred_command_test
        PRIVATE
        NeL::misc
        Ryzom::gameshare
)

add_test_executable(ryzom_dynworld_disk_file_db_test
        test_dynworld_disk_file_db.cpp
)

target_include_directories(ryzom_dynworld_disk_file_db_test
        PRIVATE
        ${CMAKE_SOURCE_DIR}/ryzom/common/src
)

target_link_libraries(ryzom_dynworld_disk_file_db_test
        PRIVATE
        NeL::misc
        Ryzom::gameshare
)

add_test_executable(ryzom_dynworld_map_collision_test
        test_dynworld_map_collision.cpp
)

target_include_directories(ryzom_dynworld_map_collision_test
        PRIVATE
        ${CMAKE_SOURCE_DIR}/ryzom/common/src
        ${POLYVOX_INCLUDE_DIR}
)

target_link_libraries(ryzom_dynworld_map_collision_test
        PRIVATE
        NeL::misc
        Ryzom::gameshare
)

add_test_executable(ryzom_dynworld_move_collision_test
        test_dynworld_move_collision.cpp
)

target_include_directories(ryzom_dynworld_move_collision_test
        PRIVATE
        ${CMAKE_SOURCE_DIR}/ryzom/common/src
        ${POLYVOX_INCLUDE_DIR}
)

target_link_libraries(ryzom_dynworld_move_collision_test
        PRIVATE
        NeL::misc
        NeL::pacs
        Ryzom::gameshare
)

add_test_executable(ryzom_dynworld_map_cache_test
        test_dynworld_map_cache.cpp
)

target_include_directories(ryzom_dynworld_map_cache_test
        PRIVATE
        ${CMAKE_SOURCE_DIR}/ryzom/common/src
        ${POLYVOX_INCLUDE_DIR}
)

target_link_libraries(ryzom_dynworld_map_cache_test
        PRIVATE
        NeL::misc
        Ryzom::gameshare
)

add_test_executable(ryzom_dynworld_vegetation_test
        test_dynworld_vegetation.cpp
)

target_include_directories(ryzom_dynworld_vegetation_test
        PRIVATE
        ${CMAKE_SOURCE_DIR}/ryzom/common/src
        ${POLYVOX_INCLUDE_DIR}
)

target_link_libraries(ryzom_dynworld_vegetation_test
        PRIVATE
        NeL::misc
        Ryzom::gameshare
        NeL::3d
)


add_test_executable(ryzom_game_share_rt_scenario_check
        rt_scenario_check_test.cpp
)

target_include_directories(ryzom_game_share_rt_scenario_check
        PRIVATE
        ${CMAKE_SOURCE_DIR}/ryzom/common/src
)

target_link_libraries(ryzom_game_share_rt_scenario_check
        PRIVATE
        Ryzom::gameshare
        NeL::misc
)

add_test_executable(ryzom_game_share_object_serializer
        object_serializer_test.cpp
)

target_include_directories(ryzom_game_share_object_serializer
        PRIVATE
        ${CMAKE_SOURCE_DIR}/ryzom/common/src
)

target_compile_definitions(ryzom_game_share_object_serializer
        PRIVATE
        RYZOM_EXAMPLES_DIR="${CMAKE_SOURCE_DIR}/ryzom/client/examples"
)

target_link_libraries(ryzom_game_share_object_serializer
        PRIVATE
        Ryzom::gameshare
        NeL::misc
)
