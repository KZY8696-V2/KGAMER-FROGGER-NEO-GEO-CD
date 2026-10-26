/* ========================================================================== *
 * KGAMER FROGGER NEO GEO CD / CDZ - FULL IN-GAME HUD & ACTIVE HARDWARE CORE   *
 * Live In-Game HUD: FPS, HW Model (CD/CDZ), Current Biome & Score Tracker     *
 * ========================================================================== */

#include <stdint.h>
#include <string.h>
#include "neogeo.h"

// Donanımsal VDP Register Adresleri
#define REG_VRAM_ADDR       (*(volatile uint16_t*)0x3C0000)
#define REG_VRAM_DATA       (*(volatile uint16_t*)0x3C0002)
#define REG_VRAM_MOD        (*(volatile uint16_t*)0x3C0004)
#define REG_VBLANK_STATUS   (*(volatile uint16_t*)0x3C0006)
#define REG_JOYPAD1         (*(volatile uint8_t*)0x3C000B)
#define REG_SYSTEM_CTRL     (*(volatile uint16_t*)0xFF0000)

#define SCREEN_WIDTH        320
#define SCREEN_HEIGHT       224
#define LANE_HEIGHT         16
#define TOTAL_LANES         48
#define MAX_OBJECTS         8

typedef enum {
    HARDWARE_MODEL_CD = 1,
    HARDWARE_MODEL_CDZ
} HardwareModel;

typedef enum {
    STATE_MAIN_MENU = 0,
    STATE_GAMEPLAY,
    STATE_PAUSE_MENU,
    STATE_GAME_OVER
} GameModeState;

typedef enum {
    BIOME_TAIGA = 0,      // Karlı Taiga (Soğuk: Buz & Timsah)
    BIOME_RED_DESERT,     // Kızıl Çöl (Sıcak: Develer & Kervanlar)
    BIOME_PLAINS          // Ovalar (Tampon Güvenli Bölge)
} BiomeType;

typedef enum {
    LANE_SAFE = 0,
    LANE_HIGHWAY,
    LANE_RIVER
} LaneType;

typedef struct {
    int32_t x, y;
    int32_t width, height;
    int32_t velocity;
    uint8_t active;
    uint8_t variant;
    
    int32_t mouth_start;
    int32_t mouth_end;
    int32_t back_start;
    int32_t back_end;
    uint8_t mouth_open;
} GameObject;

typedef struct {
    uint8_t index;
    BiomeType biome;
    LaneType type;
    int32_t world_y;
    int32_t base_speed;
    uint16_t timer;
    uint16_t interval;
    GameObject objects[MAX_OBJECTS];
} LaneData;

typedef struct {
    int32_t x, y, w, h;
    uint8_t lives;
    uint32_t score;
    uint8_t alive;
} FrogPlayer;

typedef struct {
    HardwareModel active_hardware;
    GameModeState state;
    LaneData lanes[TOTAL_LANES];
    FrogPlayer player;
    uint32_t ticks;
    uint32_t seed;
    int32_t camera_y;
    BiomeType current_biome;
    
    uint8_t menu_sel;
    uint8_t pause_sel;
    uint8_t gameover_sel;
    uint8_t last_pad;
    
    // FPS ve İstatistik Sayacı
    uint32_t fps_counter;
    uint32_t current_fps;
    uint32_t second_timer;
} GameContext;

static GameContext ctx;

// --- DİNAMİK CİHAZ TESPİTİ ---
static HardwareModel DetectHardwareVersion(void) {
    uint16_t hw_status = REG_SYSTEM_CTRL;
    if ((hw_status & 0x0002) != 0 || (*(volatile uint8_t*)0xFF0002 == 'Z')) {
        return HARDWARE_MODEL_CDZ;
    }
    return HARDWARE_MODEL_CD;
}

// --- VDP VRAM RENDER İŞLEMLERİ ---
static void VDP_SetAddress(uint32_t addr, uint16_t mode_inc) {
    REG_VRAM_MOD = mode_inc;
    REG_VRAM_ADDR = (uint16_t)(addr & 0xFFFF);
}

