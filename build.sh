#sudo apt-get install -y cmake g++ libpng-dev libsqlite3-dev python3
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j "$(nproc)"
ctest --test-dir build --output-on-failure

./build/garmin2osm --input '/home/opthq/gmapsupp.img' \
   --output BGMountains-legend-reviewed.osm \
   --poi-overrides config/poi-overrides-legend-reviewed.csv \
   --poi-name-fixes config/poi-name-fixes.csv 
   --poi-manifest BGMountains-source-reviewed.csv \
   --poi-types-out BGMountains-types-reviewed.csv

python3 tools/validate_pois.py compare \
  --source BGMountains-source-reviewed.csv \
  --osm BGMountains-legend-reviewed.osm \
  --rules config/poi-overrides-legend-reviewed.csv \
  --report validated.json --mismatches mismatches.csv
