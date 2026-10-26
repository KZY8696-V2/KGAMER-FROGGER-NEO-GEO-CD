; ==========================================================================
; KGAMER FROGGER NEO GEO CD / CDZ - FULL VASM M68K ASSEMBLY KERNEL
; Features: Snowy Taiga, Standard Desert, Red Desert, Snowy Plains, Plains,
;           Highway with Cars, Live HW & FPS, Smooth Scroll & CD Eject
; ==========================================================================

    SECTION .text

; --- CONSTANTS ---
SCREEN_WIDTH    EQU 320
SCREEN_HEIGHT   EQU 224
LANE_HEIGHT     EQU 16
TOTAL_LANES     EQU 48
MAX_OBJECTS     EQU 8

; Hardware / VDP Addresses
VRAM_ADDR       EQU $3C0000
VRAM_DATA       EQU $3C0002
VRAM_MOD        EQU $3C0004
SYS_CTRL        EQU $FF0000
JOYPAD_PORT0    EQU $300000

; Game States (State Machine)
STATE_MAIN_MENU EQU 0
STATE_GAMEPLAY  EQU 1
STATE_PAUSE_MENU EQU 2
STATE_GAME_OVER EQU 3

; Biome Types
BIOME_TAIGA         EQU 0
BIOME_STD_DESERT    EQU 1
BIOME_RED_DESERT    EQU 2
BIOME_SNOWY_PLAINS  EQU 3
BIOME_PLAINS        EQU 4

; Lane Types
LANE_SAFE       EQU 0
LANE_HIGHWAY    EQU 1
LANE_RIVER      EQU 2

; --- RAM / DATA STRUCTURES (BSS) ---
    SECTION .bss

ctx_active_hardware: ds.w 1
ctx_state:           ds.w 1
ctx_ticks:           ds.l 1
ctx_seed:            ds.l 1
ctx_camera_y:        ds.l 1
ctx_target_camera_y: ds.l 1
ctx_current_biome:   ds.w 1

ctx_menu_sel:        ds.b 1
ctx_pause_sel:       ds.b 1
ctx_gameover_sel:    ds.b 1
ctx_last_pad:        ds.b 1
                     even

ctx_fps_counter:     ds.l 1
ctx_current_fps:     ds.l 1
ctx_second_timer:    ds.l 1

; FrogPlayer
player_x:            ds.l 1
player_y:            ds.l 1
player_w:            ds.l 1
player_h:            ds.l 1
player_lives:        ds.b 1
                     even
player_score:        ds.l 1
player_alive:        ds.b 1
                     even

; LaneData Array
lanes_data:          ds.b TOTAL_LANES*320
                     even

; ==========================================================================
; ENTRY POINT
; ==========================================================================
    SECTION .text
    org $000000

    dc.l $0010c000          ; Stack Pointer
    dc.l EntryPoint         ; Reset Vector

EntryPoint:
    move.w  #$2700, sr      ; Disable interrupts

    bsr     DetectHardwareVersion
    
    ; 1. BOOT HELLO MESSAGE
    bsr     NeoDev_ClearScreen
    lea     (.txt_boot_hello), a0
    moveq   #5, d0
    moveq   #7, d1
    bsr     NeoDev_DrawText
    
    move.l  #1200000, d0
.boot_delay:
    subq.l  #1, d0
    bne.s   .boot_delay

    bsr     SelectRandomBiome     ; Pick random biome for Main Menu
    move.w  #STATE_MAIN_MENU, ctx_state

MainLoop:
    bsr     WaitVBlank
    
    addq.l  #1, ctx_ticks
    addq.l  #1, ctx_fps_counter
    addq.l  #1, ctx_second_timer
    cmp.l   #60, ctx_second_timer
    blt.s   .skip_fps
    
    move.l  ctx_fps_counter, ctx_current_fps
    clr.l   ctx_fps_counter
    clr.l   ctx_second_timer
