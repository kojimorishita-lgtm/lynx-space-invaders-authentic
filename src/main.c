/* V42 shield-display / player-shot erosion diagnostic; V41 baseline retained.
 * Based on the user's emulator-tested V40 main.c.
 * HIT_MODE 0=original 7x7 rectangle, 1=7x7 pixel mask,
 * 2=type-specific rectangle, 3=transposed mask orientation probe.
 */
#include <lynx.h>
#include <tgi.h>
#include <6502.h>
#include <peekpoke.h>
#include <joystick.h>
#include "sprites.h"
#include "hit_effect_assets.h"
/* enemy_bullet_suzy.h / enemy_bullet_compact.h (V44-era bullet
 * sprites) are kept in the tree for reference; the current
 * bullets are the generated bomb_spr in sprites_data.h. */
#include "../generated/rom_data.h" /* ROM-derived tables, generated at build time */
#include "../generated/sprites_data.h" /* bullet patterns + UFO explosion, generated at build time */

/* v50 sound engine API (sndirq.asm) */
#define SND_SHOOT     1
#define SND_KILLED    2
#define SND_EXPLOSION 3
#define SND_EXTEND    4
#define SND_MARCH1    5
#define SND_UFO       9
#define SND_UFOHIT   10
/* v50 r6: march tempo scale.  The arcade delay table counts 60 fps
 * frames; this port updates ~22 times a second in play, so the
 * unscaled table made the march crawl at ~1/3 of the arcade tempo.
 * Delays are multiplied by NUM/DEN (2/5 = x0.4).  To revert to the
 * r4/r5 cadence, set NUM and DEN equal (e.g. both 5). */
#define MARCH_TEMPO_NUM 2
#define MARCH_TEMPO_DEN 5
#define MARCH_FIRST ((ROM_MARCH_DELAY_FIRST*MARCH_TEMPO_NUM)/MARCH_TEMPO_DEN)
extern void snd_init(void);
extern void __fastcall__ snd_play(unsigned char id);
extern void snd_stop(void);
extern unsigned char snd_active;
extern unsigned char snd_cur;
static unsigned char march_timer=MARCH_FIRST; /* v50 march sequencer state */
static unsigned char march_note=0;
static unsigned char march_pending=0; /* r2: a march note that came due
    while a one-shot owned the channel is deferred, never dropped */
#ifndef V44_E_MODE
#define V44_E_MODE 0
#endif
#if V44_E_MODE > 3 || V44_E_MODE < 0
#error V44_E_MODE_must_be_0_to_3
#endif

/* Independent low-count step diagnostic. Defaults: 10 survivors, Step 3. */
#ifndef V45_START_ALIENS
#define V45_START_ALIENS 10
#endif
#ifndef V45_STEP_MODE
#define V45_STEP_MODE 3
#endif
#if V45_START_ALIENS != 1 && V45_START_ALIENS != 3 && V45_START_ALIENS != 5 && V45_START_ALIENS != 10 && V45_START_ALIENS != 55
#error V45_START_ALIENS_must_be_1_3_5_10_or_55
#endif
#if V45_STEP_MODE < 0 || V45_STEP_MODE > 3
#error V45_STEP_MODE_must_be_0_to_3
#endif
#define ALIEN_COUNT 55
#define COLUMNS 11
#define ROWS 5
#define DESCENT_STEP 5 /* r12 trial: arcade ratio (half the 10-dot row pitch) */
#define DESCENT_LIMIT 38
#define NONE 255
#define SHIP_Y 140
/* V45 color stage 1: 0=left-start, 1=also left-respawn, 2=also red palette. */
#ifndef V45_COLOR_STEP
#define V45_COLOR_STEP 2
#endif
#if V45_COLOR_STEP < 0 || V45_COLOR_STEP > 2
#error V45_COLOR_STEP_must_be_0_1_or_2
#endif
#define SHIP_START_X 2
#define SHIP_MIN_X 2
#define SHIP_MAX_X 93
#define SHOT_TOP 0
#ifndef SHOT_SPEED
#define SHOT_SPEED 4
#endif
#define SHOT_HEIGHT 3
#ifndef HIT_MODE
#define HIT_MODE 1
#endif
#if HIT_MODE < 0 || HIT_MODE > 3
#error HIT_MODE_must_be_0_1_2_or_3
#endif
/* Test controls: 1=freeze formation, 0=normal game.
 * Fixed shot lane is in portrait X coordinates; 255 means manual ship aim.
 * TEST_POSE=0 or 1 sets both walking phases for frozen tests. */
#ifndef TEST_FREEZE
#define TEST_FREEZE 0
#endif
#ifndef TEST_SHOT_X
#define TEST_SHOT_X 14
#endif
/* Auto-fire test: repeatedly fires down a fixed X lane with a short
 * cooldown. Allows reproducible moving-wave tests without pressing A. */
#ifndef TEST_AUTO_FIRE
#define TEST_AUTO_FIRE 0
#endif
#ifndef TEST_FIRE_COOLDOWN
#define TEST_FIRE_COOLDOWN 8
#endif
#ifndef TEST_POSE
#define TEST_POSE 0
#endif
#if TEST_POSE != 0 && TEST_POSE != 1
#error TEST_POSE_must_be_0_or_1
#endif
#if TEST_SHOT_X != 255 && TEST_SHOT_X > 101
#error TEST_SHOT_X_out_of_range
#endif
/* 0 = original V42 full redraw on hit/expiration.
 * 1 = only erase killed alien and update the explosion on each page. */
#ifndef HIT_DRAW_DELTA
#define HIT_DRAW_DELTA 1
#endif
#if HIT_DRAW_DELTA != 0 && HIT_DRAW_DELTA != 1
#error HIT_DRAW_DELTA_must_be_0_or_1
#endif
#ifndef START_WAIT_TICKS
#define START_WAIT_TICKS 176
#endif
#ifndef INTRO_PRE_FRAMES
#define INTRO_PRE_FRAMES 30
#endif
#ifndef INTRO_HOLD_FRAMES
#define INTRO_HOLD_FRAMES 30
#endif
/* V43 stage 0: one enemy projectile; stage 1: shield erosion;
 * stage 2: ship hit (brief hide / reappear, no lives yet). */
#ifndef ENEMY_SHOT_STAGE
#define ENEMY_SHOT_STAGE 0
#endif
#if ENEMY_SHOT_STAGE < 0 || ENEMY_SHOT_STAGE > 2
#error ENEMY_SHOT_STAGE_must_be_0_1_or_2
#endif
#ifndef ENEMY_SHOT_SPEED
#define ENEMY_SHOT_SPEED 1
#endif
#ifndef ENEMY_SHOT_PERIOD
#define ENEMY_SHOT_PERIOD 90
#endif
#ifndef ENEMY_BULLET_COMPACT
#define ENEMY_BULLET_COMPACT 1
#endif
#if ENEMY_BULLET_COMPACT
#define ENEMY_SHOT_HEIGHT 5
#else
#define ENEMY_SHOT_HEIGHT 7
#endif
/* Fixed-lane diagnostic: portrait X=26 lies in the 8-pixel gap
 * between shield 0 (X=8..22) and shield 1 (X=31..45).
 * Bullet pixels occupy X=25..27. No new screen markers. */
#ifndef V44_FIXED_X
#define V44_FIXED_X 26
#endif
#if V44_FIXED_X < 4 || V44_FIXED_X > 97
#error V44_FIXED_X_out_of_safe_range
#endif
#define ENEMY_SHOT_WIDTH 3
#ifndef ENEMY_BULLET_ANIM_TICKS
#define ENEMY_BULLET_ANIM_TICKS 4
#endif
#define EXPLOSION_FRAMES 9
/* Stage A = static shields; Stage B = shot collision and 3x3 erosion. */
#ifndef SHIELD_STAGE
#define SHIELD_STAGE 1
#endif
#if SHIELD_STAGE < 0 || SHIELD_STAGE > 1
#error SHIELD_STAGE_must_be_0_or_1
#endif
/* 0: original per-pixel TGI calls; 1: contiguous run per TGI call. */
#ifndef SHIELD_DRAW_RUNS
#define SHIELD_DRAW_RUNS 1
#endif
#if SHIELD_DRAW_RUNS != 0 && SHIELD_DRAW_RUNS != 1
#error SHIELD_DRAW_RUNS_must_be_0_or_1
#endif
#define SHIELD_COUNT 4
#define SHIELD_W 15
#define SHIELD_H 9
#define SHIELD_Y 119

#ifndef TIMING_MARKER
#define TIMING_MARKER 1
#endif
#ifndef AUDIT_SWEEPS
#define AUDIT_SWEEPS 0
#endif

static SCB_REHV_PAL sprite;
static unsigned char ship_x=SHIP_START_X;
static unsigned char page_ship_x[2];
static unsigned char ship_repair[2];   /* ship clipped by an alien erase */
static unsigned char ship_input_ok=0;
static unsigned char shot_active=0, shot_x=0, shot_y=0;
static unsigned char page_shot_active[2], page_shot_x[2], page_shot_y[2];
static unsigned char fire_was_down=0;
static unsigned char fire_buffered=0;
static unsigned char near_occupation(void);
/* v49 r3: the arcade's three independent enemy shots.
 * Type 0 rolling (aims at the ship), 1 plunger and 2 squiggly
 * (column table; squiggly shares its object with the UFO).
 * See hidden_files/arcade_enemy_fire_analysis.md. */
#define BOMB_ROLLING 0
#define BOMB_PLUNGER 1
#define BOMB_SQUIGGLY 2
static unsigned char bomb_active[3], bomb_x[3], bomb_y[3];
static unsigned char bomb_pose[3], bomb_tick[3];
static unsigned char bomb_steps[3]; /* updates since this type last fired */
static unsigned char page_bomb_active[2][3], page_bomb_x[2][3],
                     page_bomb_y[2][3];
static unsigned char shot_sync=0; /* whose firing turn it is: 0,1,2 */
static unsigned char plunger_idx=0, squiggly_idx=6;
static unsigned char rolling_skip=1; /* rolling's first chance is skipped */
/* col_fire_table comes from ../generated/rom_data.h (ROM $1D00). */
/* Bullet patterns (bomb_bits) and their Suzy form (bomb_spr)
 * come from ../generated/sprites_data.h: rolling and plunger
 * are the port's own designs, squiggly is converted from
 * the ROM at build time. */
static void reset_bombs(void) {
    unsigned char i;
    for(i=0;i<3;++i) {
        bomb_active[i]=0; bomb_pose[i]=0; bomb_tick[i]=0; bomb_steps[i]=255;
    }
    shot_sync=0; plunger_idx=0; squiggly_idx=6; rolling_skip=1;
}
static void clear_bursts(void); /* r25, defined with the burst code */
static void ufo_reset_wave(void); /* v48, defined with the UFO code */
static unsigned char ship_hit_ticks=0;
static unsigned char ship_force_hidden=0; /* ship stays hidden once the game is over */
static unsigned char world_frozen=0;    /* world waits during the ship break */
static unsigned char page_ship_visible[2]={1,1};
static unsigned char page_ship_effect[2]={0,0};

#if TEST_AUTO_FIRE
static unsigned char auto_fire_cooldown=0;
#endif
static unsigned int shield_bits[SHIELD_COUNT][SHIELD_H];
static unsigned char shield_dirty[2][SHIELD_COUNT];
static const unsigned char shield_left[SHIELD_COUNT]={8,31,54,77};
/* Each row is a 15-bit occupancy mask; bit 14 is leftmost. */
static const unsigned int shield_initial[SHIELD_H]={
    0x07F0, 0x1FFC, 0x3FFE, 0x7FFF, 0x7FFF,
    0x7FFF, 0x7E3F, 0x7C1F, 0x781F
};
static unsigned char explosion_ticks=0, explosion_x=0, explosion_y=0;
static signed char offset_x[ALIEN_COUNT];
static unsigned char offset_y[ALIEN_COUNT];
static unsigned char alien_alive[ALIEN_COUNT];
static unsigned int shots_fired=0; /* V46: shot counter for the future UFO score table */
static unsigned char alive_count=ALIEN_COUNT;
static unsigned char walk_frame[ALIEN_COUNT];
static signed char page_x[2][ALIEN_COUNT];
static unsigned char page_y[2][ALIEN_COUNT];
static unsigned char page_frame[2][ALIEN_COUNT];
static unsigned char page_marker[2];
static unsigned char base_x[ALIEN_COUNT], base_y[ALIEN_COUNT], type_of[ALIEN_COUNT];
static unsigned char col_of[ALIEN_COUNT];
static unsigned char visible_page=0, draw_page=1;
static unsigned char descent_pending=0;
static signed char direction=1;
static unsigned char completed_sweeps=0;
static unsigned char next_rom_id=0;
static unsigned char sweep_boundary=0;
static unsigned char last_live_id=54;
static unsigned char previous_id=NONE;
static unsigned char page_full[2];
static unsigned char intro_mode=1;
static unsigned int start_wait_ticks=0;
#if HIT_DRAW_DELTA
static unsigned char page_kill_pending[2];
static unsigned char killed_id=NONE;
static unsigned char page_explosion_drawn[2];
static unsigned char page_explosion_x[2], page_explosion_y[2];
static unsigned char explosion_id=NONE;
#endif

/* Up to 4 local neighborhoods (current, previous, killed, explosion). */
static unsigned char repaint_ids[12], repaint_count;

static void init_sprite(void) {
    unsigned char i;
    sprite.sprctl0 = BPP_1 | TYPE_NORMAL;
    sprite.sprctl1 = LITERAL | REHV;
    sprite.sprcoll = 0x20;
    sprite.next = 0;
    sprite.hsize = 0x0100;
    sprite.vsize = 0x0100;
    sprite.penpal[0] = 0x0F;
    for(i=1; i<8; ++i) sprite.penpal[i]=0;
}
/* V45 Color 3: provisional position-based color bands in PORTRAIT Y.
 * No changes to enemy movement, collision or the 38px descent limit.
 * One alien uses its center row as a single color (no split sprites).
 * Band boundaries will be calibrated from the arcade screenshots later. */
#ifndef V45_ALIEN_COLOR
#define V45_ALIEN_COLOR 1
#endif
static unsigned char v45_alien_color_at(unsigned char y) {
    if(y<48) return COLOR_GREEN;
    if(y<76) return COLOR_LIGHTBLUE;
    if(y<101) return COLOR_PINK;
    if(y<119) return COLOR_YELLOW;
    return COLOR_RED;
}
static void paint_alien(unsigned char n) {
    unsigned char x, y;
    if(!alien_alive[n]) return;
    x=(unsigned char)((int)base_x[n]+offset_x[n]);
    y=(unsigned char)(base_y[n]+offset_y[n]);
#if V45_ALIEN_COLOR
    /* SCB_REHV_PAL maps the sprite's foreground pixel to a TGI color
     * index; preserve every other SCB field and all movement logic. */
    sprite.penpal[0]=v45_alien_color_at((unsigned char)(y+3));
#else
    sprite.penpal[0]=0x0F;
#endif
    sprite.data=alien_frames[type_of[n]+(walk_frame[n]?3:0)];
    sprite.hpos=y;
    sprite.vpos=(unsigned char)(101-(x+6));
    tgi_sprite(&sprite);
}
static void restore_shield_rect(unsigned char x, unsigned char y,
                                unsigned char w, unsigned char h);
static void erase_old(unsigned char n, unsigned char page) {
    unsigned char x=(unsigned char)((int)base_x[n]+page_x[page][n]);
    unsigned char y=(unsigned char)(base_y[n]+page_y[page][n]);
    /* v51 opt4: erase the whole 7x7 box with ONE tgi_bar instead of
     * one bar per ink run (~12 calls).  Everything the box can touch
     * is restored by an existing mechanism, the same combination the
     * explosion erase already relies on: shield bits come back via
     * restore_shield_rect, an overlapped neighbour is repainted by
     * the neighbourhood pass after the erases, the ship via
     * ship_repair (rect_hits_ship covers both erased rects), and the
     * shots, bursts, UFO and explosion redraw after this section.
     * (A black sprite is NOT an option: pen 0 is transparent to
     * Suzy, so a sprite mapped to pen 0 draws nothing -- verified
     * the hard way in opt2/opt3, which smeared.) */
    tgi_setcolor(0);
    tgi_bar(y,(unsigned char)(101-(x+6)),
            (unsigned char)(y+6),(unsigned char)(101-x));
    restore_shield_rect(x,y,7,7);
}
/* Native Lynx coordinates: portrait down = native right.
 * The ship is a small 7x7 placeholder; gun points toward the invaders. */
#define V45_BREAK_POSE_TICKS 36
#define V45_BREAK_BLANK_TICKS 60
#define V45_BREAK_TOTAL_TICKS (2*V45_BREAK_POSE_TICKS+V45_BREAK_BLANK_TICKS)
/* The Lynx Mikey palette is 16 GREEN bytes at FDA0 and 16
 * BLUE/RED bytes at FDB0. Preserve TGI's exact palette, including
 * color 0 (black). Change only entries 1..15 during ship damage.
 * This changes display colors without touching either framebuffer. */
#if V45_COLOR_STEP >= 2
static unsigned char v45_saved_green[16];
static unsigned char v45_saved_bluered[16];
static unsigned char v45_red_active=0;
static void v45_capture_palette(void) {
    unsigned char i;
    for(i=0;i<16;++i) {
        v45_saved_green[i]=PEEK(0xFDA0u+i);
        v45_saved_bluered[i]=PEEK(0xFDB0u+i);
    }
}
static void v45_update_red_palette(void) {
    unsigned char i;
    /* Return to normal palette during the final 60 blank updates. */
    unsigned char want=(unsigned char)(ship_hit_ticks>V45_BREAK_BLANK_TICKS);
    if(want==v45_red_active) return;
    for(i=1;i<16;++i) {
        if(want) {
            POKE(0xFDA0u+i,0x00);  /* green = 0 */
            POKE(0xFDB0u+i,0x0F);  /* blue = 0, red = F */
        } else {
            POKE(0xFDA0u+i,v45_saved_green[i]);
            POKE(0xFDB0u+i,v45_saved_bluered[i]);
        }
    }
    v45_red_active=want;
}
#endif
/* V45 ship A: only the NORMAL ship is changed. The two breakup
 * sprites, collision logic, animation duration and page states are intact.
 * bit6 = leftmost portrait X. Each row is 7 pixels wide. */
#ifndef V45_SHIP_A
#define V45_SHIP_A 1
#endif
#ifndef V45_SHIP_B
#define V45_SHIP_B 1
#endif
#if V45_SHIP_B != 0 && V45_SHIP_B != 1
#error V45_SHIP_B_must_be_0_or_1
#endif
#if V45_SHIP_A != 0 && V45_SHIP_A != 1
#error V45_SHIP_A_must_be_0_or_1
#endif
#if V45_SHIP_A && !V45_SHIP_B
static const unsigned char ship_a_rows[7] = {
    0x08, /* ...#... */
    0x08, /* ...#... */
    0x1C, /* ..###.. */
    0x3E, /* .#####. */
    0x7F, /* ####### */
    0x7F, /* ####### */
    0x7F  /* ####### */
};
#endif
/* Proposed low-profile gun: same 7x7 canvas, lower three rows empty. */
#if V45_SHIP_B
static const unsigned char ship_b_rows[7] = {
    0x08, /* ...#... */
    0x3E, /* .#####. */
    0x7F, /* ####### */
    0x7F, /* ####### */
    0x00, 0x00, 0x00
};
#endif
/* V45 independent color test. Only normal ship and shield pixels change. */
#ifndef V45_OBJECT_COLOR
#define V45_OBJECT_COLOR 1
#endif
#if V45_OBJECT_COLOR != 0 && V45_OBJECT_COLOR != 1
#error V45_OBJECT_COLOR_must_be_0_or_1
#endif
#if V45_OBJECT_COLOR
#define V45_SHIP_PEN COLOR_LIGHTBLUE
#define V45_SHIELD_PEN COLOR_RED
#else
#define V45_SHIP_PEN 1
#define V45_SHIELD_PEN 1
#endif
static void draw_ship(unsigned char x) {
#if V45_SHIP_A || V45_SHIP_B
    unsigned char row, col, start, mask;
    tgi_setcolor(V45_SHIP_PEN);
    for(row=0; row<7; ++row) {
#if V45_SHIP_B
        mask=ship_b_rows[row];
#else
        mask=ship_a_rows[row];
#endif
        col=0;
        while(col<7) {
            if(!(mask & (unsigned char)(0x40u >> col))) { ++col; continue; }
            start=col;
            do { ++col; } while(col<7 && (mask & (unsigned char)(0x40u >> col)));
            /* Portrait X grows opposite native Y. Keep TGI endpoints sorted. */
            tgi_bar((unsigned char)(SHIP_Y+row),
                    (unsigned char)(101-x-(col-1)),
                    (unsigned char)(SHIP_Y+row),
                    (unsigned char)(101-x-start));
        }
    }
#else
    unsigned char native_y=(unsigned char)(101-(x+6));
    tgi_setcolor(V45_SHIP_PEN);
    tgi_bar(SHIP_Y,native_y,SHIP_Y+6,native_y+6);
    tgi_bar(SHIP_Y-3,native_y+3,SHIP_Y,native_y+4);
#endif
}
/* V45 isolated diagnostic: shift ONLY the ship breakup image upward
 * in portrait coordinates.  Normal ship and gameplay coordinates stay put. */
