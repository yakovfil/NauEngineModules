"""Check deterministic publication and nonzero packaging failures."""
import argparse, hashlib, json, pathlib, subprocess, tempfile
parser=argparse.ArgumentParser()
parser.add_argument('--tool',required=True)
parser.add_argument('--results',type=pathlib.Path,required=True)
args=parser.parse_args();results=[]
with tempfile.TemporaryDirectory(prefix='nau-pack-',dir=args.results.parent) as directory:
    root=pathlib.Path(directory);source=root/'source.bin';source.write_bytes(bytes(range(256))*300)
    manifest=root/'manifest.json';output=root/'output.nau_pack'
    files=[{'source':'source.bin','path':'a'}]
    def run(expect=True):
        manifest.write_text(json.dumps({'files':files}))
        p=subprocess.run([args.tool,'--manifest',str(manifest),'--output',str(output)],capture_output=True,text=True,timeout=20)
        results.append({'code':p.returncode,'stdout':p.stdout,'stderr':p.stderr})
        assert (p.returncode==0)==expect,results[-1]
        assert not pathlib.Path(str(output)+'.staging').exists()
    run();original=output.read_bytes();run();assert output.read_bytes()==original
    files.append({'source':'source.bin','path':'a'});run(False);assert output.read_bytes()==original
    files[:]=[{'source':'source.bin','path':'../a'}];run(False)
    files[:]=[{'source':'missing.bin','path':'a'}];run(False);assert output.read_bytes()==original
    files[:]=[{'source':'source.bin','path':'a'}];source.write_bytes(b'changed');run();assert output.read_bytes()!=original
    source.unlink();run(False)
    source.write_bytes(b'valid');output.unlink();output.mkdir();run(False)
args.results.write_text(json.dumps({'passed':True,'cases':results},indent=2))
print('AssetPack CLI checks passed')