.skip_fps:

    bsr     HandleControllerInput

    move.w  ctx_state, d0
    cmp.w   #STATE_GAMEPLAY, d0
    bne.s   .render_only
    bsr     UpdateCameraScroll
    bsr     UpdateLanes
    bsr     CheckCollisions

.render_only:
    bsr     RenderFrame
    bra.s   MainLoop

; ==========================================================================
; HARDWARE DETECTION (CD vs CDZ)
; ==========================================================================
DetectHardwareVersion:
    movea.l #SYS_CTRL, a0
    move.w  (a0), d0
    and.w   #$0002, d0
    bne.s   .is_cdz
    move.b  2(a0), d0
    cmp.b   #'Z', d0
    beq.s   .is_cdz
    
    move.w  #1, ctx_active_hardware
    rts
.is_cdz:
    move.w  #2, ctx_active_hardware
    rts

; ==========================================================================
; VRAM & FIX LAYER DRAWING
; ==========================================================================
NeoDev_ClearScreen:
    move.w  #1, VRAM_MOD
    move.l  #$00007000, VRAM_ADDR
    move.w  #32*32-1, d0
    move.l  #VRAM_DATA, a1
.clear_loop:
    move.w  #$0020, (a1)
    dbra    d0, .clear_loop
    rts

NeoDev_DrawText:
    move.w  #1, VRAM_MOD
    mulu.w  #32, d1
    add.w   d0, d1
    addi.l  #$00007000, d1
    move.l  d1, VRAM_ADDR
    
    move.l  #VRAM_DATA, a1
.draw_char_loop:
    move.b  (a0)+, d0
    beq.s   .done
    move.w  d0, (a1)
    bra.s   .draw_char_loop
.done:
    rts

; ==========================================================================
; RANDOM NUMBER GENERATOR (LCG)
; ==========================================================================
CD_Random:
    move.l  ctx_seed, d0
    mulu.l  #1664525, d0
    addi.l  #1013904223, d0
    move.l  d0, ctx_seed
    rts

SelectRandomBiome:
    bsr     CD_Random
    divu.w  #4, d0
    swap    d0
    move.w  d0, ctx_current_biome
    rts

GetStrictNextBiome:
    cmp.w   #BIOME_PLAINS, d0
    beq.s   .from_plains
    cmp.w   #BIOME_SNOWY_PLAINS, d0
    beq.s   .from_plains
    
    bsr     CD_Random
    and.l   #1, d0
    bne.s   .to_plains
    move.w  #BIOME_SNOWY_PLAINS, d0
    rts
.to_plains:
    move.w  #BIOME_PLAINS, d0
    rts

.from_plains:
    bsr     CD_Random
    divu.w  #3, d0
    swap    d0
    cmp.w   #2, d0
    bgt.s   .to_taiga
    beq.s   .to_stddesert
    move.w  #BIOME_RED_DESERT, d0
    rts
.to_stddesert:
    move.w  #BIOME_STD_DESERT, d0
    rts
.to_taiga:
    move.w  #BIOME_TAIGA, d0
    rts

; ==========================================================================
; INIT GAMEPLAY
; ==========================================================================
InitGameplay:
    move.w  ctx_active_hardware, d0
    cmp.w   #2, d0
    bne.s   .seed_cd
    move.l  #$43445A31, ctx_seed
    bra.s   .seed_set
.seed_cd:
    move.l  #$4344312D, ctx_seed
.seed_set:
    
    clr.l   ctx_camera_y
    clr.l   ctx_target_camera_y
    
    move.l  #160, player_x
    move.l  #192, player_y
    move.l  #16, player_w
    move.l  #16, player_h
    move.b  #3, player_lives
    clr.l   player_score
    move.b  #1, player_alive

    moveq   #0, d6
    move.w  ctx_current_biome, d5

.lane_init_loop:
    cmp.w   #TOTAL_LANES, d6
    bge.s   .init_done

    tst.w   d6
    beq.s   .biome_check_skip
    move.w  d6, d0
    ext.l   d0
    divu.w  #4, d0
    swap    d0
    tst.w   d0
    bne.s   .biome_check_skip
    
    move.w  d5, d0
    bsr     GetStrictNextBiome
    move.w  d0, d5

