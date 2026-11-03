"""Offline package audit. Does not open a serial port or change the board."""
from pathlib import Path
import hashlib,json,subprocess,sys,argparse
ROOT=Path(__file__).resolve().parent
def main():
    parser=argparse.ArgumentParser();parser.add_argument('--toolchain');args=parser.parse_args()
    c=json.loads((ROOT/'build_config.json').read_text())
    for name in c['sources']+c['includes']+[c['linker']]:
        p=(ROOT/name).resolve();assert p.is_relative_to(ROOT) and p.exists(),name
    forbidden=['usurper_stress','us_game_arena','us_demo_','flux_case','exhibition_console']
    for p in (ROOT/'Game').rglob('*'):
        if p.suffix in ['.c','.h']:
            s=p.read_text(encoding='utf-8');assert not any(word in s for word in forbidden),p
    audio=json.loads((ROOT/'Assets/Audio/manifest.json').read_text())
    for name,digest in audio['files'].items():
        assert hashlib.sha256((ROOT/'Assets/Audio'/name).read_bytes()).hexdigest()==digest,name
    for name in audio['music_tracks']:assert (ROOT/'Assets/Audio/Music'/name).is_file(),name
    sys.path.insert(0,str(ROOT/'Scripts'))
    import load_usurper,usurper_pc_audio
    payload=(ROOT/'Build/game.bin').read_bytes();load_usurper.checked_boot_upgrade(ROOT/'Build/game.bin',payload)
    assert usurper_pc_audio.DEFAULT_BANK==ROOT/'Assets/Audio'
    assert usurper_pc_audio.MUSIC_ROOT==ROOT/'Assets/Audio/Music'
    meta=json.loads((ROOT/'Build/preparation.json').read_text())['warden_baked_assets']
    assert hashlib.sha256((ROOT/'Build'/meta['compressed_file']).read_bytes()).hexdigest()==meta['compressed_sha256']
    nm=Path(args.toolchain)/'riscv-none-embed-nm.exe' if args.toolchain else Path('riscv-none-embed-nm.exe')
    symbols=subprocess.check_output([str(nm),str(ROOT/'Build/game.elf')],text=True)
    assert not any(x in symbols for x in ['usurper_stress','exhibition_draw','exhibition_dense','us_demo_','flux_case']), 'Removed module linked'
    result={'status':'PASS','compile_units':len(c['sources']),'firmware_bytes':len(payload),'audio_files':len(audio['files']),'music_tracks':len(audio['music_tracks']),'standalone_paths':True,'removed_demo_symbols_absent':True,'board_tested':False}
    (ROOT/'Build/package_audit.json').write_text(json.dumps(result,indent=2));print(json.dumps(result))
if __name__=='__main__':main()
