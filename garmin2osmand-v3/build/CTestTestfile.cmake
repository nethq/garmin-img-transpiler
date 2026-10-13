# CMake generated Testfile for 
# Source directory: /home/opthq/vector-v4/garmin2osmand-v3
# Build directory: /home/opthq/vector-v4/garmin2osmand-v3/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(help "/home/opthq/vector-v4/garmin2osmand-v3/build/garmin2osmand" "--help")
set_tests_properties(help PROPERTIES  _BACKTRACE_TRIPLES "/home/opthq/vector-v4/garmin2osmand-v3/CMakeLists.txt;17;add_test;/home/opthq/vector-v4/garmin2osmand-v3/CMakeLists.txt;0;")
add_test(classic_and_gmp_end_to_end "/usr/bin/python3" "/home/opthq/vector-v4/garmin2osmand-v3/tests/test_smoke.py" "/home/opthq/vector-v4/garmin2osmand-v3/build/garmin2osmand")
set_tests_properties(classic_and_gmp_end_to_end PROPERTIES  _BACKTRACE_TRIPLES "/home/opthq/vector-v4/garmin2osmand-v3/CMakeLists.txt;21;add_test;/home/opthq/vector-v4/garmin2osmand-v3/CMakeLists.txt;0;")
add_test(native_vector_export "/usr/bin/python3" "/home/opthq/vector-v4/garmin2osmand-v3/tests/test_vector.py" "/home/opthq/vector-v4/garmin2osmand-v3/build/garmin2osm")
set_tests_properties(native_vector_export PROPERTIES  _BACKTRACE_TRIPLES "/home/opthq/vector-v4/garmin2osmand-v3/CMakeLists.txt;22;add_test;/home/opthq/vector-v4/garmin2osmand-v3/CMakeLists.txt;0;")