.biome_check_skip:
    lea     lanes_data, a0
    move.w  d6, d0
    mulu.w  #320, d0
    adda.l  d0, a0

    move.b  d6, 0(a0)
    move.w  d5, 1(a0)
    
    move.w  d6, d0
    mulu.w  #LANE_HEIGHT, d0
    move.l  d0, 5(a0)

    tst.w   d6
    beq.s   .is_safe
    move.w  d6, d0
    divu.w  #5, d0
    swap    d0
    tst.w   d0
    beq.s   .is_safe

    move.w  d6, d0
    btst    #0, d0
    bne.s   .is_highway

.is_river:
    move.w  #LANE_RIVER, 3(a0)
    move.l  #-2, 9(a0)
    move.w  #90, 15(a0)
    bra.s   .objects_init

.is_highway:
    move.w  #LANE_HIGHWAY, 3(a0)
    move.w  ctx_active_hardware, d0
    cmp.w   #2, d0
    beq.s   .hw_cdz
    move.l  #-3, 9(a0)
    move.w  #75, 15(a0)
    bra.s   .objects_init
.hw_cdz:
    move.l  #-4, 9(a0)
    move.w  #55, 15(a0)
    bra.s   .objects_init

.is_safe:
    move.w  #LANE_SAFE, 3(a0)
    clr.l   9(a0)
    clr.w   15(a0)

.objects_init:
    lea     17(a0), a1
    moveq   #0, d7
.obj_loop:
    cmp.w   #MAX_OBJECTS, d7
    bge.s   .next_lane

    clr.l   0(a1)
    move.l  #-999, 0(a1)
    move.l  5(a0), 4(a1)
    move.l  #32, 8(a1)
    move.l  #14, 12(a1)
    move.l  9(a0), 16(a1)
    clr.b   20(a1)
    clr.b   21(a1)

    move.w  3(a0), d0
    cmp.w   #LANE_HIGHWAY, d0
    bne.s   .obj_river_check

    cmp.w   #BIOME_RED_DESERT, d5
    beq.s   .obj_red_desert
    cmp.w   #BIOME_STD_DESERT, d5
    beq.s   .obj_std_desert
    
    ; Highway with Standard Cars
    move.b  #0, 21(a1)
    move.l  #36, 8(a1)
    bra.s   .obj_next

.obj_red_desert:
    move.b  #3, 21(a1)      ; Variant 3: Red Desert Lion / Caravan
    move.l  #38, 8(a1)
    bra.s   .obj_next

.obj_std_desert:
    move.b  #5, 21(a1)      ; Variant 5: Standard Desert Camel / Vehicle
    move.l  #36, 8(a1)
    bra.s   .obj_next

.obj_river_check:
    cmp.w   #LANE_RIVER, d0
    bne.s   .obj_next
    
    cmp.w   #BIOME_TAIGA, d5
    bne.s   .river_norm

    move.w  d7, d0
    btst    #0, d0
    bne.s   .obj_taiga_ice
    move.b  #4, 21(a1)
    move.l  #40, 8(a1)
    bra.s   .obj_next
.obj_taiga_ice:
    move.b  #2, 21(a1)
    move.l  #48, 8(a1)
    clr.l   22(a1)
    move.l  #12, 26(a1)
    move.l  #13, 30(a1)
    move.l  #48, 34(a1)
    bra.s   .obj_next

.river_norm:
    move.w  d7, d0
    btst    #0, d0
    bne.s   .obj_crocodile
    move.b  #1, 21(a1)
    move.l  #36, 8(a1)
    bra.s   .obj_next

.obj_crocodile:
    move.b  #2, 21(a1)
    move.l  #48, 8(a1)
    clr.l   22(a1)
    move.l  #12, 26(a1)
    move.l  #13, 30(a1)
    move.l  #48, 34(a1)
    
    bsr     CD_Random
    and.l   #1, d0
    move.b  d0, 38(a1)

