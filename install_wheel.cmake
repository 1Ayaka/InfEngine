# install_wheel.cmake
file(GLOB WHEELS "${CMAKE_SOURCE_DIR}/dist/*.whl")

list(LENGTH WHEELS WHEEL_COUNT)
if(WHEEL_COUNT EQUAL 0)
    message(FATAL_ERROR "❌ No wheel found in dist/")
endif()

list(GET WHEELS 0 FIRST_WHEEL)
execute_process(COMMAND pip install --force-reinstall "${FIRST_WHEEL}")
