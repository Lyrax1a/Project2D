#include "usurper_warden.h"
#include "usurper_fixed.h"
#include "usurper_softfloat.h"
#include <string.h>
static float distance(float x,float y){return us_fx_sqrt_float(x*x+y*y);}
static double root_double(double value){
    if(value<=0)return 0;
    uint64_t bits;memcpy(&bits,&value,8);bits=(bits>>1)+UINT64_C(0x1ff8000000000000);double result;memcpy(&result,&bits,8);
    for(unsigned i=0;i<7;++i)result=(result+value/result)*0.5;
    return result;
}
void us_warden_deploy(us_warden_field_t *field,float x,float y,const us_warden_ranks_t *ranks){
    memset(field,0,sizeof(*field));field->x=x;field->y=y;field->radius=(float)(72+10*ranks->w4);field->duration=4+2*(double)ranks->w2;field->block_dx=1;field->active=1;
}
unsigned us_warden_contains(const us_warden_field_t *field,float px,float py){return distance(px-field->x,py-field->y)<=field->radius;}
void us_warden_project(const us_warden_field_t *field,us_warden_enemy_t *enemy){
    if(!enemy->alive||enemy->excluded)return;
    float ox=enemy->x-field->x,oy=enemy->y-field->y,clearance=field->radius+(enemy->boss?28:12),range=distance(ox,oy);
    if(range>=clearance)return;
    float nx=1,ny=0;if(!(ox<0.00001f&&ox>-0.00001f&&oy<0.00001f&&oy>-0.00001f)){nx=range?ox/range:0;ny=range?oy/range:0;}
    enemy->x=us_sf_add(field->x,us_sf_mul(nx,clearance));enemy->y=us_sf_add(field->y,us_sf_mul(ny,clearance));
    float dot=enemy->vx*nx+enemy->vy*ny;
    if(dot<0){enemy->vx=us_sf_add(enemy->vx,-us_sf_mul(nx,dot));enemy->vy=us_sf_add(enemy->vy,-us_sf_mul(ny,dot));}
}
unsigned us_warden_step(us_warden_field_t *field,float px,float py,unsigned ending,const us_warden_ranks_t *ranks,
    us_warden_enemy_t *enemies,unsigned enemy_count,us_warden_shot_t *shots,unsigned shot_count,const us_warden_callbacks_t *api){
    if(!field->active)return 0;
    if(ending){field->active=0;return 0;}
    const double delta=1.0/60;field->age+=delta;field->block_flash=field->block_flash>delta?field->block_flash-delta:0;
    if(field->age>=field->duration){
        if(ranks->w7&&us_warden_contains(field,px,py))api->restore_shield(api->context,20,1);
        field->active=0;return 0;
    }
    if(ranks->zw){field->x=px;field->y=py;}
    for(unsigned i=0;i<enemy_count;++i)us_warden_project(field,&enemies[i]);
    unsigned contains=us_warden_contains(field,px,py);
    if(contains)api->restore_shield(api->context,(0.5+ranks->w8)*delta,0);
    for(unsigned i=0;i<shot_count;++i){us_warden_shot_t *shot=&shots[i];if(!shot->active||!shot->interceptible||distance(shot->x-field->x,shot->y-field->y)>field->radius)continue;
        shot->active=0;api->recycle(api->context,i);++field->interceptions;
        if((ranks->w6||ranks->zw)&&field->interceptions%(ranks->zw?2u:3u)==0)api->retaliate(api->context,field->x,field->y,ranks->zw?30u:10u,ranks->zw?2u:1u);
    }return contains;
}
us_warden_sweep_t us_warden_sweep(us_warden_field_t *field,float sx,float sy,float tx,float ty,double hull_radius,
    unsigned responds,unsigned boss,double max_health,unsigned strengthened){
    us_warden_sweep_t result={.x=tx,.y=ty};if(!field->active||field->age>=field->duration)return result;
    float mx=tx-sx,my=ty-sy,norm=mx*mx+my*my,near_x=sx,near_y=sy;
    if(norm>=1e-20f){float along=(mx*(field->x-sx)+my*(field->y-sy))/norm;
        if(along>=1){near_x=tx;near_y=ty;}else if(along>0){near_x=us_sf_add(sx,us_sf_mul(mx,along));near_y=us_sf_add(sy,us_sf_mul(my,along));}}
    double radius=field->radius+hull_radius;float ox=near_x-field->x,oy=near_y-field->y;
    if((double)(ox*ox+oy*oy)>radius*radius)return result;
    float length=distance(mx,my),dx=length?mx/length:0,dy=length?my/length:0;
    if(dx<0.00001f&&dx>-0.00001f&&dy<0.00001f&&dy>-0.00001f)return result;
    float px=sx-field->x,py=sy-field->y;double projection=px*dx+py*dy;if(projection>=0)return result;
    double radicand=projection*projection-(double)(px*px+py*py)+radius*radius;
    double travel=-projection-root_double(radicand>0?radicand:0);if(travel<0)travel=0;
    float nx=us_sf_add(us_sf_add(sx,us_sf_mul(dx,(float)travel)),-field->x),ny=us_sf_add(us_sf_add(sy,us_sf_mul(dy,(float)travel)),-field->y);
    length=distance(nx,ny);nx=length?nx/length:0;ny=length?ny/length:0;
    result.x=us_sf_add(field->x,us_sf_mul(nx,(float)radius));result.y=us_sf_add(field->y,us_sf_mul(ny,(float)radius));result.hit=1;
    if(!responds)return result;
    result.stopped=1;result.damage=boss?max_health*0.035:65;
    if(boss){if(result.damage<35)result.damage=35;else if(result.damage>420)result.damage=420;}
    if(strengthened)result.damage*=3;
    result.at_x=us_sf_add(field->x,us_sf_mul(nx,field->radius));result.at_y=us_sf_add(field->y,us_sf_mul(ny,field->radius));
    ox=result.at_x-field->x;oy=result.at_y-field->y;length=distance(ox,oy);
    field->block_dx=length?ox/length:0;field->block_dy=length?oy/length:0;field->block_flash=0.24;return result;
}