.obj_next:
    lea     38(a1), a1
    addq.w  #1, d7
    bra.s   .obj_loop

.next_lane:
    addq.w  #1, d6
    bra     .lane_init_loop

.init_done:
    rts

; ==========================================================================
; UPDATE CAMERA SCROLL ANIMATION
; ==========================================================================
UpdateCameraScroll:
    move.l  ctx_camera_y, d0
    move.l  ctx_target_camera_y, d1
    cmp.l   d0, d1
    beq.s   .scroll_done
    bgt.s   .scroll_up
    
    subq.l  #2, ctx_camera_y
    bra.s   .scroll_done

.scroll_up:
    addq.l  #2, ctx_camera_y

.scroll_done:
    rts

; ==========================================================================
; UPDATE LANES
; ==========================================================================
UpdateLanes:
    moveq   #0, d6
.upd_lane_loop:
    cmp.w   #TOTAL_LANES, d6
    bge.s   .upd_done

    lea     lanes_data, a0
    move.w  d6, d0
    mulu.w  #320, d0
    adda.l  d0, a0

    move.w  3(a0), d0
    cmp.w   #LANE_SAFE, d0
    beq     .upd_next_lane

    addq.w  #1, 17(a0)
    move.w  17(a0), d0
    cmp.w   15(a0), d0
    blt.s   .upd_objects

    clr.w   17(a0)
    bsr     SpawnLaneObj

.upd_objects:
    lea     19(a0), a1
    moveq   #0, d7
.upd_obj_loop:
    cmp.w   #MAX_OBJECTS, d7
    bge.s   .upd_next_lane

    tst.b   20(a1)
    beq.s   .upd_obj_cont

    move.l  16(a1), d0
    add.l   d0, 0(a1)

    move.b  21(a1), d0
    cmp.b   #2, d0
    bne.s   .check_bounds

    move.l  ctx_ticks, d0
    ext.l   d0
    divu.w  #40, d0
    swap    d0
    tst.w   d0
    bne.s   .check_bounds
    
    move.b  38(a1), d0
    eori.b  #1, d0
    move.b  d0, 38(a1)

.check_bounds:
    move.l  0(a1), d0
    cmp.l   #-64, d0
    bge.s   .upd_obj_cont
    clr.b   20(a1)

.upd_obj_cont:
    lea     38(a1), a1
    addq.w  #1, d7
    bra.s   .upd_obj_loop

.upd_next_lane:
    addq.w  #1, d6
    bra.s   .upd_lane_loop

.upd_done:
    rts

SpawnLaneObj:
    lea     19(a0), a1
    moveq   #0, d7
.spawn_loop:
    cmp.w   #MAX_OBJECTS, d7
    bge.s   .spawn_exit

    tst.b   20(a1)
    bne.s   .spawn_next

    move.b  #1, 20(a1)
    move.l  #SCREEN_WIDTH+32, 0(a1)
    move.l  9(a0), 16(a1)

    move.b  21(a1), d0
    cmp.b   #2, d0
    bne.s   .spawn_exit
    
    bsr     CD_Random
    and.l   #1, d0
    move.b  d0, 38(a1)
    bra.s   .spawn_exit

.spawn_next:
    lea     38(a1), a1
    addq.w  #1, d7
    bra.s   .spawn_loop
.spawn_exit:
    rts

; ==========================================================================
; RENDER (VDP & HUD)
; ==========================================================================
RenderFrame:
    bsr     NeoDev_ClearScreen

    ; 1. CORNER HUD: Hardware Mode
    move.w  ctx_active_hardware, d0
    cmp.w   #2, d0
    bne.s   .hud_cd
    lea     (.txt_cdz), a0
    bra.s   .draw_hw
.hud_cd:
    lea     (.txt_cd), a0
