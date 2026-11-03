"""Check packaged inputs and image identities without accessing a board."""
from pathlib import Path
import hashlib,json,re,xml.etree.ElementTree as ET

ROOT=Path(__file__).resolve().parent
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def local(base,name):
    p=(base/name).resolve()
    assert p.is_relative_to(ROOT) and p.exists(),str(p)
    return p
def main():
    config=json.loads((ROOT/'build_config.json').read_text())
    for name in config['sources']+config['includes']+[config['linker']]:local(ROOT,name)
    includes=[ROOT/p for p in config['includes']]
    sources=[]
    for base in ['Game','Engine/Software','Assets/Generated','Hardware/Boot']:
        sources.extend(p for p in (ROOT/base).rglob('*') if p.suffix in ['.c','.h','.S'])
    for p in sources:
        for name in re.findall(r'^\s*#\s*include\s*"([^"]+)"',p.read_text(encoding='utf-8'),re.M):
            options=[p.parent/name,*[i/name for i in includes]]
            assert any(q.is_file() and q.resolve().is_relative_to(ROOT) for q in options),(str(p),name)
    project=ROOT/'Hardware/Project'
    ns={'e':'http://www.efinixinc.com/enf_proj'}
    xml=ET.parse(project/'starfield_ts2d.xml')
    hdl=[local(project,x.attrib['name']) for x in xml.findall('.//e:design_file',ns)]
    for x in xml.findall('.//e:sdc_file',ns):local(project,x.attrib['name'])
    ET.parse(project/'starfield_ts2d.peri.xml')
    memories=0
    for p in hdl:
        s=p.read_text(encoding='utf-8')
        for name in re.findall(r'`include\s+"([^"]+)"',s):
            assert any(q.is_file() and q.resolve().is_relative_to(ROOT) for q in [p.parent/name,project/name]),name
        for name in re.findall(r'\$readmem[bh]\("([^"]+)"',s):local(project,name);memories+=1
    identity=json.loads((ROOT/'Validation/identities.json').read_text())
    assert digest(ROOT/'Build/game.bin')==identity['firmware_sha256']
    assert digest(ROOT/'Hardware/usurper_ts2d.bit')==identity['bitstream_sha256']
    for name,expected in identity['hardware_files'].items():assert digest(local(ROOT,name))==expected,name
    for base in ['Build','Hardware']:
        meta=json.loads((ROOT/base/'boot_upgrade.json').read_text())
        assert digest(ROOT/base/'boot_upgrade.bin')==meta['sha256']
        assert meta['firmware_sha256']==identity['firmware_sha256']
    for name in ['boot.c','boot.ld','starfield.ld','start.S','runtime.c','runtime.h','usurper_boot_upgrade.c','usurper_boot_upgrade_image.h','build.py']:
        local(ROOT/'Hardware/Boot',name)
    golden=json.loads((ROOT/'Tests/expected_gameplay.json').read_text())
    assert golden['ticks']==30000 and len(golden['trace_sha256'])==64
    audio=json.loads((ROOT/'Assets/Audio/manifest.json').read_text())
    for name,expected in audio['files'].items():assert digest(local(ROOT/'Assets/Audio',name))==expected,name
    for name in audio['music_tracks']:local(ROOT/'Assets/Audio/Music',name)
    manifest=ROOT/'SHA256SUMS.json'
    count=0
    if manifest.exists():
        for name,expected in json.loads(manifest.read_text(encoding='utf-8')).items():assert digest(local(ROOT,name))==expected,name;count+=1
    result=dict(status='PASS',compile_units=len(config['sources']),hdl_inputs=len(hdl),ram_images=memories,
                audio_files=len(audio['files']),music_tracks=len(audio['music_tracks']),manifest_entries=count,
                firmware_sha256=identity['firmware_sha256'],board_tested=False,hardware_resynthesized=False)
    print(json.dumps(result))
if __name__=='__main__':main()
