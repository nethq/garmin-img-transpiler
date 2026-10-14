export JAVA_OPTS="-Xms256M -Xmx25G"

../../scripts/build_obf.sh \
  $(realpath .) \
  $(realpath ../Bulgaria-garmin.osm)
