# Locate Accidental Noise Library (ANL)
# This module defines:
#  ANL_FOUND, if false, do not try to link to ANL
#  ANL_INCLUDE_DIR, where to find Noise/noise_gen.h

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

IF(ANL_INCLUDE_DIR)
	SET(ANL_FOUND TRUE)
ENDIF(ANL_INCLUDE_DIR)

if (ANL_FOUND)
	message(STATUS "Found ANL location: ${ANL_INCLUDE_DIR}")
endif(ANL_FOUND)
