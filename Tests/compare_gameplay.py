"""Compare gameplay state traces and emitted audio events."""
import argparse
import hashlib
import json
import re
from pathlib import Path
import subprocess
import sys
MODULES='fixed mechanic archon flame flame_visual twin boundary boundary_visual raster softfloat hangar hangar_visual dart controller controller_visual heavy_bomb supplies rng warden warden_visual interceptor threat_sort geometry_cache warden_enemy warden_hull_visual polyline triangle atan asset_codec campaign cpu_renderer'.split()
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from Scripts.usurper_pc_audio import AudioDecoder

HARNESS=r'''
#define USURPER_HOST
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "SCENE_PATH"
#ifdef USURPER_AUDIO_PC
int uart_try_write(const char *data,unsigned length){fwrite(data,1,length,stdout);return 1;}
#endif
static unsigned hash=2166136261u;
static void digest(const void *data,size_t size){const unsigned char *p=data;while(size--)hash=(hash^*p++)*16777619u;}
#define H(v) digest(&(v),sizeof(v))
int main(int argc,char **argv){
    assert(argc==2);FILE *trace=fopen(argv[1],"wb");assert(trace);
#ifdef USURPER_AUDIO_PC
    usurper_audio_init();
#endif
    usurper_scene_reset();
    for(unsigned tick=0;tick<30000;++tick){
        /* Identical start fixtures for each of the first three sectors. These
         * are regression fixtures, not an earned-profile or full-clear claim. */
        if(tick%10000==0){
            usurper_scene_reset();campaign_stage_id=tick/10000;
            int core=tech_index("核心");assert(core>=0);tech_owned[core]=1;
            launch_from_research();
        }
        if(phase==USURPER_PHASE_RESULT)launch_from_research();
        if(tick%240==0)usurper_scene_key('v');
        if(tick%120==0)usurper_scene_key("wasd"[(tick/120)%4]);
        if(tick%300==0)usurper_scene_key('f');
        usurper_scene_step();
        H(phase);H(level_tick);H(player_x);H(player_y);H(player_hp);
        DIGEST_OBJECTS
        H(enemy_damage_fraction);H(unit_count);H(unit_species);
        H(boss_hp);H(boss_phase);H(alloy_run);H(alloy_wallet);H(captured_total);H(released_total);H(kills_total);
        assert(fwrite(&hash,sizeof(hash),1,trace)==1);
#ifdef USURPER_AUDIO_PC
        usurper_audio_service();
#endif
    }
    fclose(trace);
#ifdef USURPER_AUDIO_PC
    /* Exercise real successful emitters independently of random combat. */
    usurper_scene_reset();phase=USURPER_PHASE_BATTLE;
    for(unsigned i=0;i<60;++i)usurper_audio_tick(phase);
    assert(spawn_ally_projectile(player_x,player_y,0,1,580,1)>=0);
    spawn_pickup(player_x,player_y,1,0);pickup_step();
    assert(spawn_dart_projectile(0,1,1,1)>=0);
    usurper_audio_service();
#endif
    return 0;
}
'''

def main():
    p=argparse.ArgumentParser();p.add_argument('--baseline',type=Path)
    p.add_argument('--candidate',type=Path,default=ROOT);p.add_argument('--output',type=Path,default=ROOT/'Build/GameRegression')
    p.add_argument('--vcvars',type=Path,default=Path('C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Auxiliary/Build/vcvars64.bat'))
    a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
    expected=json.loads((ROOT/'Tests/expected_gameplay.json').read_text())
    hashes={};events=set()
    projects=([('baseline',a.baseline.resolve())] if a.baseline else [])+[('candidate',a.candidate.resolve())]
    for label,project in projects:
        dest=out/label;dest.mkdir(exist_ok=True);snapshot=project/('Source' if (project/'Source').is_dir() else 'runtime_snapshot');fw=project/'Game' if (project/'Game').is_dir() else snapshot/'platform/board_conformance/firmware'
        # Hash named members, never C padding bytes (aggregate initialization
        # leaves those unspecified and unrelated to gameplay semantics).
        scene=(fw/'usurper_scene.c').read_text(encoding='utf-8');digests=[]
        for typename,array in [('Enemy','enemies'),('Projectile','projectiles'),('Pickup','pickups')]:
            body=next(body for body,name in re.findall(r'typedef struct\s*\{([^}]+)\}\s*(\w+);',scene) if name==typename)
            fields=[]
            for decl in re.findall(r'\b(?:u?int\d+_t|unsigned|double)\s+([^;]+);',body):
                fields.extend(part.strip().split('[')[0] for part in decl.split(','))
            assert fields
            digests.append(f'for(unsigned i=0;i<sizeof({array})/sizeof({array}[0]);++i){{'+''.join(f'H({array}[i].{field});' for field in fields)+'}')
        source=dest/'scene_test.c';source.write_text(HARNESS.replace('SCENE_PATH',(fw/'usurper_scene.c').as_posix()).replace('DIGEST_OBJECTS','\n'.join(digests)),encoding='utf-8')
        files=[source,*[fw/f'usurper_{m}.c' for m in [*MODULES,'profile','warden_baked']],(project/'Engine/Software/preboard_core/sw/src/renderer_sw.c' if (project/'Game').is_dir() else snapshot/'preboard_core/sw/src/renderer_sw.c')]
        flags=''
        if label=='candidate':
            flags='/DUSURPER_AUDIO_ENABLED /DUSURPER_AUDIO_PC /DUSURPER_AUDIO_HOST';files.append(fw/'usurper_audio.c')
        vc=a.vcvars.resolve();exe=dest/'scene.exe'
        command=f'"{vc}" >nul && cl /nologo /utf-8 /std:c11 /O2 /fp:strict /D_CRT_SECURE_NO_WARNINGS /DUSURPER_PROFILE_ENABLED /DUSURPER_WARDEN_BAKED /DUSURPER_EXHIBITION_THREE_STAGES {flags} /I"{project}" /I"{project / 'Assets/Generated'}" /I"{fw}" '
        command+=' '.join(f'"{f}"' for f in files)+f' /Fe"{exe}"'
        result=subprocess.run(command,shell=True,cwd=dest,capture_output=True,text=True,encoding='utf-8',errors='replace')
        (dest/'build.log').write_text(result.stdout+result.stderr,encoding='utf-8');result.check_returncode()
        run=subprocess.run([str(exe),str(dest/'trace.bin')],capture_output=True,timeout=60)
        (dest/'audio.log').write_bytes(run.stdout+run.stderr);run.check_returncode()
        hashes[label]=hashlib.sha256((dest/'trace.bin').read_bytes()).hexdigest()
        if label=='candidate':
            decoder=AudioDecoder();packets=decoder.feed(run.stdout);assert not decoder.invalid
            events={e for packet in packets for e in packet.events};assert {2,12,15}<=events
    assert hashes['candidate']==hashes.get('baseline',expected['trace_sha256']),hashes
    assert sorted(events)==expected['audio_events'],events
    report=dict(status='PASS',ordinary_ticks=30000,trace_sha256=hashes,actual_events=sorted(events),
                scope='Three identical sector-start fixtures, 10000 ticks each with automatic restart after defeat; C gameplay state trace unchanged with PC audio enabled; successful allied shot/pickup/rail emitters. Not a full clear, earned profile, board FPS or physical sound claim.')
    (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print(report)
if __name__=='__main__':main()