#ifndef V45_BREAK_SHIFT_UP
#define V45_BREAK_SHIFT_UP 0
#endif
#if V45_BREAK_SHIFT_UP < 0 || V45_BREAK_SHIFT_UP > 3
#error V45_BREAK_SHIFT_UP_must_be_0_to_3
#endif
/* Erase exactly the footprint the ship's previous pose drew.
 * The old code always erased a tall union (SHIP_Y-3-SHIFT .. SHIP_Y+6:
 * 13 rows for a 7-row ship). That was harmless while the formation
 * could not descend into the band, but since v47 it can, and every
 * ship redraw then ate the alien flying just above the ship, which
 * made it blink out and back. Break poses are drawn shifted up by
 * V45_BREAK_SHIFT_UP, so only their footprint reaches higher; the
 * blank pose drew nothing and needs no erase. */
static void erase_ship(unsigned char x, unsigned char old_state) {
    unsigned char native_y=(unsigned char)(101-(x+6));
    unsigned char top;
    if(old_state==3) return;
    top=(old_state==0) ? SHIP_Y : (unsigned char)(SHIP_Y-V45_BREAK_SHIFT_UP);
    tgi_setcolor(0);
    tgi_bar(top,native_y,(unsigned char)(SHIP_Y+6),(unsigned char)(native_y+6));
}
/* V45 final: OPTION 2 + PAUSE edge-triggered 180-degree flip.
 * Preserve the proven Hit 3 gameplay and default joystick orientation. */
static unsigned char v45_prev_option_pause=0;
static void v45_poll_option_pause(void) {
    unsigned char pressed;
    pressed=(unsigned char)(((PEEK(0xFCB0u)&0x04u)!=0) &&
                            ((PEEK(0xFCB1u)&0x01u)!=0));
    if(pressed && !v45_prev_option_pause) {
        while(tgi_busy()) { }
        tgi_flip();
    }
    v45_prev_option_pause=pressed;
}
/* --- v49: attract mode (title / score table / demo / credit) and the
 * demo player. demo_mode runs the normal game loop with synthesized
 * input; attract_phase != 0 runs the splash screens instead. */
static unsigned char demo_mode=0;
static unsigned char demo_over=0;
static unsigned char demo_start_request=0;
static unsigned char demo_cmd_idx=0;
static unsigned char demo_cmd=0;
static unsigned int demo_ticks=0;
/* Lynx adaptation: the arcade demo ends by death or landing, which
 * its denser enemy fire (up to 3 shots) brings about in seconds. The
 * port fires one enemy shot at a time, so an uncapped demo can run
 * for minutes; cap it so the attract cycle keeps turning. */
#define DEMO_MAX_TICKS 1200
/* The arcade demo replays a recorded 10-byte command buffer (ROM
 * $1F74): 1 = right, 2 = left, 0 = stay, one consumed per shot. */
/* demo_cmds comes from ../generated/rom_data.h (ROM $1F74). */
static unsigned char attract_phase=0; /* 0 = in game; phases below */
static void attract_step(void); /* defined after the UFO section */
static void attract_enter_title(void);
static void attract_enter_credit(void);
static void poll_ship(void) {
    unsigned char keys;
    keys=0;
    if(ship_input_ok) keys=joy_read(0);
    if(demo_mode) {
        static unsigned char demo_move_phase=0;
        unsigned char step;
        /* A real press ends the demo and starts a game. */
        if(keys & (JOY_BTN_1_MASK|JOY_BTN_2_MASK|JOY_UP_MASK|JOY_DOWN_MASK|JOY_LEFT_MASK|JOY_RIGHT_MASK)) demo_start_request=1;
        if(!ship_hit_ticks && demo_cmd) {
            demo_move_phase^=1;
            step=demo_move_phase?2:1;
            if(demo_cmd==1 && ship_x<SHIP_MAX_X) {
                ship_x=(unsigned char)(ship_x+step);
                if(ship_x>SHIP_MAX_X) ship_x=SHIP_MAX_X;
            }
            if(demo_cmd==2 && ship_x>SHIP_MIN_X) {
                ship_x=(step>=ship_x)?SHIP_MIN_X:(unsigned char)(ship_x-step);
                if(ship_x<SHIP_MIN_X) ship_x=SHIP_MIN_X;
            }
        }
        if(++demo_ticks>DEMO_MAX_TICKS) demo_over=1;
        /* Constant fire, as on the arcade; each shot consumes the
         * next recorded command. */
        if(!ship_hit_ticks && !shot_active) {
            shot_active=1;
            ++shots_fired;
            shot_x=(unsigned char)(ship_x+3);
            shot_y=(unsigned char)(SHIP_Y-7);
            demo_cmd=demo_cmds[demo_cmd_idx];
            demo_cmd_idx=(unsigned char)((demo_cmd_idx+1)%10);
        }
        return;
    }
#if TEST_SHOT_X == 255
    /* r15: ship speed x1.5 (user asked for a little faster, 2026-11-19):
     * while a direction is held, alternate 1px and 2px steps. */
    if(!ship_hit_ticks && (keys & (JOY_UP_MASK|JOY_DOWN_MASK))) {
        static unsigned char move_phase=0;
        unsigned char step;
        move_phase^=1;
        step=move_phase?2:1;
        if((keys & JOY_UP_MASK) && ship_x<SHIP_MAX_X) {
            ship_x=(unsigned char)(ship_x+step);
            if(ship_x>SHIP_MAX_X) ship_x=SHIP_MAX_X;
        }
        if((keys & JOY_DOWN_MASK) && ship_x>SHIP_MIN_X) {
            ship_x=(step>=ship_x)?SHIP_MIN_X:(unsigned char)(ship_x-step);
            if(ship_x<SHIP_MIN_X) ship_x=SHIP_MIN_X;
        }
    }
#endif
#if TEST_AUTO_FIRE
    if(auto_fire_cooldown) --auto_fire_cooldown;
    if(!ship_hit_ticks && !shot_active && auto_fire_cooldown==0) {
        shot_active=1;
        ++shots_fired;
#if TEST_SHOT_X == 255
        shot_x=(unsigned char)(ship_x+3);
#else
        shot_x=TEST_SHOT_X;
#endif
        shot_y=(unsigned char)(SHIP_Y-7);
        auto_fire_cooldown=TEST_FIRE_COOLDOWN;
    }
#else
    if(keys & JOY_BTN_1_MASK) {
        if(!fire_was_down && (!shot_active || near_occupation())) fire_buffered=1; /* r23: remember a fresh press. r24: a press made while a shot is flying is remembered only near occupation; a press with no shot flying always registers (it fires on the same poll). */
        fire_was_down=1;
    } else {
        fire_was_down=0;
    }
    /* r23: fire input buffering (user request, 2026-11-21). The firing
     * style stays press-per-shot (no autofire while held), but a press
     * made while the previous shot is still flying is no longer eaten:
     * it fires the moment that shot clears. Timing the re-press to the
     * exact frame the shot disappears was the hard part. A press during
     * the damage break still queues nothing. */
    if(ship_hit_ticks) fire_buffered=0;
    if(fire_buffered && !ship_hit_ticks && !shot_active) {
        fire_buffered=0;
        shot_active=1;
        snd_play(SND_SHOOT); /* v50 (this block is real play only) */
        ++shots_fired;
#if TEST_SHOT_X == 255
        shot_x=(unsigned char)(ship_x+3);
#else
        shot_x=TEST_SHOT_X;
#endif
        shot_y=(unsigned char)(SHIP_Y-7);
    }
#endif
}
/* V45 stage 0: two 7x7 breakup poses, oriented in portrait coordinates.
 * Each bitmask uses bit6=leftmost portrait X.  No new Suzy calls. */
static const unsigned char ship_break_rows[2][7]={
    {0x41,0x14,0x24,0x59,0x1C,0x3E,0x5D},
    {0x21,0x12,0x49,0x2C,0x1D,0x7C,0x2D}
};
/* V45 timing test: 36 updates pose 1, 36 pose 2, 60 blank. */
static unsigned char ship_effect_state(void) {
    if(ship_force_hidden) return 3;
    if(!ship_hit_ticks) return 0;
    if(ship_hit_ticks>V45_BREAK_BLANK_TICKS+V45_BREAK_POSE_TICKS) return 1;
    if(ship_hit_ticks>V45_BREAK_BLANK_TICKS) return 2;
    return 3;
}
static void draw_ship_break(unsigned char x,unsigned char pose) {
    unsigned char row,col,bits;
    tgi_setcolor(1);
    for(row=0;row<7;++row) {
        bits=ship_break_rows[pose][row];
        for(col=0;col<7;++col) {
            if(bits & (unsigned char)(0x40u >> col)) {
                tgi_bar((unsigned char)(SHIP_Y+row-V45_BREAK_SHIFT_UP),
                        (unsigned char)(101-x-col),
                        (unsigned char)(SHIP_Y+row-V45_BREAK_SHIFT_UP),
                        (unsigned char)(101-x-col));
            }
        }
    }
}
static void update_ship_page(unsigned char page) {
    unsigned char state=ship_effect_state();
    if(ship_repair[page] || page_ship_x[page]!=ship_x || page_ship_effect[page]!=state) {
        /* Clears old normal ship, explosion, or blank footprint on THIS page. */
        ship_repair[page]=0;
        erase_ship(page_ship_x[page], page_ship_effect[page]);
        if(state==0) draw_ship(ship_x);
        else if(state<3) draw_ship_break(ship_x,(unsigned char)(state-1));
    }
    page_ship_visible[page]=(unsigned char)(state==0);
    page_ship_effect[page]=state;
    page_ship_x[page]=ship_x;
}
/* 1x3 portrait bullet. Portrait up means native X decreases. */
#ifndef V45_BULLET_COLOR
#define V45_BULLET_COLOR 1
#endif
#if V45_BULLET_COLOR != 0 && V45_BULLET_COLOR != 1
#error V45_BULLET_COLOR_must_be_0_or_1
#endif
/* The portrait Y coordinate, not bullet identity, determines color. */
static void paint_shot(unsigned char x, unsigned char y) {
#if V45_BULLET_COLOR
    unsigned char i, yy;
    /* The shot is 3px tall and the color zones are wide bands, so all
     * rows usually share one color: draw a single bar then (r10 load
     * trim; the per-row path remains for zone boundaries). */
    if(v45_alien_color_at(y)==v45_alien_color_at((unsigned char)(y+SHOT_HEIGHT-1))) {
        tgi_setcolor(v45_alien_color_at(y));
        tgi_bar(y,(unsigned char)(101-x),(unsigned char)(y+SHOT_HEIGHT-1),(unsigned char)(101-x));
        return;
    }
    for(i=0;i<SHOT_HEIGHT;++i) {
        yy=(unsigned char)(y+i);
        tgi_setcolor(v45_alien_color_at(yy));
        tgi_bar(yy,(unsigned char)(101-x),yy,(unsigned char)(101-x));
    }
#else
    tgi_setcolor(1);
    tgi_bar(y,(unsigned char)(101-x),(unsigned char)(y+SHOT_HEIGHT-1),(unsigned char)(101-x));
#endif
}
static void erase_shot(unsigned char x, unsigned char y) {
    tgi_setcolor(0);
    tgi_bar(y,(unsigned char)(101-x),(unsigned char)(y+SHOT_HEIGHT-1),(unsigned char)(101-x));
}
/* Shield positions are logical PORTRAIT coordinates. Draw on native
 * TGI with native X=portrait Y and native Y=101-portrait X. */
static unsigned char shield_solid(unsigned char k, unsigned char lx,
                                  unsigned char ly) {
#if V45_CONTACT_MODE == 3
    (void)k; (void)lx; (void)ly;
    return 0; /* Invisible shields and no shield projectile collisions. */
#else
    return (unsigned char)((shield_bits[k][ly] & (1u << (14-lx))) != 0);
#endif
}
static void paint_shield(unsigned char k) {
    unsigned char lx, ly;
#if SHIELD_DRAW_RUNS
    unsigned char run_start;
#else
    unsigned char x;
#endif
    /* The full 15x9 rectangle is cleared before redrawing surviving bits. */
    tgi_setcolor(0);
    tgi_bar(SHIELD_Y,(unsigned char)(101-(shield_left[k]+SHIELD_W-1)),
            SHIELD_Y+SHIELD_H-1,(unsigned char)(101-shield_left[k]));
    tgi_setcolor(V45_SHIELD_PEN);
    for(ly=0;ly<SHIELD_H;++ly) {
#if SHIELD_DRAW_RUNS
        /* Portrait horizontal runs become native vertical TGI bars.
         * A run from portrait X=start..end maps to native Y=
         * 101-(left+end) .. 101-(left+start). */
        lx=0;
        while(lx<SHIELD_W) {
            if(!shield_solid(k,lx,ly)) {
                ++lx;
                continue;
            }
            run_start=lx;
            do {
                ++lx;
            } while(lx<SHIELD_W && shield_solid(k,lx,ly));
            tgi_bar((unsigned char)(SHIELD_Y+ly),
                    (unsigned char)(101-(shield_left[k]+lx-1)),
                    (unsigned char)(SHIELD_Y+ly),
                    (unsigned char)(101-(shield_left[k]+run_start)));
        }
#else
        /* Original V42 path: one TGI call for each surviving pixel. */
        for(lx=0;lx<SHIELD_W;++lx) {
            if(shield_solid(k,lx,ly)) {
                x=(unsigned char)(shield_left[k]+lx);
                tgi_bar((unsigned char)(SHIELD_Y+ly),
                        (unsigned char)(101-x),
                        (unsigned char)(SHIELD_Y+ly),
                        (unsigned char)(101-x));
            }
        }
#endif
    }
}
static void paint_all_shields(void) {
    unsigned char k;
    for(k=0;k<SHIELD_COUNT;++k) paint_shield(k);
}
/* V45 load diagnostic: 0=full redraw reference, 1=mask+changed-only,
 * 2=box+changed-only, 3=mask+dirty-row contiguous runs. */
#ifndef V45_SHIELD_LOAD_MODE
#define V45_SHIELD_LOAD_MODE 0
#endif
#if V45_SHIELD_LOAD_MODE < 0 || V45_SHIELD_LOAD_MODE > 3
#error V45_SHIELD_LOAD_MODE_must_be_0_to_3
#endif
/* V45 Shield Solid Cache 0..1: normal gameplay A/B comparison.
 * 0 = original shield_solid() tests; 1 = row-local snapshot.
 * No timer instrumentation or frozen results screen.
 */
#ifndef V45_SOLID_CACHE_MODE
#define V45_SOLID_CACHE_MODE 0
#endif
#if V45_SOLID_CACHE_MODE < 0 || V45_SOLID_CACHE_MODE > 1
#error V45_SOLID_CACHE_MODE_must_be_0_or_1
#endif
#if V45_SHIELD_LOAD_MODE == 3
static unsigned int v45_shield_dirty_rows[2][SHIELD_COUNT];
static void v45_paint_shield_rows(unsigned char k, unsigned int rows) {
    unsigned char ly,lx,start;
#if V45_SOLID_CACHE_MODE == 1
    unsigned int row_mask;
#endif
    for(ly=0;ly<SHIELD_H;++ly) {
        if(!(rows & (1u << ly))) continue;
#if V45_SOLID_CACHE_MODE == 1
        /* Shield data is not mutated during this synchronous row paint. */
        row_mask=shield_bits[k][ly];
#endif
        tgi_setcolor(0);
        tgi_bar((unsigned char)(SHIELD_Y+ly),
                (unsigned char)(101-(shield_left[k]+SHIELD_W-1)),
                (unsigned char)(SHIELD_Y+ly),
                (unsigned char)(101-shield_left[k]));
        tgi_setcolor(V45_SHIELD_PEN);
        lx=0;
        while(lx<SHIELD_W) {
#if V45_SOLID_CACHE_MODE == 1
            if(!(row_mask & (1u << (14-lx)))) { ++lx; continue; }
#else
            if(!shield_solid(k,lx,ly)) { ++lx; continue; }
#endif
            start=lx;
            do { ++lx; }
#if V45_SOLID_CACHE_MODE == 1
            while(lx<SHIELD_W && (row_mask & (1u << (14-lx))));
#else
            while(lx<SHIELD_W && shield_solid(k,lx,ly));
#endif
            tgi_bar((unsigned char)(SHIELD_Y+ly),
                    (unsigned char)(101-(shield_left[k]+lx-1)),
                    (unsigned char)(SHIELD_Y+ly),
                    (unsigned char)(101-(shield_left[k]+start)));
        }
    }
}
#endif
/* Rows repainted by the latest update_shield_page call, per shield
 * (r11): render_delta repaints living aliens standing over them. */
static unsigned int shield_repainted_rows[SHIELD_COUNT];
static void update_shield_page(unsigned char page) {
    unsigned char k;
#if V45_BREAK_MODE == 2
    /* Diagnostic only: retain old wall pixels on screen after first draw.
       Data changes continue, but this test suppresses redraw cost. */
    if(!page_full[page]) return;
#endif
    for(k=0;k<SHIELD_COUNT;++k) {
        unsigned char dirty;
        unsigned int rows=0u;
        shield_repainted_rows[k]=0;
        dirty=shield_dirty[page][k];
#if V45_SHIELD_LOAD_MODE == 3
        if(dirty) rows=v45_shield_dirty_rows[page][k];
#endif
        if(dirty) {
#if V45_SHIELD_LOAD_MODE == 3
            if(rows) {
                v45_paint_shield_rows(k,rows);
                shield_repainted_rows[k]=rows;
            } else {
                paint_shield(k); /* fallback unchanged; excluded from measured span */
                shield_repainted_rows[k]=0x1FFu;
            }
            v45_shield_dirty_rows[page][k]=0;
#else
            paint_shield(k);
            shield_repainted_rows[k]=0x1FFu;
#endif
            shield_dirty[page][k]=0;
        }
    }
}
/* The shield row repaint blacks the full row width before drawing the
 * solid runs, so it slices the page copy of ANY alien standing over a
 * repainted row -- including bystanders the alien section does not
 * redraw this call. Repaint those aliens on top (r11). A bystander's
 * page copies are in sync with its current position and frame (it was
 * reconciled in the two calls after its own step), and the shield bits
 * under its sprite were eroded when it stepped there, so painting the
 * sprite over the fresh shield rows restores the correct image. Costs
 * nothing when no shield row was repainted. */
static void repair_shield_row_aliens(void) {
    unsigned char k, n, ly;
    int ax, ay;
    for(k=0;k<SHIELD_COUNT;++k) {
        unsigned int rows=shield_repainted_rows[k];
        if(!rows) continue;
        for(n=0;n<ALIEN_COUNT;++n) {
            if(!alien_alive[n]) continue;
            ax=(int)base_x[n]+offset_x[n];
            ay=(int)base_y[n]+offset_y[n];
            if(ax+6<(int)shield_left[k] ||
               ax>=(int)(shield_left[k]+SHIELD_W)) continue;
            for(ly=0;ly<SHIELD_H;++ly) {
                if(!(rows & (unsigned int)(1u<<ly))) continue;
                if(ay+6>=(int)(SHIELD_Y+ly) && ay<=(int)(SHIELD_Y+ly)) {
                    paint_alien(n);
                    break;
                }
            }
        }
    }
}
/* Restore only shield dots touched by the old 1x3 bullet, so a
 * projectile passing through a pre-existing hole does not erase a wall. */
static void restore_shield_under_shot(unsigned char x, unsigned char y) {
    unsigned char k, ly, lx, yy;
    for(k=0;k<SHIELD_COUNT;++k) {
        if(x<shield_left[k] || x>=shield_left[k]+SHIELD_W) continue;
        lx=(unsigned char)(x-shield_left[k]);
        for(yy=y; yy<(unsigned char)(y+SHOT_HEIGHT); ++yy) {
            if(yy<SHIELD_Y || yy>=SHIELD_Y+SHIELD_H) continue;
            ly=(unsigned char)(yy-SHIELD_Y);
            if(shield_solid(k,lx,ly)) {
                tgi_setcolor(V45_SHIELD_PEN);
                tgi_bar(yy,(unsigned char)(101-x),yy,(unsigned char)(101-x));
            }
        }
    }
}
/* Detect first solid wall pixel on the swept shot path (from bottom to top).
 * On impact remove a small 3x3 crater; shield contents are persistent. */
/* Repaint the shield pixels inside a rect from shield_bits (r11).
 * Erase bars (alien old box, explosion box) wipe display pixels of
 * shield parts the sprite never touched; those bits are still solid in
 * the data but were never marked dirty, so without this they stay black
 * on this page until an unrelated repaint -- the shield half of the
 * alien/shield flicker. Costs nothing outside the shield band. */
static void restore_shield_rect(unsigned char x, unsigned char y,
                                unsigned char w, unsigned char h) {
    unsigned char k, lx, ly, xx, yy;
    if(y>=(unsigned char)(SHIELD_Y+SHIELD_H) ||
       (unsigned char)(y+h)<=SHIELD_Y) return;
    for(k=0;k<SHIELD_COUNT;++k) {
        if((unsigned char)(x+w)<=shield_left[k] ||
           x>=(unsigned char)(shield_left[k]+SHIELD_W)) continue;
        for(yy=y; yy<(unsigned char)(y+h); ++yy) {
            if(yy<SHIELD_Y || yy>=(unsigned char)(SHIELD_Y+SHIELD_H)) continue;
            ly=(unsigned char)(yy-SHIELD_Y);
            for(xx=x; xx<(unsigned char)(x+w); ++xx) {
                if(xx<shield_left[k] ||
                   xx>=(unsigned char)(shield_left[k]+SHIELD_W)) continue;
                lx=(unsigned char)(xx-shield_left[k]);
                if(shield_solid(k,lx,ly)) {
                    tgi_setcolor(V45_SHIELD_PEN);
                    tgi_bar(yy,(unsigned char)(101-xx),yy,(unsigned char)(101-xx));
                }
            }
        }
    }
}
/* Formation y-extent gate (r10 load trim). Every offset_y sits within
 * [D, D+DESCENT_STEP] of the wave's total descent D, and the reference
 * living alien's offset_y is in that range, so all alien pixels lie in
 * [base_y[0]+ref-DESCENT_STEP, base_y[44]+ref+DESCENT_STEP+7). A shot
 * band outside that range cannot touch any alien, and the per-alien
 * scans in check_shot_hit / restore_shot_background are pure waste
 * there (they ran on every iteration of the shot's flight and measurably
 * slowed the game while firing). */