.draw_hw:
    moveq   #1, d0
    moveq   #1, d1
    bsr     NeoDev_DrawText

    ; Canlı FPS Göstergesi
    lea     (.fps_buf), a1
    move.l  ctx_current_fps, d0
    divu.w  #10, d0
    add.b   #$30, d0
    move.b  d0, 4(a1)
    swap    d0
    add.b   #$30, d0
    move.b  d0, 5(a1)
    
    move.l  a1, a0
    moveq   #24, d0
    moveq   #1, d1
    bsr     NeoDev_DrawText

    ; Active Biome Check & Print (Only during gameplay)
    move.w  ctx_state, d0
    cmp.w   #STATE_GAMEPLAY, d0
    bne.s   .skip_gameplay_biome

    move.l  player_y, d0
    add.l   ctx_camera_y, d0
    divu.w  #LANE_HEIGHT, d0
    ext.l   d0
    move.l  d0, d6

    bmi.s   .skip_gameplay_biome
    cmp.w   #TOTAL_LANES, d6
    bge.s   .skip_gameplay_biome

    lea     lanes_data, a0
    move.w  d6, d0
    mulu.w  #320, d0
    adda.l  d0, a0
    move.w  1(a0), ctx_current_biome

.skip_gameplay_biome:
    move.w  ctx_state, d0
    cmp.w   #STATE_MAIN_MENU, d0
    beq.s   .skip_hud_biome

    move.w  ctx_current_biome, d0
    cmp.w   #BIOME_TAIGA, d0
    beq.s   .b_taiga
    cmp.w   #BIOME_STD_DESERT, d0
    beq.s   .b_std_desert
    cmp.w   #BIOME_RED_DESERT, d0
    beq.s   .b_red_desert
    cmp.w   #BIOME_SNOWY_PLAINS, d0
    beq.s   .b_snowy_plains
    lea     (.txt_b_plains), a0
    bra.s   .draw_biome
.b_taiga:
    lea     (.txt_b_taiga), a0
    bra.s   .draw_biome
.b_std_desert:
    lea     (.txt_b_stddesert), a0
    bra.s   .draw_biome
.b_red_desert:
    lea     (.txt_b_reddesert), a0
    bra.s   .draw_biome
.b_snowy_plains:
    lea     (.txt_b_snowyplains), a0
.draw_biome:
    moveq   #1, d0
    moveq   #2, d1
    bsr     NeoDev_DrawText
.skip_hud_biome:

    ; State Machine Screens
    move.w  ctx_state, d0
    cmp.w   #STATE_MAIN_MENU, d0
    beq     .render_menu
    cmp.w   #STATE_PAUSE_MENU, d0
    beq     .render_pause
    cmp.w   #STATE_GAME_OVER, d0
    beq     .render_gameover
    cmp.w   #STATE_GAMEPLAY, d0
    beq     .render_gameplay
    rts

.render_menu:
    move.w  ctx_current_biome, d0
    cmp.w   #BIOME_TAIGA, d0
    beq.s   .m_taiga
    cmp.w   #BIOME_STD_DESERT, d0
    beq.s   .m_stddesert
    cmp.w   #BIOME_RED_DESERT, d0
    beq.s   .m_reddesert
    cmp.w   #BIOME_SNOWY_PLAINS, d0
    beq.s   .m_snowyplains
    lea     (.txt_m_plains), a0
    bra.s   .m_draw_title
.m_taiga:
    lea     (.txt_m_taiga), a0
    bra.s   .m_draw_title
.m_stddesert:
    lea     (.txt_m_stddesert), a0
    bra.s   .m_draw_title
.m_reddesert:
    lea     (.txt_m_reddesert), a0
    bra.s   .m_draw_title
.m_snowyplains:
    lea     (.txt_m_snowyplains), a0
.m_draw_title:
    moveq   #4, d0
    moveq   #6, d1
    bsr     NeoDev_DrawText

    lea     (.txt_btn_start), a0
    moveq   #9, d0
    moveq   #11, d1
    bsr     NeoDev_DrawText

    lea     (.txt_btn_eject), a0
    moveq   #9, d0
    moveq   #13, d1
    bsr     NeoDev_DrawText
    rts

