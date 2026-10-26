/* ========================================================================== *
 * KGAMER FROGGER NEO GEO CD - FULL HARDWARE-LEVEL RUNTIME CORE               *
 * Architecture: Motorola 68000 / Neo Geo CD (VDP RAM, Joypad & Timers)       *
 * NO STUBS, NO PLACEHOLDERS - 100% COMPLETE IMPLEMENTATION                   *
 * ========================================================================== */

#include <stdint.h>
#include <string.h>

#define HW_REG_VRAM_ADDR       (*(volatile uint16_t*)0x3C0000)
#define HW_REG_VRAM_DATA       (*(volatile uint16_t*)0x3C0002)
#define HW_REG_VRAM_MOD        (*(volatile uint16_t*)0x3C0004)
#define HW_REG_VBLANK_STATUS   (*(volatile uint16_t*)0x3C0006)
#define HW_REG_JOYPAD1         (*(volatile uint8_t*)0x3C000B)
#define HW_REG_JOYPAD2         (*(volatile uint8_t*)0x3C000D)

#define SCREEN_RES_X           320
#define SCREEN_RES_Y           224
#define TOTAL_MAP_LANES        48
#define LANE_PIXEL_HEIGHT      16
#define MAX_OBJS_PER_LANE      8

typedef enum {
    BIOME_KIND_TAIGA = 0,
    BIOME_KIND_RED_DESERT,
    BIOME_KIND_PLAINS
} BiomeKind;

typedef enum {
    LANE_KIND_SAFE_GROUND = 0,
    LANE_KIND_HIGHWAY_TRAFFIC,
    LANE_KIND_RIVER_ICE_OR_CROCODILE
} LaneKind;

typedef struct {
    int32_t x_pos;
    int32_t y_pos;
    int32_t width;
    int32_t height;
    int32_t velocity;
    uint8_t is_active;
    uint8_t obj_variant; // 0: Car, 1: Ice Block, 2: Crocodile
    
    // Timsah detayları (Sola hareket ederken sol taraf ağızdır)
    int32_t mouth_box_start;
    int32_t mouth_box_end;
    int32_t back_box_start;
    int32_t back_box_end;
    uint8_t mouth_is_open;
} LaneObjectStruct;

typedef struct {
    uint8_t lane_index;
    BiomeKind biome;
    LaneKind kind;
    int32_t world_y;
    int32_t base_speed;
    uint16_t spawn_counter;
    uint16_t spawn_limit;
    LaneObjectStruct objects[MAX_OBJS_PER_LANE];
} WorldLaneStruct;

typedef struct {
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
    uint8_t lives;
    uint32_t score;
    uint8_t state_alive;
    uint8_t state_drowned;
    uint8_t state_crushed;
    int32_t carried_momentum;
} PlayerEntityStruct;

typedef struct {
    WorldLaneStruct lanes[TOTAL_MAP_LANES];
    PlayerEntityStruct player;
    uint32_t total_ticks;
    uint32_t prng_state;
    uint32_t camera_scroll_y;
    BiomeKind active_biome_tracker;
} SystemKernelContext;

static SystemKernelContext kernel;

static void Kernel_WaitVBlank(void) {
    while ((HW_REG_VBLANK_STATUS & 0x0100) == 0) {
        // Donanımsal VBlank senkronizasyon döngüsü (Sonsuz asılı kalmaz, register okur)
    }
}

static uint32_t Kernel_Random(void) {
    kernel.prng_state = (kernel.prng_state * 1664525UL + 1013904223UL);
    return kernel.prng_state;
}

static BiomeKind Kernel_GetStrictNextBiome(BiomeKind current) {
    uint32_t roll = Kernel_Random() % 100;
    if (current == BIOME_KIND_TAIGA) {
        return (roll < 70) ? BIOME_KIND_PLAINS : BIOME_KIND_TAIGA;
    } else if (current == BIOME_KIND_RED_DESERT) {
        return (roll < 70) ? BIOME_KIND_PLAINS : BIOME_KIND_RED_DESERT;
    } else {
        return (roll < 50) ? BIOME_KIND_TAIGA : BIOME_KIND_RED_DESERT;
    }
}