static void VDP_WriteWord(uint16_t data) {
    REG_VRAM_DATA = data;
}

static void VDP_DrawTextToFixLayer(int col, int row, const char *str) {
    uint32_t vram_offset = 0x7000 + (row * 32 + col);
    VDP_SetAddress(vram_offset, 1);
    while (*str) {
        VDP_WriteWord((uint16_t)(*str));
        str++;
    }
}

static void VDP_ClearScreenData(void) {
    VDP_SetAddress(0x7000, 1);
    for (int i = 0; i < 32 * 32; i++) {
        VDP_WriteWord(0x0020);
    }
}

static void CD_WaitVBlank(void) {
    while ((REG_VBLANK_STATUS & 0x0100) == 0) {
        // VDP Tarama Beklemesi
    }
}

static uint32_t CD_Random(void) {
    ctx.seed = (ctx.seed * 1664525UL + 1013904223UL);
    return ctx.seed;
}

// Katı Biyom Geçiş Matrisi
static BiomeType GetStrictNextBiome(BiomeType current) {
    if (current == BIOME_TAIGA || current == BIOME_RED_DESERT) {
        return BIOME_PLAINS;
    }
    return (ctx.seed % 2 == 0) ? BIOME_TAIGA : BIOME_RED_DESERT;
}

static void InitGameplay(void) {
    ctx.active_hardware = DetectHardwareVersion();
    ctx.seed = (ctx.active_hardware == HARDWARE_MODEL_CDZ) ? 0x43445A31 : 0x4344312D;
    
    ctx.camera_y = 0;
    ctx.current_biome = BIOME_PLAINS;
    
    ctx.player.x = 160;
    ctx.player.y = 192;
    ctx.player.w = 16;
    ctx.player.h = 16;
    ctx.player.lives = 3;
    ctx.player.score = 0;
    ctx.player.alive = 1;

    BiomeType b = BIOME_PLAINS;
    for (int i = 0; i < TOTAL_LANES; i++) {
        if (i > 0 && (i % 4 == 0)) {
            b = GetStrictNextBiome(b);
            ctx.current_biome = b;
        }

        LaneData *l = &ctx.lanes[i];
        l->index = (uint8_t)i;
        l->biome = b;
        l->world_y = i * LANE_HEIGHT;

        if (i == 0 || (i % 5 == 0)) {
            l->type = LANE_SAFE;
            l->base_speed = 0;
            l->interval = 0;
        } else if (i % 2 == 1) {
            l->type = LANE_HIGHWAY;
            int32_t speed_mult = (ctx.active_hardware == HARDWARE_MODEL_CDZ) ? 4 : 3;
            l->base_speed = -((int32_t)(CD_Random() % 3) + speed_mult);
            l->interval = (ctx.active_hardware == HARDWARE_MODEL_CDZ) ? 55 : 75;
        } else {
            l->type = LANE_RIVER;
            l->base_speed = -2;
            l->interval = 90;
        }

        for (int j = 0; j < MAX_OBJECTS; j++) {
            GameObject *o = &l->objects[j];
            o->active = 0;
            o->x = -999;
            o->y = l->world_y;
            o->width = 32;
            o->height = 14;
            o->velocity = l->base_speed;

            if (l->type == LANE_HIGHWAY) {
                o->variant = (b == BIOME_RED_DESERT) ? 3 : 0;
                o->width = 36;
            } else if (l->type == LANE_RIVER) {
                if (j % 2 == 0) {
                    o->variant = 1; // Buz
                } else {
                    o->variant = 2; // Timsah
                    o->width = 48;
                    o->mouth_start = 0;
                    o->mouth_end = 12;
                    o->back_start = 13;
                    o->back_end = 48;
                    o->mouth_open = (uint8_t)(CD_Random() % 2);
                }
            }
        }
    }
}

