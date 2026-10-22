# Locate Accidental Noise Library (ANL)
# This module defines:
#  ANL_FOUND, if false, do not try to link to ANL
#  ANL_INCLUDE_DIR, where to find Noise/noise_gen.h
#  ANL_LIBRARY, the ANL noise library to link

find_path(ANL_INCLUDE_DIR Noise/noise_gen.h
  PATH_SUFFIXES include/ANL include
  PATHS
  ~/Library/Frameworks
  /Library/Frameworks
  /sw # Fink
  /opt/local # DarwinPorts
  /opt/csw # Blastwave
  /opt
)

# ANL consumer needs the noise classes (CImplicitBasisFunction, CCellularGenerator, ...).
# In an ANL source checkout (the supported discovery layout) these are built by the
# Noise target as a static library; search the usual in-tree output layouts.
find_library(ANL_LIBRARY
  NO_MODULE
  NAMES Noise anl
  HINTS
    ${ANL_DIR}
    ENV ANL_DIR
    ${ANL_INCLUDE_DIR}/ANL/Build_2017/Noise/Release
    ${ANL_INCLUDE_DIR}/ANL/Build_2017/Noise/RelWithDebInfo
    ${ANL_INCLUDE_DIR}/ANL/Build_2017/Module/Release
    ${ANL_INCLUDE_DIR}/ANL/Build_2017/Module/RelWithDebInfo
    ${ANL_INCLUDE_DIR}/ANL/Build/Noise/Release
    ${ANL_INCLUDE_DIR}/ANL/Build/Noise/RelWithDebInfo
    ${ANL_INCLUDE_DIR}/ANL/Build/Module/Release
    ${ANL_INCLUDE_DIR}/ANL/Build/Module/RelWithDebInfo
    ${ANL_INCLUDE_DIR}/Build/Noise/Release
    ${ANL_INCLUDE_DIR}/Build/Noise/RelWithDebInfo
    ${ANL_INCLUDE_DIR}/Build/Module/Release
    ${ANL_INCLUDE_DIR}/Build/Module/RelWithDebInfo
)
IF(NOT ANL_LIBRARY)
  FIND_LIBRARY(ANL_LIBRARY anl)
ENDIF()

IF(ANL_INCLUDE_DIR AND ANL_LIBRARY)
	SET(ANL_FOUND TRUE)
ELSE(ANL_INCLUDE_DIR AND ANL_LIBRARY)
	SET(ANL_FOUND FALSE)
ENDIF(ANL_INCLUDE_DIR AND ANL_LIBRARY)

if (ANL_FOUND)
	message(STATUS "Found ANL location: ${ANL_INCLUDE_DIR}")
	message(STATUS "Found ANL library: ${ANL_LIBRARY}")
endif(ANL_FOUND)

MARK_AS_ADVANCED(ANL_INCLUDE_DIR ANL_LIBRARY)