static unsigned char rom_to_lynx(unsigned char id); /* fwd (also prototyped below) */
static unsigned char formation_band_miss(unsigned char lo, unsigned char hi) {
    unsigned char ref;
    if(!alive_count) return 1;
    ref=offset_y[rom_to_lynx(last_live_id)];
    if(hi<=(unsigned char)(base_y[0]+ref-DESCENT_STEP)) return 1;
    if(lo>=(unsigned char)(base_y[44]+ref+DESCENT_STEP+7)) return 1;
    return 0;
}
static unsigned char check_shot_shield(unsigned char top,
                                      unsigned char bottom) {
#if SHIELD_STAGE == 1
    unsigned char k, lx, ly, yy, xx, yy2;
    unsigned char x=shot_x;
    if(bottom<=SHIELD_Y || top>=(unsigned char)(SHIELD_Y+SHIELD_H)) return 0; /* r10: band gate */
    /* Player shot is 1px wide; do not use the 3px enemy-shot loop here. */
    for(yy=bottom; yy>top; ) {
        --yy;
        if(yy<SHIELD_Y || yy>=SHIELD_Y+SHIELD_H) continue;
        for(k=0;k<SHIELD_COUNT;++k) {
            if(x<shield_left[k] || x>=shield_left[k]+SHIELD_W) continue;
            lx=(unsigned char)(x-shield_left[k]);
            ly=(unsigned char)(yy-SHIELD_Y);
            if(!shield_solid(k,lx,ly)) continue;
            for(yy2=0; yy2<SHIELD_H; ++yy2) {
                if(yy2+1<ly || yy2>ly+1) continue;
                for(xx=0;xx<SHIELD_W;++xx) {
                    if(xx+1<lx || xx>lx+1) continue;
                    shield_bits[k][yy2] &= (unsigned int)~(1u << (14-xx));
                }
            }
            shield_dirty[0][k]=1;
            shield_dirty[1][k]=1;
#if V45_SHIELD_LOAD_MODE == 3
            /* r17: craters must also mark their rows, or update_shield_page
             * (which repaints only the marked rows when any are pending)
             * leaves the crater row stale until an unrelated erosion of
             * the same row reveals it all at once -- the "shield vanishes
             * ahead of the alien" the user caught on video (2026-11-19). */
            {
                unsigned int crater_rows=(unsigned int)(1u<<ly);
                if(ly>0) crater_rows |= (unsigned int)(1u<<(ly-1));
                if(ly<SHIELD_H-1) crater_rows |= (unsigned int)(1u<<(ly+1));
                v45_shield_dirty_rows[0][k] |= crater_rows;
                v45_shield_dirty_rows[1][k] |= crater_rows;
            }
#endif
            shot_active=0;
            return 1;
        }
    }
#else
    (void)top; (void)bottom;
#endif
    return 0;
}
/* Restore enemy pixels under the previous bullet footprint. No hits yet. */
static void restore_shot_background(unsigned char x, unsigned char y) {
    unsigned char i, ax, ay;
    if(formation_band_miss(y,(unsigned char)(y+SHOT_HEIGHT))) return; /* r10 */
    for(i=0; i<ALIEN_COUNT; ++i) {
        if(!alien_alive[i]) continue;
        /* Same band-first reject as check_shot_hit: this scan also
         * runs on every iteration while the shot flies (r10). */
        ay=(unsigned char)(base_y[i]+offset_y[i]);
        if(y>=(unsigned char)(ay+7) || (unsigned char)(y+SHOT_HEIGHT)<=ay) continue;
        ax=(unsigned char)((int)base_x[i]+offset_x[i]);
        if(x>=ax && x<ax+7) {
            paint_alien(i);
        }
    }
}
static unsigned char rom_to_lynx(unsigned char id);
/* Swept bullet collision in PORTRAIT logical coordinates.
 * Shot x is one pixel wide; Y interval is [new top, old bottom).
 * In HIT_MODE 1, masks use bit6=leftmost portrait X, rows=portrait Y.
 * Mode 3 probes a transposed interpretation, NOT a recommended final fix.
 * Keep the original hit/kill/explosion code below unchanged. */
static unsigned char hit_pixel(unsigned char pose,
                               unsigned char local_x,
                               unsigned char local_y) {
#if HIT_MODE == 0
    (void)pose; (void)local_x; (void)local_y;
    return 1;
#elif HIT_MODE == 2
    unsigned char width;
    width=(pose==0 || pose==3)?7:((pose==1 || pose==4)?6:5);
    (void)local_y;
    if(pose==2 || pose==5) return (local_x>=1 && local_x<6);
    return local_x<width;
#elif HIT_MODE == 3
    return (unsigned char)((alien_hit_rows[pose][local_x] &
                           (1u << (6-local_y))) != 0);
#else
    return (unsigned char)((alien_hit_rows[pose][local_y] &
                           (1u << (6-local_x))) != 0);
#endif
}

/* ==== V46 score (arcade-ROM faithful) ====
 * Points by alien type (ROM score-advance table): bottom type 0 = 10,
 * middle type 1 = 20, top type 2 = 30. Four digits, wraps at 9999.
 * Hi-score is RAM-only, as on the arcade board. Header band is portrait
 * y 0..17; the formation starts at y 42, so nothing else lives there
 * except the player shot passing through (handled in update_shot_page).
 * Labels use the original ROM font (score_font.h); the ROM font has no
 * hyphen glyph, so the HI-SCORE hyphen is drawn as a 3-pixel line. */
static unsigned int score_value=0;
static unsigned int hi_score_value=0;
/* shots_fired itself is declared with the early globals above. */
static unsigned int page_shown_score[2]={0xFFFF,0xFFFF};
static unsigned int page_shown_hi[2]={0xFFFF,0xFFFF};
/* --- v47: lives, stage progression, game over --- */
/* v47 r2: header layout matched to the arcade (measured from the
 * reference GIF): labels lower, digits centered under their labels,
 * SCORE side in light blue (cyan), HI-SCORE side in blue. */
#define HEADER_LABEL_Y 18
#define HEADER_DIGITS_Y 26
#define SCORE_DIGITS_X 9
#define HI_DIGITS_X 43
#define SCORE2_DIGITS_X 77
#define HEADER_BAND 34
#define SCORE_PEN COLOR_LIGHTBLUE
#define HI_PEN COLOR_BLUE
static unsigned char ships_left=3;      /* including the ship in play */
static unsigned char stage_no=1;
static unsigned char extend_awarded=0;
static unsigned char game_over=0;
static unsigned int game_over_ticks=0;
static unsigned char stage_clear_ticks=0;
static unsigned char wave_end_synced=0;  /* hidden page converged at wave end */
static unsigned char page_shown_ships[2]={0xFF,0xFF};
static const unsigned char label_game_over[9]={6,0,12,4,38,14,21,4,17}; /* GAME OVER */
static const unsigned char alien_points[3]={10,20,30};
static void add_score(unsigned char pts) {
    unsigned int raw=(unsigned int)(score_value+pts);
    /* EXTEND: one extra ship when the score crosses 1500 (arcade standard). */
    if(!extend_awarded && score_value<1500 && raw>=1500) {
        extend_awarded=1;
        if(ships_left<9) ++ships_left;
        if(!demo_mode) snd_play(SND_EXTEND); /* v50: the arcade's extra-ship sound */
    }
    score_value=raw;
    if(score_value>9999) score_value=(unsigned int)(score_value-10000);
    if(!demo_mode && score_value>hi_score_value) hi_score_value=score_value;
}
/* One portrait pixel via the same native mapping as paint_shot. */
static void header_pixel(unsigned char x, unsigned char y) {
    tgi_bar(y,(unsigned char)(101-x),y,(unsigned char)(101-x));
}
static void draw_font_char(unsigned char code, unsigned char x, unsigned char y) {
    unsigned char c, r, b;
    if(code==0xFF) { /* hyphen: 3 pixels on the middle row */
        header_pixel((unsigned char)(x+1),(unsigned char)(y+3));
        header_pixel((unsigned char)(x+2),(unsigned char)(y+3));
        header_pixel((unsigned char)(x+3),(unsigned char)(y+3));
        return;
    }
    for(c=1;c<7;++c) {
        b=score_font[code][c];
        if(!b) continue;
        for(r=0;r<7;++r) if(b&(1<<r))
            header_pixel((unsigned char)(x+c-1),(unsigned char)(y+6-r)); /* ROM bit0 is the bottom row in Lynx portrait orientation */
    }
}
static void draw_font_text(const unsigned char *codes, unsigned char n,
                           unsigned char x, unsigned char y) {
    unsigned char i;
    for(i=0;i<n;++i) {
        draw_font_char(codes[i],x,y);
        x=(unsigned char)(x+6);
    }
}
/* --- v49: small 3x5 font (advance 4) for the header and the attract
 * screens; the big ROM font made SCORE/HI-SCORE and the score digits
 * too large and "*SCORE ADVANCE TABLE*" could not fit the 102-wide
 * screen. Glyph rows are 3-bit masks, bit2 = left pixel. Codes match
 * the ROM font numbering (A=0..Z=25, 0=26..9=35, <=36, >=37, space=38,
 * ==39, *=40, ?=41) plus 42 = hyphen (the ROM font has none). */
static const unsigned char small_font[43][5] = {
    {2,5,7,5,5},{6,5,6,5,6},{3,4,4,4,3},{6,5,5,5,6},{7,4,6,4,7},
    {7,4,6,4,4},{3,4,5,5,3},{5,5,7,5,5},{7,2,2,2,7},{1,1,1,5,2},
    {5,5,6,5,5},{4,4,4,4,7},{5,7,7,5,5},{5,7,5,5,5},{2,5,5,5,2},
    {6,5,6,4,4},{2,5,5,2,1},{6,5,6,5,5},{3,4,2,1,6},{7,2,2,2,2},
    {5,5,5,5,7},{5,5,5,5,2},{5,5,5,7,7},{5,5,2,5,5},{5,5,2,2,2},
    {7,1,2,4,7},{7,5,5,5,7},{2,6,2,2,7},{6,1,2,4,7},{6,1,2,1,6},
    {5,5,7,1,1},{7,4,6,1,6},{3,4,6,5,2},{7,1,2,2,2},{2,5,2,5,2},
    {2,5,3,1,6},{1,2,4,2,1},{4,2,1,2,4},{0,0,0,0,0},{0,7,0,7,0},
    {5,2,5,0,0},{6,1,2,0,2},{0,0,7,0,0}
};
static unsigned char small_code(char ch) {
    if(ch>='A' && ch<='Z') return (unsigned char)(ch-'A');
    if(ch>='0' && ch<='9') return (unsigned char)(26+ch-'0');
    switch(ch) {
        case '<': return 36;
        case '>': return 37;
        case '=': return 39;
        case '*': return 40;
        case '?': return 41;
        case '-': return 42;
        default: return 38;
    }
}
static void draw_small_char(unsigned char code, unsigned char x, unsigned char y) {
    unsigned char r, c, bits;
    for(r=0;r<5;++r) {
        bits=small_font[code][r];
        for(c=0;c<3;++c)
            if(bits&(4>>c)) header_pixel((unsigned char)(x+c),(unsigned char)(y+r));
    }
}
static void draw_small_text(const char *s, unsigned char x, unsigned char y) {
    while(*s) {
        draw_small_char(small_code(*s),x,y);
        x=(unsigned char)(x+4);
        ++s;
    }
}
static void draw_small_number(unsigned char x, unsigned char y, unsigned int v) {
    unsigned char i, d;
    unsigned int div;
    div=1000;
    for(i=0;i<4;++i) {
        d=(unsigned char)((v/div)%10);
        draw_small_char((unsigned char)(26+d),x,y);
        x=(unsigned char)(x+4);
        div/=10;
    }
}
static void erase_small_cell(unsigned char x, unsigned char y) {
    tgi_setcolor(0);
    tgi_bar(y,(unsigned char)(101-(x+2)),(unsigned char)(y+4),(unsigned char)(101-x));
    tgi_setcolor(1);
}
static void paint_header(unsigned char page) {
    unsigned char i, d, sd;
    unsigned int div;
    if(page_shown_score[page]==0xFFFF || page_shown_hi[page]==0xFFFF) {
        /* Full draw: labels and all digits (after a page clear). */
        tgi_setcolor(0);
        tgi_bar(0,0,33,101); /* portrait band y 0..33, full width */
        tgi_setcolor(SCORE_PEN);
        draw_small_text("SCORE<1>",1,HEADER_LABEL_Y);
        draw_small_number(SCORE_DIGITS_X,HEADER_DIGITS_Y,score_value);
        tgi_setcolor(HI_PEN);
        draw_small_text("HI-SCORE",35,HEADER_LABEL_Y);
        draw_small_number(HI_DIGITS_X,HEADER_DIGITS_Y,hi_score_value);
        tgi_setcolor(SCORE_PEN);
        draw_small_text("SCORE<2>",69,HEADER_LABEL_Y);
        draw_small_number(SCORE2_DIGITS_X,HEADER_DIGITS_Y,0);
        page_shown_score[page]=score_value;
        page_shown_hi[page]=hi_score_value;
        return;
    }
    /* Steady state: redraw only digit cells whose value changed. */
    div=1000;
    for(i=0;i<4;++i) {
        d=(unsigned char)((score_value/div)%10);
        sd=(unsigned char)((page_shown_score[page]/div)%10);
        if(d!=sd) {
            erase_small_cell((unsigned char)(SCORE_DIGITS_X+4*i),HEADER_DIGITS_Y);
            tgi_setcolor(SCORE_PEN);
            draw_small_char((unsigned char)(26+d),(unsigned char)(SCORE_DIGITS_X+4*i),HEADER_DIGITS_Y);
        }
        d=(unsigned char)((hi_score_value/div)%10);
        sd=(unsigned char)((page_shown_hi[page]/div)%10);
        if(d!=sd) {
            erase_small_cell((unsigned char)(HI_DIGITS_X+4*i),HEADER_DIGITS_Y);
            tgi_setcolor(HI_PEN);
            draw_small_char((unsigned char)(26+d),(unsigned char)(HI_DIGITS_X+4*i),HEADER_DIGITS_Y);
        }
        div/=10;
    }
    page_shown_score[page]=score_value;
    page_shown_hi[page]=hi_score_value;
}
/* Re-plot the header ink of one portrait column after the player shot
 * passed through it (its erase leaves a 1-column scar). Ink only, so the
 * shot itself stays visible in blank parts of the column. */
static void repair_header_column(unsigned char x) {
    static const char lab1[]="SCORE<1>";
    static const char lab2[]="HI-SCORE";
    static const char lab3[]="SCORE<2>";
    const char *s;
    unsigned char base, idx, lx, r, code, pen;
    unsigned int v;
    /* Label row (y 18..22), small font: three label zones. */
    s=0; base=0; pen=SCORE_PEN;
    if(x>=1 && x<33) { base=1; s=lab1; pen=SCORE_PEN; }
    else if(x>=35 && x<67) { base=35; s=lab2; pen=HI_PEN; }
    else if(x>=69) { base=69; s=lab3; pen=SCORE_PEN; }
    if(s) {
        idx=(unsigned char)((x-base)/4); lx=(unsigned char)((x-base)%4);
        if(lx<3) {
            code=small_code(s[idx]);
            tgi_setcolor(pen);
            for(r=0;r<5;++r)
                if(small_font[code][r]&(4>>lx)) header_pixel(x,(unsigned char)(HEADER_LABEL_Y+r));
        }
    }
    /* Digit row (y 26..30). */
    base=0; v=0;
    if(x>=SCORE_DIGITS_X && x<SCORE_DIGITS_X+16) { base=SCORE_DIGITS_X; pen=SCORE_PEN; v=score_value; }
    else if(x>=HI_DIGITS_X && x<HI_DIGITS_X+16) { base=HI_DIGITS_X; pen=HI_PEN; v=hi_score_value; }
    else if(x>=SCORE2_DIGITS_X && x<SCORE2_DIGITS_X+16) { base=SCORE2_DIGITS_X; pen=SCORE_PEN; v=0; }
    if(base) {
        unsigned int div=1000;
        unsigned char k;
        idx=(unsigned char)((x-base)/4); lx=(unsigned char)((x-base)%4);
        if(lx<3) {
            for(k=0;k<idx;++k) div/=10;
            code=(unsigned char)(26+(v/div)%10);
            tgi_setcolor(pen);
            for(r=0;r<5;++r)
                if(small_font[code][r]&(4>>lx)) header_pixel(x,(unsigned char)(HEADER_DIGITS_Y+r));
        }
    }
}

/* --- v47 helpers --- */
static void draw_ship_icon(unsigned char x) {
    unsigned char row, col, start, mask;
    tgi_setcolor(V45_SHIP_PEN);
    for(row=0; row<7; ++row) {
#if V45_SHIP_B
        mask=ship_b_rows[row];
#else
        mask=ship_a_rows[row];
#endif
        col=0;
        while(col<7) {
            if(!(mask & (unsigned char)(0x40u >> col))) { ++col; continue; }
            start=col;
            do { ++col; } while(col<7 && (mask & (unsigned char)(0x40u >> col)));
            /* The ship shape's ink occupies rows 0..3 of its cell;
             * shift down 3 so the icon's bottom aligns with the
             * lives digit (ink rows 151..157). */
            tgi_bar((unsigned char)(154+row),
                    (unsigned char)(101-x-(col-1)),
                    (unsigned char)(154+row),
                    (unsigned char)(101-x-start));
        }
    }
    tgi_setcolor(1);
}
/* Bottom band (portrait y 149..158): remaining-ships digit at x=4 and one
 * spare-ship icon per ship beyond the one in play. Redrawn only on change. */
static void paint_lives(unsigned char page) {
    unsigned char i;
    if(page_shown_ships[page]==ships_left) return;
    tgi_setcolor(0);
    tgi_bar(149,0,158,101);
    tgi_setcolor(SCORE_PEN);
    if(ships_left>0) {
        draw_font_char((unsigned char)(26+ships_left),4,151);
        for(i=1;i<ships_left;++i) draw_ship_icon((unsigned char)(16+9*(i-1)));
    }
    page_shown_ships[page]=ships_left;
}
/* r24: input buffering applies only when the formation is about to
 * land -- the lowest living alien at ay>=128 (the last two descent
 * positions, 128 and the flush 133). Everywhere else the press timing
 * stays exactly as it was. */
static unsigned char near_occupation(void) {
    unsigned char i;
    for(i=0;i<ALIEN_COUNT;++i)
        if(alien_alive[i] && (unsigned char)(base_y[i]+offset_y[i])>=128) return 1;
    return 0;
}
static unsigned char formation_reached_bottom(void) {
    unsigned char i;
    for(i=0;i<ALIEN_COUNT;++i)
        if(alien_alive[i] && (unsigned char)(base_y[i]+offset_y[i]+8)>=142) return 1; /* r16: occupation as soon as the lowest alien's bottom row (ay+6) reaches the ship's top row (140) and the sprites overlap in height. r20: with the start shifted +1 (base 43), the stage-1 descent sequence lands exactly on the flush position ay=133 (bottom row 139, one row above the ship) and marches there; the next descent (138) crosses this line. */
    return 0;
}
static void v45_choose_sweep_step(void);
/* Start a wave: rebuild the formation (starting lower on later stages),
 * restore shields, clear shots and sweep state. Mirrors the startup init;
 * startup itself keeps its original proven code path. */