static void SpawnLaneObj(LaneData *l) {
    if (l->type == LANE_SAFE) return;
    for (int i = 0; i < MAX_OBJECTS; i++) {
        GameObject *o = &l->objects[i];
        if (!o->active) {
            o->active = 1;
            o->x = SCREEN_WIDTH + 32;
            o->velocity = l->base_speed;
            if (o->variant == 2) o->mouth_open = (uint8_t)(CD_Random() % 2);
            break;
        }
    }
}

static void UpdateLanes(void) {
    for (int i = 0; i < TOTAL_LANES; i++) {
        LaneData *l = &ctx.lanes[i];
        if (l->type == LANE_SAFE) continue;

        l->timer++;
        if (l->timer >= l->interval) {
            l->timer = 0;
            SpawnLaneObj(l);
        }

        for (int j = 0; j < MAX_OBJECTS; j++) {
            GameObject *o = &l->objects[j];
            if (!o->active) continue;

            o->x += o->velocity;
            if (o->variant == 2 && (ctx.ticks % 40) == 0) {
                o->mouth_open = !o->mouth_open;
            }
            if (o->x < -64) o->active = 0;
        }
    }
}

// --- OYUN İÇİ VE MENÜ KÖŞE HUD & VDP RENDERER ---
static void VDP_RenderFrameToHardware(void) {
    VDP_ClearScreenData();

    // 1. OYUN İÇİNDE VE TÜM EKRANLARDA SABİT KÖŞE BİLGİ ALANI (HUD: FPS & Donanım)
    VDP_DrawTextToFixLayer(1, 1, ctx.active_hardware == HARDWARE_MODEL_CDZ ? "HW: CDZ [2X]" : "HW: CD [1X]");
    
    char fps_buf[8];
    int f_val = (int)ctx.current_fps;
    fps_buf[0] = 'F'; fps_buf[1] = 'P'; fps_buf[2] = 'S'; fps_buf[3] = ':';
    fps_buf[4] = '0' + (f_val / 10);
    fps_buf[5] = '0' + (f_val % 10);
    fps_buf[6] = '\0';
    VDP_DrawTextToFixLayer(24, 1, fps_buf);

    // Aktif Biyom Bilgisi Ekranın Üst Köşesinde Sürekli Görünür
    int absolute_lane = (ctx.player.y + ctx.camera_y) / LANE_HEIGHT;
    if (absolute_lane >= 0 && absolute_lane < TOTAL_LANES) {
        ctx.current_biome = ctx.lanes[absolute_lane].biome;
    }

    if (ctx.current_biome == BIOME_TAIGA) {
        VDP_DrawTextToFixLayer(1, 2, "BIOME: KARLI TAIGA");
    } else if (ctx.current_biome == BIOME_RED_DESERT) {
        VDP_DrawTextToFixLayer(1, 2, "BIOME: KIZIL COL");
    } else {
        VDP_DrawTextToFixLayer(1, 2, "BIOME: OVA TAMPON");
    }

    // 2. DURUM MAKİNESİNE GÖRE EKRAN ÇİZİMLERİ
    if (ctx.state == STATE_MAIN_MENU) {
        if (ctx.current_biome == BIOME_TAIGA) {
            VDP_DrawTextToFixLayer(6, 6, "FROGGER: KARLI TAIGA");
        } else if (ctx.current_biome == BIOME_RED_DESERT) {
            VDP_DrawTextToFixLayer(6, 6, "FROGGER: KIZIL COL");
        } else {
            VDP_DrawTextToFixLayer(6, 6, "FROGGER: OVA MODU");
        }

        VDP_DrawTextToFixLayer(9, 11, ctx.menu_sel == 0 ? "> OYUNA BASLA" : "  OYUNA BASLA");
        VDP_DrawTextToFixLayer(9, 13, ctx.menu_sel == 1 ? "> CIKIS" : "  CIKIS");
    } 
    else if (ctx.state == STATE_PAUSE_MENU) {
        VDP_DrawTextToFixLayer(10, 8,  "--- DURAKLATILDI ---");
        VDP_DrawTextToFixLayer(9, 11, ctx.pause_sel == 0 ? "> OYUNA DEVAM ET" : "  OYUNA DEVAM ET");
        VDP_DrawTextToFixLayer(9, 13, ctx.pause_sel == 1 ? "> ANA MENUYE DON" : "  ANA MENUYE DON");
    } 
    else if (ctx.state == STATE_GAME_OVER) {
        VDP_DrawTextToFixLayer(11, 8,  "GAME OVER");
        VDP_DrawTextToFixLayer(9, 11, ctx.gameover_sel == 0 ? "> YENIDEN BASLA" : "  YENIDEN BASLA");
        VDP_DrawTextToFixLayer(9, 13, ctx.gameover_sel == 1 ? "> ANA MENUYE DON" : "  ANA MENUYE DON");
    }
    else if (ctx.state == STATE_GAMEPLAY) {
        // Oyun İçerisinde Can ve Skor HUD Bilgisi
        char score_buf[16];
        int s_val = (int)ctx.player.score;
        score_buf[0] = 'S'; score_buf[1] = ':';
        score_buf[2] = '0' + ((s_val / 1000) % 10);
        score_buf[3] = '0' + ((s_val / 100) % 10);
        score_buf[4] = '0' + ((s_val / 10) % 10);
        score_buf[5] = '0' + (s_val % 10);
        score_buf[6] = '\0';
        VDP_DrawTextToFixLayer(1, 26, score_buf);

        // VDP Sprite Tablosuna Aktarım
        VDP_SetAddress(0x8000, 1);

        int32_t frog_screen_y = ctx.player.y - ctx.camera_y;
        VDP_WriteWord((uint16_t)((512 - (frog_screen_y & 0x1FF)) << 7));
        VDP_WriteWord(0x0002);
        VDP_WriteWord(0x1000);
        VDP_WriteWord((uint16_t)(ctx.player.x & 0x1FF));

        for (int i = 0; i < TOTAL_LANES; i++) {
            LaneData *l = &ctx.lanes[i];
            for (int j = 0; j < MAX_OBJECTS; j++) {
                GameObject *o = &l->objects[j];
                if (o->active) {
                    int32_t obj_screen_y = o->y - ctx.camera_y;
                    if (obj_screen_y >= -32 && obj_screen_y <= SCREEN_HEIGHT + 32) {
                        VDP_WriteWord((uint16_t)((512 - (obj_screen_y & 0x1FF)) << 7));
                        VDP_WriteWord(0x0002);
                        VDP_WriteWord(o->variant == 2 ? 0x1200 : 0x1100);
                        VDP_WriteWord((uint16_t)(o->x & 0x1FF));
                    }
                }
            }
        }
    }
}

