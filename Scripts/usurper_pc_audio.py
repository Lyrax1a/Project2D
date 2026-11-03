"""Original Usurper sounds on the PC; consumes UART bytes without owning a port."""
from __future__ import annotations
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import queue
import random
import sys
import threading
import time
import warnings

ROOT=Path(__file__).resolve().parents[1]
DEFAULT_BANK=ROOT/'Assets/Audio'
MUSIC_ROOT=ROOT/'Assets/Audio/Music'
# Original AudioManager feedback volume/pitch. Capture and non-projectile release
# use original upgrade/overdrive cues because the native game has coarser events.
EVENTS={1:('ui_click',.72,1),2:('shot',.36,.94),3:('hit',.24,1.04),
        4:('kill',.28,.98),5:('hurt',.8,.96),6:('upgrade',.35,1.25),
        7:('overdrive',.52,1),8:('upgrade',.74,1),9:('boss_break',.86,.88),
        10:('boss_break',.72,.74),11:('warning',.42,1),12:('pickup_alloy',.38,1),
        13:('armor_break',.42,1.05),14:('boss_break',.86,.72),15:('rail_fire',.76,1)}

def initialize_output(pygame,device=None):
    """Retry a failed Windows default device through real DirectSound output."""
    configured=os.environ.get('SDL_AUDIODRIVER')
    options=dict(frequency=44100,size=-16,channels=2,buffer=512)
    if device:options['devicename']=device
    try:
        pygame.mixer.init(**options)
        return configured or 'default'
    except pygame.error as primary:
        if sys.platform!='win32' or configured:raise
        pygame.mixer.quit()
        os.environ['SDL_AUDIODRIVER']='directsound'
        try:
            pygame.mixer.init(**options)
        except pygame.error as fallback:
            raise RuntimeError(f'Default audio failed: {primary}; DirectSound failed: {fallback}') from fallback
        finally:
            os.environ.pop('SDL_AUDIODRIVER',None)
        warnings.warn('Default Windows audio failed; using DirectSound output: '+str(primary),RuntimeWarning)
        return 'directsound'

@dataclass(frozen=True)
class AudioPacket:
    sequence:int
    phase:int
    flags:int
    music:int
    sfx:int
    muted:bool
    events:tuple[int,...]

class AudioDecoder:
    """Fragment-safe ASCII parser; unrelated diagnostics never become sounds."""
    def __init__(self):
        self.buffer=bytearray();self.discard=False;self.invalid=0
    def feed(self,data):
        packets=[]
        for byte in data:
            if byte==10:
                line=bytes(self.buffer).rstrip(b'\r');self.buffer.clear()
                if not self.discard and line.startswith(b'UA1='):
                    try:
                        value=line[4:]
                        if len(value)%2 or any(c not in b'0123456789ABCDEF' for c in value):raise ValueError()
                        raw=bytes.fromhex(value.decode('ascii'))
                        check=0
                        for c in raw:check^=c
                        if not 8<=len(raw)<=16 or check or raw[2]>9 or raw[3]>7 or raw[4]>100 or raw[5]>100 or raw[6]>1:raise ValueError()
                        if any(e not in EVENTS for e in raw[7:-1]):raise ValueError()
                        packets.append(AudioPacket(int.from_bytes(raw[:2],'big'),raw[2],raw[3],raw[4],raw[5],bool(raw[6]),tuple(raw[7:-1])))
                    except (ValueError,UnicodeError):self.invalid+=1
                self.discard=False
            elif not self.discard:
                if len(self.buffer)>=4096:self.buffer.clear();self.discard=True
                else:self.buffer.append(byte)
        return packets

