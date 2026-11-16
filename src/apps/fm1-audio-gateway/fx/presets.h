/* SPDX-License-Identifier: MPL-2.0
 * Foreground-only working presets and atomic committed bank. No DSP histories. */
#define GATEWAY_FX_PRESETS 8u
typedef struct { fx_band_t band[FX_BANDS]; int16_t post; uint8_t slope[FX_BANDS],reserved; } gateway_eq_preset_t;
typedef struct { int16_t input,ceiling; uint16_t release; uint8_t reserved[2]; } gateway_lim_preset_t;
typedef struct {
    uint8_t magic[3],version,selected[2],chain,enabled;
    gateway_eq_preset_t eq[GATEWAY_FX_PRESETS];
    gateway_lim_preset_t lim[GATEWAY_FX_PRESETS];
    /* v5 appends an independent LM bank to the unchanged v3/v4 prefix. */
    gateway_lim_preset_t lm[GATEWAY_FX_PRESETS];
    uint8_t lm_selected,reserved;
} gateway_fx_presets_t;
static gateway_fx_presets_t gateway_fx_presets_saved;
static gateway_eq_preset_t gateway_eq_work[GATEWAY_FX_PRESETS];
static gateway_lim_preset_t gateway_lim_work[GATEWAY_FX_PRESETS];
static gateway_lim_preset_t gateway_lm_work[GATEWAY_FX_PRESETS];
static uint8_t gateway_fx_selected[3]; /* EQ, Peak Limiter, LM-Maximizer */
static unsigned gateway_fx_preset_index(unsigned dynamics){return dynamics?1u+gateway_dynamics_mode:0u;}
static gateway_lim_preset_t *gateway_fx_dynamics_work(void){return gateway_dynamics_mode?gateway_lm_work:gateway_lim_work;}
static gateway_lim_preset_t *gateway_fx_dynamics_saved(gateway_fx_presets_t *r){return gateway_dynamics_mode?r->lm:r->lim;}
/* Dialog 1 SAVE / 2 INIT; failed attempts stay open for an explicit retry. */
static uint8_t gateway_fx_dialog,gateway_fx_save_tracking,gateway_fx_save_held;
static uint32_t gateway_fx_save_press_ms;
static void gateway_fx_eq_get(gateway_eq_preset_t *e,const fx_params_t *p)
{
    memcpy(e->band,p->band,sizeof e->band);memcpy(e->slope,p->slope,sizeof e->slope);
    e->post=p->post;e->reserved=0;
}
static void gateway_fx_lim_get(gateway_lim_preset_t *l,const fx_params_t *p)
{
    *l=(gateway_lim_preset_t){p->input,p->ceiling,p->release,{0,0}};
}
static void gateway_fx_eq_put(fx_params_t *p,const gateway_eq_preset_t *e)
{
    memcpy(p->band,e->band,sizeof p->band);memcpy(p->slope,e->slope,sizeof p->slope);
    p->post=e->post;
}
static void gateway_fx_lim_put(fx_params_t *p,const gateway_lim_preset_t *l)
{
    p->input=l->input;p->ceiling=l->ceiling;p->release=l->release;
}
static void gateway_fx_work_capture(void)
{
    gateway_fx_eq_get(&gateway_eq_work[gateway_fx_selected[0]],&gateway_fx_params);
    gateway_fx_lim_get(&gateway_fx_dynamics_work()[gateway_fx_selected[gateway_fx_preset_index(1)]],&gateway_fx_params);
}
static int gateway_fx_preset_dirty(unsigned limiter)
{
    unsigned slot=gateway_fx_selected[gateway_fx_preset_index(limiter)];
    return limiter?memcmp(&gateway_fx_dynamics_work()[slot],&gateway_fx_dynamics_saved(&gateway_fx_presets_saved)[slot],sizeof(gateway_lim_preset_t))!=0:
        memcmp(&gateway_eq_work[slot],&gateway_fx_presets_saved.eq[slot],sizeof(gateway_eq_preset_t))!=0;
}
static unsigned gateway_fx_enabled(void){return gateway_fx_params.eq | (gateway_fx_params.lim<<1);}
static void gateway_fx_metadata(gateway_fx_presets_t *r)
{
    memcpy(r->selected,gateway_fx_selected,sizeof r->selected);r->chain=gateway_fx_params.chain;
    r->lm_selected=gateway_fx_selected[2];
    r->enabled=gateway_fx_enabled();
}
static void gateway_fx_metadata_changed(void)
{
    gateway_fx_dirty=gateway_fx_params.chain!=gateway_fx_presets_saved.chain ||
        gateway_fx_enabled()!=gateway_fx_presets_saved.enabled ||
        memcmp(gateway_fx_selected,gateway_fx_presets_saved.selected,sizeof gateway_fx_presets_saved.selected)!=0 ||
        gateway_fx_selected[2]!=gateway_fx_presets_saved.lm_selected;
    gateway_fx_ms=plat_ms();
}
static int gateway_fx_write(const gateway_fx_presets_t *r)
{
    plat_cpu_work_begin();int failed=plat_store_save(OBJ_FX,r,sizeof *r);plat_cpu_work_end();
    if(!failed){gateway_fx_presets_saved=*r;gateway_fx_dirty=0;gateway_fx_notice=1;gateway_fx_retry_ms=plat_ms()-5000u;}
    else{gateway_fx_notice=2;gateway_fx_retry_ms=plat_ms();}
    return failed;
}
static int gateway_fx_presets_valid(const gateway_fx_presets_t *r)
{
    if(memcmp(r->magic,"AFX",3) || r->version!=5 || r->selected[0]>=8 || r->selected[1]>=8 || r->lm_selected>=8 || r->reserved || r->chain>1 || r->enabled>3)return 0;
    for(unsigned i=0;i<8;i++){
        fx_params_t p;fx_defaults(&p);gateway_fx_eq_put(&p,&r->eq[i]);gateway_fx_lim_put(&p,&r->lim[i]);
        if(r->eq[i].reserved || r->lim[i].reserved[0] || r->lim[i].reserved[1] || !fx_valid(&p))return 0;
        gateway_fx_lim_put(&p,&r->lm[i]);
        if(r->lm[i].reserved[0] || r->lm[i].reserved[1] || !fx_valid(&p))return 0;
    }
    return 1;
}
static int gateway_fx_presets_migrate(gateway_fx_presets_t *r)
{
    /* v3 had a bypass flag in each slot. Retain only the selected effects'
     * flags as independent metadata; parameter-only slots never recall them. */
    if(r->version==3){
        if(r->selected[0]>=8 || r->selected[1]>=8 || r->enabled)return 0;
        unsigned enabled=0;
        for(unsigned i=0;i<8;i++){
            if(r->eq[i].reserved>1 || r->lim[i].reserved[0]>1)return 0;
            if(i==r->selected[0])enabled|=r->eq[i].reserved;
            if(i==r->selected[1])enabled|=r->lim[i].reserved[0]<<1;
            r->eq[i].reserved=r->lim[i].reserved[0]=0;
        }
        r->enabled=enabled;r->version=4;
    }
    if(r->version==4){
        /* The old bank was shared: retain all saved slots and its selection
         * in both modes, then allow them to diverge independently. */
        memcpy(r->lm,r->lim,sizeof r->lm);r->lm_selected=r->selected[1];
        r->reserved=0;r->version=5;
    }
    return gateway_fx_presets_valid(r);
}
static void gateway_fx_presets_load(void)
{
    gateway_fx_presets_t *r=&gateway_fx_presets_saved;fx_defaults(&gateway_fx_params);
    memset(r,0,sizeof *r);memcpy(r->magic,"AFX",3);r->version=5;r->enabled=gateway_fx_enabled();
    for(unsigned i=0;i<8;i++){gateway_fx_eq_get(&r->eq[i],&gateway_fx_params);gateway_fx_lim_get(&r->lim[i],&gateway_fx_params);r->lm[i]=r->lim[i];}
    int size=plat_store_size(OBJ_FX);
    if(size==456 || size==(int)sizeof *r){
        gateway_fx_presets_t loaded={0};
        if(plat_store_load(OBJ_FX,&loaded,sizeof loaded)==size &&
           (size==456?(loaded.version==3 || loaded.version==4):loaded.version==5) && gateway_fx_presets_migrate(&loaded))*r=loaded;
    }else if(size==56 || size==62){
        gateway_fx_record_t old={0};
        if(plat_store_load(OBJ_FX,&old,sizeof old)==size && !memcmp(old.magic,"AFX",3) &&
           ((size==56 && old.version==1) || (size==62 && old.version==2)) && fx_valid(&old.params)){
            gateway_fx_eq_get(&r->eq[0],&old.params);gateway_fx_lim_get(&r->lim[0],&old.params);r->chain=old.params.chain;
            r->lm[0]=r->lim[0];
            r->enabled=old.params.eq | (old.params.lim<<1);
        }
    }
    memcpy(gateway_eq_work,r->eq,sizeof gateway_eq_work);memcpy(gateway_lim_work,r->lim,sizeof gateway_lim_work);
    memcpy(gateway_lm_work,r->lm,sizeof gateway_lm_work);
    memcpy(gateway_fx_selected,r->selected,sizeof r->selected);gateway_fx_selected[2]=r->lm_selected;gateway_fx_params.chain=r->chain;
    gateway_fx_params.eq=r->enabled&1u;gateway_fx_params.lim=(r->enabled>>1)&1u;
    gateway_fx_eq_put(&gateway_fx_params,&gateway_eq_work[r->selected[0]]);
    gateway_fx_lim_put(&gateway_fx_params,&gateway_fx_dynamics_work()[gateway_fx_selected[gateway_fx_preset_index(1)]]);
}
static void gateway_fx_preset_select(int delta)
{
    unsigned effect=gateway_fx_preset_index(gateway_fx_limiter),old=gateway_fx_selected[effect];
    unsigned slot=(old+delta%8+8)%8;if(slot==old)return;
    gateway_fx_selected[effect]=slot;
    fx_params_t p=gateway_fx_params;
    if(effect)gateway_fx_lim_put(&p,&gateway_fx_dynamics_work()[slot]);else gateway_fx_eq_put(&p,&gateway_eq_work[slot]);
    gateway_fx_publish(&p);gateway_fx_metadata_changed();gateway_page_redraw=1;gateway_lights();
}
static int gateway_fx_preset_confirm(void)
{
    gateway_fx_presets_t r=gateway_fx_presets_saved;unsigned effect=gateway_fx_preset_index(gateway_fx_limiter),slot=gateway_fx_selected[effect];
    fx_params_t initial;fx_defaults(&initial);gateway_fx_metadata(&r);
    gateway_lim_preset_t *saved=gateway_fx_dynamics_saved(&r),*work=gateway_fx_dynamics_work();
    if(effect){saved[slot]=work[slot];if(gateway_fx_dialog==2)gateway_fx_lim_get(&saved[slot],&initial);}
    else{r.eq[slot]=gateway_eq_work[slot];if(gateway_fx_dialog==2)gateway_fx_eq_get(&r.eq[slot],&initial);}
    if(gateway_fx_write(&r)){gateway_page_redraw=1;return 0;}
    if(effect)work[slot]=saved[slot];else gateway_eq_work[slot]=r.eq[slot];
    fx_params_t p=gateway_fx_params;
    if(effect)gateway_fx_lim_put(&p,&saved[slot]);else gateway_fx_eq_put(&p,&r.eq[slot]);
    gateway_fx_publish(&p);gateway_fx_dialog=0;gateway_page_redraw=1;gateway_lights();return 1;
}