static void CheckCollisions(void) {
    int32_t abs_y = ctx.player.y + ctx.camera_y;
    int lane_idx = abs_y / LANE_HEIGHT;
    if (lane_idx < 0 || lane_idx >= TOTAL_LANES) return;

    LaneData *l = &ctx.lanes[lane_idx];

    if (l->type == LANE_HIGHWAY) {
        for (int i = 0; i < MAX_OBJECTS; i++) {
            GameObject *o = &l->objects[i];
            if (!o->active) continue;
            if (ctx.player.x + ctx.player.w > o->x && ctx.player.x < o->x + o->width) {
                ctx.player.alive = 0;
                ctx.gameover_sel = 0;
                ctx.state = STATE_GAME_OVER;
                return;
            }
        }
    } 
    else if (l->type == LANE_RIVER) {
        uint8_t safe = 0;
        int32_t p_vel = 0;

        for (int i = 0; i < MAX_OBJECTS; i++) {
            GameObject *o = &l->objects[i];
            if (!o->active) continue;
            if (ctx.player.x + ctx.player.w > o->x && ctx.player.x < o->x + o->width) {
                if (o->variant == 1) {
                    safe = 1;
                    p_vel = o->velocity;
                    break;
                } else if (o->variant == 2) {
                    int32_t local_x = ctx.player.x - o->x;
                    if (local_x >= o->mouth_start && local_x <= o->mouth_end && o->mouth_open) {
                        ctx.player.alive = 0;
                        ctx.gameover_sel = 0;
                        ctx.state = STATE_GAME_OVER;
                        return;
                    } else if (local_x >= o->back_start && local_x <= o->back_end) {
                        safe = 1;
                        p_vel = o->velocity;
                        break;
                    }
                }
            }
        }

        if (!safe) {
            ctx.player.alive = 0;
            ctx.gameover_sel = 0;
            ctx.state = STATE_GAME_OVER;
        } else {
            ctx.player.x += p_vel;
            if (ctx.player.x < 0 || ctx.player.x > SCREEN_WIDTH) {
                ctx.player.alive = 0;
                ctx.gameover_sel = 0;
                ctx.state = STATE_GAME_OVER;
            }
        }
    }
}

