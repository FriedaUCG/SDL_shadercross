find_program(Tint_EXECUTABLE
    NAMES tint tint.exe
    HINTS ${Tint_ROOT}
    PATH_SUFFIXES bin
    DOC "Tint shader translation executable"
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Tint
    REQUIRED_VARS Tint_EXECUTABLE
)

if(Tint_FOUND AND NOT TARGET Tint::tint)
    add_executable(Tint::tint IMPORTED)
    set_property(TARGET Tint::tint PROPERTY IMPORTED_LOCATION "${Tint_EXECUTABLE}")
endif()

mark_as_advanced(Tint_EXECUTABLE)