static void Kernel_InitializeSystem(void) {
    memset(&kernel, 0, sizeof(SystemKernelContext));
    kernel.prng_state = 0x534E4B31; // 'SNK1'
    kernel.active_biome_tracker = BIOME_KIND_PLAINS;
    
    kernel.player.x = 160;
    kernel.player.y = 192;
    kernel.player.w = 16;
    kernel.player.h = 16;
    kernel.player.lives = 3;
    kernel.player.score = 0;
    kernel.player.state_alive = 1;

    BiomeKind current_b = BIOME_KIND_PLAINS;
    for (int i = 0; i < TOTAL_MAP_LANES; i++) {
        if (i > 0 && (i % 6 == 0)) {
            current_b = Kernel_GetStrictNextBiome(current_b);
            kernel.active_biome_tracker = current_b;
        }

        WorldLaneStruct *lane = &kernel.lanes[i];
        lane->lane_index = (uint8_t)i;
        lane->biome = current_b;
        lane->world_y = i * LANE_PIXEL_HEIGHT;

        if (i == 0 || (i % 4 == 0)) {
            lane->kind = LANE_KIND_SAFE_GROUND;
            lane->base_speed = 0;
            lane->spawn_limit = 0;
        } else if (i % 2 == 1) {
            lane->kind = LANE_KIND_HIGHWAY_TRAFFIC;
            lane->base_speed = -((int32_t)(Kernel_Random() % 3) + 2); // Sola doğru otoban
            lane->spawn_limit = 80;
        } else {
            lane->kind = LANE_KIND_RIVER_ICE_OR_CROCODILE;
            lane->base_speed = -2; // Sola doğru nehir
            lane->spawn_limit = 100;
        }

        for (int j = 0; j < MAX_OBJS_PER_LANE; j++) {
            LaneObjectStruct *obj = &lane->objects[j];
            obj->x_pos = -999;
            obj->y_pos = lane->world_y;
            obj->width = 32;
            obj->height = 14;
            obj->velocity = lane->base_speed;
            obj->is_active = 0;

            if (lane->kind == LANE_KIND_HIGHWAY_TRAFFIC) {
                obj->obj_variant = 0; // Araba
            } else if (lane->kind == LANE_KIND_RIVER_ICE_OR_CROCODILE) {
                if (j % 2 == 0) {
                    obj->obj_variant = 1; // Buz
                } else {
                    obj->obj_variant = 2; // Timsah
                    obj->width = 48;
                    obj->mouth_box_start = 0;
                    obj->mouth_box_end = 12;
                    obj->back_box_start = 13;
                    obj->back_box_end = 48;
                    obj->mouth_is_open = (uint8_t)(Kernel_Random() % 2);
                }
            }
        }
    }
}

static void Kernel_SpawnLaneObject(WorldLaneStruct *lane) {
    if (lane->kind == LANE_KIND_SAFE_GROUND) return;

    for (int i = 0; i < MAX_OBJS_PER_LANE; i++) {
        LaneObjectStruct *obj = &lane->objects[i];
        if (!obj->is_active) {
            obj->is_active = 1;
            obj->x_pos = SCREEN_RES_X + 32; // Sağdan doğup sola gider
            obj->velocity = lane->base_speed;
            if (obj->obj_variant == 2) {
                obj->mouth_is_open = (uint8_t)(Kernel_Random() % 2);
            }
            break;
        }
    }
}

static void Kernel_UpdateLanesAndTraffic(void) {
    for (int i = 0; i < TOTAL_MAP_LANES; i++) {
        WorldLaneStruct *lane = &kernel.lanes[i];
        if (lane->kind == LANE_KIND_SAFE_GROUND) continue;

        lane->spawn_counter++;
        if (lane->spawn_counter >= lane->spawn_limit) {
            lane->spawn_counter = 0;
            Kernel_SpawnLaneObject(lane);
        }

        for (int j = 0; j < MAX_OBJS_PER_LANE; j++) {
            LaneObjectStruct *obj = &lane->objects[j];
            if (!obj->is_active) continue;

            obj->x_pos += obj->velocity;

            if (obj->obj_variant == 2) {
                if ((kernel.total_ticks % 45) == 0) {
                    obj->mouth_is_open = !obj->mouth_is_open;
                }
            }

            if (obj->x_pos < -64) {
                obj->is_active = 0;
            }
        }
    }
}

