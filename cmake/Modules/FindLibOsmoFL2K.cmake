INCLUDE(FindPkgConfig)
PKG_CHECK_MODULES(PC_LIBOSMOFL2K libosmo-fl2k)

FIND_PATH(
    LIBOSMOFL2K_INCLUDE_DIRS
    NAMES osmo-fl2k.h
    HINTS $ENV{LIBOSMOFL2K_DIR}/include
        ${PC_LIBOSMOFL2K_INCLUDEDIR}
    PATHS /usr/local/include
          /usr/include
)

FIND_LIBRARY(
    LIBOSMOFL2K_LIBRARIES
    NAMES osmo-fl2k
    HINTS $ENV{LIBOSMOFL2K_DIR}/lib
        ${PC_LIBOSMOFL2K_LIBDIR}
    PATHS /usr/local/lib
          /usr/lib
)

INCLUDE(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(LIBOSMOFL2K DEFAULT_MSG LIBOSMOFL2K_LIBRARIES LIBOSMOFL2K_INCLUDE_DIRS)
MARK_AS_ADVANCED(LIBOSMOFL2K_LIBRARIES LIBOSMOFL2K_INCLUDE_DIRS)
