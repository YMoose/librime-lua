# Plugin tests, following the conventions of librime's test/ directory.
# The 'lua' module self-registers via its RIME_MODULE_INITIALIZER
# constructor, so linking the plugin objects is enough; no plugin .so
# needs to be dlopened.
aux_source_directory(. rime_lua_test_src)
add_executable(rime-lua-test ${rime_lua_test_src} $<TARGET_OBJECTS:rime-lua-objs>)
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR}/test)
target_link_libraries(rime-lua-test
  ${LUA_TARGET}
  ${rime_library}
  ${rime_dict_library}
  ${rime_gears_library}
  ${GTEST_LIBRARIES})

set(rime_lua_test_executable ${EXECUTABLE_OUTPUT_PATH}/rime-lua-test${ext})
add_test(NAME rime-lua-test COMMAND ${rime_lua_test_executable})
