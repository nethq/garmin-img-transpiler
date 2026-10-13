cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j "$(nproc)"

./build/garmin2osm \
  --input '/home/opthq/gmapsupp.img' \
  --output Bulgaria-Garmin.osm