class OriginalMixer:
    def __init__(self,bank=DEFAULT_BANK,music_root=MUSIC_ROOT,device=None):
        os.environ.setdefault('PYGAME_HIDE_SUPPORT_PROMPT','1')
        import pygame
        import numpy as np
        self.pg=pygame;self.rng=random.Random();self.music_root=Path(music_root)
        self.metadata=json.loads((Path(bank)/'manifest.json').read_text(encoding='utf-8'))
        self.music_tracks=self.metadata['music_tracks']
        if any(Path(p).is_absolute() or '..' in Path(p).parts for p in self.music_tracks):raise ValueError('Invalid original music playlist')
        self.sounds={};self.variants={};self.last={};self.cursor=0;self.owners=[None]*20
        self.context=None;self.music_path=None;self.music_enabled=False;self.next_music=0.
        self.last_state=None;self.played=0;self.played_events={}
        self.backend=initialize_output(pygame,device)
        try:
            from pygame._sdl2.audio import get_audio_device_names
            self.devices=list(get_audio_device_names(False))
        except Exception:self.devices=[]
        pygame.mixer.set_num_channels(20)
        try:
            for event,(name,volume,pitch) in EVENTS.items():
                info=self.metadata['streams'][name];sounds=[]
                for filename in info['files']:
                    path=Path(bank)/filename
                    if path.parent.resolve()!=Path(bank).resolve():raise ValueError('Invalid audio bank path')
                    if hashlib.sha256(path.read_bytes()).hexdigest()!=self.metadata['files'][filename]:raise ValueError('Audio bank hash mismatch: '+filename)
                    source=pygame.mixer.Sound(str(path));samples=pygame.sndarray.array(source)
                    # Cache three small pitch variations outside the serial/input loop.
                    for step in (-1,0,1):
                        rate=pitch*2**(step*info['pitch_semitones']/12)
                        positions=np.arange(0,len(samples)-1,rate)
                        result=np.column_stack([np.interp(positions,np.arange(len(samples)),samples[:,i]) for i in range(2)]).astype(np.int16)
                        sounds.append(pygame.sndarray.make_sound(np.ascontiguousarray(result)))
                self.sounds[event]=sounds
        except Exception:
            pygame.mixer.quit();raise
    def stop(self):
        self.pg.mixer.stop();self.pg.mixer.music.stop();self.music_enabled=False
    def apply(self,p,now):
        self.last_state=p
        if p.muted or p.phase==9:
            self.stop();return
        if not p.flags&2 or not p.sfx:self.pg.mixer.stop()
        context={0:'menu',4:'upgrade',1:'battle',5:'battle',6:'opening',8:'menu'}.get(p.phase,self.context)
        enabled=bool(p.flags&1 and p.music and p.phase!=2)
        if context!=self.context:
            self.context=context;self.pg.mixer.music.stop();self.next_music=0
        if not enabled:self.pg.mixer.music.stop()
        slider=p.music/100
        self.pg.mixer.music.set_volume(.8*(slider*.8 if slider<=.5 else .4+(slider-.5)*1.2)*10**(-6/20))
        if enabled and not self.music_enabled:self.next_music=0
        self.music_enabled=enabled
        if not p.flags&2 or not p.sfx:return
        for event in p.events:
            name,gain,_=EVENTS[event];info=self.metadata['streams'][name]
            if now-self.last.get(name,-100)<info['interval_ms']/1000:continue
            self.last[name]=now
            matching=[i for i in range(20) if self.owners[i]==name and self.pg.mixer.Channel(i).get_busy()]
            available=next((i for n in range(20) if not self.pg.mixer.Channel(i:=(self.cursor+n)%20).get_busy()),None)
            index=matching[0] if len(matching)>=info['voices'] else available
            if index is None:index=self.cursor
            self.cursor=(index+1)%20;self.owners[index]=name
            variants=len(info['files']);previous=self.variants.get(name,-1)
            choices=[i for i in range(variants) if i!=previous] or [0]
            variant=self.rng.choice(choices);self.variants[name]=variant
            sound=self.sounds[event][variant*3+self.rng.randrange(3)]
            channel=self.pg.mixer.Channel(index)
            channel.set_volume(min(1.,.8*p.sfx/100*gain*10**(self.rng.uniform(-info['volume_offset_db'],info['volume_offset_db'])/20)))
            channel.play(sound);self.played+=1
            self.played_events[event]=self.played_events.get(event,0)+1
    def poll(self,now):
        if not self.music_enabled or not self.context or self.pg.mixer.music.get_busy():return
        if self.next_music==0:self.next_music=now
        if now<self.next_music:return
        if self.context=='menu':paths=[self.music_root/'menu/space_riddle.mp3']
        elif self.context=='opening':paths=[self.music_root/'battle/04_lost_spaceship_signal.ogg']
        else:
            paths=[self.music_root/p for p in self.music_tracks
                   if p.startswith(self.context+'/') or self.context=='upgrade' and p=='shared/planet.ogg']
        paths=[p for p in paths if p.is_file()]
        if not paths:self.music_enabled=False;return
        options=[p for p in paths if p!=self.music_path] or paths
        self.music_path=self.rng.choice(options)
        self.pg.mixer.music.load(str(self.music_path))
        # mixer.music.load resets gain on some SDL_mixer versions.
        p=self.last_state;v=p.music/100
        self.pg.mixer.music.set_volume(.8*(v*.8 if v<=.5 else .4+(v-.5)*1.2)*10**(-6/20))
        self.pg.mixer.music.play(loops=-1 if self.context in ('menu','opening') else 0,fade_ms=700)
        self.next_music=float('inf')
    def tick(self,now):
        if self.next_music==float('inf') and not self.pg.mixer.music.get_busy():self.next_music=now+1.5
        self.poll(now)
    def close(self):self.stop();self.pg.mixer.quit()

