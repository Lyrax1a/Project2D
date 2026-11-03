#include "usurper_controller.h"
#include "usurper_atan.h"
#include <string.h>
static float clamp(float value,float low,float high){return value<low?low:value>high?high:value;}
static double positive(double value){return value<0?0:value;}
void us_controller_birth(int32_t pxq,int32_t pyq,int32_t cxq,int32_t cyq,int32_t *xq,int32_t *yq,unsigned preferred){
    float px=pxq/65536.0f,py=pyq/65536.0f,cx=cxq/65536.0f,cy=cyq/65536.0f,half_x=320.0f*0.36f;
    float left=cx-half_x,top=cy-48,right=left+half_x*2,bottom=top+96;
    if(left<132)left=132;
    if(top<132)top=132;
    if(right>668)right=668;
    if(bottom>348)bottom=348;
    float center_x=left+(right-left)*0.5f,center_y=top+(bottom-top)*0.5f,best_x=center_x,best_y=center_y;
    float dx=best_x-px,dy=best_y-py;double score=us_fx_sqrt_float(dx*dx+dy*dy);if(score>110)score=110;
    if(preferred){best_x=clamp(*xq/65536.0f,left,right);best_y=clamp(*yq/65536.0f,top,bottom);}
    else for(unsigned i=0;i<4;++i){static const float ax[4]={1,-1,0,0},ay[4]={0,0,1,-1};float x=clamp(center_x+ax[i]*110,left,right),y=clamp(center_y+ay[i]*110,top,bottom);
        dx=x-px;dy=y-py;double range=us_fx_sqrt_float(dx*dx+dy*dy);if(range>110)range=110;dx=x-center_x;dy=y-center_y;double value=range-us_fx_sqrt_float(dx*dx+dy*dy)*0.15;
        if(value>score){score=value;best_x=x;best_y=y;}
    }*xq=(int32_t)((double)best_x*65536);*yq=(int32_t)((double)best_y*65536);
}
static void direction(float dx,float dy,float *x,float *y){
    float distance=us_fx_sqrt_float(dx*dx+dy*dy);*x=distance?dx/distance:0;*y=distance?dy/distance:0;
}
static void rotate(float x,float y,float angle,float *rx,float *ry){
    float cx,sy;us_fx_direction_float(angle,&cx,&sy);*rx=cx*x-sy*y;*ry=sy*x+cx*y;
}
void us_controller_init(us_controller_t *b,int32_t x,int32_t y,int32_t px,int32_t py){
    memset(b,0,sizeof(*b));b->x=x/65536.0f;b->y=y/65536.0f;
    b->rotation=us_atan_float((px-x)/65536.0f,(py-y)/65536.0f);
    us_fx_direction_float(b->rotation,&b->locked_x,&b->locked_y);b->shot_clock=0.6;
    b->hp=b->max_hp=14000;b->shield=1500;b->active=1;b->defense_layer=1;b->launch_clock=1;b->activating=-1;
}
static void forms(us_controller_t *b,float px,float py,us_controller_output_t *out){
    const double delta=1.0/60.0;b->form_clock+=delta;b->shot_clock-=delta;
    float ax,ay;direction(px-b->x,py-b->y,&ax,&ay);
    if(b->form_clock>=5.4){b->form_clock=0;b->form=(b->form+1)%3;b->shot_clock=0.6;b->point_x=b->point_y=0;}
    if(b->form_clock<0.6){b->locked_x=ax;b->locked_y=ay;b->rotation=us_fx_turn_float(b->rotation,us_atan_float(ax,ay),delta*3);return;}
    if(b->form==0){
        double value=b->form_clock-0.6,beat=value-(int)(value/2.05)*2.05;
        if(beat<0.42){b->locked_x=ax;b->locked_y=ay;}
        if(beat>=0.65&&beat<1.35){
            b->vx=b->locked_x*540;b->vy=b->locked_y*540;
            if(b->guard_sweep&&b->guard_sweep(b->guard_context,b->x,b->y,b->x+b->vx*(float)delta,b->y+b->vy*(float)delta,&b->x,&b->y))return;
            float sx=clamp(b->x,38,762),sy=clamp(b->y,38,442),mx=b->vx*(float)delta,my=b->vy*(float)delta,travel=1;
            if(mx>0){float t=(762-sx)/mx;if(t<travel)travel=t;}else if(mx<0){float t=(38-sx)/mx;if(t<travel)travel=t;}
            if(my>0){float t=(442-sy)/my;if(t<travel)travel=t;}else if(my<0){float t=(38-sy)/my;if(t<travel)travel=t;}
            b->x=sx+mx*travel;b->y=sy+my*travel;
            if(travel<1){b->vx=b->vy=0;b->form_clock=0.6+((int)(value/2.05)+1)*2.05;b->recovery=0.85;return;}
            float dx=px-b->x,dy=py-b->y;if(us_fx_sqrt_float(dx*dx+dy*dy)<38)out->charge_damage=26;
        }else b->vx=b->vy=0;
        b->rotation=us_fx_turn_float(b->rotation,us_atan_float(b->locked_x,b->locked_y),delta*6);
    }else if(b->form==1){
        float dx=px-b->x,dy=py-b->y,distance=us_fx_sqrt_float(dx*dx+dy*dy),pace=clamp((float)((distance-155)*0.65),-50,85);
        b->vx=ax*pace;b->vy=ay*pace;b->x+=b->vx*(float)delta;b->y+=b->vy*(float)delta;
        b->rotation=us_fx_turn_float(b->rotation,us_atan_float(ax,ay),delta*2);
        if(b->shot_clock<=0){
            b->shot_clock=b->form_clock>3?0.36:0.52;unsigned count=b->form_clock>3?3:7;
            for(unsigned i=0;i<count;++i){
                float rx,ry;rotate(ax,ay,(float)((i-(count-1)*0.5)*0.16),&rx,&ry);
                out->shots[i].x=b->x+ax*28;out->shots[i].y=b->y+ay*28;
                out->shots[i].dx=rx;out->shots[i].dy=ry;out->shots[i].damage=14;out->shots[i].speed=225;
            }out->count=count;
        }
    }else{
        b->vx=b->vy=0;
        if(!b->point_x&&!b->point_y){b->point_x=clamp(px+ay*130,45,755);b->point_y=clamp(py-ax*130,45,435);}
        if(b->form_clock>=1.35&&b->form_clock-delta<1.35){b->x=b->point_x;b->y=b->point_y;direction(px-b->x,py-b->y,&b->locked_x,&b->locked_y);}
        if(b->form_clock>=1.65&&b->form_clock<4.6){
            float cross=b->locked_x*ay-b->locked_y*ax,dot=b->locked_x*ax+b->locked_y*ay;
            double turn=us_atan_float(dot,cross),limit=0.9*delta;if(turn>limit)turn=limit;else if(turn<-limit)turn=-limit;
            float rx,ry;rotate(b->locked_x,b->locked_y,(float)turn,&rx,&ry);b->locked_x=rx;b->locked_y=ry;
            if(b->shot_clock<=0){
                b->shot_clock=0.24;float dx=px-b->x,dy=py-b->y;
                double along=(double)(dx*b->locked_x+dy*b->locked_y)-10;
                float across=dx*b->locked_y-dy*b->locked_x;
                if(along>=0&&along<=220&&(across<0?-across:across)<=7+us_fx_sin_double(along/220*3.14159265358979323846)*42)out->flame_damage=16;
            }
        }
        b->rotation=us_fx_turn_float(b->rotation,us_atan_float(b->locked_x,b->locked_y),delta*4);
    }
}
void us_controller_forms_step(us_controller_t *b,int32_t px,int32_t py,us_controller_output_t *out){
    memset(out,0,sizeof(*out));const double delta=1.0/60.0;b->elapsed+=delta;b->attack_clock+=delta;
    if(b->recovery>0){b->recovery-=delta;return;}
    forms(b,px/65536.0f,py/65536.0f,out);b->x=clamp(b->x,38,762);b->y=clamp(b->y,38,442);
}
unsigned us_controller_living_nests(const us_controller_t *b){
    unsigned count=0;for(unsigned i=0;i<6;++i)if(b->nests[i].active)++count;return count;
}
void us_controller_hit(us_controller_t *b,double damage,unsigned nursery){
    if(!b->active||b->finishing)return;
    if(b->shield>0){b->shield=positive(b->shield-damage);b->flash=0.1;if(b->shield<=0)b->recovery=0.8;return;}
    if(damage<=0)return;
    if(b->phase==1&&!nursery)damage/=1.0+0.25*us_controller_living_nests(b);
    double before=b->hp,floor=b->phase==0?b->max_hp*0.75:b->phase==1?b->max_hp*0.5:0,dealt=damage*1.6;
    if(dealt>before-floor)dealt=before-floor;
    b->hp=positive(before-positive(dealt));b->damage_received+=before-b->hp;b->flash=0.12;
    if(before>b->hp&&b->core_hit_clock<=0)b->core_hit_clock=0.08;
    if(b->hp<=0){b->finishing=1;return;}
    if(b->phase<2&&b->hp<=floor+0.001){
        ++b->phase;b->phase_started=b->elapsed;b->attack_clock=0;b->recovery=1;b->defense_layer=3;b->form_clock=0;
        b->pending_flags|=US_CONTROLLER_CLEAR_ARRIVALS|US_CONTROLLER_CLEAR_ENEMIES|US_CONTROLLER_SUPPLY_NEXT;
        if(b->phase==2)b->pending_wave_begin=1;
    }
}
static void remove_nest(us_controller_t *b,unsigned slot,unsigned destroyed){
    us_controller_nest_t *n=&b->nests[slot];b->cooldowns[slot]=7;
    if(b->activating==(int)slot){b->activating=-1;b->launch_clock=0.65;}
    if(destroyed){
        if(!n->hatched){
            ++b->destroyed_bubbles;
            if(b->pending_bubble_count<6){unsigned i=b->pending_bubble_count++;b->pending_bubbles[i].x=n->x;b->pending_bubbles[i].y=n->y;b->pending_bubbles[i].species=n->species;}
        }else if(n->species==US_CONTROLLER_CARRIER)++b->destroyed_carriers;
        if(b->phase==1&&b->active)us_controller_hit(b,b->max_hp*0.03/0.9,1);
    }n->active=0;
}
void us_controller_hit_nest(us_controller_t *b,unsigned slot,double damage,unsigned rail){
    if(slot>=6||!b->nests[slot].active)return;
    us_controller_nest_t *n=&b->nests[slot];if(damage>0){n->hp=positive(n->hp-damage*(n->hatched&&n->species==US_CONTROLLER_CARRIER&&rail?2:1));if(n->hp<=0)remove_nest(b,slot,1);}n->flash=0.12;n->hit_kick=1;
}
void us_controller_hit_fighter(us_controller_t *b,unsigned slot,double damage){
    if(slot>=12||!b->fighters[slot].present||b->fighters[slot].queued||damage<=0)return;
    us_controller_fighter_t *f=&b->fighters[slot];f->hp=positive(f->hp-damage);if(f->hp<=0)f->queued=1;f->flash=0.085;f->hit_kick=1;
}
static void append_shot(us_controller_output_t *out,float x,float y,float dx,float dy,unsigned damage,unsigned speed){
    if(out->count>=32)return;
    unsigned i=out->count++;out->shots[i].x=x;out->shots[i].y=y;out->shots[i].dx=dx;out->shots[i].dy=dy;out->shots[i].damage=damage;out->shots[i].speed=speed;
}
static void append_ordnance(us_controller_output_t *out,float x,float y,float dx,float dy,unsigned damage,unsigned speed,unsigned mode,double lifetime){
    if(out->ordnance_count>=18)return;
    unsigned i=out->ordnance_count++;out->ordnance[i].x=x;out->ordnance[i].y=y;out->ordnance[i].dx=dx;out->ordnance[i].dy=dy;
    out->ordnance[i].damage=damage;out->ordnance[i].speed=speed;out->ordnance[i].mode=mode;out->ordnance[i].lifetime=lifetime;
}
static unsigned fighter_group_count(const us_controller_t *b,uint32_t carrier,unsigned match,unsigned live_only){
    unsigned count=0;for(unsigned i=0;i<12;++i)if(b->fighters[i].present&&(!live_only||!b->fighters[i].queued)&&(!match||b->fighters[i].carrier_serial==carrier))++count;return count;
}
static void launch_fighter(us_controller_t *b,uint32_t carrier,float x,float y,float rotation,unsigned wave){
    for(unsigned i=0;i<12;++i)if(!b->fighters[i].present){
        us_controller_fighter_t *f=&b->fighters[i];memset(f,0,sizeof(*f));f->present=1;f->capture_immune=1;f->serial=++b->next_serial;f->carrier_serial=carrier;f->x=x;f->y=y;f->rotation=rotation;f->shot_clock=0.8;f->hp=24;f->wave_index=wave;
        b->fighter_order[b->fighter_count++]=i;return;
    }
}
static void spawn_nest(us_controller_t *b,unsigned slot){
    static const unsigned brood[6]={US_CONTROLLER_DART,US_CONTROLLER_GUNNER,US_CONTROLLER_CARRIER,US_CONTROLLER_DART,US_CONTROLLER_GUNNER,US_CONTROLLER_DART};
    unsigned species=brood[(slot+b->generation)%6];if(slot==5)++b->generation;
    unsigned carriers=0;for(unsigned i=0;i<6;++i)if(b->nests[i].active&&b->nests[i].species==US_CONTROLLER_CARRIER)++carriers;
    if(species==US_CONTROLLER_CARRIER&&carriers>=2)species=US_CONTROLLER_GUNNER;
    us_controller_nest_t *n=&b->nests[slot];memset(n,0,sizeof(*n));n->active=1;n->capture_immune=1;n->species=species;n->hp=n->max_hp=160;
    n->duration=5.5+slot*0.35;n->heading_x=1;n->drone_clock=1;n->serial=++b->next_serial;b->nest_order[b->nest_count++]=slot;
}
static void army(us_controller_t *b,float px,float py){
    const double delta=1.0/60.0;float ax,ay,dx=px-b->x,dy=py-b->y;direction(dx,dy,&ax,&ay);
    float pace=clamp((float)((us_fx_sqrt_float(dx*dx+dy*dy)-190)*0.5),-45,55);
    b->vx=ax*pace;b->vy=ay*pace;b->x+=b->vx*(float)delta;b->y+=b->vy*(float)delta;b->rotation=us_fx_turn_float(b->rotation,us_atan_float(ax,ay),delta*2);
    unsigned count=0;for(unsigned i=0;i<b->nest_count;++i)if(b->nests[b->nest_order[i]].active)b->nest_order[count++]=b->nest_order[i];b->nest_count=count;
    for(unsigned i=0;i<6;++i){b->cooldowns[i]-=delta;if(b->cooldowns[i]<=0&&!b->nests[i].active)spawn_nest(b,i);}
    for(unsigned i=0;i<b->nest_count;++i){unsigned slot=b->nest_order[i];us_controller_nest_t *n=&b->nests[slot];
        float hx,hy;us_fx_direction_float((float)(slot*6.28318530717958647692/6+(b->elapsed-b->phase_started)*0.12),&hx,&hy);
        n->x=clamp(b->x+hx*116,24,776);n->y=clamp(b->y+hy*116,24,456);
        if(b->activating!=(int)slot)n->rotation=us_atan_float(px-n->x,py-n->y);
    }b->launch_clock-=delta;
    if(b->activating>=0&&b->nests[b->activating].active){if(b->nests[b->activating].fire_left<=0){b->activating=-1;b->launch_clock=0.65;}}
    else if(b->launch_clock<=0)for(unsigned i=0;i<b->nest_count;++i){unsigned slot=b->nest_order[i];us_controller_nest_t *n=&b->nests[slot];
        if(n->hatched&&n->species!=US_CONTROLLER_CARRIER&&!n->firing){
            b->activating=(int)slot;n->firing=1;n->destination_x=px;n->destination_y=py;direction(px-n->x,py-n->y,&n->heading_x,&n->heading_y);
            n->fire_age=0;n->fire_left=n->species==US_CONTROLLER_DART?1.2:2.6;n->rotation=us_atan_float(n->heading_x,n->heading_y);++b->activated_count;break;
        }
    }
}
static void nests_step(us_controller_t *b,float px,float py,us_controller_output_t *out){
    if(!b->active)return;
    const double delta=1.0/60.0;
    for(unsigned i=0;i<b->nest_count;++i){unsigned slot=b->nest_order[i];us_controller_nest_t *n=&b->nests[slot];if(!n->active)continue;
        n->flash=positive(n->flash-delta);n->hit_kick=positive(n->hit_kick-delta*11);n->age+=delta;
        if(!n->hatched&&n->age>=n->duration){n->hatched=1;++b->hatched_count;if(n->species==US_CONTROLLER_CARRIER){double ratio=n->hp/n->max_hp;n->hp=n->max_hp=230;n->hp*=ratio;}}
        if(n->hatched&&n->species==US_CONTROLLER_CARRIER){n->drone_clock-=delta;if(n->drone_clock<=0){n->drone_clock=3;
            for(unsigned side=0;side<2;++side){if(fighter_group_count(b,0,0,0)>=12||fighter_group_count(b,n->serial,1,0)>=4)continue;
                float sign=side?1.0f:-1.0f;launch_fighter(b,n->serial,n->x+n->heading_y*sign*14,n->y-n->heading_x*sign*14,us_atan_float(px-n->x,py-n->y),0);
            }
        }}
        if(!n->firing)continue;
        n->fire_age+=delta;n->fire_left-=delta;
        if(n->fire_age<0.35){n->destination_x=px;n->destination_y=py;direction(px-n->x,py-n->y,&n->heading_x,&n->heading_y);n->rotation=us_atan_float(n->heading_x,n->heading_y);}
        if(n->fire_age>=0.75){n->shot_clock-=delta;unsigned limit=n->species==US_CONTROLLER_DART?3:12;
            if(n->shot_clock<=0&&n->volleys<limit){n->shot_clock=n->species==US_CONTROLLER_DART?0.12:0.14;++n->volleys;
                if(n->species==US_CONTROLLER_DART)append_ordnance(out,n->x,n->y,n->heading_x,n->heading_y,18,680,1,1.3);
                else for(int side=-1;side<=1;++side){float rx,ry;rotate(n->heading_x,n->heading_y,(float)(side*0.075),&rx,&ry);append_ordnance(out,n->x,n->y,rx,ry,10,310,2,4);}
            }
        }if(n->fire_left<=0)remove_nest(b,slot,0);
    }
}
static void fighters_step(us_controller_t *b,float px,float py,us_controller_output_t *out){
    const double delta=1.0/60.0;
    for(unsigned i=0;i<b->fighter_count;++i){us_controller_fighter_t *f=&b->fighters[b->fighter_order[i]];if(f->queued)continue;
        unsigned carrier_alive=!f->carrier_serial?b->active:0;for(unsigned n=0;n<6;++n)if(b->nests[n].active&&b->nests[n].serial==f->carrier_serial)carrier_alive=1;
        if(!carrier_alive){f->queued=1;continue;}f->age+=delta;if(f->age>10){f->queued=1;continue;}
        f->flash=positive(f->flash-delta);f->hit_kick=positive(f->hit_kick-delta*11);
        float ax,ay;direction(px-f->x,py-f->y,&ax,&ay);f->rotation=us_fx_turn_float(f->rotation,us_atan_float(ax,ay),2.5*delta);
        float hx,hy;us_fx_direction_float(f->rotation,&hx,&hy);f->vx=hx*105;f->vy=hy*105;f->x+=f->vx*(float)delta;f->y+=f->vy*(float)delta;f->shot_clock-=delta;
        if(f->shot_clock<=0&&(hx*ax+hy*ay)>0.8){f->shot_clock=1.15;append_shot(out,f->x,f->y,hx,hy,7,210);}
    }
}
unsigned us_controller_wave_slots(const us_controller_t *b,unsigned external,unsigned scheduled){
    unsigned count=external+scheduled;for(unsigned i=0;i<12;++i)if(b->fighters[i].present&&!b->fighters[i].queued&&!b->fighters[i].carrier_serial)++count;return count>=36?0:36-count;
}
unsigned us_controller_wave_roster(const us_controller_t *b,unsigned external,unsigned scheduled,unsigned carriers){
    if(!b->waves.started)return 0;
    unsigned count=b->waves.index%5==1?8:6;
    if(b->waves.index%5==4)count=carriers>=6?0:6-carriers>2?2:6-carriers;
    unsigned slots=us_controller_wave_slots(b,external,scheduled);return count>slots?slots:count;
}
static void retire_previous(us_controller_t *b){
    for(unsigned i=0;i<12;++i)if(b->fighters[i].present&&!b->fighters[i].carrier_serial&&b->fighters[i].wave_index<b->waves.index)b->fighters[i].queued=1;
    b->waves.retiring=1;
}
static void announce(us_controller_t *b,float px,float py){
    b->waves.clock=b->waves.age=b->waves.hit_clock=0;b->waves.volleys=0;b->waves.retiring=0;
    direction(px-b->x,py-b->y,&b->locked_x,&b->locked_y);b->flash=0.18;b->pending_flags|=US_CONTROLLER_CLEAR_ARRIVALS;
}
static void waves_step(us_controller_t *b,float px,float py,float pvx,float pvy,float max_speed,unsigned external,unsigned scheduled,unsigned heavy_bombs,us_controller_output_t *out){
    us_controller_waves_t *w=&b->waves;if(!w->started)return;const double delta=1.0/60.0;w->clock+=delta;
    if(w->clock>=14){retire_previous(b);++w->index;announce(b,px,py);}
    if(w->index>0&&w->clock>=4&&!w->retiring)retire_previous(b);
    w->age+=delta;w->hit_clock=positive(w->hit_clock-delta);float ax,ay,dx=px-b->x,dy=py-b->y;direction(dx,dy,&ax,&ay);float distance=us_fx_sqrt_float(dx*dx+dy*dy);
    switch(w->index%5){
    case 0:{
        float pace=clamp((distance-115)*2,-110,150),hx=ay*145+ax*pace,hy=-ax*145+ay*pace,gain=0.09516258537769317627f;
        b->vx=b->vx+(hx-b->vx)*gain;b->vy=b->vy+(hy-b->vy)*gain;b->x+=b->vx*(float)delta;b->y+=b->vy*(float)delta;b->rotation=us_fx_turn_float(b->rotation,us_atan_float(ax,ay),delta*5);
        double beat=w->age-(int)(w->age/3.2)*3.2;
        if(beat>=0.85&&beat<2.25&&w->hit_clock<=0)for(unsigned slot=0;slot<2;++slot){float bx,by;us_fx_direction_float((float)(w->age*4.5+slot*3.14159265358979323846),&bx,&by);
            bx=b->x+bx*82-px;by=b->y+by*82-py;if(us_fx_sqrt_float(bx*bx+by*by)<17){out->blade_damage+=16;w->hit_clock=0.3;}}
        break;}
    case 1:{
        float pace=clamp(distance-175,-65,95);b->vx=ay*75+ax*pace;b->vy=-ax*75+ay*pace;b->x+=b->vx*(float)delta;b->y+=b->vy*(float)delta;
        if(w->age>=2.8){w->age-=2.8;w->volleys=0;}direction(px-b->x,py-b->y,&b->locked_x,&b->locked_y);b->rotation=us_fx_turn_float(b->rotation,us_atan_float(b->locked_x,b->locked_y),delta*7);
        if(w->age>=0.8+w->volleys*0.13&&w->volleys<3){append_ordnance(out,b->x+b->locked_x*32,b->y+b->locked_y*32,b->locked_x,b->locked_y,18,680,1,1.3);++w->volleys;}break;}
    case 2:{
        float pace=clamp((distance-185)*1.5f,-75,80);b->vx=ax*pace;b->vy=ay*pace;b->x+=b->vx*(float)delta;b->y+=b->vy*(float)delta;
        if(w->age>=3.6){w->age-=3.6;w->volleys=0;}if(w->age<0.45){b->locked_x=ax;b->locked_y=ay;}b->rotation=us_fx_turn_float(b->rotation,us_atan_float(b->locked_x,b->locked_y),delta*6);
        if(w->age>=0.75+w->volleys*0.18&&w->volleys<10){for(int side=-1;side<=1;++side){float rx,ry;rotate(b->locked_x,b->locked_y,(float)(side*0.12),&rx,&ry);append_ordnance(out,b->x+b->locked_x*32,b->y+b->locked_y*32,rx,ry,10,250,2,3.5);}++w->volleys;}break;}
    case 3:{
        float pace=clamp((float)((distance-200)*1.4),-105,75);b->vx=ax*pace+ay*45;b->vy=ay*pace-ax*45;b->x+=b->vx*(float)delta;b->y+=b->vy*(float)delta;b->rotation=us_fx_turn_float(b->rotation,us_atan_float(ax,ay),delta*4);
        if(w->age>=2.6){w->age-=2.6;w->volleys=0;}if(w->age>=0.75&&!w->volleys){w->volleys=1;if(heavy_bombs<6){
            float length=us_fx_sqrt_float(pvx*pvx+pvy*pvy);if(length>max_speed){pvx=pvx/length*max_speed;pvy=pvy/length*max_speed;}
            float lx=clamp(px+pvx*1.25f,65,735),ly=clamp(py+pvy*1.25f,65,415),hx,hy;direction(lx-b->x,ly-b->y,&hx,&hy);
            float ox=b->x+hx*34,oy=b->y+hy*34;out->bomb_count=1;out->bombs[0].x=ox;out->bombs[0].y=oy;
            dx=lx-ox;dy=ly-oy;out->bombs[0].speed=us_fx_sqrt_float(dx*dx+dy*dy)/1.8;direction(dx,dy,&hx,&hy);
            out->bombs[0].landing_x=ox+(hx*(float)out->bombs[0].speed)*1.8f;out->bombs[0].landing_y=oy+(hy*(float)out->bombs[0].speed)*1.8f;
        }}break;}
    default:{
        float pace=clamp((float)((distance-220)*0.8),-90,35);b->vx=ax*pace;b->vy=ay*pace;b->x+=b->vx*(float)delta;b->y+=b->vy*(float)delta;b->rotation=us_fx_turn_float(b->rotation,us_atan_float(ax,ay),delta*2.5);
        if(w->age>=3){w->age-=3;w->volleys=0;}if(w->age>=0.9&&!w->volleys){w->volleys=1;unsigned active=fighter_group_count(b,0,0,1),slots=us_controller_wave_slots(b,external,scheduled),count=active>=6?0:6-active;
            if(count>slots)count=slots;
            if(count>2)count=2;
            for(unsigned side=0;side<count;++side){float rx,ry,sign=side?1.0f:-1.0f;rotate(38,sign*22,b->rotation,&rx,&ry);launch_fighter(b,0,b->x+rx,b->y+ry,(float)(b->rotation+sign*0.18),w->index);}
        }break;}
    }
}
void us_controller_step(us_controller_t *b,int32_t pxq,int32_t pyq,int32_t pvxq,int32_t pvyq,float max_speed,unsigned external,unsigned scheduled,unsigned heavy_bombs,us_controller_output_t *out){
    memset(out,0,sizeof(*out));if(!b->active)return;float px=pxq/65536.0f,py=pyq/65536.0f;const double delta=1.0/60.0;
    if(!b->finishing){b->flash=positive(b->flash-delta);b->elapsed+=delta;b->attack_clock+=delta;b->core_hit_clock=positive(b->core_hit_clock-delta);
        if(b->recovery>0)b->recovery-=delta;
        else{if(b->phase==0)forms(b,px,py,out);else if(b->phase==1)army(b,px,py);else waves_step(b,px,py,pvxq/65536.0f,pvyq/65536.0f,max_speed,external,scheduled,heavy_bombs,out);
            b->x=clamp(b->x,38,762);b->y=clamp(b->y,38,442);}
    }nests_step(b,px,py,out);fighters_step(b,px,py,out);
}
void us_controller_finish(us_controller_t *b,int32_t pxq,int32_t pyq,us_controller_output_t *out){
    if(b->pending_wave_begin){
        for(unsigned i=0;i<6;++i)b->nests[i].active=0;
        b->nest_count=0;b->activating=-1;
        for(unsigned i=0;i<12;++i)if(b->fighters[i].present&&b->fighters[i].carrier_serial)b->fighters[i].queued=1;
        b->pending_flags|=US_CONTROLLER_CLEAR_SHOTS;b->pending_wave_begin=0;b->waves.started=1;announce(b,pxq/65536.0f,pyq/65536.0f);
    }
    unsigned count=0;for(unsigned i=0;i<b->fighter_count;++i){unsigned slot=b->fighter_order[i];if(b->fighters[slot].queued)b->fighters[slot].present=0;else b->fighter_order[count++]=slot;}b->fighter_count=count;
    if(b->finishing){b->active=0;b->vx=b->vy=0;}
    out->flags=b->pending_flags;b->pending_flags=0;out->bubble_count=b->pending_bubble_count;b->pending_bubble_count=0;
    for(unsigned i=0;i<out->bubble_count;++i){out->bubbles[i].x=b->pending_bubbles[i].x;out->bubbles[i].y=b->pending_bubbles[i].y;out->bubbles[i].species=b->pending_bubbles[i].species;}
}
