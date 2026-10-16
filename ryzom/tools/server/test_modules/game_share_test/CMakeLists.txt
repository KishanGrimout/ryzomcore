add_test_executable(ryzom_server_test_module_interface
        test_module_interface.cpp
        test_module_itf.cpp
)

target_link_libraries(ryzom_server_test_module_interface
        PRIVATE
        NeL::misc
        NeL::net
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
