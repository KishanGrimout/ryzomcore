FIND_PACKAGE(NeL REQUIRED COMPONENTS sevenzip)

INCLUDE_DIRECTORIES(${NeL_sevenzip_INCLUDE_DIRS})

FILE(GLOB SRC *.cpp)
FILE(GLOB HEADERS *.h)

SOURCE_GROUP("") FILES ${SRC} ${HEADERS})

NL_TARGET_LIB(ryzom_sevenzip ${HEADERS} ${SRC})
add_library(Ryzom::sevenzip ALIAS ryzom_sevenzip)

TARGET_LINK_LIBRARIES(ryzom_sevenzip PRIVATE NeL::sevenzip)

NL_DEFAULT_PROPS(ryzom_sevenzip "Ryzom, Library: Seven Zip Wrapper")
NL_ADD_RUNTIME_FLAGS(ryzom_sevenzip)
NL_ADD_LIB_SUFFIX(ryzom_sevenzip)
