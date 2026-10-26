add_subdirectory(DatGen)

add_executable(z_ea_launcher WIN32)
set_target_properties(z_ea_launcher PROPERTIES OUTPUT_NAME launcher)

target_link_libraries(z_ea_launcher PRIVATE
    corei_launcher
    zi_always
)
