# Vendored 7zip SDK + the packLZMA/unpackLZMA wrapper (ported verbatim from the
# legacy dynworld fork, which built the SDK together with its wrapper).
# Note: this is NOT the NeL sevenzip library (nel/src/misc/seven_zip.cpp): the
# wrapper's memory-based API targets the API of the SDK vendored right here.

INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR})

FILE(GLOB LIB_SRC *.cpp *.h)

LIST(REMOVE_ITEM LIB_SRC ${CMAKE_CURRENT_SOURCE_DIR}/7zMain.cpp)

NL_TARGET_LIB(ryzom_sevenzip ${LIB_SRC})
add_library(Ryzom::sevenzip ALIAS ryzom_sevenzip)

TARGET_LINK_LIBRARIES(ryzom_sevenzip PRIVATE NeL::misc)

NL_DEFAULT_PROPS(ryzom_sevenzip "Ryzom, Library: Seven Zip")
NL_ADD_RUNTIME_FLAGS(ryzom_sevenzip)
NL_ADD_LIB_SUFFIX(ryzom_sevenzip)

# Sourced from the legacy directory-scope ADD_DEFINITIONS, scoped to the target
TARGET_COMPILE_DEFINITIONS(ryzom_sevenzip PRIVATE UNICODE _UNICODE _7ZIP_PPMD_SUPPPORT)

IF(WITH_PCH)
  target_precompile_headers(ryzom_sevenzip PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Precomp.h)
ENDIF()

IF((WITH_INSTALL_LIBRARIES AND WITH_STATIC) OR NOT WITH_STATIC)
  INSTALL(TARGETS ryzom_sevenzip LIBRARY DESTINATION ${RYZOM_LIB_PREFIX} ARCHIVE DESTINATION ${RYZOM_LIB_PREFIX} COMPONENT libraries)
ENDIF()

IF(WITH_RYZOM_TOOLS)
  ADD_EXECUTABLE(7zDec ${CMAKE_CURRENT_SOURCE_DIR}/7zMain.cpp)

  TARGET_LINK_LIBRARIES(7zDec PRIVATE ryzom_sevenzip)
  NL_DEFAULT_PROPS(7zDec "Ryzom, Tools, Misc: Seven Zip Decoder")
  NL_ADD_RUNTIME_FLAGS(7zDec)

  INSTALL(TARGETS 7zDec RUNTIME DESTINATION ${RYZOM_BIN_PREFIX} COMPONENT tools)
ENDIF()