static void HandleControllerInput(void) {
    uint8_t pad = REG_JOYPAD1;
    uint8_t pressed = (ctx.last_pad ^ pad) & ~pad;

    if (ctx.state == STATE_MAIN_MENU) {
        if (pressed & 0x01 && ctx.menu_sel > 0) ctx.menu_sel--;
        if (pressed & 0x02 && ctx.menu_sel < 1) ctx.menu_sel++;
        if ((pressed & 0x10) || (pressed & 0x20)) {
            if (ctx.menu_sel == 0) {
                InitGameplay();
                ctx.state = STATE_GAMEPLAY;
            }
        }
    } else if (ctx.state == STATE_GAMEPLAY) {
        if (pressed & 0x20) {
            ctx.pause_sel = 0;
            ctx.state = STATE_PAUSE_MENU;
            return;
        }
        if (!(pad & 0x01)) {
            ctx.player.y -= 2;
            if (ctx.player.y < 60) {
                ctx.camera_y += 16;
                ctx.player.y += 16;
                ctx.player.score += 25;
            }
        }
        if (!(pad & 0x02)) ctx.player.y += 2;
        if (!(pad & 0x04)) ctx.player.x -= 2;
        if (!(pad & 0x08)) ctx.player.x += 2;
    } else if (ctx.state == STATE_PAUSE_MENU) {
        if (pressed & 0x01 && ctx.pause_sel > 0) ctx.pause_sel--;
        if (pressed & 0x02 && ctx.pause_sel < 1) ctx.pause_sel++;
        if ((pressed & 0x10) || (pressed & 0x20)) {
            if (ctx.pause_sel == 0) ctx.state = STATE_GAMEPLAY;
            else ctx.state = STATE_MAIN_MENU;
        }
    } else if (ctx.state == STATE_GAME_OVER) {
        if (pressed & 0x01 && ctx.gameover_sel > 0) ctx.gameover_sel--;
        if (pressed & 0x02 && ctx.gameover_sel < 1) ctx.gameover_sel++;
        if ((pressed & 0x10) || (pressed & 0x20)) {
            if (ctx.gameover_sel == 0) {
                InitGameplay();
                ctx.state = STATE_GAMEPLAY;
            } else {
                ctx.state = STATE_MAIN_MENU;
            }
        }
    }
    ctx.last_pad = pad;
}

int main(void) {
    memset(&ctx, 0, sizeof(GameContext));
    
    ctx.active_hardware = DetectHardwareVersion();
    ctx.state = STATE_MAIN_MENU;
    ctx.seed = 0x47414D45;
    ctx.last_pad = 0xFF;
    ctx.current_biome = BIOME_PLAINS;

    while (1) {
        CD_WaitVBlank();
        ctx.ticks++;

        // Canlı FPS Sayaç Güncellemesi (Her 60 VBlank karesinde bir hesaplanır)
        ctx.fps_counter++;
        ctx.second_timer++;
        if (ctx.second_timer >= 60) {
            ctx.current_fps = ctx.fps_counter;
            ctx.fps_counter = 0;
            ctx.second_timer = 0;
        }

        HandleControllerInput();

        if (ctx.state == STATE_GAMEPLAY) {
            UpdateLanes();
            CheckCollisions();
        }

        // Oyun içinde ve menülerde anlık HUD ve VDP güncellemesi
        VDP_RenderFrameToHardware();
    }
    return 0;
}
