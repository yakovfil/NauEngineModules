import copy
import json
import unittest
import uuid
from portable_scene_identity import stable_scene, read_scene


class StableScene(unittest.TestCase):
    def fixture(self):
        obj, root = str(uuid.uuid4()), str(uuid.uuid4())
        authored = "11111111-2222-3333-4444-555555555555"
        header = dict(objects=[dict(offset=0, size=0)], referencesInfo=[dict(componentUid=root, target=obj)], topLevelObjectIds=[1])
        objects = [dict(localId=1, parentLocalId=0, childLocalIds=[], uid=obj, name="root", rootComponent=dict(uid=root, properties=dict(uid=root, reference="scene-object:"+obj)), additionalComponents=[dict(uid=authored, properties=dict(uid=authored, texture="uid:"+authored))])]
        return header, objects, authored

    def encode(self, header, objects):
        header = copy.deepcopy(header)
        payload = json.dumps(objects).encode()
        header["objects"] = [dict(offset=0,size=len(payload))]
        raw = json.dumps(header).encode()
        return f"NauContent-Kind: nau-scene\nContent-Type: application/json\nContent-Length: {len(raw)}\n\n\n".encode()+raw+payload

    def test_repeat_random_ids_and_preserve_references(self):
        scene = "22222222-3333-4444-5555-666666666666"
        first = self.fixture(); second = self.fixture()
        a = stable_scene(self.encode(*first[:2]),scene,{first[2]})
        b = stable_scene(self.encode(*second[:2]),scene,{second[2]})
        self.assertEqual(a,b)
        self.assertEqual(a,stable_scene(a,scene,{first[2]}))
        header,objects=read_scene(a); obj=objects[0]
        self.assertEqual(header["referencesInfo"][0]["componentUid"],obj["rootComponent"]["uid"])
        self.assertEqual(header["referencesInfo"][0]["target"],obj["uid"])
        self.assertEqual(obj["rootComponent"]["properties"]["reference"],"scene-object:"+obj["uid"])
        self.assertEqual(obj["additionalComponents"][0]["uid"],first[2])
        self.assertEqual(obj["additionalComponents"][0]["properties"]["texture"],"uid:"+first[2])

    def test_invalid_hierarchy_and_duplicate_identity(self):
        header,objects,authored=self.fixture()
        objects[0]["childLocalIds"]=[2]
        with self.assertRaisesRegex(ValueError,"hierarchy"):
            stable_scene(self.encode(header,objects),authored,{authored})
        objects[0]["childLocalIds"]=[]
        objects[0]["rootComponent"]["uid"]=objects[0]["uid"]
        with self.assertRaisesRegex(ValueError,"Duplicate"):
            stable_scene(self.encode(header,objects),authored,{authored})


if __name__ == "__main__":
    unittest.main()