static void start_wave(unsigned char stage) {
    unsigned char i, r, c, drop;
    wave_end_synced=0;
    march_timer=MARCH_FIRST; march_note=0; march_pending=0; /* v50: the march restarts with the rack */
    ship_force_hidden=0;
    drop=(unsigned char)((stage-1)*10);
    if(drop>30) drop=30; /* r21: +10 per stage (user decision, 2026-11-20) so every stage's start stays on the same 5-dot phase (43+drop+40 = 83/93/103/113, all landing on the flush position ay=133); was +8 (max +24) through r20. */
    for(i=0;i<ALIEN_COUNT;++i) {
        r=(unsigned char)(i/COLUMNS);
        c=(unsigned char)(i%COLUMNS);
        col_of[i]=c;
        base_x[i]=(unsigned char)(7+8*c);
        base_y[i]=(unsigned char)(48+drop+10*r); /* r20: start +1 so the 5-dot descent sequence lands on the flush position ay=133 (shift the start, do not clip the final descent). r22: +5 more (48) at the user's choice so the descent count to 133 is odd (9/7/5/3 by stage) and the formation lands on 133 marching LEFT, matching the arcade; the +10 stage drops keep every stage on the same phase. */
        type_of[i]=(r==0)?2:((r<3)?1:0);
        offset_x[i]=0; offset_y[i]=0;
#if TEST_FREEZE
        walk_frame[i]=TEST_POSE;
#else
        walk_frame[i]=0;
#endif
        alien_alive[i]=1;
    }
    alive_count=ALIEN_COUNT;
    last_live_id=54;
#if V45_START_ALIENS < ALIEN_COUNT
    alive_count=V45_START_ALIENS;
    for(i=0;i<ALIEN_COUNT;++i)
        alien_alive[rom_to_lynx(i)]=(unsigned char)(i<V45_START_ALIENS);
    last_live_id=(unsigned char)(V45_START_ALIENS-1);
    intro_mode=0;
#endif
    for(i=0;i<SHIELD_COUNT;++i) {
        for(r=0;r<SHIELD_H;++r) shield_bits[i][r]=shield_initial[r];
        shield_dirty[0][i]=1;
        shield_dirty[1][i]=1;
    }
    shot_active=0;
    reset_bombs();
    clear_bursts();
    ufo_reset_wave();
    direction=1;
    descent_pending=0;
    completed_sweeps=0;
    next_rom_id=0;
    previous_id=NONE;
    sweep_boundary=0;
    stage_clear_ticks=0;
    page_full[0]=1;
    page_full[1]=1;
    v45_choose_sweep_step();
}
static void reset_game(void) {
    snd_stop(); /* v50: silence whatever the previous game left playing */
    intro_mode=0; /* v49: run_intro no longer gates the formation;
                   * attract/demo entries come through here too */
    score_value=0;
    ships_left=3;
    stage_no=1;
    extend_awarded=0;
    game_over=0;
    game_over_ticks=0;
    ship_force_hidden=0;
    ship_hit_ticks=0;
    explosion_ticks=0;
    ship_x=SHIP_START_X;
    start_wave(1);
}
static void check_shot_hit(unsigned char swept_top,
                           unsigned char swept_bottom) {
    unsigned char i, row, pose, best_id=NONE;
    unsigned char best_y=0, pixel_y, relative_x;
    unsigned char ay;
    int ax, bx;
    if(!shot_active) return;
    if(formation_band_miss(swept_top,swept_bottom)) return; /* r10 */
    bx=(int)shot_x;
    for(i=0; i<ALIEN_COUNT; ++i) {
        if(!alien_alive[i]) continue;
        /* Cheap band reject first (offset_y is unsigned, so this sum
         * is exact in 8 bits): only the one or two rows inside the
         * swept band reach the int column math below. This scan runs
         * on every iteration while the shot flies, and the main loop
         * sits at a display-timing edge, so the full int scan measurably
         * dropped the update rate while firing (user report, r10). */
        ay=(unsigned char)(base_y[i]+offset_y[i]);
        if((unsigned char)(ay+7)<=swept_top || ay>=swept_bottom) continue;
        ax=(int)base_x[i]+offset_x[i];
        if(bx<ax || bx>=ax+7) continue;
        relative_x=(unsigned char)(bx-ax);
        pose=(unsigned char)(type_of[i]+(walk_frame[i]?3:0));
        for(row=0; row<7; ++row) {
            pixel_y=(unsigned char)(ay+row);
            if(pixel_y<swept_top || pixel_y>=swept_bottom) continue;
            if(!hit_pixel(pose,relative_x,row)) continue;
            if(best_id==NONE || pixel_y>best_y) {
                best_id=i;
                best_y=pixel_y;
            }
        }
    }
    if(best_id==NONE) return;
    alien_alive[best_id]=0;
    if(!demo_mode) snd_play(SND_KILLED); /* v50 */
    add_score(alien_points[type_of[best_id]]);
    --alive_count;
    if(alive_count && best_id==rom_to_lynx(last_live_id)) {
        unsigned char id;
        for(id=ALIEN_COUNT; id>0; ) {
            --id;
            if(alien_alive[rom_to_lynx(id)]) {
                last_live_id=id;
                break;
            }
        }
    }
    explosion_x=(unsigned char)((int)base_x[best_id]+offset_x[best_id]);
    explosion_y=(unsigned char)((int)base_y[best_id]+offset_y[best_id]);
    explosion_ticks=EXPLOSION_FRAMES;
    shot_active=0;
#if HIT_DRAW_DELTA
    /* Mark both pages for targeted erase. The inactive page contains
     * the old alien, potentially at a different walking position. */
    killed_id=best_id;
    explosion_id=best_id;
    page_kill_pending[0]=1;
    page_kill_pending[1]=1;
#else
    /* Original V42 reference behavior. */
    page_full[0]=1;
    page_full[1]=1;
#endif
}

#ifndef V45_EXPLOSION_COLOR
#define V45_EXPLOSION_COLOR 1
#endif
static void paint_explosion(void) {
    if(!explosion_ticks) return;
#if V45_ALIEN_COLOR && V45_EXPLOSION_COLOR
    /* Match the killed alien's position band, not the last drawn alien. */
    sprite.penpal[0]=v45_alien_color_at((unsigned char)(explosion_y+3));
#else
    sprite.penpal[0]=0x0F;
#endif
    sprite.data=explosion_sprite;
    sprite.hpos=explosion_y;
    sprite.vpos=(unsigned char)(101-(explosion_x+6));
    tgi_sprite(&sprite);
}
/* Explosion lasts EXPLOSION_FRAMES display updates. A full page refresh on
 * expiration removes it cleanly from BOTH alternating display buffers. */
static void advance_explosion(void) {
    if(!explosion_ticks) return;
    --explosion_ticks;
    if(!explosion_ticks) {
#if !HIT_DRAW_DELTA
        page_full[0]=1;
        page_full[1]=1;
#endif
        /* In delta mode each page clears its own explosion footprint
         * the next time that page becomes the hidden draw page. */
    }
}

/* Advance ROM IDs in ascending order, skipping dead enemies without
 * consuming a display frame. Return NONE when no aliens remain.
 * A sweep boundary follows the highest surviving ROM ID. */
static unsigned char next_living_alien(void) {
    unsigned char tries, id, n;
    if(alive_count==0) return NONE;
    sweep_boundary=0;
    for(tries=0; tries<ALIEN_COUNT; ++tries) {
        id=next_rom_id;
        ++next_rom_id;
        if(next_rom_id==ALIEN_COUNT) next_rom_id=0;
        n=rom_to_lynx(id);
        if(alien_alive[n]) {
            if(id==last_live_id) {
                next_rom_id=0;
                sweep_boundary=1;
            }
            return n;
        }
    }
    return NONE;
}

/* ---- r25: laser vs laser -------------------------------------------
 * The arcade has no explicit winner table for a shot/bomb meeting:
 * both projectiles are drawn sprites, a draw that lands on lit pixels
 * kills its owner, and the dead projectile's explosion stays on screen
 * briefly as an obstacle for the other one. The three outcomes (bomb
 * wins / shot wins / both die) emerge from that mechanism. Verified
 * against the arcade ROM (probe runs, Nov 2026): a centre hit
 * kills the shot on its own step and the bomb on its next step into
 * the shot's explosion (both die); a bomb that steps into the shot
 * dies first and the shot then dies stepping into the bomb's
 * explosion (both die again, other order). Survivors are the grazes:
 * whoever's remaining path misses the lingering burst's lit pixels.
 * The port reproduces the mechanism with its own sprites: a dead
 * projectile leaves a 5x5 burst (sparse star: centre column and the
 * far corners, so a bomb whose centre column misses the burst's
 * centre can slip past on its dark columns, while a dead-centre
 * meeting almost always takes both). The burst is an obstacle only,
 * drawn on both pages like the projectiles themselves. */
#ifndef SHOT_BURST_TTL
#define SHOT_BURST_TTL 5
#endif
#ifndef BOMB_BURST_TTL
#define BOMB_BURST_TTL 4
#endif
static const unsigned char burst_bits[5]={0x04,0x11,0x04,0x11,0x04};
static unsigned char shot_burst_active=0, shot_burst_x=0, shot_burst_y=0,
                     shot_burst_ttl=0;
static unsigned char bomb_burst_active=0, bomb_burst_x=0, bomb_burst_y=0,
                     bomb_burst_ttl=0;
static unsigned char page_shot_burst[2], page_shot_burst_x[2],
                     page_shot_burst_y[2];
static unsigned char page_bomb_burst[2], page_bomb_burst_x[2],
                     page_bomb_burst_y[2];
static unsigned char v45_enemy_bullet_solid_at(unsigned char type,
                     unsigned char frame, unsigned char bx, unsigned char by);
static unsigned char burst_lit(int dx, int dy) {
    if(dx<-2 || dx>2 || dy<0 || dy>4) return 0;
    return (unsigned char)((burst_bits[dy] >> (2-dx)) & 1);
}
static void spawn_shot_burst(unsigned char x, unsigned char y) {
    shot_burst_active=1;
    shot_burst_x=x;
    shot_burst_y=(unsigned char)(y>0 ? y-1 : 0);
    shot_burst_ttl=SHOT_BURST_TTL;
}
static void spawn_bomb_burst(unsigned char x, unsigned char y) {
    bomb_burst_active=1;
    bomb_burst_x=x;
    bomb_burst_y=y;
    bomb_burst_ttl=BOMB_BURST_TTL;
}
static void clear_bursts(void) {
    shot_burst_active=0;
    bomb_burst_active=0;
}
static void age_bursts(void) {
    if(shot_burst_active && --shot_burst_ttl==0) shot_burst_active=0;
    if(bomb_burst_active && --bomb_burst_ttl==0) bomb_burst_active=0;
}
/* ---- v48: the mystery saucer ---------------------------------------
 * Appearance, flight and scoring follow the arcade ROM, verified by
 * disassembly and probe runs.
 * Timer: 1536 arcade frames (~25.6 s) between saucers, counted only
 * while the fleet is below its rack-start height (stage 1: after the
 * first descent; later stages start lower, so from the wave start).
 * The timer and the mystery cycle reset at every wave start (the
 * arcade re-mirrors its RAM there); a lost ship resets neither.
 * Launch needs >= 8 aliens alive. Flight crosses the lane y34..40 at
 * 1 px per update (~3 s), direction alternating per appearance (the arcade
 * re-rolls it from a ROM byte walk per shot removal -- simplified).
 * The timer pauses while a saucer is up (the arcade keeps counting;
 * with a 1024-update interval against a ~250-update flight+display,
 * the difference is when the next one can come, never whether).
 * Hit: the saucer's explosion sprite shows briefly, then the score
 * value, while the world keeps moving. Score: the mystery table
 * indexed by (shots fired this wave, the killing shot included)
 * mod 15 -- the arcade wraps its pointer after 15 entries, so index
 * 15 (50) never comes up; kept faithfully. The 300 sits at index 8:
 * the 8th, 23rd, 38th ... shot of a wave. */
#define UFO_LANE_Y 34
#define UFO_CELL_W 10
#define UFO_CELL_H 6
#define UFO_EXP_W 12   /* explosion cell, drawn at ufo_x-1 */
#define UFO_EXP_DX (-1)
#ifndef UFO_INTERVAL
#define UFO_INTERVAL 1024 /* updates between saucers (~25 s) */
#endif
#ifndef UFO_MOVE_DIV
#define UFO_MOVE_DIV 1    /* 1 px per this many updates */
#endif
#ifndef UFO_BURST_TTL
#define UFO_BURST_TTL 16  /* explosion display, updates */
#endif
#ifndef UFO_SCORE_TTL
#define UFO_SCORE_TTL 40  /* score value display, updates */
#endif
/* The arcade saucer (ROM SpriteSaucer, 24x8 cell, 16 px of ink) at
 * the port's alien scale: the port's aliens are 7x7 where the arcade's
 * are ~12x8, so the saucer is redrawn 11x7 (r1, at the user's
 * request -- the full-size one dwarfed the fleet). The current
 * 10x6 shape is the user's own redraw (2026-11-23): one size
 * smaller still, with a lower dome; the window row was redrawn
 * the same evening (###.##.###) at the user's request. The explosion is the ROM
 * SpriteSaucerExp downsampled, trimmed to 12x6 to match. Painting walks precomputed
 * runs (one tgi_bar each); the r0 build painted through 32-bit row
 * masks whose variable shifts are runtime calls on cc65, which cost
 * enough to feel while the saucer was up. */
static const unsigned int ufo_saucer_masks[6]={
    0x030,0x0FC,0x3B7,0x3FF,0x1B6,0x084}; /* bit 9 = leftmost */
static const unsigned char ufo_saucer_runs[11][3]={
    {0,4,2},{1,2,6},{2,0,3},{2,4,2},{2,7,3},{3,0,10},
    {4,1,2},{4,4,2},{4,7,2},{5,2,1},{5,7,1}};
/* ufo_explosion_runs comes from ../generated/sprites_data.h
 * (converted from the ROM saucer explosion at build time). */
/* ufo_points comes from ../generated/rom_data.h (ROM $1D54). */
static unsigned char ufo_state=0; /* 0 none, 1 flying, 2 burst, 3 score */
static int ufo_x=0;               /* cell left edge; may be off-screen */
static unsigned char ufo_dir=1, ufo_next_dir=1;
static unsigned char ufo_move_acc=0, ufo_ttl=0;
static unsigned int ufo_timer=UFO_INTERVAL;
static unsigned char ufo_pending=0;
static unsigned int ufo_cycle_base=0; /* shots_fired at wave start */
static unsigned int ufo_score_pts=0;
static unsigned char page_ufo_state[2]={0,0};
static int page_ufo_x[2]={0,0};
static unsigned int page_ufo_pts[2]={0,0};
static void add_score_wide(unsigned int pts) {
    while(pts>200) { add_score(200); pts=(unsigned int)(pts-200); }
    add_score((unsigned char)pts);
}
static void ufo_reset_wave(void) {
    ufo_state=0;
    ufo_pending=0;
    ufo_timer=UFO_INTERVAL;
    ufo_cycle_base=shots_fired;
}
static unsigned char ufo_armed(void) {
    /* The arcade stops counting while the reference alien is at its
     * rack-start height ($78). Port equivalent: stage 1 counts only
     * after the first descent; later stages start lower. */
    return (unsigned char)(stage_no>1 || offset_y[0]>0);
}
static void ufo_try_launch(void) {
    if(!ufo_pending || ufo_state!=0 || alive_count<8) return;
    ufo_pending=0;
    ufo_state=1;
    ufo_move_acc=0;
    ufo_dir=ufo_next_dir;
    ufo_next_dir=(unsigned char)!ufo_next_dir;
    ufo_x=ufo_dir ? -UFO_CELL_W : 102;
}
static void ufo_tick(void) {
    if(ufo_state==1) {
        if(++ufo_move_acc>=UFO_MOVE_DIV) {
            ufo_move_acc=0;
            ufo_x=(int)(ufo_x+(ufo_dir?1:-1));
            if((ufo_dir && ufo_x>=102) || (!ufo_dir && ufo_x<=-UFO_CELL_W))
                ufo_state=0;
        }
    } else if(ufo_state==2) {
        if(--ufo_ttl==0) { ufo_state=3; ufo_ttl=UFO_SCORE_TTL; }
    } else if(ufo_state==3) {
        if(--ufo_ttl==0) ufo_state=0;
    }
    if(ufo_state==0 && ufo_armed()) {
        if(ufo_pending) ufo_try_launch();
        else if(--ufo_timer==0) {
            ufo_timer=UFO_INTERVAL;
            ufo_pending=1;
            ufo_try_launch();
        }
    }
}
/* --- v50 sound: the march sequencer (engine in sndirq.asm) --- */
/* The arcade march notes run on a timer whose reload comes from a
 * table indexed by the number of aliens left (ROM $1A11/$1A21): the
 * first threshold <= alive_count gives the delay, in game-loop
 * iterations.  The port's update is the analogous unit. */
/* march_thresholds / march_delays come from ../generated/rom_data.h
 * (ROM $1A11 / $1A21). */
static unsigned char march_delay(unsigned char alive) {
    unsigned char i;
    unsigned int d=5;
    for(i=0;i<16;++i) if(alive>=march_thresholds[i]) { d=march_delays[i]; break; }
    d=(d*MARCH_TEMPO_NUM)/MARCH_TEMPO_DEN; /* v50 r6 tempo scale */
    if(d<2) d=2;
    return (unsigned char)d;
}
static void sound_update(void) {
    /* The UFO loop follows the saucer's flight in real play; a
     * one-shot that preempts it simply delays its restart. */
    if(ufo_state==1 && !demo_mode && !game_over) {
        if(!snd_active) snd_play(SND_UFO);
    } else if(snd_cur==SND_UFO) snd_stop();
    if(demo_mode || world_frozen || game_over || !alive_count) return;
    /* r2: the march is the game's heartbeat and must not be dropped.
     * On the arcade each sound has its own circuit and the march
     * always sounds on schedule; with one channel, a note that comes
     * due while a one-shot (shoot/killed/explosion/extend) owns the
     * channel is deferred until the channel frees, and a note may
     * cut in over the UFO loop (which resumes right after it). */
    if(march_pending && (!snd_active || snd_cur==SND_UFO)) {
        snd_play((unsigned char)(SND_MARCH1+march_note));
        march_note=(unsigned char)((march_note+1)&3);
        march_pending=0;
    }
    if(march_timer) { --march_timer; return; }
    /* The note comes due: reload the timer now, on schedule, whether
     * or not the note can start this update, so a deferral delays one
     * note without dragging the tempo of the following ones. */
    march_timer=(unsigned char)(march_delay(alive_count)-1);
    if(!snd_active || snd_cur==SND_UFO) {
        snd_play((unsigned char)(SND_MARCH1+march_note));
        march_note=(unsigned char)((march_note+1)&3);
    } else {
        march_pending=1;
    }
}
static unsigned char ufo_lit(int cx, int cy) {
    if(cx<0 || cx>=UFO_CELL_W || cy<0 || cy>=UFO_CELL_H) return 0;
    return (unsigned char)((ufo_saucer_masks[cy] >> (9-cx)) & 1);
}
/* The player's shot, swept over [top,bottom), against the saucer.
 * Mask-precise, like the bomb test. The lane (34..41) and the top
 * alien row (48..) are 7 px apart, more than one step's sweep, so a
 * step can never touch both. */
static unsigned char shot_meets_ufo(unsigned char top,
                                    unsigned char bottom) {
    unsigned char y0, y1, ry;
    int cx;
    if(ufo_state!=1) return 0;
    cx=(int)shot_x-ufo_x;
    if(cx<0 || cx>=UFO_CELL_W) return 0;
    y0=top>UFO_LANE_Y ? top : UFO_LANE_Y;
    y1=bottom<(UFO_LANE_Y+UFO_CELL_H) ? bottom : (UFO_LANE_Y+UFO_CELL_H);
    for(ry=y0;ry<y1;++ry)
        if(ufo_lit(cx,(int)ry-UFO_LANE_Y)) return 1;
    return 0;
}
static int ufo_text_x(int cellx) {
    int tx=(int)(cellx-4);
    if(tx<0) tx=0;
    if(tx>84) tx=84;
    return tx;
}
static void paint_ufo_runs(int x, const unsigned char *runs,
                           unsigned char count, unsigned char pen) {
    unsigned char i, py;
    int a, b;
    tgi_setcolor(pen);
    for(i=0;i<count;++i) {
        py=(unsigned char)(UFO_LANE_Y+runs[0]);
        a=(int)(x+(int)runs[1]);
        b=(int)(a+(int)runs[2]-1);
        runs+=3;
        if(b<0 || a>101) continue;
        if(a<0) a=0;
        if(b>101) b=101;
        tgi_bar(py,(unsigned char)(101-b),py,(unsigned char)(101-a));
    }
}
static void paint_ufo_score(int cellx, unsigned int pts) {
    unsigned char codes[3];
    codes[0]=(pts>=100) ? (unsigned char)(26+(pts/100)%10) : 38;
    codes[1]=(unsigned char)(26+(pts/10)%10);
    codes[2]=(unsigned char)(26+pts%10);
    tgi_setcolor(1);
    draw_font_text(codes,3,(unsigned char)ufo_text_x(cellx),UFO_LANE_Y);
}
static void erase_ufo_score(int cellx) {
    int tx=ufo_text_x(cellx);
    tgi_setcolor(0);
    tgi_bar(UFO_LANE_Y,(unsigned char)(101-(tx+17)),
            (unsigned char)(UFO_LANE_Y+6),(unsigned char)(101-tx));
}
/* The saucer, its explosion and its score text live in the lane,
 * whose background is plain black, so a page is reconciled by erasing
 * its own copy and painting the world state. Unlike the 5x5 bursts
 * this is skipped when nothing changed and no shot is crossing the
 * lane: the sprite is 24x8 and the main loop sits at a display-timing
 * edge, so a standing repaint every update could cost a rate step. */
static void update_ufo_page(unsigned char page) {
    unsigned char changed, shot_near;
    if(!ufo_state && !page_ufo_state[page]) return; /* idle: one test */
    changed=(unsigned char)(page_ufo_state[page]!=ufo_state ||
        page_ufo_x[page]!=ufo_x || page_ufo_pts[page]!=ufo_score_pts);
    shot_near=(unsigned char)(shot_active &&
        shot_y<(unsigned char)(UFO_LANE_Y+UFO_CELL_H+SHOT_HEIGHT));
    if(!changed && !shot_near) return;
    if(page_ufo_state[page]==1)
        paint_ufo_runs(page_ufo_x[page],&ufo_saucer_runs[0][0],11,0);
    else if(page_ufo_state[page]==2)
        paint_ufo_runs((int)(page_ufo_x[page]+UFO_EXP_DX),
                       &ufo_explosion_runs[0][0],UFO_EXPLOSION_RUN_COUNT,0);
    else if(page_ufo_state[page]==3)
        erase_ufo_score(page_ufo_x[page]);
    if(ufo_state==1)
        paint_ufo_runs(ufo_x,&ufo_saucer_runs[0][0],11,1);
    else if(ufo_state==2)
        paint_ufo_runs((int)(ufo_x+UFO_EXP_DX),&ufo_explosion_runs[0][0],UFO_EXPLOSION_RUN_COUNT,1);
    else if(ufo_state==3) paint_ufo_score(ufo_x,ufo_score_pts);
    page_ufo_state[page]=ufo_state;
    page_ufo_x[page]=ufo_x;
    page_ufo_pts[page]=ufo_score_pts;
}

/* The player's shot, swept over [top,bottom), against the enemy
 * bomb's current box. Mask-precise: only the bomb's lit pixels count. */
