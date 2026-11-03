/* Intentionally included twice: exact shared scan conversion with bounded
 * 32-bit edges or unrestricted 64-bit edges. Do not add an include guard.
 * The caller validates the narrow coordinate extent before entering here. */
static void US_TRIANGLE_RASTER(const vertex_t v[3],US_TRIANGLE_INT area,
    int left,int right,int top,int bottom,
    void (*span)(void *,int,int,unsigned,float),
    void (*alpha_span)(void *,int,int,unsigned,unsigned),float paint,void *context){
    unsigned inside[3]={inclusive(v[1],v[2]),inclusive(v[2],v[0]),inclusive(v[0],v[1])};
    US_TRIANGLE_INT row[3],step[3],down[3];
    for(unsigned i=0;i<3;++i){vertex_t a=v[(i+1)%3],b=v[(i+2)%3];
        row[i]=(US_TRIANGLE_INT)(b.x-a.x)*(top*256+128-a.y)-(US_TRIANGLE_INT)(b.y-a.y)*(left*256+128-a.x);step[i]=-(US_TRIANGLE_INT)(b.y-a.y)*256;down[i]=(US_TRIANGLE_INT)(b.x-a.x)*256;}
    unsigned binary=1,opaque=1,transparent=1;
    for(unsigned i=0;i<3;++i){binary&=v[i].alpha==0||v[i].alpha==1;opaque&=v[i].alpha==1;transparent&=v[i].alpha==0;}
    if(transparent)return;
    /* Opaque coverage applies one constant paint value to every row. Resolve
     * it once, and do not prepare unused division/coefficient machinery. */
    unsigned opaque_alpha=opaque&&alpha_span?us_triangle_paint_channel(paint,1):0;
    us_triangle_alpha_t alpha_math;us_triangle_alpha_prepare(&alpha_math,alpha_span&&binary&&!opaque?area:0,paint);
    us_sf_divisor_t divisor={0};
    unsigned prepared=binary&&!opaque&&!alpha_math.valid&&area<=0x1000000;
    if(prepared)us_sf_div_prepare((float)(uint32_t)area,&divisor);
    US_TRIANGLE_WALK bounds[3];
    for(unsigned i=0;i<3;++i)if(step[i]){
        US_TRIANGLE_INT threshold=inside[i]?0:1;
        bounds[i]=US_TRIANGLE_WALK_BEGIN(row[i]-threshold,down[i],step[i]>0?step[i]:-step[i]);
    }
    for(int y=top;y<bottom;++y){
        US_TRIANGLE_INT first=0,last=right-left-1;
        /* ceil((threshold-row)/step) == -floor((row-threshold)/step).
         * Advance exact quotient/remainder pairs between rows, avoiding
         * repeated division while retaining half-open pixel ownership. */
        for(unsigned i=0;i<3;++i){US_TRIANGLE_INT threshold=inside[i]?0:1;
            if(step[i]>0){US_TRIANGLE_INT limit=-bounds[i].quotient;if(limit>first)first=limit;}
            else if(step[i]<0){US_TRIANGLE_INT limit=bounds[i].quotient;if(limit<last)last=limit;}
            else if(row[i]<threshold){last=-1;break;}}
        if(first<=last){
            int start=left+(int)first,end=left+(int)last+1;
            if(opaque){if(alpha_span)alpha_span(context,start,y,(unsigned)(end-start),opaque_alpha);else span(context,start,y,(unsigned)(end-start),1);}
            else if(alpha_span&&binary&&alpha_math.valid){
                US_TRIANGLE_INT numerator=0,increment=0;
                for(unsigned i=0;i<3;++i)if(v[i].alpha==1){numerator+=row[i]+first*step[i];increment+=step[i];}
                for(int x=start;x<end;++x,numerator+=increment){
                    unsigned alpha;
                    if(numerator<=0)continue;
                    if(!us_triangle_alpha_fast(&alpha_math,numerator,&alpha)){
                        float coverage=us_triangle_binary_ratio(numerator,area);
                        if(coverage>1)coverage=1;
                        alpha=us_triangle_paint_channel(paint,coverage);
                    }
                    /* Nonzero x increments are multiples of 256, so for
                     * area<2^32 adjacent ratios differ by more than a
                     * binary32 ulp throughout [0,1]. Preserve the
                     * original coverage span boundaries, even where the final
                     * integer alpha happens to be equal. */
                    alpha_span(context,x,y,increment?1u:(unsigned)(end-x),alpha);
                    if(!increment)break;
                }
            }else{
            int run=-1;unsigned count=0;float previous=0;
            US_TRIANGLE_INT a=row[0]+first*step[0],b=row[1]+first*step[1],c=row[2]+first*step[2];
            for(int x=start;x<end;++x,a+=step[0],b+=step[1],c+=step[2]){
            float coverage;
            if(binary){US_TRIANGLE_INT numerator=(v[0].alpha==1?a:0)+(v[1].alpha==1?b:0)+(v[2].alpha==1?c:0);coverage=prepared&&numerator>=0&&numerator<=area?us_sf_div_prepared((float)(uint32_t)numerator,&divisor):us_triangle_binary_ratio(numerator,area);}
            else coverage=(float)((a*(double)v[0].alpha+b*(double)v[1].alpha+c*(double)v[2].alpha)/(double)area);
            if(coverage<0)coverage=0;else if(coverage>1)coverage=1;
            if(!coverage){if(count){if(alpha_span)alpha_span(context,run,y,count,us_triangle_paint_channel(paint,previous));else span(context,run,y,count,previous);count=0;}continue;}
            if(count&&previous==coverage&&run+(int)count==x)++count;else{if(count){if(alpha_span)alpha_span(context,run,y,count,us_triangle_paint_channel(paint,previous));else span(context,run,y,count,previous);}run=x;count=1;previous=coverage;}
            }
            if(count){if(alpha_span)alpha_span(context,run,y,count,us_triangle_paint_channel(paint,previous));else span(context,run,y,count,previous);}
            }
        }
        for(unsigned i=0;i<3;++i){row[i]+=down[i];if(step[i])US_TRIANGLE_WALK_NEXT(&bounds[i]);}
    }
}