.render_pause:
    lea     (.txt_pause), a0
    moveq   #10, d0
    moveq   #8, d1
    bsr     NeoDev_DrawText
    rts

.render_gameover:
    lea     (.txt_gameover), a0
    moveq   #11, d0
    moveq   #8, d1
    bsr     NeoDev_DrawText
    rts

.render_gameplay:
    ; Sprite Table (SAT) Update
    move.w  #1, VRAM_MOD
    move.l  #$00008000, VRAM_ADDR
    move.l  #VRAM_DATA, a2

    move.l  player_y, d0
    sub.l   ctx_camera_y, d0
    and.l   #$1FF, d0
    neg.l   d0
    addi.l  #512, d0
    lsl.w   #7, d0
    move.w  d0, (a2)
    move.w  #$0002, (a2)
    move.w  #$1000, (a2)
    move.l  player_x, d0
    and.l   #$1FF, d0
    move.w  d0, (a2)

    moveq   #0, d6
.rnd_lane_loop:
    cmp.w   #TOTAL_LANES, d6
    bge.s   .rnd_done

    lea     lanes_data, a0
    move.w  d6, d0
    mulu.w  #320, d0
    adda.l  d0, a0
    lea     19(a0), a1

    moveq   #0, d7
.rnd_obj_loop:
    cmp.w   #MAX_OBJECTS, d7
    bge.s   .rnd_next_lane

    tst.b   20(a1)
    beq.s   .rnd_obj_cont

    move.l  4(a1), d0
    sub.l   ctx_camera_y, d0
    
    cmp.l   #-32, d0
    blt.s   .rnd_obj_cont
    cmp.l   #SCREEN_HEIGHT+32, d0
    bgt.s   .rnd_obj_cont

    and.l   #$1FF, d0
    neg.l   d0
    addi.l  #512, d0
    lsl.w   #7, d0
    move.w  d0, (a2)
    move.w  #$0002, (a2)
    
    move.b  21(a1), d1
    cmp.b   #5, d1
    beq.s   .attr_std_desert
    cmp.b   #4, d1
    beq.s   .attr_taiga_ice
    cmp.b   #3, d1
    beq.s   .attr_red_desert
    cmp.b   #2, d1
    beq.s   .attr_croc
    move.w  #$1100, (a2)        ; Highway Car / Standard object
    bra.s   .pos_x
.attr_croc:
    move.w  #$1200, (a2)
    bra.s   .pos_x
.attr_red_desert:
    move.w  #$1300, (a2)
    bra.s   .pos_x
.attr_taiga_ice:
    move.w  #$1400, (a2)
    bra.s   .pos_x
.attr_std_desert:
    move.w  #$1500, (a2)

.pos_x:
    move.l  0(a1), d0
    and.l   #$1FF, d0
    move.w  d0, (a2)

.rnd_obj_cont:
    lea     38(a1), a1
    addq.w  #1, d7
    bra.s   .rnd_obj_loop

.rnd_next_lane:
    addq.w  #1, d6
    bra.s   .rnd_lane_loop

.rnd_done:
    rts

; --- Text Constants ---
.txt_cd:        dc.b "HW: CD [1X]",0
.txt_cdz:       dc.b "HW: CDZ [2X]",0
.fps_buf:       dc.b "FPS:00",0
.txt_boot_hello:dc.b "BIOME: HELLO",0
.txt_boot_bye:  dc.b "BIOME: GOODBYE",0
.txt_b_taiga:   dc.b "BIOME: SNOWY TAIGA",0
.txt_b_stddesert:dc.b "BIOME: STANDARD DESERT",0
.txt_b_reddesert:dc.b "BIOME: RED DESERT",0
.txt_b_snowyplains: dc.b "BIOME: SNOWY PLAINS",0
.txt_b_plains:  dc.b "BIOME: PLAINS",0
.txt_m_taiga:   dc.b "FROGGER: SNOWY TAIGA",0
.txt_m_stddesert:dc.b "FROGGER: STD DESERT",0
.txt_m_reddesert:dc.b "FROGGER: RED DESERT",0
.txt_m_snowyplains: dc.b "FROGGER: SNOWY PLAINS",0
.txt_m_plains:  dc.b "FROGGER: PLAINS",0
.txt_btn_start: dc.b "> START GAME",0
.txt_btn_eject: dc.b "> EJECT CD (EXIT)",0
.txt_pause:     dc.b "--- GAME PAUSED ---",0
.txt_gameover:  dc.b "GAME OVER",0
                even