static unsigned char shot_meets_bomb(unsigned char top,
                                     unsigned char bottom,
                                     unsigned char bi) {
    unsigned char bx, by, y0, y1, ry;
    if(shot_x+1<bomb_x[bi] || shot_x>bomb_x[bi]+1) return 0;
    bx=(unsigned char)(shot_x-(bomb_x[bi]-1));
    y0=top>bomb_y[bi] ? top : bomb_y[bi];
    y1=bottom<(unsigned char)(bomb_y[bi]+ENEMY_SHOT_HEIGHT)
        ? bottom : (unsigned char)(bomb_y[bi]+ENEMY_SHOT_HEIGHT);
    for(ry=y0;ry<y1;++ry) {
        by=(unsigned char)(ry-bomb_y[bi]);
        if(v45_enemy_bullet_solid_at(bi,bomb_pose[bi],bx,by)) return 1;
    }
    return 0;
}
/* The player's shot, swept over [top,bottom), against a burst whose
 * box is centred on column cx and starts at row cy. */
static unsigned char shot_meets_burst(unsigned char top,
                                      unsigned char bottom,
                                      unsigned char cx, unsigned char cy) {
    unsigned char y0, y1, ry;
    int dx=(int)shot_x-(int)cx;
    if(dx<-2 || dx>2) return 0;
    y0=top>cy ? top : cy;
    y1=bottom<(unsigned char)(cy+5) ? bottom : (unsigned char)(cy+5);
    for(ry=y0;ry<y1;++ry)
        if(burst_lit(dx,(int)ry-(int)cy)) return 1;
    return 0;
}
/* The enemy bomb, after stepping to top row ny, against the player's
 * shot's current body. */
static unsigned char bomb_meets_shot(unsigned char ny, unsigned char bi) {
    unsigned char bx, by, ry;
    if(shot_x+1<bomb_x[bi] || shot_x>bomb_x[bi]+1) return 0;
    bx=(unsigned char)(shot_x-(bomb_x[bi]-1));
    for(by=0;by<ENEMY_SHOT_HEIGHT;++by) {
        ry=(unsigned char)(ny+by);
        if(ry>=shot_y && ry<(unsigned char)(shot_y+SHOT_HEIGHT) &&
           v45_enemy_bullet_solid_at(bi,bomb_pose[bi],bx,by)) return 1;
    }
    return 0;
}
/* The enemy bomb, after stepping to top row ny, against a burst. */
static unsigned char bomb_meets_burst(unsigned char ny,
                                      unsigned char cx, unsigned char cy,
                                      unsigned char bi) {
    unsigned char bx, by, ry;
    int dy;
    for(by=0;by<ENEMY_SHOT_HEIGHT;++by) {
        ry=(unsigned char)(ny+by);
        dy=(int)ry-(int)cy;
        if(dy<0 || dy>4) continue;
        for(bx=0;bx<3;++bx) {
            if(v45_enemy_bullet_solid_at(bi,bomb_pose[bi],bx,by) &&
               burst_lit((int)(bomb_x[bi]-1+bx)-(int)cx,dy)) return 1;
        }
    }
    return 0;
}
static unsigned char shot_meets_any_bomb(unsigned char top,
                                         unsigned char bottom) {
    unsigned char i;
    for(i=0;i<3;++i)
        if(bomb_active[i] && shot_meets_bomb(top,bottom,i)) return 1;
    return 0;
}

/* Check the full path from old to new Y so fast shots cannot skip aliens.
 * The 1x3 shot covers [new_y, old_y + SHOT_HEIGHT) in portrait Y.
 * This keeps the V36/V37 rectangle collision semantics. */
static void advance_shot(void) {
    unsigned char old_y, new_y, swept_bottom;
    if(!shot_active) return;
    old_y=shot_y;
    if(old_y<=SHOT_TOP+SHOT_SPEED) new_y=SHOT_TOP;
    else new_y=(unsigned char)(old_y-SHOT_SPEED);
    swept_bottom=(unsigned char)(old_y+SHOT_HEIGHT);
    /* Point-blank blind spot (r13): the shot materialises at SHIP_Y-7,
     * so invader rows at or below its spawn box are never swept during
     * flight. The lower the invader, the fewer of its rows can be hit --
     * at the lowest positions only the top row's centre pixel connects,
     * which breaks the arcade's point-blank shot (Nagoya-uchi). On the
     * spawn step only, extend the sweep down to the cannon's base so
     * anything directly in front of the cannon is hit. */
    if(old_y==(unsigned char)(SHIP_Y-7) &&
       swept_bottom<(unsigned char)(SHIP_Y+7))
        swept_bottom=(unsigned char)(SHIP_Y+7);
    shot_y=new_y;
    /* A projectile reaches the shields before it reaches the invaders. */
    if(!check_shot_shield(new_y,swept_bottom)) {
        /* r25: on the way up the shot first crosses the burst a dead
         * bomb left, then the live bomb, then the invaders. Stepping
         * into either kills the shot and leaves its own burst; the
         * bomb's fate is decided when the bomb steps into it. */
        if(bomb_burst_active &&
           shot_meets_burst(new_y,swept_bottom,bomb_burst_x,bomb_burst_y)) {
            shot_active=0;
            spawn_shot_burst(shot_x,new_y);
        } else if(shot_meets_any_bomb(new_y,swept_bottom)) {
            shot_active=0;
            spawn_shot_burst(shot_x,new_y);
        } else if(ufo_state==1 && shot_meets_ufo(new_y,swept_bottom)) {
            /* v48: the saucer dies, the shot with it; the explosion
             * and then the score value show in the lane (ufo_tick). */
            shot_active=0;
            ufo_state=2;
            ufo_ttl=UFO_BURST_TTL;
            if(!demo_mode) snd_play(SND_UFOHIT); /* v50 r4: the saucer has its own sound (ufo_lowpitch); the flight loop stops via sound_update */
            ufo_score_pts=ufo_points[(shots_fired-ufo_cycle_base)%15];
            add_score_wide(ufo_score_pts);
        } else {
            check_shot_hit(new_y,swept_bottom);
        }
    }
    if(shot_active && new_y==SHOT_TOP) shot_active=0;
}
/* Both pages are reconciled with the current logical bullet state. */
static void update_shot_page(unsigned char page) {
    if(page_shot_active[page]) {
        erase_shot(page_shot_x[page],page_shot_y[page]);
        restore_shot_background(page_shot_x[page],page_shot_y[page]);
        restore_shield_under_shot(page_shot_x[page],page_shot_y[page]);
        if(page_shot_y[page]<HEADER_BAND) repair_header_column(page_shot_x[page]);
    }
    if(shot_active) paint_shot(shot_x,shot_y);
    if(shot_active && shot_y<HEADER_BAND) repair_header_column(shot_x);
    page_shot_active[page]=shot_active;
    page_shot_x[page]=shot_x;
    page_shot_y[page]=shot_y;
}

/* v49 r3: fire one bomb of the given type from the lowest living
 * alien of the given column (the arcade's FindInColumn). A column
 * whose lowest alien is already at the ship's row stays silent. */
static unsigned char spawn_bomb(unsigned char type, unsigned char col) {
    unsigned char row, n;
    for(row=ROWS;row>0;) {
        --row;
        n=(unsigned char)(row*COLUMNS+col);
        if(!alien_alive[n]) continue;
        bomb_y[type]=(unsigned char)(base_y[n]+offset_y[n]+7);
        if(bomb_y[type]>=SHIP_Y) return 0;
        bomb_x[type]=(unsigned char)((int)base_x[n]+offset_x[n]+3);
        bomb_active[type]=1;
        bomb_pose[type]=0;
        bomb_tick[type]=0;
        bomb_steps[type]=0;
        return 1;
    }
    return 0;
}
/* Rolling aims at the ship: the column whose lowest living alien is
 * nearest the ship's centre. 0xFF when no alien is alive. */
static unsigned char rolling_column(void) {
    unsigned char col, row, n, best=0xFF;
    int ship_c=(int)ship_x+3, best_d=1000, d;
    for(col=0;col<COLUMNS;++col) {
        for(row=ROWS;row>0;) {
            --row;
            n=(unsigned char)(row*COLUMNS+col);
            if(!alien_alive[n]) continue;
            d=(int)base_x[n]+offset_x[n]+3-ship_c;
            if(d<0) d=-d;
            if(d<best_d) { best_d=d; best=col; }
            break; /* only the lowest alien of the column counts */
        }
    }
    return best;
}
/* The arcade's score-linked reload rate (AShotReloadRate): a shot
 * may fire only once both other shots have travelled at least this
 * many steps since they fired. The bands follow the arcade table's
 * BCD high byte (<=0x02/0x10/0x20/0x30 -> 48/16/11/8, else 7). */
static unsigned char reload_rate_compute(void) {
    /* reload_bounds / reload_rates come from ../generated/rom_data.h
     * (ROM $1CB8 thresholds, converted to integer score bounds,
     * and the rate table at ROM $1AA1). */
    unsigned char i;
    for(i=0;i<4;++i) if(score_value<=reload_bounds[i]) return reload_rates[i];
    return reload_rates[4];
}
/* One type is considered per update, in rotation (the arcade's
 * shotSync). The gates are the arcade's: the reload gate above,
 * plunger silent against the last alien, squiggly gated on the UFO
 * sharing its object and on >= 8 aliens, and rolling skipping its
 * first opportunity after each re-initialisation. */
static unsigned char reload_rate(void) {
    /* The score changes only on a kill, so cache the rate. */
    static unsigned int cached_score=0xFFFF;
    static unsigned char cached_rate=48;
    if(score_value!=cached_score) {
        cached_score=score_value;
        cached_rate=reload_rate_compute();
    }
    return cached_rate;
}
static void fire_control(void) {
    unsigned char t=shot_sync, o1, o2, rate, col;
    if(++shot_sync==3) shot_sync=0;
    if(bomb_active[t]) return;
    rate=reload_rate();
    o1=(unsigned char)(t==2?0:t+1);
    o2=(unsigned char)(t==0?2:t-1);
    if(bomb_steps[o1]<rate || bomb_steps[o2]<rate) return;
    if(t==BOMB_PLUNGER && alive_count<=1) return;
    if(t==BOMB_SQUIGGLY && (alive_count<8 || ufo_state!=0)) return;
    if(t==BOMB_ROLLING) {
        if(rolling_skip) { rolling_skip=0; return; }
        col=rolling_column();
        if(col==0xFF) return;
    } else if(t==BOMB_PLUNGER) {
        col=(unsigned char)(col_fire_table[plunger_idx]-1);
    } else {
        col=(unsigned char)(col_fire_table[squiggly_idx]-1);
    }
    if(spawn_bomb(t,col)) {
        if(t==BOMB_PLUNGER) {
            if(++plunger_idx>15) plunger_idx=0;
        } else if(t==BOMB_SQUIGGLY) {
            if(++squiggly_idx>20) squiggly_idx=6;
        }
    }
}
/* E0-E3: isolate loops, bit condition, and computed coordinates.
 * The rest of the D0 success baseline is unchanged. */
static void paint_enemy_bullet(unsigned char x,unsigned char y,
                               unsigned char type,unsigned char frame) {
#if V44_E_MODE == 0
    /* E0: D0's seven explicit pixel bars, unchanged. */
    tgi_setcolor(1);
    tgi_bar(y,(unsigned char)(101-x),y,(unsigned char)(101-x));
    tgi_bar((unsigned char)(y+1),(unsigned char)(102-x),(unsigned char)(y+1),(unsigned char)(102-x));
    tgi_bar((unsigned char)(y+1),(unsigned char)(101-x),(unsigned char)(y+1),(unsigned char)(101-x));
    tgi_bar((unsigned char)(y+2),(unsigned char)(101-x),(unsigned char)(y+2),(unsigned char)(101-x));
    tgi_bar((unsigned char)(y+3),(unsigned char)(101-x),(unsigned char)(y+3),(unsigned char)(101-x));
    tgi_bar((unsigned char)(y+3),(unsigned char)(100-x),(unsigned char)(y+3),(unsigned char)(100-x));
    tgi_bar((unsigned char)(y+4),(unsigned char)(101-x),(unsigned char)(y+4),(unsigned char)(101-x));
#elif V44_E_MODE == 1
    /* E1: loop only. Coordinates are constant offsets, no bit tests. */
    static const unsigned char px[7]={1,0,1,1,1,2,1};
    static const unsigned char py[7]={0,1,1,2,3,3,4};
    unsigned char i;
    tgi_setcolor(1);
    for(i=0;i<7;++i) {
        tgi_bar((unsigned char)(y+py[i]),
                (unsigned char)(102-x-px[i]),
                (unsigned char)(y+py[i]),
                (unsigned char)(102-x-px[i]));
    }
#elif V44_E_MODE == 2
    /* E2: bit tests, but explicit fixed pixel coordinates and no loops.
     * Each mask test corresponds to one of the seven E0 dots. */
    tgi_setcolor(1);
    if(2u & 2u) tgi_bar(y,(unsigned char)(101-x),y,(unsigned char)(101-x));
    if(6u & 4u) tgi_bar((unsigned char)(y+1),(unsigned char)(102-x),(unsigned char)(y+1),(unsigned char)(102-x));
    if(6u & 2u) tgi_bar((unsigned char)(y+1),(unsigned char)(101-x),(unsigned char)(y+1),(unsigned char)(101-x));
    if(2u & 2u) tgi_bar((unsigned char)(y+2),(unsigned char)(101-x),(unsigned char)(y+2),(unsigned char)(101-x));
    if(3u & 2u) tgi_bar((unsigned char)(y+3),(unsigned char)(101-x),(unsigned char)(y+3),(unsigned char)(101-x));
    if(3u & 1u) tgi_bar((unsigned char)(y+3),(unsigned char)(100-x),(unsigned char)(y+3),(unsigned char)(100-x));
    if(2u & 2u) tgi_bar((unsigned char)(y+4),(unsigned char)(101-x),(unsigned char)(y+4),(unsigned char)(101-x));
#else
    /* E3 (r3): one Suzy sprite per bomb, the aliens' own draw path.
     * The per-pixel bar loop this replaces cost ~5-9 tgi_bar calls
     * per bomb per page and dropped the update rate a full tier
     * once three bombs could fly. The colour is the bomb's middle
     * row's, the same single-colour compromise the aliens make. */
#if V45_BULLET_COLOR
    sprite.penpal[0]=v45_alien_color_at((unsigned char)(y+2));
#else
    sprite.penpal[0]=1;
#endif
    sprite.data=&bomb_spr[type][frame][0];
    sprite.hpos=y;
    sprite.vpos=(unsigned char)(101-(x+1));
    tgi_sprite(&sprite);
#endif
}
/* Gap1-style tight erase, with selective restoration for variable lanes.
 * Portrait X is the centered projectile coordinate. E3 draws x-1..x+1.
 * Shield pixels are restored ONLY when the tight footprint intersects
 * actual surviving shield pixels. This avoids the costly 7x7 scan when
 * the bullet travels in a gap, without filling previously eroded holes. */
static void erase_enemy_bullet(unsigned char x,unsigned char y) {
    tgi_setcolor(0);
    tgi_bar(y,(unsigned char)(100-x),
            (unsigned char)(y+4),(unsigned char)(102-x));
}
static void restore_enemy_shot_shield(unsigned char x,unsigned char y) {
    unsigned char k,xx,yy,lx,ly;
    unsigned char first,last,top,bottom;
    unsigned char left,right;
    /* Avoid unsigned wraparound near the edges. */
    first=(x>0)?(unsigned char)(x-1):0;
    last=(x<101)?(unsigned char)(x+1):101;
    top=y;
    bottom=(y<=155)?(unsigned char)(y+4):159;
    if(bottom<SHIELD_Y || top>=SHIELD_Y+SHIELD_H) return;
    if(top<SHIELD_Y) top=SHIELD_Y;
    if(bottom>=SHIELD_Y+SHIELD_H) bottom=SHIELD_Y+SHIELD_H-1;
    for(k=0;k<SHIELD_COUNT;++k) {
        left=shield_left[k];
        right=(unsigned char)(left+SHIELD_W-1);
        if(last<left || first>right) continue;
        for(xx=first;xx<=last;++xx) {
            if(xx<left || xx>right) continue;
            lx=(unsigned char)(xx-left);
            for(yy=top;yy<=bottom;++yy) {
                ly=(unsigned char)(yy-SHIELD_Y);
                if(shield_solid(k,lx,ly)) {
                    tgi_setcolor(V45_SHIELD_PEN);
                    tgi_bar(yy,(unsigned char)(101-xx),
                            yy,(unsigned char)(101-xx));
                }
            }
        }
    }
}
static unsigned char enemy_shot_hits_shield(unsigned char top,unsigned char bottom,unsigned char sx) {
#if ENEMY_SHOT_STAGE >= 1
    unsigned char yy,k,lx,ly,xx,yy2,x,dx;
    for(yy=top;yy<bottom;++yy) {
        if(yy<SHIELD_Y || yy>=SHIELD_Y+SHIELD_H) continue;
        for(dx=0;dx<ENEMY_SHOT_WIDTH;++dx) {
        x=(unsigned char)(sx+dx-1);
        for(k=0;k<SHIELD_COUNT;++k) {
            if(x<shield_left[k] || x>=shield_left[k]+SHIELD_W) continue;
            lx=(unsigned char)(x-shield_left[k]);
            ly=(unsigned char)(yy-SHIELD_Y);
            if(!shield_solid(k,lx,ly)) continue;
            /* 3x3 crater, centered on incoming upper pixel. */
            for(yy2=0;yy2<SHIELD_H;++yy2) {
                if(yy2+1<ly || yy2>ly+1) continue;
                for(xx=0;xx<SHIELD_W;++xx) {
                    if(xx+1<lx || xx>lx+1) continue;
                    shield_bits[k][yy2] &= (unsigned int)~(1u << (14-xx));
                }
            }
            shield_dirty[0][k]=1;
            shield_dirty[1][k]=1;
#if V45_SHIELD_LOAD_MODE == 3
            /* r17: mark the crater rows too (see check_shield_shield). */
            {
                unsigned int crater_rows=(unsigned int)(1u<<ly);
                if(ly>0) crater_rows |= (unsigned int)(1u<<(ly-1));
                if(ly<SHIELD_H-1) crater_rows |= (unsigned int)(1u<<(ly+1));
                v45_shield_dirty_rows[0][k] |= crater_rows;
                v45_shield_dirty_rows[1][k] |= crater_rows;
            }
#endif
            return 1;
        }
        }
    }
#else
    (void)top;(void)bottom;
#endif
    return 0;
}
/* V45 ship hit diagnostics: 0=legacy box, 1=7x4 box,
 * 2=ship pixel mask vs projectile box, 3=both pixel masks. */
#ifndef V45_SHIP_HIT_MODE
#define V45_SHIP_HIT_MODE 0
#endif
#if V45_SHIP_HIT_MODE < 0 || V45_SHIP_HIT_MODE > 3
#error V45_SHIP_HIT_MODE_must_be_0_to_3
#endif
#if V45_SHIP_HIT_MODE >= 2
static unsigned char v45_ship_solid_at(unsigned char sx, unsigned char sy) {
    static const unsigned char mask[4]={0x08,0x3E,0x7F,0x7F};
    if(sx>=7 || sy>=4) return 0;
    return (unsigned char)((mask[sy] & (unsigned char)(0x40u >> sx))!=0);
}
#endif
#if V45_SHIP_HIT_MODE == 3
static unsigned char v45_enemy_bullet_solid_at(unsigned char type, unsigned char frame, unsigned char bx, unsigned char by) {
    static const unsigned char cols[3]={4,2,1};
    if(type>=3 || frame>=4 || bx>=3 || by>=5) return 0;
    return (unsigned char)((bomb_bits[type][frame][by] & cols[bx])!=0);
}
#endif
static unsigned char v45_enemy_hits_ship(unsigned char top, unsigned char bottom, unsigned char bi) {
#if V45_SHIP_HIT_MODE == 0
    return (unsigned char)(bomb_x[bi]+1>=ship_x && bomb_x[bi]<=ship_x+7 &&
        bottom>SHIP_Y-3 && top<SHIP_Y+7);
#elif V45_SHIP_HIT_MODE == 1
    return (unsigned char)(bomb_x[bi]+1>=ship_x && bomb_x[bi]<=ship_x+6 &&
        bottom>SHIP_Y && top<SHIP_Y+4);
#else
    unsigned char bx,sy,by;
    int xx;
    unsigned char first=(top>4)?(unsigned char)(top-4):0;
    unsigned char last=bottom;
    if(last>SHIP_Y+4) last=SHIP_Y+4;
    /* At 1px/update, inspect current and next bullet positions.
     * This prevents missed overlaps while preserving the existing speed. */
    for(by=0;by<=ENEMY_SHOT_SPEED;++by) {
        unsigned char base=(unsigned char)(top+by);
        unsigned char row;
        for(row=0;row<5;++row) {
            unsigned char yy=(unsigned char)(base+row);
            if(yy<SHIP_Y || yy>=SHIP_Y+4) continue;
            sy=(unsigned char)(yy-SHIP_Y);
            for(bx=0;bx<3;++bx) {
#if V45_SHIP_HIT_MODE == 3
                if(!v45_enemy_bullet_solid_at(bi,bomb_pose[bi],bx,row)) continue;
#endif
                xx=(int)bomb_x[bi]+(int)bx-1-(int)ship_x;
                if(xx<0 || xx>=7) continue;
                if(v45_ship_solid_at((unsigned char)xx,sy)) return 1;
            }
        }
    }
    (void)first; (void)last;
    return 0;
#endif
}

