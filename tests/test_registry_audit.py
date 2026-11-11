import unittest, tempfile, csv, pathlib, sys
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/"tools"))
import audit_osmand_poi_mapping as a
class TestPoiRegistry(unittest.TestCase):
 def test_registry_and_semantics(self):
  with tempfile.TemporaryDirectory() as t:
   p=pathlib.Path(t)/'types.xml'
   p.write_text('''<poi_types><poi_type name="water" tag="amenity" value="drinking_water"/><poi_type name="locality" tag="place" value="locality" no_indx="true"/><poi_additional name="dwater" tag="drinking_water" value="yes"/></poi_types>''')
   reg,ex=a.registry(p)
   base={'code':'0x6414','source_count':'32922','priority':'critical','osm_tags':'amenity=drinking_water;drinking_water=yes'}
   self.assertEqual(a.interpret(base,reg,ex)['status'],'native_indexable_type')
   self.assertEqual(a.interpret({**base,'osm_tags':'place=locality'},reg,ex)['status'],'only_nonindexed_type')
   self.assertEqual(a.interpret({**base,'osm_tags':'drinking_water=yes'},reg,ex)['status'],'additional_without_primary_type')
   self.assertEqual(a.interpret({**base,'osm_tags':'man_made=water_intake'},reg,ex)['status'],'no_matching_native_type')
   self.assertEqual(a.interpret({**base,'code':'0x6600','priority':'terrain','osm_tags':''},reg,ex)['severity'],'terrain_exemption')
if __name__=='__main__':unittest.main()
