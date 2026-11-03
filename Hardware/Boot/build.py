"""Rebuild the volatile UART loader and upgrade bridge without programming hardware."""
from pathlib import Path
import argparse,hashlib,json,os,re,subprocess

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser();p.add_argument('--toolchain');a=p.parse_args()
    compiler=str(Path(a.toolchain)/'riscv-none-embed-gcc.exe') if a.toolchain else 'riscv-none-embed-gcc.exe'
    objcopy=str(Path(a.toolchain)/'riscv-none-embed-objcopy.exe') if a.toolchain else 'riscv-none-embed-objcopy.exe'
    out=ROOT/'Build/Boot';out.mkdir(parents=True,exist_ok=True)
    flags=['-march=rv32im','-mabi=ilp32','-O3','-ffreestanding','-fno-builtin','-fno-common','-msmall-data-limit=0','-nostdlib','-Wall','-Wextra','-Werror','-ffunction-sections','-fdata-sections','-Wl,--gc-sections']
    env=os.environ.copy()
    for k in ['CPATH','C_INCLUDE_PATH','CPLUS_INCLUDE_PATH','LIBRARY_PATH']:env.pop(k,None)
    for name,source,linker in [('boot','boot.c','boot.ld'),('boot_upgrade','usurper_boot_upgrade.c','starfield.ld')]:
        cmd=[compiler,*flags,'-I'+str(HERE),str(HERE/'start.S'),str(HERE/'runtime.c'),str(HERE/source),'-T'+str(HERE/linker),'-o',str(out/(name+'.elf')),'-lgcc']
        subprocess.run(cmd,cwd=ROOT,env=env,check=True)
        subprocess.run([objcopy,'-O','binary',str(out/(name+'.elf')),str(out/(name+'.bin'))],check=True)
    meta=json.loads((ROOT/'Hardware/boot_upgrade.json').read_text())
    assert digest(out/'boot.bin')==meta['boot_sha256'],'UART loader differs from packaged version'
    assert digest(out/'boot_upgrade.bin')==meta['sha256'],'Upgrade bridge differs from packaged version'
    header=(HERE/'usurper_boot_upgrade_image.h').read_text()
    body=header.split('{',1)[1].split('}',1)[0]
    embedded=bytes(int(v.strip(),0) for v in body.split(',') if v.strip())
    assert embedded==(out/'boot.bin').read_bytes(),'Embedded loader header differs'
    result={'status':'PASS','boot_sha256':digest(out/'boot.bin'),'bridge_sha256':digest(out/'boot_upgrade.bin'),'matches_packaged_images':True,'board_tested':False}
    (out/'validation.json').write_text(json.dumps(result,indent=2),encoding='utf-8');print(json.dumps(result))
if __name__=='__main__':main()