/* The ship's break countdown, split out of advance_enemy_shot so the
 * main loop can run it on its own while the world is frozen. */
static void advance_ship_break(void) {
    if(ship_hit_ticks) {
        --ship_hit_ticks;
#if V45_COLOR_STEP >= 1
        if(!ship_hit_ticks) {
            if(demo_mode) demo_over=1; /* the arcade demo ends at the first hit */
            else if(ships_left>1) { --ships_left; ship_x=SHIP_START_X; }
            else { ships_left=0; game_over=1; game_over_ticks=0; ship_force_hidden=1; snd_stop(); }
        }
#endif
    }
}
static void advance_bombs(void) {
    unsigned char i, top, bottom, ny;
    fire_control();
    for(i=0;i<3;++i) {
        if(bomb_steps[i]<255) ++bomb_steps[i];
        if(!bomb_active[i]) continue;
        top=bomb_y[i];
        bottom=(unsigned char)(top+ENEMY_SHOT_SPEED+ENEMY_SHOT_HEIGHT);
        if(bottom>SHIP_Y+7) bottom=SHIP_Y+7;
        ny=(unsigned char)(top+ENEMY_SHOT_SPEED);
        /* r25 mechanics, per bomb: stepping into the player's shot
         * (or into the burst a dead shot left) kills the bomb and
         * leaves the bomb's own burst; the shot's fate is decided
         * when the shot steps into it. */
        if(shot_active && bomb_meets_shot(ny,i)) {
            bomb_active[i]=0;
            if(i==BOMB_ROLLING) rolling_skip=1;
            spawn_bomb_burst(bomb_x[i],ny);
            continue;
        }
        if(shot_burst_active &&
           bomb_meets_burst(ny,shot_burst_x,shot_burst_y,i)) {
            bomb_active[i]=0;
            if(i==BOMB_ROLLING) rolling_skip=1;
            spawn_bomb_burst(bomb_x[i],ny);
            continue;
        }
        if(bottom>SHIELD_Y && top<SHIELD_Y+SHIELD_H &&
           enemy_shot_hits_shield(top,bottom,bomb_x[i])) {
            bomb_active[i]=0;
            if(i==BOMB_ROLLING) rolling_skip=1;
            continue;
        }
#if ENEMY_SHOT_STAGE >= 2
        /* V45 diagnostic: compare old box with ship/bullet pixel masks. */
        if(!ship_hit_ticks && bottom>SHIP_Y-4 && top<SHIP_Y+7 &&
           v45_enemy_hits_ship(top,bottom,i)) {
            bomb_active[i]=0;
            if(i==BOMB_ROLLING) rolling_skip=1;
            ship_hit_ticks=V45_BREAK_TOTAL_TICKS;
            if(!demo_mode) snd_play(SND_EXPLOSION); /* v50 */
            shot_active=0; /* the player's shot dies with the ship */
            continue;
        }
#endif
        if(top+ENEMY_SHOT_SPEED>=SHIP_Y+7) {
            bomb_active[i]=0;
            if(i==BOMB_ROLLING) rolling_skip=1;
            continue;
        }
        bomb_y[i]=ny;
        ++bomb_tick[i];
        if(bomb_tick[i]>=ENEMY_BULLET_ANIM_TICKS) {
            bomb_tick[i]=0;
            ++bomb_pose[i];
            if(bomb_pose[i]==4) bomb_pose[i]=0;
        }
    }
}
static void update_enemy_shot_page(unsigned char page) {
    unsigned char i;
    for(i=0;i<3;++i) {
        if(page_bomb_active[page][i]) {
            erase_enemy_bullet(page_bomb_x[page][i],page_bomb_y[page][i]);
            if(page_bomb_y[page][i]+ENEMY_SHOT_HEIGHT>SHIELD_Y &&
               page_bomb_y[page][i]<SHIELD_Y+SHIELD_H)
                restore_enemy_shot_shield(page_bomb_x[page][i],
                                          page_bomb_y[page][i]);
            /* Restore ship pixels if the old projectile crossed the ship. */
            if(page_ship_visible[page] &&
               page_bomb_y[page][i]+ENEMY_SHOT_HEIGHT>SHIP_Y-3) {
                draw_ship(page_ship_x[page]);
            }
        }
        if(bomb_active[i]) {
            paint_enemy_bullet(bomb_x[i],bomb_y[i],i,bomb_pose[i]);
            /* The bullet is BEHIND intact shield pixels. Stage 0 passes
             * through the wall, but must never make temporary visual
             * holes. Restore the exact shield bits on this hidden page
             * immediately after the bullet is drawn, before the page
             * is displayed. */
            if(bomb_y[i]+ENEMY_SHOT_HEIGHT>SHIELD_Y &&
               bomb_y[i]<SHIELD_Y+SHIELD_H)
                restore_enemy_shot_shield(bomb_x[i],bomb_y[i]);
        }
        page_bomb_active[page][i]=bomb_active[i];
        page_bomb_x[page][i]=bomb_x[i];
        page_bomb_y[page][i]=bomb_y[i];
    }
}
/* r25: the laser-vs-laser bursts, drawn on both pages like the
 * projectiles themselves (world state + per-page drawn copies). */
static void paint_burst(unsigned char x, unsigned char y) {
    static const unsigned char col_bit[5]={16,8,4,2,1};
    unsigned char r, c, mask, nx, ny;
    for(r=0;r<5;++r) {
        mask=burst_bits[r];
        if(!mask) continue;
        ny=(unsigned char)(y+r);
#if V45_BULLET_COLOR
        tgi_setcolor(v45_alien_color_at(ny));
#else
        tgi_setcolor(1);
#endif
        for(c=0;c<5;++c) {
            if(mask & col_bit[c]) {
                nx=(unsigned char)(x-2+c);
                tgi_bar(ny,(unsigned char)(101-nx),ny,(unsigned char)(101-nx));
            }
        }
    }
}
static void erase_burst(unsigned char page, unsigned char x, unsigned char y) {
    static const unsigned char col_bit[5]={16,8,4,2,1};
    unsigned char r, c, mask, nx, ny;
    tgi_setcolor(0);
    for(r=0;r<5;++r) {
        mask=burst_bits[r];
        if(!mask) continue;
        ny=(unsigned char)(y+r);
        for(c=0;c<5;++c) {
            if(mask & col_bit[c]) {
                nx=(unsigned char)(x-2+c);
                tgi_bar(ny,(unsigned char)(101-nx),ny,(unsigned char)(101-nx));
            }
        }
    }
    restore_shield_rect((unsigned char)(x-2),y,5,5);
    /* If the burst sat over the ship, the blackened pixels may have
     * bitten it; force the ship's redraw on this page. */
    if((unsigned char)(y+4)>=SHIP_Y && y<(unsigned char)(SHIP_Y+7) &&
       (unsigned char)(x+2)>=ship_x && (unsigned char)(x-2)<(unsigned char)(ship_x+7))
        ship_repair[page]=1;
}
static void update_bursts_page(unsigned char page) {
    if(page_shot_burst[page])
        erase_burst(page,page_shot_burst_x[page],page_shot_burst_y[page]);
    if(shot_burst_active) paint_burst(shot_burst_x,shot_burst_y);
    page_shot_burst[page]=shot_burst_active;
    page_shot_burst_x[page]=shot_burst_x;
    page_shot_burst_y[page]=shot_burst_y;
    if(page_bomb_burst[page])
        erase_burst(page,page_bomb_burst_x[page],page_bomb_burst_y[page]);
    if(bomb_burst_active) paint_burst(bomb_burst_x,bomb_burst_y);
    page_bomb_burst[page]=bomb_burst_active;
    page_bomb_burst_x[page]=bomb_burst_x;
    page_bomb_burst_y[page]=bomb_burst_y;
}
static void draw_marker(unsigned char page) {
#if TIMING_MARKER
    unsigned char marker=(unsigned char)(completed_sweeps & 1);
    if(page_marker[page]!=marker) {
        tgi_setcolor(0);
        if(page_marker[page]) tgi_bar(10,4,19,13);
        else tgi_bar(24,4,33,13);
        tgi_setcolor(1);
        if(marker) tgi_bar(10,4,19,13);
        else tgi_bar(24,4,33,13);
        page_marker[page]=marker;
    }
#else
    (void)page;
#endif
}
static void save_snapshot(unsigned char n, unsigned char page) {
    page_x[page][n]=offset_x[n];
    page_y[page][n]=offset_y[n];
    page_frame[page][n]=walk_frame[n];
}
#if HIT_DRAW_DELTA
/* Old sprite footprint on the hidden page, from its per-page snapshot. */
static void add_neighborhood(unsigned char n);
static void erase_killed_on_page(unsigned char page) {
    if(!page_kill_pending[page]) return;
    erase_old(killed_id,page);
    page_kill_pending[page]=0;
}
/* The explosion itself can outlive the enemy, so each buffer tracks whether
 * it currently contains the 7x7 explosion and where it was drawn. */
static void reconcile_explosion_on_page(unsigned char page) {
    unsigned char x,y;
    if(page_explosion_drawn[page] &&
       (!explosion_ticks ||
        page_explosion_x[page]!=explosion_x ||
        page_explosion_y[page]!=explosion_y)) {
        x=page_explosion_x[page];
        y=page_explosion_y[page];
        tgi_setcolor(0);
        tgi_bar(y,(unsigned char)(101-(x+6)),
                (unsigned char)(y+6),(unsigned char)(101-x));
        restore_shield_rect(x,y,7,7); /* r11: see erase_old */
        page_explosion_drawn[page]=0;
        /* Neighboring living sprites may overlap the old explosion box. */
        if(explosion_id!=NONE) {
            add_neighborhood(explosion_id);
        }
    }
    if(explosion_ticks) {
        /* Draw after the nearby aliens have been restored, below. */
        page_explosion_x[page]=explosion_x;
        page_explosion_y[page]=explosion_y;
    }
}
#endif

static void render_full(unsigned char page) {
    unsigned char i;
    tgi_setdrawpage(page);
    tgi_clear();
    for(i=0; i<ALIEN_COUNT; ++i) {
        if(alien_alive[i] && !intro_mode) paint_alien(i);
        save_snapshot(i,page);
    }
    /* Clear sets both marker footprints to blank; force marker redraw. */
    page_marker[page]=2;
    if(!ship_hit_ticks && !ship_force_hidden) draw_ship(ship_x);
    page_ship_visible[page]=(unsigned char)(ship_hit_ticks==0 && !ship_force_hidden);
    page_ship_effect[page]=ship_effect_state();
    if(page_ship_effect[page]==1 || page_ship_effect[page]==2)
        draw_ship_break(ship_x,(unsigned char)(page_ship_effect[page]-1));
    page_ship_x[page]=ship_x;
    paint_all_shields();
    for(i=0;i<SHIELD_COUNT;++i) {
        shield_dirty[page][i]=0;
#if V45_SHIELD_LOAD_MODE == 3
        v45_shield_dirty_rows[page][i]=0;
#endif
    }
    /* Freshly cleared page has no old bullet. */
    page_shot_active[page]=0;
    update_shot_page(page);
    update_shield_page(page);
    page_bomb_active[page][0]=0;
    page_bomb_active[page][1]=0;
    page_bomb_active[page][2]=0;
    update_enemy_shot_page(page);
    page_shot_burst[page]=0;
    page_bomb_burst[page]=0;
    update_bursts_page(page);
    page_ufo_state[page]=0;
    update_ufo_page(page);
    paint_explosion();
#if HIT_DRAW_DELTA
    page_explosion_drawn[page]=(unsigned char)(explosion_ticks!=0);
#endif
    page_shown_score[page]=0xFFFF;
    page_shown_hi[page]=0xFFFF;
    paint_header(page);
    page_shown_ships[page]=0xFF;
    paint_lives(page);
    draw_marker(page);
    tgi_updatedisplay();
    while(tgi_busy()) { }
    page_full[page]=0;
#if HIT_DRAW_DELTA
    page_kill_pending[page]=0;
    page_explosion_drawn[page]=(unsigned char)(explosion_ticks!=0);
    if(explosion_ticks) {
        page_explosion_x[page]=explosion_x;
        page_explosion_y[page]=explosion_y;
    }
#endif
}
/* Add at most one copy of each of the 6 nearby IDs. */
static void add_repaint(unsigned char n) {
    unsigned char k;
    for(k=0; k<repaint_count; ++k) {
        if(repaint_ids[k]==n) return;
    }
    if(repaint_count<12) repaint_ids[repaint_count++]=n;
}
static void add_neighborhood(unsigned char n) {
    add_repaint(n);
    if(col_of[n]>0) add_repaint((unsigned char)(n-1));
    if(col_of[n]<10) add_repaint((unsigned char)(n+1));
}
/* On alternating pages, last and current alien can be stale. */
/* Does the 7x7 sprite rectangle at portrait (x,y) overlap the ship? */
static unsigned char rect_hits_ship(unsigned char x, unsigned char y) {
    return (unsigned char)(x+6>=ship_x && x<=ship_x+6 &&
                           y+6>=SHIP_Y && y<=SHIP_Y+6);
}
static void render_delta(unsigned char page, unsigned char current_id) {
    unsigned char k, n;
    unsigned char cur_sx=0, cur_sy=0, prev_sx=0, prev_sy=0;
    if(page_full[page]) {
        render_full(page);
        return;
    }
    /* Snapshot rectangles about to be erased on this page (the ship
     * repair check below needs their pre-erase coordinates). */
    cur_sx=(unsigned char)(base_x[current_id]+page_x[page][current_id]);
    cur_sy=(unsigned char)(base_y[current_id]+page_y[page][current_id]);
    if(previous_id!=NONE) {
        prev_sx=(unsigned char)(base_x[previous_id]+page_x[page][previous_id]);
        prev_sy=(unsigned char)(base_y[previous_id]+page_y[page][previous_id]);
    }
    tgi_setdrawpage(page);
    /* Repaint dirty shield rows FIRST (r11). This repaint blacks the
     * whole shield row before drawing the solid runs; done after the
     * aliens (the old order) it sliced any alien standing over a
     * repainted row on this page only -- the alien half of the
     * alien/shield flicker. Everything that draws over shields (aliens,
     * both shots) now runs after it and restores what its own erase
     * wipes, so the final page state is the same from every path. */
    update_shield_page(page);
    repaint_count=0;
    add_neighborhood(current_id);
    if(previous_id!=NONE) add_neighborhood(previous_id);
#if HIT_DRAW_DELTA
    /* Targeted hit cleanup and explosion cleanup on this hidden page. */
    reconcile_explosion_on_page(page);
    if(page_kill_pending[page]) {
        erase_killed_on_page(page);
        add_neighborhood(killed_id);
    }
#endif
    /* Both erased areas are on the hidden page, never the visible one. */
    erase_old(current_id,page);
    if(previous_id!=NONE && previous_id!=current_id) erase_old(previous_id,page);
    tgi_setcolor(1);
    for(k=0; k<repaint_count; ++k) {
        n=repaint_ids[k];
        paint_alien(n);
    }
    save_snapshot(current_id,page);
    if(previous_id!=NONE) save_snapshot(previous_id,page);
    /* Erasing an alien (or the finished explosion) over the ship's
     * rectangle leaves a bite in the ship on this page, and the ship
     * is not redrawn unless it moved or its effect state changed.
     * Force the redraw so the ship does not flicker at low altitude. */
    if(rect_hits_ship(cur_sx,cur_sy) ||
       (previous_id!=NONE && rect_hits_ship(prev_sx,prev_sy)))
        ship_repair[page]=1;
    /* Same when an alien was PAINTED over the ship in this call: its
     * opaque sprite replaces the ship's pixels, and the ship is not
     * redrawn unless forced. This is the visible "ship vanishes for
     * a moment" when a low alien sweeps across the ship's column. */
    for(k=0;k<repaint_count;++k) {
        n=repaint_ids[k];
        if(rect_hits_ship((unsigned char)(base_x[n]+offset_x[n]),
                          (unsigned char)(base_y[n]+offset_y[n]))) {
            ship_repair[page]=1;
            break;
        }
    }
#if HIT_DRAW_DELTA
    if(page_explosion_drawn[page] &&
       rect_hits_ship(page_explosion_x[page],page_explosion_y[page]))
        ship_repair[page]=1;
#endif
    {
        /* The alien repair below is only needed when update_ship_page
         * actually erased and redrew the ship in this call (its own
         * firing condition): that solid erase is what bites a low
         * alien sharing the ship's rows, and the bitten alien is
         * otherwise repainted only at its next sweep update, up to a
         * full sweep later, so the two pages disagree meanwhile and
         * the alien flickers where it crosses the ship. Gating the
         * scan on the redraw keeps the normal frame at zero added
         * cost: an earlier always-scan version measurably slowed the
         * whole game (the main loop sits at a display-timing edge). */
        unsigned char st=ship_effect_state();
        unsigned char ship_redrawn=(unsigned char)(ship_repair[page] ||
            page_ship_x[page]!=ship_x || page_ship_effect[page]!=st);
        unsigned char old_sx=page_ship_x[page];
        unsigned char m, touched=0;
        update_ship_page(page);
        if(ship_redrawn) {
            /* v51 opt3: only bottom-row aliens (ids 44..54) can ever
             * reach the ship band: row 3's ay maxes at 123 in every
             * stage (48+30 drop+30 row+15 offset), below SHIP_Y-9=131,
             * so rows 0..3 always took the early-out anyway. */
            for(m=44;m<ALIEN_COUNT;++m) {
                unsigned char ax, ay, px, py;
                if(!alien_alive[m]) continue;
                ay=(unsigned char)(base_y[m]+offset_y[m]);
                ax=(unsigned char)(base_x[m]+offset_x[m]);
                py=(unsigned char)(base_y[m]+page_y[page][m]);
                px=(unsigned char)((int)base_x[m]+page_x[page][m]);
                if((ay<(unsigned char)(SHIP_Y-9) || ay>(unsigned char)(SHIP_Y+6)) &&
                   (py<(unsigned char)(SHIP_Y-9) || py>(unsigned char)(SHIP_Y+6))) continue;
                if((px+6>=old_sx && px<=(unsigned char)(old_sx+6)) ||
                   (px+6>=ship_x && px<=(unsigned char)(ship_x+6))) {
                    /* This page's own image of the alien stood in the
                     * erased ship rectangle, so it carries the bite
                     * even if the alien has since stepped away in the
                     * logic. Reconcile it fully (erase the page copy,
                     * repaint at the current position, resnapshot);
                     * repainting at the current position alone left
                     * the bitten page copy flickering. */
                    if(px!=ax || py!=ay) {
                        erase_old(m,page);
                        paint_alien(m);
                        save_snapshot(m,page);
                    } else {
                        paint_alien(m);
                    }
                    touched=1;
                } else if((ax+6>=old_sx && ax<=(unsigned char)(old_sx+6)) ||
                          (ax+6>=ship_x && ax<=(unsigned char)(ship_x+6))) {
                    paint_alien(m);
                    touched=1;
                }
            }
            if(touched) {
                if(st==0) draw_ship(ship_x);
                else if(st<3) draw_ship_break(ship_x,(unsigned char)(st-1));
            }
        }
    }
    update_shot_page(page);
    update_enemy_shot_page(page);
    update_bursts_page(page);
    update_ufo_page(page);
    repair_shield_row_aliens(); /* r11: undo this page's shield-row slices */
    paint_explosion();
#if HIT_DRAW_DELTA
    page_explosion_drawn[page]=(unsigned char)(explosion_ticks!=0);
#endif
    paint_header(page);
    paint_lives(page);
    draw_marker(page);
    tgi_updatedisplay();
    while(tgi_busy()) { }
}
static unsigned char rom_to_lynx(unsigned char id) {
    return (unsigned char)((ROWS-1-id/COLUMNS)*COLUMNS+id%COLUMNS);
}
/* Startup reveal: ROM order 0..54, bottom row to top.
 * Both pages start blank of aliens. The hidden page is one reveal behind,
 * so paint current + preceding alien before flipping. */
#if TEST_AUTO_FIRE || TEST_FREEZE
static void intro_present(unsigned char id, unsigned char include_previous) {
    unsigned char n;
    tgi_setdrawpage(draw_page);
    n=rom_to_lynx(id);
    paint_alien(n);
    if(include_previous) {
        n=rom_to_lynx((unsigned char)(id-1));
        paint_alien(n);
    }
    tgi_updatedisplay();
    while(tgi_busy()) { }
    tgi_setviewpage(draw_page);
    n=visible_page;
    visible_page=draw_page;
    draw_page=n;
}
#endif
/* --- v49: attract (splash) screens -----------------------------------
 * Cycle, measured on the arcade board (disassembly + probe runs): title
 * printed letter by letter -> score advance table -> demo game ->
 * credit screen -> title ... Any button or direction press starts a
 * real game at once. Every other cycle is a gag cycle (the arcade's
 * splashAnimate alternation): the title prints "PLAy" with an upside-
 * down Y, a squid flies in from the right edge after the table,
 * carries the bad Y off and flies back with the correct one, and
 * the credit screen shows the joke texts
 * (user's wording, 2026-11-21) with an extra C in "SKIP THE CCOIN"
 * that a squid shoots away.
 * Both display pages always carry the same attract image: every change
 * is drawn on page 0 and page 1, then the usual flip idiom runs. */
#define ATTRACT_TITLE 1
#define ATTRACT_TITLE_HOLD 2
#define ATTRACT_TABLE_HOLD 3
#define ATTRACT_TABLE_PRINT 4
#define ATTRACT_TABLE_END 5
#define ATTRACT_CREDIT 6
#define ATTRACT_GAG_Y 7
#define TITLE_CHAR_TICKS 3
static unsigned char attract_gag=0; /* 1 on gag cycles (alternates) */
static unsigned char gag_stage=0;
static unsigned char gag_x=0;
static unsigned char gag_shot_y=0;
static unsigned char gag_shot_active=0;
/* r5: the gag block is page-synced (drawn only on the hidden page,
 * like the rest of the game) instead of being drawn on both pages.
 * Drawing on the visible page made the moving block strobe, and the
 * old mask was the bottom-row alien, not the squid. */
