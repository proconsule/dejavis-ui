find_path(SHADERC_INCLUDE_DIR
    NAMES shaderc/shaderc.hpp
    HINTS ${VULKAN_SDK}/include
)

find_library(SHADERC_LIBRARY
    NAMES shaderc_combined shaderc
    HINTS ${VULKAN_SDK}/lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Shaderc
    DEFAULT_MSG
    SHADERC_LIBRARY
    SHADERC_INCLUDE_DIR
)

if(Shaderc_FOUND AND NOT TARGET shaderc::shaderc)
    add_library(shaderc::shaderc UNKNOWN IMPORTED)
    set_target_properties(shaderc::shaderc PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${SHADERC_INCLUDE_DIR}"
        IMPORTED_LOCATION "${SHADERC_LIBRARY}"
    )
endif()

mark_as_advanced(SHADERC_INCLUDE_DIR SHADERC_LIBRARY)
