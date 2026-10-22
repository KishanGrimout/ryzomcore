# The packLZMA/unpackLZMA/7zip wrapper ported verbatim from the legacy dynworld
# fork, which built it together with a full vendored copy of the 7zip SDK.
# That SDK copy is NOT reproduced here: the current mainline ships its own 7zip
# SDK (nel/3rdparty/seven_zip, exposed as NeL::sevenzip) and duplicate upstream
# C sources caused multiply-defined symbols whenever both copies were linked
# into the same binary (the client pulls the NeL SDK through NLMISC's seven_zip
# and the wrapper through the dynworld map code). The wrapper therefore builds
# on top of the NeL SDK; the minor downstream API differences (LookToRead ->
# CLookToRead2) are adapted in seven_zip.cpp.

FILE(GLOB LIB_SRC *.cpp *.h)

NL_TARGET_LIB(ryzom_sevenzip ${LIB_SRC})
add_library(Ryzom::sevenzip ALIAS ryzom_sevenzip)

TARGET_LINK_LIBRARIES(ryzom_sevenzip
        PRIVATE NeL::misc
        PUBLIC NeL::sevenzip
)

# The wrapper includes the SDK headers without prefix ("7z.h", "7zCrc.h", ...);
# consumers include them as "seven_zip/<header>" through the parent directory.
TARGET_INCLUDE_DIRECTORIES(ryzom_sevenzip
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../../../nel/3rdparty/seven_zip
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../../../nel/3rdparty
)

NL_DEFAULT_PROPS(ryzom_sevenzip "Ryzom, Library: Seven Zip")
NL_ADD_RUNTIME_FLAGS(ryzom_sevenzip)
NL_ADD_LIB_SUFFIX(ryzom_sevenzip)

IF((WITH_INSTALL_LIBRARIES AND WITH_STATIC) OR NOT WITH_STATIC)
  INSTALL(TARGETS ryzom_sevenzip LIBRARY DESTINATION ${RYZOM_LIB_PREFIX} ARCHIVE DESTINATION ${RYZOM_LIB_PREFIX} COMPONENT libraries)
ENDIF()