; ==========================================================================
; CHECK COLLISIONS
; ==========================================================================
CheckCollisions:
    move.l  player_y, d0
    add.l   ctx_camera_y, d0
    ext.l   d0
    divu.w  #LANE_HEIGHT, d0
    ext.l   d0
    move.l  d0, d6

    bmi.s   .col_exit
    cmp.w   #TOTAL_LANES, d6
    bge.s   .col_exit

    lea     lanes_data, a0
    move.w  d6, d0
    mulu.w  #320, d0
    adda.l  d0, a0

    move.w  3(a0), d0
    cmp.w   #LANE_HIGHWAY, d0
    beq.s   .col_highway
    cmp.w   #LANE_RIVER, d0
    beq.s   .col_river
    bra.s   .col_exit

.col_highway:
    lea     19(a0), a1
    moveq   #0, d7
.hw_col_loop:
    cmp.w   #MAX_OBJECTS, d7
    bge.s   .col_exit
    tst.b   20(a1)
    beq.s   .hw_col_next

    move.l  player_x, d0
    add.l   player_w, d0
    cmp.l   0(a1), d0
    ble.s   .hw_col_next

    move.l  player_x, d0
    move.l  0(a1), d1
    add.l   8(a1), d1
    cmp.l   d1, d0
    bge.s   .hw_col_next

    clr.b   player_alive
    clr.b   ctx_gameover_sel
    move.w  #STATE_GAME_OVER, ctx_state
    bra.s   .col_exit

.hw_col_next:
    lea     38(a1), a1
    addq.w  #1, d7
    bra.s   .hw_col_loop

.col_river:
    lea     19(a0), a1
    moveq   #0, d7
    moveq   #0, d5
    moveq   #0, d4

.riv_col_loop:
    cmp.w   #MAX_OBJECTS, d7
    bge.s   .riv_check_safe
    tst.b   20(a1)
    beq.s   .riv_col_next

    move.l  player_x, d0
    add.l   player_w, d0
    cmp.l   0(a1), d0
    ble.s   .riv_col_next

    move.l  player_x, d0
    move.l  0(a1), d1
    add.l   8(a1), d1
    cmp.l   d1, d0
    bge.s   .riv_col_next

    move.b  21(a1), d2
    cmp.b   #1, d2
    beq.s   .riv_safe_obj
    cmp.b   #4, d2
    beq.s   .riv_safe_obj

    cmp.b   #2, d2
    bne.s   .riv_col_next

    move.l  player_x, d0
    sub.l   0(a1), d0
    
    cmp.l   22(a1), d0
    blt.s   .riv_back_check
    cmp.l   26(a1), d0
    bgt.s   .riv_back_check

    tst.b   38(a1)
    bne.s   .game_over_trigger

.riv_back_check:
    cmp.l   30(a1), d0
    blt.s   .riv_col_next
    cmp.l   34(a1), d0
    bgt.s   .riv_col_next

    moveq   #1, d5
    move.l  16(a1), d4
    bra.s   .riv_col_next

.riv_safe_obj:
    moveq   #1, d5
    move.l  16(a1), d4
    bra.s   .riv_col_next

.riv_col_next:
    lea     38(a1), a1
    addq.w  #1, d7
    bra.s   .riv_col_loop

.riv_check_safe:
    tst.b   d5
    beq.s   .game_over_trigger

    move.l  player_x, d0
    add.l   d4, d0
    move.l  d0, player_x
    
    bmi.s   .game_over_trigger
    cmp.l   #SCREEN_WIDTH, d0
    bgt.s   .game_over_trigger
    bra.s   .col_exit

