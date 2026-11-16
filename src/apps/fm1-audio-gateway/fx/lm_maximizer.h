/* SPDX-License-Identifier: MPL-2.0
 * Original LoudMax-inspired, linked sample-peak maximizer. No plugin code,
 * binary, PCM or empirical release-curve tables are included.
 *
 * The black-box observations underpinning this implementation are described in
 * docs/DYNAMICS.md. Charging/capture is a causal approximation, not a claim of
 * exact LoudMax equivalence. State is per route, fixed-size and audio-owned.
 */
#pragma once
#define LM_LOOK 55u       /* 1.247 ms at the actual ALNK clock. */
#define LM_BUCKET 90u     /* Non-extending peak admission window, about 2.05 ms. */
#define LM_HOLD 5515u     /* Approximately 125 ms. */
#define LM_PAUSE (60u*LM_LOOK)
#define LM_FLOOR 0.45563453f
typedef struct {
    float inverse,target,slope,slew,hold_peak;
    float charge_s,charge_f,medium,history_peak,captured,long_env;
    float held,reference,previous,visible;
    uint32_t bucket,quiet,age,medium_hold,long_hold,pause,visible_hold;
    uint32_t tick,middle_age,low_age;
    uint8_t ramping,borrowed,armed,stage;
} lm_state_t;
static void lm_reset(lm_state_t *s)
{
    memset(s,0,sizeof *s);s->inverse=s->target=s->medium=s->long_env=1.0f;
}
static float lm_gain(lm_state_t *s,float raw,float upcoming)
{
    /* Peak admission never extends an existing bucket for equal/smaller input.
     * Larger peaks may preempt it. This avoids counting every sample as a
     * separate charging event, and bridges short gaps without an audio ring. */
    int admitted=raw>1.0f && ((s->bucket && raw>s->held) || (!s->bucket && raw>=s->reference*0.70710678f));
    if(admitted){
        if(!s->age || raw>s->history_peak*3.16227766f){
            s->charge_s=s->charge_f=0;s->history_peak=raw;s->age=1;s->armed=0;
            /* A larger new transient does not erase an already captured tail. */
        }
        if(s->armed && s->age>=100u*LM_LOOK && raw<=s->history_peak*3.16227766f && raw>s->captured){
            s->captured=raw;s->long_env=fm_maxf(s->long_env,s->medium);s->stage=0;s->middle_age=0;
        }
        s->history_peak=fm_maxf(s->history_peak,raw);
        s->held=s->reference=raw;s->bucket=LM_BUCKET;
    }
    if(s->age && s->age<441176u)s->age++;
    if(s->bucket){
        float drive=fm_maxf(0,s->held-LM_FLOOR);
        s->charge_s+=(drive-s->charge_s)*0.0006966904f;
        s->charge_f+=(drive-s->charge_f)*0.0027315006f;
        float charge=LM_FLOOR+0.15153f*drive+0.89648f*s->charge_s-0.04801f*s->charge_f;
        if(charge>s->medium)s->medium=charge;
        if(charge>=s->history_peak*0.94f)s->armed=1;
        if(s->captured){s->long_env=fm_maxf(s->long_env,charge);s->long_hold=LM_HOLD;}
        s->medium_hold=LM_HOLD;s->bucket--;
    }else{
        s->held=0;
        /* Leaking producer history distinguishes closely split bursts from
         * the same total loud duration separated by long quiet intervals. */
        s->charge_s-=s->charge_s*0.00011332691f;s->charge_f-=s->charge_f*0.00011332691f;
        if(s->medium_hold)s->medium_hold--;
        else if(!s->pause)s->medium=fm_maxf(1, s->medium+(LM_FLOOR-s->medium)*0.00007364335f);
        if(s->charge_s<0.01f && s->medium<=1 && !s->captured){s->age=s->armed=0;s->history_peak=0;s->charge_s=s->charge_f=0;}
    }
    /* A fresh quiet input can hold the audible gain without pausing its hidden
     * recovery. Only the higher threshold pauses the latter. Edge qualification
     * prevents constant backgrounds from rearming a hold on every sample. */
    float relative=s->history_peak>1?raw/s->history_peak:0;
    if(relative<0.01024f){if(s->low_age<3u*LM_LOOK)s->low_age++;}
    else if(s->low_age>=3u*LM_LOOK && raw<1.0f && raw>s->previous){
        if(s->stage<2 || relative>=0.01455f){s->visible=s->inverse;s->visible_hold=s->stage<2?LM_HOLD:LM_PAUSE;}
        if(relative>=0.01455f)s->pause=LM_PAUSE;
        s->low_age=0;
    }
    s->previous=raw;
    if(s->pause)s->pause--;
    if(s->long_hold)s->long_hold--;
    if(++s->tick==LM_LOOK){
        s->tick=0;s->reference=s->reference>1?s->reference*0.70710678f:0;
        /* Stage age advances independently of a quiet input's decay pause. */
        if(s->stage==1 && ++s->middle_age>=398u)s->stage=2;
        if(s->captured && !s->long_hold && !s->pause){
            float floor=fm_maxf(LM_FLOOR,0.16417916f*s->captured);
            if(!s->stage){
                s->long_env=floor+(s->long_env-floor)*0.99595768f;
                if(s->long_env<=0.3572f*s->captured){s->stage=1;s->middle_age=0;}
            }else if(s->stage==1){
                floor=fm_maxf(floor,0.95311287f);
                s->long_env=floor+(s->long_env-floor)*0.99682633f;
            }else s->long_env=fm_maxf(s->long_env*0.99829163f,
                0.95311287f+(s->long_env-0.95311287f)*0.99682633f);
            if(s->long_env<=1.00001f){s->long_env=1;s->captured=0;s->stage=0;}
        }
    }
    /* Inverse-gain lookahead ramp with retained slew for overlapping attacks.
     * A final shared stereo ceiling guard also covers startup/live edits. */
    if(upcoming>s->target+0.000001f && upcoming>s->inverse+0.000001f){
        float requested=(upcoming-s->inverse)*(0.99501706f/LM_LOOK);
        float slope=fm_maxf(requested,fm_maxf(s->slew,s->ramping?s->slope:0));
        s->borrowed=slope>requested*1.001f;s->slope=slope;
        if(!s->borrowed)s->slew=slope;
        s->target=upcoming;s->ramping=1;s->hold_peak=upcoming;
    }
    if(s->ramping || upcoming>=s->inverse-0.000001f)s->quiet=0;
    else if(s->quiet<LM_HOLD+1u)s->quiet++;
    if(s->ramping){
        s->inverse=fm_minf(s->target,s->inverse+s->slope);
        if(s->inverse>=s->target-0.000001f){s->ramping=0;if(s->borrowed)s->slew=s->slope*0.748331f;}
    }else if(upcoming<s->inverse){
        float rel=s->hold_peak>1?upcoming/s->hold_peak:0;
        unsigned hold=rel>=0.015625f?2u*(LM_LOOK+1u):rel>=0.01024f?20u*(LM_LOOK+1u):LM_HOLD;
        float target=fm_maxf(1,fm_maxf(upcoming,fm_maxf(s->medium,s->long_env)));
        if(s->quiet>hold)s->inverse+=(target-s->inverse)*0.010451474f;
        s->target=1;
    }
    if(s->visible_hold){s->visible_hold--;s->inverse=fm_maxf(s->inverse,s->visible);}
    s->inverse=fm_maxf(1,s->inverse);
    return 1.0f/s->inverse;
}
