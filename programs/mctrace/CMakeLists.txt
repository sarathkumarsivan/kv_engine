add_executable(mctrace mctrace.cc $<TARGET_OBJECTS:mc_program_utils>)
target_link_libraries(mctrace
                      mc_client_connection
                      mcd_util
                      platform)
add_sanitizers(mctrace)
install(TARGETS mctrace RUNTIME DESTINATION bin)
