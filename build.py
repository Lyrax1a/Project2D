from pathlib import Path
import json,subprocess,hashlib,os,argparse,shutil
ROOT=Path(__file__).resolve().parent
def main():
    parser=argparse.ArgumentParser();parser.add_argument('--toolchain');args=parser.parse_args()
    c=json.loads((ROOT/'build_config.json').read_text());out=ROOT/'Build';out.mkdir(exist_ok=True)
    compiler=str(Path(args.toolchain)/'riscv-none-embed-gcc.exe') if args.toolchain else c['compiler']
    objcopy=str(Path(args.toolchain)/'riscv-none-embed-objcopy.exe') if args.toolchain else c['objcopy']
    cmd=[compiler]+c['flags']+['-I'+str(ROOT/p) for p in c['includes']]+[str(ROOT/p) for p in c['sources']]+['-T'+str(ROOT/c['linker']),'-Wl,-Map='+str(out/'game.map'),'-o',str(out/'game.elf')]
    # Libraries must follow translation units for non-LTO as well as LTO builds.
    cmd.remove('-lgcc');cmd.append('-lgcc')
    env=os.environ.copy()
    for k in ['CPATH','C_INCLUDE_PATH','CPLUS_INCLUDE_PATH','LIBRARY_PATH']:env.pop(k,None)
    with (out/'compile.log').open('w',encoding='utf-8') as log:
        result=subprocess.run(cmd,cwd=ROOT,env=env,stdout=log,stderr=log)
    if result.returncode:print((out/'compile.log').read_text());raise SystemExit(result.returncode)
    subprocess.run([objcopy,'-O','binary',str(out/'game.elf'),str(out/'game.bin')],check=True)
    data=(out/'game.bin').read_bytes();assert len(data)<=896*1024
    boot=json.loads((ROOT/'Hardware/boot_upgrade.json').read_text())
    boot['firmware_sha256']=hashlib.sha256(data).hexdigest()
    (out/'boot_upgrade.json').write_text(json.dumps(boot,indent=2))
    shutil.copy2(ROOT/'Hardware/boot_upgrade.bin',out/'boot_upgrade.bin')
    shutil.copy2(ROOT/'Assets/Graphics/upload_manifest.json',out/'preparation.json')
    (out/'build_result.json').write_text(json.dumps({'status':'BUILD_PASS_NOT_BOARD_TESTED','bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()},indent=2));print('BUILD_PASS',len(data))
if __name__=='__main__':main()
