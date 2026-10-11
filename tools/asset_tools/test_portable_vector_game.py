import copy
import json
import pathlib
import unittest
from portable_vector_game import validate_game

class GameDataTests(unittest.TestCase):
    def test_valid_and_invalid_data(self):
        root = pathlib.Path(__file__).resolve().parents[3]
        good = json.loads((root / "NauDemo/content/game/battlezone.json").read_text())
        self.assertEqual(validate_game(good), good)
        for mutation in (lambda x: x.update(version=2),
                         lambda x: x["geometry"]["tank"]["edges"].append([0,100000]),
                         lambda x: x["geometry"]["tank"]["vertices"].append([float("nan"),0,0]),
                         lambda x: x["geometry"].pop("tank"),
                         lambda x: x["obstacles"].append(dict(x=0,z=0,radius=3,pyramid=0))):
            value=copy.deepcopy(good); mutation(value)
            with self.assertRaises(ValueError): validate_game(value)

if __name__ == "__main__": unittest.main()
