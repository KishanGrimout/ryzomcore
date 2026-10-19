# Locate PolyVox library
# This module defines
#  POLYVOX_FOUND, if false, do not try to link to PolyVox
#  POLYVOX_INCLUDE_DIR, where to find PagedVolume.h


find_path(POLYVOX_INCLUDE_DIR PolyVox/PagedVolume.h
  PATH_SUFFIXES include/PolyVox include/PolyVoxCore include/PolyVox/PolyVoxCore
  PATHS
  ~/Library/Frameworks
  /Library/Frameworks
  /sw # Fink
  /opt/local # DarwinPorts
  /opt/csw # Blastwave
  /opt
)

IF(POLYVOX_INCLUDE_DIR)
	SET(POLYVOX_FOUND TRUE)
ENDIF(POLYVOX_INCLUDE_DIR)

if (POLYVOX_FOUND)
	message(STATUS "Found PolyVox location: ${POLYVOX_INCLUDE_DIR}")
endif(POLYVOX_FOUND)