class PCAudioBridge:
    """A bounded worker keeps disk access/mixing off the UART and Tk input loop."""
    def __init__(self,bank=DEFAULT_BANK,*,music_root=MUSIC_ROOT,enabled=True,device=None):
        self.decoder=AudioDecoder();self.queue=queue.Queue(maxsize=64);self.stop_event=threading.Event()
        self.error=None;self.received=0;self.dropped=0;self.played=0;self.thread=None
        self.ready=False;self.backend=None;self.played_events={};self.processed=0;self.latency_sum=0.;self.latency_max=0.
        self.device=device or os.environ.get('USURPER_AUDIO_DEVICE') or None
        self.devices=[];self.restart_event=threading.Event();self.preview_event=threading.Event()
        self.last_received=None;self.last_packet=None;self.music_playing=False;self.track=None;self.retries=0
        if enabled:
            self.thread=threading.Thread(target=self._run,args=(bank,music_root),daemon=True,name='Usurper audio')
            self.thread.start()
    def feed(self,data):
        if not self.thread:return
        now=time.perf_counter()
        for packet in self.decoder.feed(data):
            self.received+=1
            self.last_received=now;self.last_packet=packet
            try:self.queue.put_nowait((now,packet))
            except queue.Full:
                try:self.queue.get_nowait()
                except queue.Empty:pass
                self.dropped+=1
                try:self.queue.put_nowait((now,packet))
                except queue.Full:self.dropped+=1
    def select_device(self,device=None):
        self.device=device or None;self.restart_event.set()
    def preview(self):self.preview_event.set()
    def _run(self,bank,music_root):
        while not self.stop_event.is_set():
            mixer=None;self.restart_event.clear()
            try:
                mixer=OriginalMixer(bank,music_root,device=self.device) if self.device else OriginalMixer(bank,music_root)
                self.backend=getattr(mixer,'backend',None);self.devices=getattr(mixer,'devices',[])
                self.ready=True;self.error=None;last_rx=time.perf_counter();silent=False;preview_until=0
                # A device retry resumes current settings, never replays old effects.
                if self.last_packet and self.last_received and last_rx-self.last_received<3:
                    p=self.last_packet;mixer.apply(AudioPacket(p.sequence,p.phase,p.flags,p.music,p.sfx,p.muted,()),last_rx)
                while not self.stop_event.is_set() and not self.restart_event.is_set():
                    now=time.perf_counter()
                    try:
                        stamp,p=self.queue.get(timeout=.01)
                        now=time.perf_counter();last_rx=stamp;silent=False
                        if now-stamp>.25:
                            self.dropped+=len(p.events)
                            p=AudioPacket(p.sequence,p.phase,p.flags,p.music,p.sfx,p.muted,())
                        mixer.apply(p,now)
                        self.processed+=1;latency=max(0.,time.perf_counter()-stamp)
                        self.latency_sum+=latency;self.latency_max=max(self.latency_max,latency)
                    except queue.Empty:pass
                    if self.preview_event.is_set():
                        self.preview_event.clear();mixer.apply(AudioPacket(0,0,7,50,80,False,(1,)),now);preview_until=now+2
                    if preview_until and now>=preview_until:
                        preview_until=0
                        if self.last_packet and self.last_received and now-self.last_received<3:
                            p=self.last_packet;mixer.apply(AudioPacket(p.sequence,p.phase,p.flags,p.music,p.sfx,p.muted,()),now)
                        else:mixer.stop()
                    if now-last_rx>3 and not silent and not preview_until:mixer.stop();silent=True
                    mixer.tick(now);self.played=mixer.played
                    self.played_events=dict(getattr(mixer,'played_events',{}))
                    if hasattr(mixer,'pg'):
                        self.music_playing=bool(mixer.pg.mixer.music.get_busy())
                        self.track=str(mixer.music_path.name) if mixer.music_path else None
            except Exception as exc:
                self.error=str(exc);self.retries+=1
                warnings.warn('PC audio unavailable; retrying: '+str(exc),RuntimeWarning)
            finally:
                self.ready=False;self.music_playing=False
                if mixer:
                    try:mixer.close()
                    except Exception:pass
            if self.error and not self.restart_event.is_set():self.stop_event.wait(2)
    def close(self):
        self.stop_event.set()
        if self.thread:self.thread.join(timeout=3)
    def statistics(self):
        return dict(ready=self.ready,backend=self.backend,error=self.error,device=self.device or 'system default',devices=self.devices,
                    music_playing=self.music_playing,track=self.track,retries=self.retries,received=self.received,processed=self.processed,
                    invalid=self.decoder.invalid,dropped=self.dropped,played=self.played,played_events=self.played_events,
                    host_dispatch_mean_ms=1000*self.latency_sum/max(1,self.processed),host_dispatch_max_ms=1000*self.latency_max,
                    latency_scope='Host receive to mixer submission; excludes UART wire time and device output latency')
    def __enter__(self):return self
    def __exit__(self,*args):self.close()
