#ifndef USURPER_LINE_MESH_CACHE_H
#define USURPER_LINE_MESH_CACHE_H
#include <string.h>
#include "usurper_polyline.h"
/* Cache only the two-point AA mesh in local coordinates. Every input bit is
 * part of the key; owner transform, camera, paint and triangle order stay live.
 * Two-point strips contain at most nine vertices, unlike general polylines. */
static unsigned us_line_mesh_cached(const us_flame_vertex_t *points,float width,
                                    us_polyline_mesh_t *mesh){
    typedef struct {uint32_t key[5],valid,count,loop,counts[3];
                    us_polyline_vertex_t vertices[3][9];} entry_t;
    static entry_t cache[32][4];static unsigned cursors[32];
    uint32_t key[5],hash=2166136261u;
    memcpy(key,points,16);memcpy(key+4,&width,4);
    for(unsigned i=0;i<5;++i)hash=(hash^key[i])*16777619u;
    unsigned set=(hash^(hash>>16))&31u;
    for(unsigned way=0;way<4;++way){entry_t *entry=&cache[set][way];
        if(entry->valid&&entry->key[0]==key[0]&&entry->key[1]==key[1]&&
           entry->key[2]==key[2]&&entry->key[3]==key[3]&&entry->key[4]==key[4]){
            mesh->count=entry->count;mesh->loop=entry->loop;
            for(unsigned part=0;part<mesh->count;++part){
                mesh->strips[part].count=entry->counts[part];
                memcpy(mesh->strips[part].vertices,entry->vertices[part],entry->counts[part]*sizeof(us_polyline_vertex_t));
            }
            return 1;
        }
    }
    if(!us_polyline_mesh(points,2,width,1,mesh))return 0;
    for(unsigned part=0;part<mesh->count;++part)if(mesh->strips[part].count>9)return 1;
    entry_t *entry=&cache[set][cursors[set]++&3u];
    memcpy(entry->key,key,sizeof key);entry->count=mesh->count;entry->loop=mesh->loop;
    for(unsigned part=0;part<mesh->count;++part){
        entry->counts[part]=mesh->strips[part].count;
        memcpy(entry->vertices[part],mesh->strips[part].vertices,entry->counts[part]*sizeof(us_polyline_vertex_t));
    }
    entry->valid=1;return 1;
}
#endif