static unsigned char gag_on=0;      /* logical: block visible */
static unsigned char gag_y=39;
static unsigned char gag_pose=0;    /* walk pose 0/1, flips every 2 px */
static unsigned int  gag_travel=0;
static unsigned char gag_rider=0;   /* 0 none, 1 flipped Y, 2 normal Y, 3 placed */
static unsigned char pg_gag_on[2]={0,0};
static unsigned char pg_gag_x[2]={0,0}, pg_gag_yy[2]={0,0}, pg_gag_pose[2]={0,0};
static unsigned char pg_gag_rider[2]={0,0};
static unsigned char pg_shot_on[2]={0,0}, pg_shot_y[2]={0,0};
static unsigned int attract_timer=0;
static unsigned char attract_line=0;
static unsigned char attract_col=0;
static unsigned char attract_tick=0;
static const char title_line0[]="PLAY";
static const char title_line1[]="SPACE";
static const char title_line2[]="INVADERS";
static const char *const title_lines[3]={title_line0,title_line1,title_line2};
static const unsigned char title_x[3]={43,41,35};
static const unsigned char title_y[3]={46,56,66};
static const char table_line0[]="=? MYSTERY";
static const char table_line1[]="=30 POINTS";
static const char table_line2[]="=20 POINTS";
static const char table_line3[]="=10 POINTS";
static const char *const table_lines[4]={table_line0,table_line1,table_line2,table_line3};
static const unsigned char table_y[4]={98,110,122,134};
static void attract_plot(unsigned char x, unsigned char y) {
    if(x>101) return; /* clipped: lets the gag squid slide off the right edge */
    tgi_bar(y,(unsigned char)(101-x),y,(unsigned char)(101-x));
}
static void attract_draw_mask7(const unsigned char *rows, unsigned char x, unsigned char y);
static void draw_small_char_flip(unsigned char code, unsigned char x, unsigned char y) {
    unsigned char r, c, bits;
    for(r=0;r<5;++r) {
        bits=small_font[code][4-r];
        for(c=0;c<3;++c)
            if(bits&(4>>c)) attract_plot((unsigned char)(x+c),(unsigned char)(y+r));
    }
}
/* The gag character is the squid: in this port's data that is alien
 * type 2 (top row, 30 points), poses alien_hit_rows[2] and [5]. (The
 * r1-r4 code used rows[0] -- the bottom-row alien.) The block, its
 * rider Y and its shot are drawn only on the hidden page and synced
 * per page, then presented by the attract flip. */
static void gag_draw_block(unsigned char x, unsigned char y, unsigned char pose, unsigned char pen) {
    tgi_setcolor(pen?2:0);
    attract_draw_mask7(alien_hit_rows[pose?5:2],x,y);
}
static void gag_draw_rider(unsigned char x, unsigned char y, unsigned char rider, unsigned char pen) {
    tgi_setcolor(pen?15:0);
    if(rider==1) draw_small_char_flip(24,(unsigned char)(x+3),(unsigned char)(y+7));
    else draw_small_char(24,(unsigned char)(x+3),(unsigned char)(y+7));
}
static void gag_draw_shot(unsigned char y, unsigned char pen) {
    unsigned char r;
    tgi_setcolor(pen);
    for(r=0;r<3;++r) attract_plot(62,(unsigned char)(y-r));
}
static void gag_sync(void) {
    unsigned char pg=draw_page;
    tgi_setdrawpage(pg);
    if(pg_shot_on[pg] && (!gag_shot_active || pg_shot_y[pg]!=gag_shot_y)) {
        gag_draw_shot(pg_shot_y[pg],0);
        pg_shot_on[pg]=0;
    }
    if(pg_gag_on[pg] && (!gag_on || pg_gag_x[pg]!=gag_x ||
        pg_gag_yy[pg]!=gag_y || pg_gag_pose[pg]!=gag_pose)) {
        gag_draw_block(pg_gag_x[pg],pg_gag_yy[pg],pg_gag_pose[pg],0);
        pg_gag_on[pg]=0;
    }
    if(pg_gag_rider[pg] && gag_rider!=3 &&
       (pg_gag_rider[pg]!=gag_rider || !gag_on ||
        pg_gag_x[pg]!=gag_x || pg_gag_yy[pg]!=gag_y)) {
        gag_draw_rider(pg_gag_x[pg],pg_gag_yy[pg],pg_gag_rider[pg],0);
        pg_gag_rider[pg]=0;
    }
    if(gag_rider==3) pg_gag_rider[pg]=3; /* placed: leave the Y alone */
    if(gag_on && !pg_gag_on[pg]) {
        gag_draw_block(gag_x,gag_y,gag_pose,1);
        pg_gag_on[pg]=1; pg_gag_x[pg]=gag_x; pg_gag_yy[pg]=gag_y; pg_gag_pose[pg]=gag_pose;
    }
    if(gag_on && gag_rider && gag_rider!=3 && !pg_gag_rider[pg]) {
        gag_draw_rider(gag_x,gag_y,gag_rider,1);
        pg_gag_rider[pg]=gag_rider;
    }
    if(gag_shot_active && !pg_shot_on[pg]) {
        gag_draw_shot(gag_shot_y,15);
        pg_shot_on[pg]=1; pg_shot_y[pg]=gag_shot_y;
    }
}
static void attract_draw_mask7(const unsigned char *rows, unsigned char x, unsigned char y) {
    unsigned char r, c;
    for(r=0;r<7;++r)
        for(c=0;c<7;++c)
            if(rows[r]&(0x40>>c)) attract_plot((unsigned char)(x+c),(unsigned char)(y+r));
}
static void attract_draw_saucer(unsigned char x, unsigned char y) {
    unsigned char r, c;
    for(r=0;r<6;++r)
        for(c=0;c<10;++c)
            if(ufo_saucer_masks[r]&(0x0200>>c)) attract_plot((unsigned char)(x+c),(unsigned char)(y+r));
}
/* Scene changes are drawn on the hidden page first and flipped in,
 * then drawn on the other page: a full-screen clear+redraw done on
 * the visible page would tear for a frame or two. Single printed
 * characters still go to both pages directly (sub-frame work). */
static void attract_flip(void) {
    unsigned char tmp;
    tgi_updatedisplay();
    while(tgi_busy()) { }
    tgi_setviewpage(draw_page);
    tmp=visible_page;
    visible_page=draw_page;
    draw_page=tmp;
}
static void attract_scene(void (*draw)(unsigned char)) {
    gag_on=0; gag_rider=0; gag_shot_active=0;
    pg_gag_on[0]=pg_gag_on[1]=0;
    pg_gag_rider[0]=pg_gag_rider[1]=0;
    pg_shot_on[0]=pg_shot_on[1]=0;
    tgi_setdrawpage(draw_page);
    draw(draw_page);
    attract_flip();
    tgi_setdrawpage(draw_page);
    draw(draw_page);
    attract_flip();
}
static void attract_scene_blank(unsigned char page) {
    tgi_clear();
    page_shown_score[page]=0xFFFF;
    page_shown_hi[page]=0xFFFF;
    paint_header(page);
}
static void attract_scene_credit(unsigned char page) {
    attract_scene_blank(page);
    tgi_setcolor(15);
    if(attract_gag) {
        draw_small_text("SKIP THE CCOIN",25,64);
        draw_small_text("1 PLAYER ONLY",25,84);
        draw_small_text("*1 PLAYER 0 COIN",19,96);
    } else {
        draw_small_text("SKIP THE COIN",25,64);
        draw_small_text("<1 OR 2 PLAYERS>",19,84);
        draw_small_text("*1 PLAYER 1 COIN",19,96);
    }
    draw_small_text("CREDIT 00",2,151);
}
static void attract_scene_table_art(unsigned char page) {
    (void)page;
    tgi_setcolor(15);
    draw_small_text("*SCORE ADVANCE TABLE*",9,84);
    tgi_setcolor(1);
    attract_draw_saucer(14,97);
    tgi_setcolor(2);
    attract_draw_mask7(alien_hit_rows[2],16,109);
    tgi_setcolor(8);
    attract_draw_mask7(alien_hit_rows[1],16,121);
    tgi_setcolor(9);
    attract_draw_mask7(alien_hit_rows[0],16,133);
}
static void attract_enter_title(void) {
    snd_stop(); /* v50: attract is silent, as on the arcade */
    attract_phase=ATTRACT_TITLE;
    attract_line=0; attract_col=0; attract_tick=0; attract_timer=0;
    attract_scene(attract_scene_blank);
}
static void attract_enter_credit(void) {
    attract_phase=ATTRACT_CREDIT;
    attract_timer=0;
    attract_scene(attract_scene_credit);
}
/* r6: the arcade start sequence shows a prompt screen -- header,
 * "PLAY PLAYER<1>", the lives band, CREDIT -- on an empty field for
 * the isrDelay count, and only then draws the playfield. (r5 held
 * the finished field instead, which still felt wrong.) */
static void attract_scene_prompt(unsigned char page) {
    (void)page;
    tgi_clear();
    page_shown_score[draw_page]=0xFFFF;
    page_shown_hi[draw_page]=0xFFFF;
    paint_header(draw_page);
    tgi_setcolor(15);
    draw_small_text("PLAY PLAYER<1>",23,76);
    page_shown_ships[draw_page]=0xFF;
    paint_lives(draw_page);
    tgi_setcolor(15);
    draw_small_text("CREDIT 00",64,151);
}
static void attract_start_game(void) {
    attract_phase=0;
    demo_mode=0;
    reset_game();
    start_wait_ticks=START_WAIT_TICKS;
    attract_scene(attract_scene_prompt);
}
static void attract_enter_demo(void) {
    attract_phase=0;
    demo_mode=1;
    demo_over=0;
    demo_start_request=0;
    demo_cmd_idx=0;
    demo_cmd=0;
    demo_ticks=0;
    reset_game();
}
static void attract_step(void) {
    unsigned char keys=0, p;
    if(ship_input_ok) keys=joy_read(0);
    if(keys & (JOY_BTN_1_MASK|JOY_BTN_2_MASK|JOY_UP_MASK|JOY_DOWN_MASK|JOY_LEFT_MASK|JOY_RIGHT_MASK)) {
        attract_start_game();
        return;
    }
    switch(attract_phase) {
    case ATTRACT_TITLE:
        if(++attract_tick>=TITLE_CHAR_TICKS) {
            const char *s;
            attract_tick=0;
            s=title_lines[attract_line];
            if(s[attract_col]) {
                for(p=0;p<2;++p) {
                    tgi_setdrawpage(p);
                    tgi_setcolor(15);
                    if(attract_gag && attract_line==0 && attract_col==3)
                        draw_small_char_flip(small_code(s[attract_col]),
                            (unsigned char)(title_x[attract_line]+4*attract_col),
                            title_y[attract_line]);
                    else
                        draw_small_char(small_code(s[attract_col]),
                            (unsigned char)(title_x[attract_line]+4*attract_col),
                            title_y[attract_line]);
                }
                ++attract_col;
            } else {
                attract_col=0;
                if(++attract_line>=3) { attract_phase=ATTRACT_TITLE_HOLD; attract_timer=0; }
            }
        }
        break;
    case ATTRACT_TITLE_HOLD:
        if(++attract_timer>=40) {
            attract_scene(attract_scene_table_art);
            attract_phase=ATTRACT_TABLE_HOLD;
            attract_timer=0;
        }
        break;
    case ATTRACT_TABLE_HOLD:
        if(++attract_timer>=40) {
            attract_phase=ATTRACT_TABLE_PRINT;
            attract_line=0; attract_col=0; attract_tick=0;
        }
        break;
    case ATTRACT_TABLE_PRINT:
        if(++attract_tick>=TITLE_CHAR_TICKS) {
            const char *s;
            attract_tick=0;
            s=table_lines[attract_line];
            if(s[attract_col]) {
                for(p=0;p<2;++p) {
                    tgi_setdrawpage(p);
                    tgi_setcolor(15);
                    draw_small_char(small_code(s[attract_col]),
                        (unsigned char)(28+4*attract_col), table_y[attract_line]);
                }
                ++attract_col;
            } else {
                attract_col=0;
                if(++attract_line>=4) { attract_phase=ATTRACT_TABLE_END; attract_timer=0; }
            }
        }
        break;
    case ATTRACT_TABLE_END:
        if(++attract_timer>=100) {
            if(attract_gag) {
                attract_phase=ATTRACT_GAG_Y;
                gag_stage=0; gag_x=101; gag_y=39; attract_timer=0; attract_tick=0;
                gag_on=1; gag_rider=0; gag_pose=0; gag_travel=0;
            } else { attract_enter_demo(); return; }
        }
        break;
    case ATTRACT_GAG_Y:
        switch(gag_stage) {
        case 0: /* squid flies in from the right edge */
            if(gag_x>52) { --gag_x; ++gag_travel; gag_pose=(unsigned char)((gag_travel>>1)&1); }
            else { gag_stage=1; attract_timer=0; }
            break;
        case 1: /* beat, then the bad Y rides under the squid */
            if(++attract_timer>=12) {
                gag_stage=2; attract_timer=0;
                for(p=0;p<2;++p) { tgi_setdrawpage(p); tgi_setcolor(0); draw_small_char_flip(24,55,46); }
                gag_rider=1;
            }
            break;
        case 2: /* squid carries the bad Y off to the right */
            if(gag_x<103) { ++gag_x; ++gag_travel; gag_pose=(unsigned char)((gag_travel>>1)&1); }
            else { gag_stage=3; attract_timer=0; }
            break;
        case 3: /* beat off-screen, then it reappears with the correct Y */
            if(++attract_timer>=30) {
                gag_stage=4; attract_timer=0; gag_x=103;
                gag_rider=2;
            }
            break;
        case 4: /* it flies back in, the correct Y riding under it */
            if(gag_x>52) { --gag_x; ++gag_travel; gag_pose=(unsigned char)((gag_travel>>1)&1); }
            else { gag_stage=5; attract_timer=0; }
            break;
        case 5: /* beat, then the squid lets go: the Y stays in place */
            if(++attract_timer>=8) {
                gag_stage=6; attract_timer=0;
                gag_rider=3; /* placed: the sync leaves the Y alone */
            }
            break;
        case 6: /* squid flies off to the right */
            if(gag_x<103) { ++gag_x; ++gag_travel; gag_pose=(unsigned char)((gag_travel>>1)&1); }
            else { gag_on=0; gag_stage=7; attract_timer=0; }
            break;
        default: /* the Y is fixed; beat, then the demo */
            if(++attract_timer>=25) { attract_enter_demo(); return; }
            break;
        }
        break;
    case ATTRACT_CREDIT:
        ++attract_timer;
        if(attract_gag) {
            if(attract_timer==8) { gag_on=1; gag_x=101; gag_y=74; gag_rider=0; gag_pose=0; gag_travel=0; }
            if(attract_timer>8 && attract_timer<=50) {
                --gag_x; ++gag_travel; gag_pose=(unsigned char)((gag_travel>>1)&1);
            }
            if(attract_timer==75) { gag_shot_active=1; gag_shot_y=73; }
            if(gag_shot_active && attract_timer>75 && ++attract_tick>=2) {
                attract_tick=0;
                if(gag_shot_y<=71) {
                    gag_shot_active=0;
                    for(p=0;p<2;++p) { tgi_setdrawpage(p); tgi_setcolor(0); draw_small_char(small_code('C'),61,64); }
                } else { --gag_shot_y; }
            }
            if(attract_timer>100 && attract_timer<=144) {
                if(gag_x<103) { ++gag_x; ++gag_travel; gag_pose=(unsigned char)((gag_travel>>1)&1); }
            }
            if(attract_timer==145) gag_on=0;
            if(attract_timer>=150) { attract_gag=0; attract_enter_title(); return; }
        } else {
            if(attract_timer>=120) { attract_gag=1; attract_enter_title(); return; }
        }
        break;
    }
    gag_sync();
    attract_flip();
}
/* r7: deal-in order for a player start -- bottom row first (the two
 * octopus rows, then the crab rows, then the squid row), right to
 * right within each row as seen on screen (user-corrected
 * 2026-11-22: the first r7 cut had it mirrored). k = 0..54. */
static unsigned char deal_to_lynx(unsigned char k) {
    unsigned char r=(unsigned char)(4-(k/COLUMNS));
    unsigned char c=(unsigned char)(k%COLUMNS);
    return (unsigned char)(r*COLUMNS+c);
}
/* Same two-page idiom as intro_present: the hidden page is one
 * reveal behind, so paint the current and the preceding alien. */
static void deal_present(unsigned char k, unsigned char include_previous) {
    unsigned char n;
    tgi_setdrawpage(draw_page);
    n=deal_to_lynx(k);
    if(alien_alive[n]) paint_alien(n);
    if(include_previous) {
        n=deal_to_lynx((unsigned char)(k-1));
        if(alien_alive[n]) paint_alien(n);
    }
    tgi_updatedisplay();
    while(tgi_busy()) { }
    tgi_setviewpage(draw_page);
    n=visible_page;
    visible_page=draw_page;
    draw_page=n;
}
/* Runs on the alien-less field (intro_mode=1, both pages rendered):
 * deals the whole formation in, one alien per flip (~55 updates),
 * then hands over to normal play exactly like run_intro did. */
static void deal_in_formation(void) {
    unsigned char k, n;
    for(k=0; k<ALIEN_COUNT; ++k) deal_present(k,(unsigned char)(k!=0));
    /* The last reveal is missing on the page that was hidden on the
     * last step; fill it (same as run_intro). */
    tgi_setdrawpage(draw_page);
    n=deal_to_lynx(ALIEN_COUNT-1);
    if(alien_alive[n]) paint_alien(n);
    tgi_updatedisplay();
    while(tgi_busy()) { }
    intro_mode=0;
    previous_id=NONE;
}
#if TEST_AUTO_FIRE || TEST_FREEZE
static void run_intro(void) {
    unsigned char id, n;
    unsigned int hold;
    /* Hold a fully prepared, static intro background for ~0.5 sec
     * before the first alien appears. Both pages already initialized. */
    for(hold=0; hold<INTRO_PRE_FRAMES; ++hold) {
        tgi_setdrawpage(draw_page);
        tgi_updatedisplay();
        while(tgi_busy()) { }
        tgi_setviewpage(draw_page);
        n=visible_page;
        visible_page=draw_page;
        draw_page=n;
    }
    /* All 55 are logically alive, but startup pages were drawn blank. */
    for(id=0; id<ALIEN_COUNT; ++id) {
        intro_present(id,(unsigned char)(id!=0));
    }
    /* Last reveal is missing on the page that was hidden on the last
     * step. Fill it before the two-second hold starts. */
    tgi_setdrawpage(draw_page);
    n=rom_to_lynx(ALIEN_COUNT-1);
    paint_alien(n);
    tgi_updatedisplay();
    while(tgi_busy()) { }
    /* Both buffers now contain the complete formation. */
    intro_mode=0;
    for(hold=0; hold<INTRO_HOLD_FRAMES; ++hold) {
        tgi_setdrawpage(draw_page);
        tgi_updatedisplay();
        while(tgi_busy()) { }
        tgi_setviewpage(draw_page);
        n=visible_page;
        visible_page=draw_page;
        draw_page=n;
    }
    /* No alien has moved or changed pose during the intro. */
    previous_id=NONE;
}
#endif
/* V45 Edge diagnostic: comparison of 2px and 55-alien adaptive steps.
 * Edge 0: original Gap 1 end-of-sweep reversal at screen edge.
 * Edge 1: original Gap 2 early reversal (2px margin).
 * Edge 2: predict the next move at the sweep boundary.
 * Edge 3: Edge 2 plus correction of any per-alien offset drift
 *         at a turn; correction is applied only at a sweep boundary. */
#ifndef V45_EDGE_MODE
#define V45_EDGE_MODE 0
#endif
#if V45_EDGE_MODE < 0 || V45_EDGE_MODE > 3
#error V45_EDGE_MODE_must_be_0_to_3
#endif
/* Short-count tests use fixed 2px. The 55-alien test uses 1px
 * until 10 remain, then 2px. The current sweep's step is fixed
 * at its start to avoid per-alien step mismatches on a kill. */
static unsigned char v45_sweep_step=1;
static unsigned char v45_gap_step(void) { return v45_sweep_step; }
static void v45_choose_sweep_step(void) {
    /* r12 trial step 2 kept; the extra stride below 11 aliens (step 4)
     * was dropped in r13 at the user's judgement -- with stride 2 and
     * descent 5 the endgame is fast enough, and the sweep itself still
     * quickens naturally as fewer aliens remain. */
    v45_sweep_step=2;
}
/* Cache comparison: 0=Break3 baseline; 1=cheap global bounding reject;
 * 2=bounding reject plus row occupancy reject. */
#ifndef V45_CACHE_MODE
#define V45_CACHE_MODE 0
#endif
#if V45_CACHE_MODE < 0 || V45_CACHE_MODE > 2
#error V45_CACHE_MODE_must_be_0_to_2
#endif
/* Range comparison: 0=Cache2, 1=skip redundant prescan,
 * 2=skip empty rows, 3=also restrict row iteration to wall overlap. */