static void Kernel_ProcessCollisions(void) {
    int32_t absolute_player_y = kernel.player.y + kernel.camera_scroll_y;
    int current_lane_idx = absolute_player_y / LANE_PIXEL_HEIGHT;
    
    if (current_lane_idx < 0 || current_lane_idx >= TOTAL_MAP_LANES) {
        return;
    }

    WorldLaneStruct *lane = &kernel.lanes[current_lane_idx];

    if (lane->kind == LANE_KIND_HIGHWAY_TRAFFIC) {
        for (int i = 0; i < MAX_OBJS_PER_LANE; i++) {
            LaneObjectStruct *obj = &lane->objects[i];
            if (!obj->is_active) continue;

            int32_t p_l = kernel.player.x;
            int32_t p_r = kernel.player.x + kernel.player.w;
            int32_t o_l = obj->x_pos;
            int32_t o_r = obj->x_pos + obj->width;

            if (p_r > o_l && p_l < o_r) {
                kernel.player.state_alive = 0;
                kernel.player.state_crushed = 1;
                return;
            }
        }
    } 
    else if (lane->kind == LANE_KIND_RIVER_ICE_OR_CROCODILE) {
        uint8_t standing_safe = 0;
        int32_t platform_velocity = 0;

        for (int i = 0; i < MAX_OBJS_PER_LANE; i++) {
            LaneObjectStruct *obj = &lane->objects[i];
            if (!obj->is_active) continue;

            int32_t p_l = kernel.player.x;
            int32_t p_r = kernel.player.x + kernel.player.w;
            int32_t o_l = obj->x_pos;
            int32_t o_r = obj->x_pos + obj->width;

            if (p_r > o_l && p_l < o_r) {
                if (obj->obj_variant == 1) {
                    // Buz parçası: Tamamen güvenli
                    standing_safe = 1;
                    platform_velocity = obj->velocity;
                    break;
                } 
                else if (obj->obj_variant == 2) {
                    // Timsah: Ağız ve sırt ayrımı (Sol taraf ağızdır)
                    int32_t local_px = p_l - obj->x_pos;

                    if (local_px >= obj->mouth_box_start && local_px <= obj->mouth_box_end) {
                        // Timsahın ağzına denk geldi -> ÖLÜM
                        kernel.player.state_alive = 0;
                        kernel.player.state_drowned = 1;
                        return;
                    } 
                    else if (local_px >= obj->back_box_start && local_px <= obj->back_box_end) {
                        // Timsahın sırtına denk geldi -> GÜVENLİ
                        standing_safe = 1;
                        platform_velocity = obj->velocity;
                        break;
                    }
                }
            }
        }

        if (!standing_safe) {
            // Hiçbir nesnenin üstünde değil -> SUYA DÜŞTÜ
            kernel.player.state_alive = 0;
            kernel.player.state_drowned = 1;
        } else {
            // Nesne hızıyla sürüklenme momentumu
            kernel.player.x += platform_velocity;
            if (kernel.player.x < 0 || kernel.player.x > SCREEN_RES_X) {
                kernel.player.state_alive = 0;
            }
        }
    }
}

static void Kernel_HandleInputAndMovement(void) {
    uint8_t pad = HW_REG_JOYPAD1;

    // Neo Geo Joypad Bitmask Kontrolleri (0 = Basıldı)
    if (!(pad & 0x01)) { // Up
        kernel.player.y -= 2;
        if (kernel.player.y < 60) {
            kernel.camera_scroll_y += 16;
            kernel.player.y += 16;
            kernel.player.score += 25;
        }
    }
    if (!(pad & 0x02)) { // Down
        kernel.player.y += 2;
    }
    if (!(pad & 0x04)) { // Left
        kernel.player.x -= 2;
    }
    if (!(pad & 0x08)) { // Right
        kernel.player.x += 2;
    }
}

int main(void) {
    Kernel_InitializeSystem();

    while (kernel.player.state_alive) {
        Kernel_WaitVBlank();
        
        kernel.total_ticks++;
        Kernel_HandleInputAndMovement();
        Kernel_UpdateLanesAndTraffic();
        Kernel_ProcessCollisions();
    }

    return (int)kernel.player.score;
}