.game_over_trigger:
    clr.b   player_alive
    clr.b   ctx_gameover_sel
    move.w  #STATE_GAME_OVER, ctx_state

.col_exit:
    rts

; ==========================================================================
; CONTROLLER INPUT
; ==========================================================================
HandleControllerInput:
    movea.l #JOYPAD_PORT0, a0
    move.b  (a0), d0
    not.b   d0
    move.b  d0, d1

    move.b  ctx_last_pad, d2
    eor.b   d1, d2
    and.b   d1, d2
    move.b  d2, d3

    move.w  ctx_state, d4
    cmp.w   #STATE_MAIN_MENU, d4
    beq.s   .inp_menu
    cmp.w   #STATE_GAMEPLAY, d4
    beq.s   .inp_gameplay
    cmp.w   #STATE_PAUSE_MENU, d4
    beq.s   .inp_pause
    cmp.w   #STATE_GAME_OVER, d4
    beq.s   .inp_gameover
    bra.s   .inp_end

.inp_menu:
    btst    #0, d3          ; Up
    beq.s   .m_dn
    tst.b   ctx_menu_sel
    beq.s   .m_dn
    subq.b  #1, ctx_menu_sel
.m_dn:
    btst    #1, d3          ; Down
    beq.s   .m_act
    cmp.b   #1, ctx_menu_sel
    bge.s   .m_act
    addq.b  #1, ctx_menu_sel

.m_act:
    btst    #4, d3          ; Button A / Start
    bne.s   .m_action
    btst    #5, d3
    bne.s   .m_action
    bra.s   .inp_end

.m_action:
    tst.b   ctx_menu_sel
    bne.s   .cd_eject_exit

    bsr     InitGameplay
    move.w  #STATE_GAMEPLAY, ctx_state
    bra.s   .inp_end

.cd_eject_exit:
    bsr     NeoDev_ClearScreen
    lea     (.txt_boot_bye), a0
    moveq   #5, d0
    moveq   #7, d1
    bsr     NeoDev_DrawText
    
    move.l  #2000000, d0
.exit_loop:
    subq.l  #1, d0
    bne.s   .exit_loop
    stop    #$2700

.inp_gameplay:
    btst    #5, d3
    beq.s   .gp_move
    clr.b   ctx_pause_sel
    move.w  #STATE_PAUSE_MENU, ctx_state
    bra.s   .inp_end

.gp_move:
    btst    #0, d1          ; Yukarı tuşu
    bne.s   .gp_down
    subq.l  #2, player_y
    move.l  player_y, d0
    cmp.l   #60, d0
    bge.s   .gp_down
    
    addi.l  #16, player_y
    move.l  ctx_target_camera_y, d0
    addi.l  #16, d0
    move.l  d0, ctx_target_camera_y
    addi.l  #25, player_score

.gp_down:
    btst    #1, d1
    bne.s   .gp_left
    addq.l  #2, player_y
.gp_left:
    btst    #2, d1
    bne.s   .gp_right
    subq.l  #2, player_x
.gp_right:
    btst    #3, d1
    bne.s   .inp_end
    addq.l  #2, player_x
    bra.s   .inp_end

.inp_pause:
    btst    #4, d3
    bne.s   .p_resume
    btst    #5, d3
    bne.s   .p_resume
    bra.s   .inp_end
.p_resume:
    move.w  #STATE_GAMEPLAY, ctx_state
    bra.s   .inp_end

.inp_gameover:
    btst    #4, d3
    bne.s   .g_restart
    btst    #5, d3
    bne.s   .g_restart
    bra.s   .inp_end
.g_restart:
    bsr     SelectRandomBiome
    move.w  #STATE_MAIN_MENU, ctx_state

.inp_end:
    move.b  d1, ctx_last_pad
    rts

; ==========================================================================
; WAIT VBLANK
; ==========================================================================
WaitVBlank:
    move.w  VRAM_MOD, d0
    rts