#ifndef V45_RANGE_MODE
#define V45_RANGE_MODE 0
#endif
#if V45_RANGE_MODE < 0 || V45_RANGE_MODE > 3
#error V45_RANGE_MODE_must_be_0_to_3
#endif
/* V45 Break diagnostic: isolate shield erosion overhead. */
#ifndef V45_BREAK_MODE
#define V45_BREAK_MODE 0
#endif
#if V45_BREAK_MODE < 0 || V45_BREAK_MODE > 3
#error V45_BREAK_MODE_must_be_0_to_3
#endif
/* V45 contact ON/OFF isolation. 0=no alien scan; 1=scan only;
 * 2=Load 3 erosion and row redraw; 3=no alien scan, shields invisible.
 * Existing projectile/shield behavior is unchanged except Contact 3. */
#ifndef V45_CONTACT_MODE
#define V45_CONTACT_MODE 0
#endif
#if V45_CONTACT_MODE < 0 || V45_CONTACT_MODE > 3
#error V45_CONTACT_MODE_must_be_0_to_3
#endif
/* Independent shield-contact performance comparison. */
#ifndef V45_SHIELD_PERF_MODE
#if V45_CONTACT_MODE == 1
#define V45_SHIELD_PERF_MODE 1
#else
#define V45_SHIELD_PERF_MODE 3
#endif
#endif
#if V45_SHIELD_PERF_MODE < 0 || V45_SHIELD_PERF_MODE > 3
#error V45_SHIELD_PERF_MODE_must_be_0_to_3
#endif
/* V45 load modes. Collision scan is performed once per updated alien.
 * Changes to shield_bits are persistent; only changed rows/pages are marked. */
static void v45_erode_shield_by_alien(unsigned char n) {
    int ax=(int)base_x[n]+offset_x[n];
    int ay=(int)base_y[n]+offset_y[n];
    unsigned char k,ry,cx,pose,ly,lx,changed;
#if V45_RANGE_MODE == 3
    unsigned char first_ry,last_ry;
#endif
    unsigned int bit, changed_rows;
    (void)cx; (void)lx; (void)bit; /* used only in other V45 modes */
    if(ay+6<SHIELD_Y || ay>=SHIELD_Y+SHIELD_H) return;
#if V45_CACHE_MODE >= 1
    /* All four shields occupy portrait X 8..91; reject outside once. */
    if(ax+6<8 || ax>91) return;
#endif
    pose=(unsigned char)(type_of[n]+(walk_frame[n]?3:0));
    for(k=0;k<SHIELD_COUNT;++k) {
        if(ax+6<shield_left[k] || ax>=shield_left[k]+SHIELD_W) continue;
#if V45_CACHE_MODE == 2 && V45_RANGE_MODE == 0
        /* Reject this shield when every vertically overlapping row is empty.
         * Keep exact row-mask collision below for all nonempty candidates. */
        {
            unsigned char any_wall=0;
            for(ry=0;ry<7;++ry) {
                int yy=ay+ry;
                if(yy<SHIELD_Y || yy>=SHIELD_Y+SHIELD_H) continue;
                ly=(unsigned char)(yy-SHIELD_Y);
                if(shield_bits[k][ly]) { any_wall=1; break; }
            }
            if(!any_wall) continue;
        }
#endif
        changed=0;
        changed_rows=0;
#if V45_RANGE_MODE == 3
        /* Limit loop to the actual vertical overlap, without altering Y. */
        first_ry=(ay<SHIELD_Y)?(unsigned char)(SHIELD_Y-ay):0;
        last_ry=(ay+7>SHIELD_Y+SHIELD_H)?
            (unsigned char)(SHIELD_Y+SHIELD_H-ay):7;
        for(ry=first_ry;ry<last_ry;++ry) {
#else
        for(ry=0;ry<7;++ry) {
#endif
            int yy=ay+ry;
            unsigned int hitmask=0;
            if(yy<SHIELD_Y || yy>=SHIELD_Y+SHIELD_H) continue;
            ly=(unsigned char)(yy-SHIELD_Y);
#if V45_RANGE_MODE >= 2
            if(!shield_bits[k][ly]) continue;
#endif
#if V45_BREAK_MODE == 3
            /* Row-mask fast path: no per-pixel loop. Mask is 7 bits. */
            {
                int shift=(int)shield_left[k]+14-ax-6;
                unsigned int a=(unsigned int)(alien_hit_rows[pose][ry] & 0x7Fu);
                if(shift>=0 && shift<16) hitmask=(unsigned int)(a << shift);
                else if(shift<0 && shift>=-6) hitmask=(unsigned int)(a >> (-shift));
                else hitmask=0;
                hitmask &= shield_bits[k][ly];
                if(hitmask) {
                    shield_bits[k][ly] &= (unsigned int)~hitmask;
                    changed=1;
                    changed_rows |= (unsigned int)(1u << ly);
                }
            }
#elif V45_SHIELD_PERF_MODE == 3
            /* Aggregate the intersecting alien row before one wall-mask update. */
            if(!shield_bits[k][ly]) continue;
            for(cx=0;cx<7;++cx) {
                int xx=ax+cx;
                if(xx<shield_left[k] || xx>=shield_left[k]+SHIELD_W) continue;
                if(!(alien_hit_rows[pose][ry] & (unsigned char)(0x40u >> cx))) continue;
                lx=(unsigned char)(xx-shield_left[k]);
                hitmask |= (unsigned int)(1u << (14-lx));
            }
            hitmask &= shield_bits[k][ly];
            if(hitmask) {
                shield_bits[k][ly] &= (unsigned int)~hitmask;
                changed=1;
                changed_rows |= (unsigned int)(1u << ly);
            }
#else
            for(cx=0;cx<7;++cx) {
                int xx=ax+cx;
                if(xx<shield_left[k] || xx>=shield_left[k]+SHIELD_W) continue;
                if(!(alien_hit_rows[pose][ry] & (unsigned char)(0x40u >> cx))) continue;
                lx=(unsigned char)(xx-shield_left[k]);
                bit=(unsigned int)(1u << (14-lx));
                if(shield_bits[k][ly] & bit) {
#if V45_BREAK_MODE != 1
                    shield_bits[k][ly] &= (unsigned int)~bit;
#endif
                    changed=1;
                    changed_rows |= (unsigned int)(1u << ly);
                }
            }
#endif
        }
#if V45_BREAK_MODE == 0 || V45_BREAK_MODE == 2 || V45_BREAK_MODE == 3
        if(changed) {
            shield_dirty[0][k]=1;
            shield_dirty[1][k]=1;
            v45_shield_dirty_rows[0][k] |= changed_rows;
            v45_shield_dirty_rows[1][k] |= changed_rows;
        }
#else
        /* Perf 1: scan only. Perf 2: mutate shield_bits, skip redraw requests.
         * These are diagnostic-only and can leave stale visible wall pixels. */
        (void)changed;
        (void)changed_rows;
#endif
    }
}
static void update_one_alien(unsigned char n) {
    unsigned char step=v45_gap_step();
    int x=(int)base_x[n]+offset_x[n];
    /* Clip the new position to the 0..95 legal 7px sprite range.
     * Keep the existing sweep-boundary direction/2px descent logic. */
    if(direction>0) {
        if(x+step>95) step=(unsigned char)(95-x);
        offset_x[n]+=(signed char)step;
    } else {
        if(x-(int)step<0) step=(unsigned char)x;
        offset_x[n]-=(signed char)step;
    }
    if(descent_pending) offset_y[n]+=DESCENT_STEP;
    walk_frame[n]^=1;
}
/* Use a living alien as the descent reference: array element 0 may be dead. */
/* Check the actual surviving formation bounds after a complete sweep.
 * Portrait X is 0..101, each alien is 7 pixels wide.
 * A killed outer column no longer restricts travel.
 * Check AFTER all living aliens have taken their one-pixel step.
 * The next sweep applies the reversed direction and a 2px descent. */
static void update_live_edge_direction(void) {
    unsigned char i,first=NONE;
    int left=102, right=-1, x;
    unsigned char turn=0;
    for(i=0; i<ALIEN_COUNT; ++i) {
        if(!alien_alive[i]) continue;
        if(first==NONE) first=i;
        x=(int)base_x[i]+offset_x[i];
        if(x<left) left=x;
        if(x+6>right) right=x+6;
    }
    if(first==NONE) return;
#if V45_EDGE_MODE == 0
    if(direction>0 && right>=101) turn=1;
    else if(direction<0 && left<=0) turn=1;
#elif V45_EDGE_MODE == 1
    if(direction>0 && right>=99) turn=1;
    else if(direction<0 && left<=2) turn=1;
#else
    /* Turn before the NEXT step would cross the physical screen edge. */
    if(direction>0 && right+v45_gap_step()>101) turn=1;
    else if(direction<0 && left<v45_gap_step()) turn=1;
#endif
    if(!turn) return;
#if V45_EDGE_MODE == 3
    /* Test whether offsets drifted apart due to per-alien edge clipping.
     * Re-align only at a turn, never in the middle of a walking sweep.
     * Preserve the first living alien's offset as the reference. */
    {
        signed char reference=offset_x[first];
        unsigned char changed=0;
        for(i=0;i<ALIEN_COUNT;++i) {
            int target;
            if(!alien_alive[i]) continue;
            target=(int)base_x[i]+reference;
            if(target<0 || target>95) continue;
            if(offset_x[i]!=reference) {
                offset_x[i]=reference;
                changed=1;
            }
        }
        if(changed) {
            page_full[0]=1;
            page_full[1]=1;
        }
    }
#endif
    direction=-direction;
    descent_pending=1;
}
static void reset_descent_if_needed(void) {
    /* v47: the v45 descent cap (offset_y reset at DESCENT_LIMIT) is gone.
     * It was a placeholder from when no game-over existed; the formation
     * now descends until formation_reached_bottom() ends the game. */
}

void main(void) {
    unsigned char i,n,tmp,r,c;
    tgi_install(tgi_static_stddrv);
    tgi_init();
    tgi_setframerate(60);
    CLI();
    snd_init(); /* v50 */
    init_sprite();
    ship_input_ok=(joy_install(joy_static_stddrv)==JOY_ERR_OK);
    if(tgi_getpagecount()<2) while(1) { }
#if V45_COLOR_STEP >= 2
    v45_capture_palette();
#endif
    for(i=0; i<ALIEN_COUNT; ++i) {
        r=(unsigned char)(i/COLUMNS);
        c=(unsigned char)(i%COLUMNS);
        col_of[i]=c;
        base_x[i]=(unsigned char)(7+8*c);
        base_y[i]=(unsigned char)(48+10*r); /* r22: start 48 (see start_wave) */
        type_of[i]=(r==0)?2:((r<3)?1:0);
        offset_x[i]=0; offset_y[i]=0;
#if TEST_FREEZE
        walk_frame[i]=TEST_POSE;
#else
        walk_frame[i]=0;
#endif
        alien_alive[i]=1;
    }
#if V45_START_ALIENS < ALIEN_COUNT
    /* Retain the first N ROM-order enemies, avoiding sparse-row assumptions.
     * Remaining 55-alien formation and normal start are untouched in N=55. */
    alive_count=V45_START_ALIENS;
    for(i=0;i<ALIEN_COUNT;++i)
        alien_alive[rom_to_lynx(i)]=(unsigned char)(i<V45_START_ALIENS);
    last_live_id=(unsigned char)(V45_START_ALIENS-1);
    /* Short-count diagnostic: skip the 55-alien staged reveal. */
    intro_mode=0;
#endif
#if TEST_SHOT_X != 255
    /* Keep the visible gun directly below the diagnostic bullet lane. */
    ship_x=(unsigned char)(TEST_SHOT_X-3); /* explicit test lane override */
#endif
    for(i=0;i<SHIELD_COUNT;++i) {
        for(r=0;r<SHIELD_H;++r) shield_bits[i][r]=shield_initial[r];
        shield_dirty[0][i]=1;
        shield_dirty[1][i]=1;
    }
    v45_choose_sweep_step();
    /* Clear BOTH framebuffer pages before showing any intro artwork.
     * Avoid exposing an uninitialized back buffer on the first flip. */
    tgi_setdrawpage(0);
    tgi_clear();
    tgi_updatedisplay();
    while(tgi_busy()) { }
    tgi_setdrawpage(1);
    tgi_clear();
    tgi_updatedisplay();
    while(tgi_busy()) { }
    render_full(0);
    render_full(1);
    tgi_setviewpage(0);
#if V45_START_ALIENS == ALIEN_COUNT
#if TEST_AUTO_FIRE || TEST_FREEZE
    run_intro();
#else
    attract_enter_title();
#endif
#endif
    while(1) {
        if(attract_phase) { attract_step(); continue; }
        if(demo_start_request) {
            demo_start_request=0;
            demo_mode=0;
            reset_game();
            start_wait_ticks=START_WAIT_TICKS;
            attract_scene(attract_scene_prompt);
            continue;
        }
        if(start_wait_ticks) {
            /* Arcade start prompt (r6): the prompt scene is on both
             * pages; hold it for isrDelay=176 updates, blinking the
             * SCORE<1> digits (the arcade toggles them every 4
             * frames). When the count runs out, the normal render
             * path repaints the playfield (page_full is still set
             * from start_wave) and play begins. */
            unsigned char bi;
            --start_wait_ticks;
            if(!start_wait_ticks) {
                /* r7: the field appears without aliens first, then
                 * the formation is dealt in (bottom row first,
                 * right to left) -- the arcade rack sequence, and
                 * how this port's own intro used to do it. */
                intro_mode=1;
                render_full(0);
                render_full(1);
                tgi_setviewpage(0);
                visible_page=0;
                draw_page=1;
                deal_in_formation();
            }
            tgi_setdrawpage(draw_page);
            /* Blink only while still waiting: on the final update
             * the transition above has just repainted both pages as
             * the playfield, and erasing the digits here would blank
             * them on one page for good (paint_header trusts its
             * per-page record and would never redraw them) -- that
             * was the r6/r7 SCORE<1> flicker during play. */
            if(start_wait_ticks) {
                if((start_wait_ticks>>2)&1) {
                    tgi_setcolor(SCORE_PEN);
                    draw_small_number(SCORE_DIGITS_X,HEADER_DIGITS_Y,score_value);
                } else {
                    for(bi=0;bi<4;++bi)
                        erase_small_cell((unsigned char)(SCORE_DIGITS_X+4*bi),HEADER_DIGITS_Y);
                }
            }
            tgi_updatedisplay();
            while(tgi_busy()) { }
            tgi_setviewpage(draw_page);
            tmp=visible_page;
            visible_page=draw_page;
            draw_page=tmp;
            continue;
        }
        if(demo_mode && (demo_over || game_over)) {
            /* Demo over (first hit, or the fleet landed): on to the
             * credit screen, as on the arcade. */
            demo_mode=0;
            demo_over=0;
            game_over=0;
            ship_force_hidden=0;
            attract_enter_credit();
            continue;
        }
        if(game_over) {
            ++game_over_ticks;
            if(game_over_ticks==1 && !page_full[draw_page]) {
                /* Converge the hidden page with the visible one by
                 * replaying the last delta on it. Two plainer attempts
                 * failed here: a full repaint (render_full) wedges the
                 * display update and freezes the game, and flipping
                 * between pages that differ by one frame makes the
                 * formation dance (tgi_updatedisplay swaps the driver
                 * pages, so drawing on one page only still flips).
                 * After this replay both pages are identical, and the
                 * flip below shows a still field. */
                render_delta(draw_page,previous_id);
            } else {
                tgi_setdrawpage(draw_page);
                /* Hide the ship and clear the lives band. */
                tgi_setcolor(0);
                tgi_bar(SHIP_Y,(unsigned char)(101-(ship_x+6)),(unsigned char)(SHIP_Y+6),(unsigned char)(101-ship_x));
                /* The bar above also erases the part of any alien
                 * overlapping the ship, and nothing else in this
                 * branch ever repaints it, so an alien that reached
                 * the ship stayed half-erased for the whole
                 * game-over display. Reconcile those aliens on this
                 * page (the ship itself stays hidden). */
                {
                    unsigned char m, ax, ay;
                    for(m=0;m<ALIEN_COUNT;++m) {
                        if(!alien_alive[m]) continue;
                        ay=(unsigned char)(base_y[m]+offset_y[m]);
                        if(ay<(unsigned char)(SHIP_Y-9) || ay>(unsigned char)(SHIP_Y+6)) continue;
                        ax=(unsigned char)(base_x[m]+offset_x[m]);
                        if(ax+6>=ship_x && ax<=(unsigned char)(ship_x+6)) {
                            erase_old(m,draw_page);
                            paint_alien(m);
                            save_snapshot(m,draw_page);
                        }
                    }
                }
                tgi_bar(149,0,158,101);
                /* Black backing behind GAME OVER (text ink x24..77,
                 * y76..82) so it stays readable over the formation. */
                tgi_bar(73,(unsigned char)(101-80),85,(unsigned char)(101-21));
                tgi_setcolor(1);
                draw_font_text(label_game_over,9,24,76);
                tgi_updatedisplay();
                while(tgi_busy()) { }
            }
            tgi_setviewpage(draw_page);
            tmp=visible_page;
            visible_page=draw_page;
            draw_page=tmp;
            if(game_over_ticks>180) attract_enter_title();
            continue;
        }
        /* One live alien per display update. No artificial sweep delay. */
        if(alive_count==0) {
            /* Let the final explosion finish on both pages before stopping. */
            if(explosion_ticks) {
                advance_explosion();
                if(!wave_end_synced) {
                    /* Converge the hidden page (it still shows the
                     * last alien alive) by replaying the last delta,
                     * as in the game-over branch. */
                    wave_end_synced=1;
                    if(!page_full[draw_page]) render_delta(draw_page,previous_id);
                    else { tgi_setdrawpage(draw_page); tgi_updatedisplay(); while(tgi_busy()) { } }
                } else {
                    tgi_setdrawpage(draw_page);
                    if(!explosion_ticks) {
                        tgi_setcolor(0);
                        tgi_bar(explosion_y,(unsigned char)(101-(explosion_x+7)),(unsigned char)(explosion_y+7),(unsigned char)(101-explosion_x));
                    }
                    tgi_updatedisplay();
                    while(tgi_busy()) { }
                }
                tgi_setviewpage(draw_page);
                tmp=visible_page;
                visible_page=draw_page;
                draw_page=tmp;
                if(!explosion_ticks) {
                    /* The page just flipped away still shows the
                     * explosion (only one page was erased above):
                     * clear it here or it blinks through the whole
                     * stage-clear pause as the pages keep flipping. */
                    tgi_setdrawpage(draw_page);
                    tgi_setcolor(0);
                    tgi_bar(explosion_y,(unsigned char)(101-(explosion_x+7)),(unsigned char)(explosion_y+7),(unsigned char)(101-explosion_x));
                }
                continue;
            }
            /* Stage clear: brief pause, then the next wave. */
            if(stage_clear_ticks<90) {
                ++stage_clear_ticks;
                if(!wave_end_synced) {
                    wave_end_synced=1;
                    if(!page_full[draw_page]) render_delta(draw_page,previous_id);
                    else { tgi_setdrawpage(draw_page); tgi_updatedisplay(); while(tgi_busy()) { } }
                } else {
                    tgi_setdrawpage(draw_page);
                    tgi_updatedisplay();
                    while(tgi_busy()) { }
                }
                tgi_setviewpage(draw_page);
                tmp=visible_page;
                visible_page=draw_page;
                draw_page=tmp;
                continue;
            }
            ++stage_no;
            start_wave(stage_no);
            /* v50 r3: deal the new formation in, exactly as at game
             * start -- without this the wave popped in all at once
             * from stage 2 on (user report, 2026-11-23). */
            intro_mode=1;
            render_full(0);
            render_full(1);
            tgi_setviewpage(0);
            visible_page=0;
            draw_page=1;
            deal_in_formation();
            continue;
        }
        n=next_living_alien();
        if(n==NONE) continue;
        v45_poll_option_pause();
        poll_ship();
        advance_explosion();
        /* While the ship breaks up (the red phase and the blank that
         * follows it), the world waits: only the break countdown
         * runs. The formation, both shots and the sweep resume once
         * the ship is back, or the game-over branch takes over. */
        world_frozen=(unsigned char)(ship_hit_ticks!=0);
        advance_ship_break();
        sound_update(); /* v50: UFO loop + march sequencer, once per iteration */
        if(!world_frozen) {
            advance_shot();
            advance_bombs();
            age_bursts();
            ufo_tick();
        }
#if V45_COLOR_STEP >= 2
        v45_update_red_palette();
#endif
#if !TEST_FREEZE
        if(!world_frozen && alien_alive[n]) {
            update_one_alien(n);
#if V45_CONTACT_MODE == 1 || V45_CONTACT_MODE == 2
            v45_erode_shield_by_alien(n);
#endif
        }
#endif
        render_delta(draw_page,n);
        tgi_setviewpage(draw_page);
        tmp=visible_page;
        visible_page=draw_page;
        draw_page=tmp;
        previous_id=n;
        /* Occupation ends the game the moment the lowest alien's
         * descent crosses the line, not at the end of the sweep:
         * aliens descend one by one during the sweep after a turn,
         * so a boundary-only check let the formation march on for
         * the rest of that sweep. */
        if(!world_frozen && formation_reached_bottom()) { game_over=1; game_over_ticks=0; ship_force_hidden=1; snd_stop(); }
        /* A complete sweep ends when the next ROM ID wraps to 0. */
        if(sweep_boundary && !world_frozen) {
            ++completed_sweeps;
#if AUDIT_SWEEPS > 0
            if(completed_sweeps>=AUDIT_SWEEPS) while(1) { }
#endif
#if !TEST_FREEZE
            descent_pending=0;
            update_live_edge_direction();
            reset_descent_if_needed();
            v45_choose_sweep_step();
            if(formation_reached_bottom()) { game_over=1; game_over_ticks=0; ship_force_hidden=1; snd_stop(); }
#endif
        }
    }
}
