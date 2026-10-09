#include "global.h"
#include "gflib.h"
#include "mew_encounter.h"
#include "event_data.h"
#include "event_object_movement.h"
#include "field_player_avatar.h"
#include "field_camera.h"
#include "fieldmap.h"
#include "field_message_box.h"
#include "item.h"
#include "overworld.h"
#include "pokemon.h"
#include "pokemon_icon.h"
#include "pokedex.h"
#include "random.h"
#include "script.h"
#include "sound.h"
#include "task.h"
#include "trig.h"
#include "quest_log.h"
#include "menu.h"
#include "string_util.h"
#include "battle_main.h"
#include "constants/maps.h"
#include "constants/species.h"
#include "constants/moves.h"
#include "constants/items.h"
#include "constants/songs.h"
#include "constants/event_objects.h"
#include "data/mew_art.h"

#define TAG_TRUCK 12000
#define TAG_MEW 12001
#define TAG_WAVE 12002
#define TAG_SPARK 12003
#define TAG_PARTNER 12004
#define FLAG_MEW_TRUCK_CAUGHT FLAG_UNUSED_0x4AF
#define SPARKS 20
// GBA trig helpers index a 256-entry table; unbounded animation time must wrap.
#define Sin(phase, amplitude) Sin((phase) & 255, (amplitude))
#define Cos(phase, amplitude) Cos((phase) & 255, (amplitude))

extern const u8 MewTruck_EventScript[];
extern const u8 MewTruck_HintScript[];

struct MewScene
{
    u16 frame, battleFrame, state, cooldown, enemyCooldown, invuln, attackAge;
    u16 hp, maxHp, enemyHp, enemyMaxHp, originalMusic, move, truckMap[9];
    s16 truckX, truckY, mx, my, px, py, aimX, aimY;
    s16 shotX, shotY, shotDX, shotDY;
    u8 truck, mew, wave, partner, ball, sparks[SPARKS], hud, controls, lead, moveSlot;
    u8 projectile, captured, attackHit, mewFrame, waveFrame, shakes, captureSuccess, pmdPartner;
    s16 redX, redY;
    struct Pokemon enemy;
};
static EWRAM_DATA struct MewScene *sScene = NULL;
#ifdef MEW_LAB
EWRAM_DATA u16 gMewDebug[8] = {0};
#endif

static const struct OamData sOam64 = {
    .shape = SPRITE_SHAPE(64x64), .size = SPRITE_SIZE(64x64), .priority = 0
};
static const struct OamData sOam8 = {
    .shape = SPRITE_SHAPE(8x8), .size = SPRITE_SIZE(8x8), .priority = 0
};
static const struct OamData sOamWave = {
    .shape = SPRITE_SHAPE(64x64), .size = SPRITE_SIZE(64x64),
    .affineMode = ST_OAM_AFFINE_DOUBLE, .priority = 0
};
static const union AffineAnimCmd sWaveExpand[] = {
    AFFINEANIMCMD_FRAME(256,256,0,0), AFFINEANIMCMD_FRAME(3,3,0,64), AFFINEANIMCMD_END
};
static const union AffineAnimCmd *const sWaveAffine[] = {sWaveExpand};
static const union AnimCmd sStill[] = { ANIMCMD_FRAME(0, 1), ANIMCMD_END };
static const union AnimCmd *const sAnims[] = { sStill };
static const struct SpriteTemplate sTruckTemplate = {
    TAG_TRUCK, TAG_TRUCK, &sOam64, sAnims, NULL, gDummySpriteAffineAnimTable, SpriteCallbackDummy
};
static const struct SpriteTemplate sMewTemplate = {
    TAG_MEW, TAG_MEW, &sOam64, sAnims, NULL, gDummySpriteAffineAnimTable, SpriteCallbackDummy
};
static const struct SpriteTemplate sPartnerTemplate = {
    TAG_PARTNER, TAG_PARTNER, &sOam64, sAnims, NULL, gDummySpriteAffineAnimTable, SpriteCallbackDummy
};
static const struct SpriteTemplate sWaveTemplate = {
    TAG_WAVE, TAG_MEW, &sOamWave, sAnims, NULL, sWaveAffine, SpriteCallbackDummy
};
static const u16 sSparkPal[] = {0, RGB(31,31,31), RGB(31,23,5), RGB(31,10,24), RGB(8,8,18), RGB(18,8,27), RGB(12,27,31),0,0,0,0,0,0,0,0,0};
static const u8 sSparkTiles[] = {
    0,0,0,0, 0,0x10,0,0, 0,0x11,1,0, 0x10,0x11,0x11,1,
    0,0x11,1,0, 0,0x10,0,0, 0,0,0,0, 0,0,0,0,
    0,0x44,0x44,0, 0x40,0x33,0x33,4, 0x34,0x33,0x33,0x43, 0x44,0x44,0x44,0x44,
    0x14,0x11,0x11,0x41, 0x14,0x11,0x11,0x41, 0x40,0x11,0x11,4, 0,0x44,0x44,0
};
static const struct SpriteTemplate sSparkTemplate = {
    TAG_SPARK, TAG_SPARK, &sOam8, sAnims, NULL, gDummySpriteAffineAnimTable, SpriteCallbackDummy
};
static const struct WindowTemplate sHud = {0,0,0,30,3,15,512};
static const struct WindowTemplate sControlsWindow = {0,0,18,30,2,15,602};
static const u8 sHudColors[] = {0,1,2};
static const u8 sMewName[] = _("MEW  Lv30");
static const u8 sControls[] = _("A: ATTACK   B: BALL   START: RUN");
static const u8 sCaught[] = _("MEW was caught!\nThe old rumor was true.");
static const u8 sEscaped[] = _("MEW slipped away...\nThe melody might bring it back.");
static const u8 sFull[] = _("Your party and PC are full.\nMEW waits for another melody.");
static const u8 sBallMiss[] = _("MEW broke free!");

static void Task_MewScene(u8 taskId);
static s16 Distance(s16 x, s16 y) { return x*x + y*y; }

bool8 MewEncounter_TryInteract(void)
{
    s16 x, y;
    u8 i, j;
    bool8 strength = FALSE;
    if (sScene || gSaveBlock1Ptr->location.mapGroup != MAP_GROUP(MAP_SSANNE_EXTERIOR)
        || gSaveBlock1Ptr->location.mapNum != MAP_NUM(MAP_SSANNE_EXTERIOR)
        || FlagGet(FLAG_MEW_TRUCK_CAUGHT)) return FALSE;
    PlayerGetDestCoords(&x, &y);
    x -= 7; y -= 7;
    // The east-side inspection spot keeps the camera and the narrow pier aligned.
    if (x != 58 || y != 4) return FALSE;
    for (i=0; i<gPlayerPartyCount; i++)
        for (j=0; j<4; j++)
            if (GetMonData(&gPlayerParty[i], MON_DATA_MOVE1+j, NULL) == MOVE_STRENGTH)
                strength = TRUE;
    QuestLog_CutRecording();
    ScriptContext_SetupScript(strength && FlagGet(FLAG_BADGE04_GET) && CheckBagHasItem(ITEM_POKE_FLUTE,1)
        ? MewTruck_EventScript : MewTruck_HintScript);
    return TRUE;
}

static void SetFrame(u8 sprite, u8 frame)
{
    RequestSpriteCopy(sMewFrames + frame*2048,
        (u8 *)OBJ_VRAM0 + gSprites[sprite].oam.tileNum*32, 2048);
}

static void SparksOff(void)
{
    u8 i;
    for (i=0;i<SPARKS;i++) gSprites[sScene->sparks[i]].invisible = TRUE;
}

static void OrbitSparks(s16 x, s16 y, s16 radius, u16 phase)
{
    u8 i;
    for (i=0;i<SPARKS;i++)
    {
        struct Sprite *sp = &gSprites[sScene->sparks[i]];
        sp->invisible = FALSE;
        sp->x = x + Cos(phase+i*256/SPARKS, radius);
        sp->y = y + Sin(phase+i*256/SPARKS, radius/2);
        sp->hFlip = (phase & 8) != 0;
    }
}

static void SetTruckTiles(bool8 restore)
{
    u8 x,y;
    for (y=0;y<3;y++) for(x=0;x<3;x++)
    {
        if (!restore)
        {
            sScene->truckMap[y*3+x]=gMapHeader.mapLayout->map[(1+y)*gMapHeader.mapLayout->width+55+x];
            MapGridSetMetatileIdAt(55+x+7,1+y+7, y==0 ? 0x52B : y==1 ? 0x331B : 0x31C1);
        }
        else MapGridSetMetatileIdAt(55+x+7,1+y+7,sScene->truckMap[y*3+x]);
        CurrentMapDrawMetatileAt(55+x+7,1+y+7);
    }
}

void MewEncounter_Start(void)
{
    struct SpriteSheet sheets[5];
    struct SpritePalette pals[4];
    u8 i;
    s16 x,y;
    sScene=AllocZeroed(sizeof(*sScene));
    if (!sScene) { StringCopy(gStringVar4,sEscaped); ScriptContext_Enable(); return; }
    sScene->hud=WINDOW_NONE;sScene->controls=WINDOW_NONE;
    gSpecialVar_Result=0;
    sScene->lead=PARTY_SIZE;
    for(i=0;i<gPlayerPartyCount;i++)
        if (GetMonData(&gPlayerParty[i],MON_DATA_HP,NULL)
            && !GetMonData(&gPlayerParty[i],MON_DATA_IS_EGG,NULL)) {sScene->lead=i;break;}
    if(sScene->lead==PARTY_SIZE) {Free(sScene);sScene=NULL;StringCopy(gStringVar4,sEscaped);ScriptContext_Enable();return;}
    LockPlayerFieldControls();
    FreezeObjectEvents();
    SetCameraPanningCallback(NULL);
    SetCameraPanning(0,0);
    sScene->originalMusic=GetCurrentMapMusic();
    PlayNewMapMusic(MUS_POKE_FLUTE);
    sheets[0]=(struct SpriteSheet){sTruckTiles,2048,TAG_TRUCK};
    sheets[1]=(struct SpriteSheet){sMewFrames,2048,TAG_MEW};
    sheets[2]=(struct SpriteSheet){sMewFrames+24*2048,2048,TAG_WAVE};
    sheets[3]=(struct SpriteSheet){sSparkTiles,64,TAG_SPARK};
    sheets[4]=(struct SpriteSheet){NULL,0,0};
    pals[0]=(struct SpritePalette){sTruckPal,TAG_TRUCK};
    pals[1]=(struct SpritePalette){sMewPal,TAG_MEW};
    pals[2]=(struct SpritePalette){sSparkPal,TAG_SPARK};
    pals[3]=(struct SpritePalette){NULL,0};
    LoadSpriteSheets(sheets); LoadSpritePalettes(pals);
    SetSpritePosToMapCoords(55+7,1+7,&x,&y);
    x+=24+gSpriteCoordOffsetX; y+=24+gSpriteCoordOffsetY;
    sScene->truckX=x; sScene->truckY=y;
    sScene->truck=CreateSprite(&sTruckTemplate,x,y,1);
    sScene->mew=CreateSprite(&sMewTemplate,x,y+12,0);
    sScene->wave=CreateSprite(&sWaveTemplate,x,y+12,0);
    gSprites[sScene->truck].invisible=TRUE;
    gSprites[sScene->mew].invisible=TRUE;
    gSprites[sScene->wave].invisible=TRUE;
    for(i=0;i<SPARKS;i++)
    {
        sScene->sparks[i]=CreateSprite(&sSparkTemplate,0,0,0);
        gSprites[sScene->sparks[i]].invisible=TRUE;
    }
    sScene->partner=MAX_SPRITES;sScene->ball=MAX_SPRITES;
    sScene->redX=gSprites[gPlayerAvatar.spriteId].x2;
    sScene->redY=gSprites[gPlayerAvatar.spriteId].y2;
    CreateTask(Task_MewScene,0);
}

static void DrawHud(void)
{
    u8 name[24];
    FillWindowPixelBuffer(sScene->hud,PIXEL_FILL(0));
    GetMonData(&gPlayerParty[sScene->lead],MON_DATA_NICKNAME,name);
    AddTextPrinterParameterized3(sScene->hud,FONT_SMALL,4,0,sHudColors,0,name);
    AddTextPrinterParameterized3(sScene->hud,FONT_SMALL,156,0,sHudColors,0,sMewName);
    FillWindowPixelRect(sScene->hud,2,4,12,76,3);
    FillWindowPixelRect(sScene->hud,2,156,12,76,3);
    FillWindowPixelRect(sScene->hud,1,4,12,76*sScene->hp/sScene->maxHp,3);
    FillWindowPixelRect(sScene->hud,1,156,12,76*sScene->enemyHp/sScene->enemyMaxHp,3);
    PutWindowTilemap(sScene->hud);CopyWindowToVram(sScene->hud,COPYWIN_FULL);
}

static void BeginDuel(void)
{
    u8 i, v=TRUE;
    u16 species;
    sScene->state=3;sScene->frame=0;
    sScene->px=118;sScene->py=108;sScene->mx=184;sScene->my=72;
    CreateMon(&sScene->enemy,SPECIES_MEW,30,25,FALSE,0,OT_ID_PLAYER_ID,0);
    SetMonData(&sScene->enemy,MON_DATA_MODERN_FATEFUL_ENCOUNTER,&v);
    v=30;SetMonData(&sScene->enemy,MON_DATA_MET_LEVEL,&v);
    v=gMapHeader.regionMapSectionId;SetMonData(&sScene->enemy,MON_DATA_MET_LOCATION,&v);
    sScene->enemyHp=GetMonData(&sScene->enemy,MON_DATA_HP,NULL);
    sScene->enemyMaxHp=sScene->enemyHp;
    sScene->hp=GetMonData(&gPlayerParty[sScene->lead],MON_DATA_HP,NULL);
    sScene->maxHp=GetMonData(&gPlayerParty[sScene->lead],MON_DATA_MAX_HP,NULL);
    sScene->move=MOVE_STRUGGLE;sScene->moveSlot=4;
    for(i=0;i<4;i++)
    {
        u16 move=GetMonData(&gPlayerParty[sScene->lead],MON_DATA_MOVE1+i,NULL);
        if(move && gBattleMoves[move].power && GetMonData(&gPlayerParty[sScene->lead],MON_DATA_PP1+i,NULL))
        {sScene->move=move;sScene->moveSlot=i;break;}
    }
    species=GetMonData(&gPlayerParty[sScene->lead],MON_DATA_SPECIES,NULL);
    sScene->pmdPartner=species==SPECIES_PIKACHU;
    if(sScene->pmdPartner)
    {
        struct SpriteSheet sheet={sPartnerFrames,2048,TAG_PARTNER};
        struct SpritePalette pal={sPartnerPal,TAG_PARTNER};
        LoadSpriteSheet(&sheet);LoadSpritePalette(&pal);
        sScene->partner=CreateSprite(&sPartnerTemplate,sScene->px,sScene->py,0);
    }
    else
    {
        LoadMonIconPalette(species);
        sScene->partner=CreateMonIcon(species,SpriteCallbackDummy,sScene->px,sScene->py,0,0,FALSE);
    }
    gSprites[sScene->partner].oam.priority=0;
    gSprites[sScene->partner].invisible=TRUE;
    sScene->ball=CreateSprite(&sSparkTemplate,120,78,0);
    gSprites[sScene->ball].oam.tileNum++;
    PlaySE(SE_BALL_THROW);
    sScene->hud=AddWindow(&sHud);
    sScene->controls=AddWindow(&sControlsWindow);
    FillWindowPixelBuffer(sScene->controls,PIXEL_FILL(0));
    AddTextPrinterParameterized3(sScene->controls,FONT_SMALL,6,2,sHudColors,0,sControls);
    PutWindowTilemap(sScene->controls);CopyWindowToVram(sScene->controls,COPYWIN_FULL);
    DrawHud();
    gSprites[sScene->mew].x=sScene->mx;gSprites[sScene->mew].y=sScene->my;
    SetFrame(sScene->mew,12);
    SparksOff();
    PlayNewMapMusic(MUS_VS_MEWTWO);
    sScene->enemyCooldown=150;
    GetSetPokedexFlag(SpeciesToNationalPokedexNum(SPECIES_MEW),FLAG_SET_SEEN);
}

static u16 Damage(struct Pokemon *a, struct Pokemon *b, u16 move)
{
    u16 power=gBattleMoves[move].power;
    u8 type=gBattleMoves[move].type;
    u16 atk=GetMonData(a,type<TYPE_MYSTERY?MON_DATA_ATK:MON_DATA_SPATK,NULL);
    u16 def=GetMonData(b,type<TYPE_MYSTERY?MON_DATA_DEF:MON_DATA_SPDEF,NULL);
    u16 species=GetMonData(a,MON_DATA_SPECIES,NULL);
    u16 target=GetMonData(b,MON_DATA_SPECIES,NULL);
    u16 i;
    u32 d=((2*GetMonData(a,MON_DATA_LEVEL,NULL)/5+2)*(power?power:50)*atk/(def?def:1))/50+2;
    if(gSpeciesInfo[species].types[0]==type || gSpeciesInfo[species].types[1]==type)d=d*3/2;
    for(i=0;i+2<sizeof(gTypeEffectiveness);i+=3)
    {
        u8 defType=gTypeEffectiveness[i+1];
        if(gTypeEffectiveness[i]>=TYPE_FORESIGHT)break;
        if(gTypeEffectiveness[i]!=type)continue;
        if(defType==gSpeciesInfo[target].types[0])d=d*gTypeEffectiveness[i+2]/10;
        if(gSpeciesInfo[target].types[1]!=gSpeciesInfo[target].types[0]
            && defType==gSpeciesInfo[target].types[1])d=d*gTypeEffectiveness[i+2]/10;
    }
    d=d*(85+Random()%16)/100;
    return d;
}

static void StartCapture(void)
{
    u32 odds;
    u16 item=ITEM_ULTRA_BALL;
    u16 multiplier=20;
    if(gSprites[sScene->mew].invisible)return;
    if(!CheckBagHasItem(item,1)){item=ITEM_GREAT_BALL;multiplier=15;}
    if(!CheckBagHasItem(item,1)){item=ITEM_POKE_BALL;multiplier=10;}
    if(!CheckBagHasItem(item,1))return;
    RemoveBagItem(item,1);
    sScene->state=2;sScene->frame=0;sScene->shakes=0;
    sScene->projectile=FALSE;sScene->attackAge=0;
    gSprites[sScene->wave].invisible=TRUE;SparksOff();
    odds=(multiplier*gSpeciesInfo[SPECIES_MEW].catchRate/10)*(3*sScene->enemyMaxHp-2*sScene->enemyHp)/(3*sScene->enemyMaxHp);
    if(odds>254)sScene->shakes=4;
    else if(odds)
    {
        odds=1048560/Sqrt(Sqrt(16711680/odds));
        while(sScene->shakes<4 && Random()<odds)sScene->shakes++;
    }
    sScene->captureSuccess=sScene->shakes==4;
    sScene->ball=CreateSprite(&sSparkTemplate,sScene->px,sScene->py,0);
    gSprites[sScene->ball].oam.tileNum++;
    SetMonData(&sScene->enemy,MON_DATA_POKEBALL,&item);
    SetMonData(&sScene->enemy,MON_DATA_HP,&sScene->enemyHp);
    PlaySE(SE_BALL_THROW);
}

static void Finish(u8 taskId, const u8 *message)
{
    u8 i;
    gSpecialVar_Result=1;
    for(i=0;i<gPlayerPartyCount;i++)
        if(GetMonData(&gPlayerParty[i],MON_DATA_HP,NULL) && !GetMonData(&gPlayerParty[i],MON_DATA_IS_EGG,NULL))gSpecialVar_Result=0;
    if(sScene->hud!=WINDOW_NONE){ClearWindowTilemap(sScene->hud);CopyWindowToVram(sScene->hud,COPYWIN_MAP);RemoveWindow(sScene->hud);}
    if(sScene->controls!=WINDOW_NONE){ClearWindowTilemap(sScene->controls);CopyWindowToVram(sScene->controls,COPYWIN_MAP);RemoveWindow(sScene->controls);}
    FreeSpriteOamMatrix(&gSprites[sScene->wave]);
    DestroySprite(&gSprites[sScene->truck]);DestroySprite(&gSprites[sScene->mew]);DestroySprite(&gSprites[sScene->wave]);
    for(i=0;i<SPARKS;i++)DestroySprite(&gSprites[sScene->sparks[i]]);
    if(sScene->ball<MAX_SPRITES)DestroySprite(&gSprites[sScene->ball]);
    if(sScene->partner<MAX_SPRITES)
    {
        u16 species=GetMonData(&gPlayerParty[sScene->lead],MON_DATA_SPECIES,NULL);
        if(sScene->pmdPartner){DestroySprite(&gSprites[sScene->partner]);FreeSpriteTilesByTag(TAG_PARTNER);FreeSpritePaletteByTag(TAG_PARTNER);}
        else {DestroyMonIcon(&gSprites[sScene->partner]);FreeMonIconPalette(species);}
    }
    FreeSpriteTilesByTag(TAG_TRUCK);FreeSpriteTilesByTag(TAG_MEW);FreeSpriteTilesByTag(TAG_WAVE);FreeSpriteTilesByTag(TAG_SPARK);
    FreeSpritePaletteByTag(TAG_TRUCK);FreeSpritePaletteByTag(TAG_MEW);FreeSpritePaletteByTag(TAG_SPARK);
    gSprites[gPlayerAvatar.spriteId].x2=sScene->redX;gSprites[gPlayerAvatar.spriteId].y2=sScene->redY;
    StartSpriteAnim(&gSprites[gPlayerAvatar.spriteId],GetFaceDirectionAnimNum(DIR_NORTH));
    SetCameraPanning(0,0);InstallCameraPanAheadCallback();
    BlendPalettes(0xFFFFFFFF,0,RGB_BLACK);
    PlayNewMapMusic(sScene->originalMusic);
    UnfreezeObjectEvents();
    Free(sScene);sScene=NULL;
    DestroyTask(taskId);
    StringCopy(gStringVar4,message);
    ScriptContext_Enable();
}

static void UpdateDuel(u8 taskId)
{
    u16 t=++sScene->battleFrame;
    u8 i;
    struct Sprite *m=&gSprites[sScene->mew];
    struct Sprite *p=&gSprites[sScene->partner];
    s16 nx=sScene->px, ny=sScene->py;
    if(sScene->cooldown)sScene->cooldown--;
    if(sScene->invuln)sScene->invuln--;
    if(JOY_NEW(START_BUTTON)){Finish(taskId,sEscaped);return;}
    if(JOY_HELD(DPAD_LEFT) && nx>18)nx--;
    if(JOY_HELD(DPAD_RIGHT) && nx<222)nx++;
    if(JOY_HELD(DPAD_UP) && ny>74)ny--;
    if(JOY_HELD(DPAD_DOWN) && ny<138)ny++;
    // Keep the partner's feet on the T-shaped pier, not on the water.
    if(ny<=78 || (nx>=105 && nx<=132)){sScene->px=nx;sScene->py=ny;}
    p->x=sScene->px;p->y=sScene->py;
    p->invisible=sScene->invuln && (t&4);
    if(sScene->pmdPartner && t%6==0)
    {
        u8 f=(sScene->mx<sScene->px?4:0)+(t/8)%4;
        if(sScene->attackAge)f=(sScene->mx<sScene->px?15:8)+(sScene->attackAge/5)%7;
        RequestSpriteCopy(sPartnerFrames+f*2048,(u8*)OBJ_VRAM0+p->oam.tileNum*32,2048);
    }
    else if(!sScene->pmdPartner && t%12==0)UpdateMonIconFrame(p);
    m->x=sScene->mx;m->y=sScene->my+Sin(t*3,3);
    if(t%360<300 && t%6==0)SetFrame(sScene->mew,(sScene->mx<sScene->px?48:12)+(t/8)%6);
    if(JOY_NEW(B_BUTTON)){StartCapture();return;}
    if(JOY_NEW(A_BUTTON) && !sScene->cooldown)
    {
        u8 pp=0;
        if(sScene->moveSlot<4)pp=GetMonData(&gPlayerParty[sScene->lead],MON_DATA_PP1+sScene->moveSlot,NULL);
        if(pp){pp--;SetMonData(&gPlayerParty[sScene->lead],MON_DATA_PP1+sScene->moveSlot,&pp);}
        else sScene->move=MOVE_STRUGGLE;
        sScene->attackAge=1;sScene->attackHit=FALSE;sScene->cooldown=65;
        sScene->aimX=sScene->mx;sScene->aimY=sScene->my;
        PlaySE(SE_M_THUNDERBOLT);
    }
    SparksOff();
    if(sScene->attackAge)
    {
        u16 age=sScene->attackAge++;
        for(i=0;i<SPARKS;i++)
        {
            struct Sprite *sp=&gSprites[sScene->sparks[i]];
            sp->invisible=FALSE;
            sp->x=sScene->px+(sScene->aimX-sScene->px)*i/SPARKS + Sin(age*29+i*43,4);
            sp->y=sScene->py+(sScene->aimY-sScene->py)*i/SPARKS + Cos(age*33+i*71,4);
        }
        if(age==12 && !m->invisible && Distance(sScene->aimX-sScene->mx,sScene->aimY-sScene->my)<900)
        {
            u16 d=Damage(&gPlayerParty[sScene->lead],&sScene->enemy,sScene->move);
            sScene->enemyHp=d>=sScene->enemyHp?0:sScene->enemyHp-d;
            sScene->attackHit=TRUE;DrawHud();
            if(!sScene->enemyHp){Finish(taskId,sEscaped);return;}
        }
        if(age>30)sScene->attackAge=0;
    }
    if(sScene->enemyCooldown)sScene->enemyCooldown--;
    if(!sScene->enemyCooldown)
    {
        sScene->enemyCooldown=210;
        sScene->shotX=sScene->mx*16;sScene->shotY=sScene->my*16;
        sScene->shotDX=(sScene->px-sScene->mx)/2;sScene->shotDY=(sScene->py-sScene->my)/2;
        sScene->projectile=TRUE;PlaySE(SE_M_PSYBEAM);
        StartSpriteAffineAnim(&gSprites[sScene->wave],0);
    }
    if(sScene->projectile)
    {
        struct Sprite *w=&gSprites[sScene->wave];
        sScene->shotX+=sScene->shotDX;sScene->shotY+=sScene->shotDY;
        w->x=sScene->shotX/16;w->y=sScene->shotY/16;w->invisible=FALSE;
        if(t%5==0)SetFrame(sScene->wave,24+(t/5)%12);
        if(!sScene->invuln && Distance(w->x-sScene->px,w->y-sScene->py)<180)
        {
            u16 d=Damage(&sScene->enemy,&gPlayerParty[sScene->lead],MOVE_CONFUSION);
            sScene->hp=d>=sScene->hp?0:sScene->hp-d;
            SetMonData(&gPlayerParty[sScene->lead],MON_DATA_HP,&sScene->hp);
            sScene->projectile=FALSE;sScene->invuln=45;DrawHud();PlaySE(SE_M_VITAL_THROW);
            if(!sScene->hp){Finish(taskId,sEscaped);return;}
        }
        if(w->x<0 || w->x>240 || w->y<30 || w->y>150)sScene->projectile=FALSE;
        if(!sScene->projectile)w->invisible=TRUE;
    }
    // Teleport is telegraphed by orbiting particles; no damage during teleport.
    if(t%360>=300)
    {
        u16 phase=t%360;
        if(!sScene->attackAge)OrbitSparks(sScene->mx,sScene->my,18,t*6);
        if(phase==300)PlaySE(SE_M_TELEPORT);
        if(phase<336 && phase%3==0)SetFrame(sScene->mew,36+(phase-300)/3);
        if(phase==336)m->invisible=TRUE;
        if(phase==350){sScene->mx=sScene->mx>130?62:184;sScene->my=68;m->invisible=FALSE;SetFrame(sScene->mew,12);}
    }
}

static void Task_MewScene(u8 taskId)
{
    u16 t=sScene->frame++;
    struct Sprite *truck=&gSprites[sScene->truck];
    struct Sprite *mew=&gSprites[sScene->mew];
    struct Sprite *wave=&gSprites[sScene->wave];
#ifdef MEW_LAB
    gMewDebug[0]=sScene->state;gMewDebug[1]=sScene->frame;
    gMewDebug[2]=sScene->hp;gMewDebug[3]=sScene->enemyHp;
    gMewDebug[4]=sScene->px;gMewDebug[5]=sScene->py;
    gMewDebug[6]=sScene->captureSuccess;gMewDebug[7]=sScene->battleFrame;
#endif
    if(sScene->state==1){UpdateDuel(taskId);return;}
    if(sScene->state==3)
    {
        struct Sprite *ball=&gSprites[sScene->ball];
        if(t<40){ball->x=120+(sScene->px-120)*t/40;ball->y=132+(sScene->py-132)*t/40-Sin(t*128/40,18);}
        if(t==40){ball->invisible=TRUE;gSprites[sScene->partner].invisible=FALSE;PlaySE(SE_BALL_OPEN);}
        if(t>=40 && t<70)OrbitSparks(sScene->px,sScene->py,t-36,t*8);
        if(t==70){SparksOff();PlayCry_Normal(GetMonData(&gPlayerParty[sScene->lead],MON_DATA_SPECIES,NULL),-40);}
        if(t>=100){DestroySprite(ball);sScene->ball=MAX_SPRITES;sScene->state=1;sScene->frame=0;}
        return;
    }
    if(sScene->state==2)
    {
        struct Sprite *ball=&gSprites[sScene->ball];
        if(t<40){ball->x=sScene->px+(sScene->mx-sScene->px)*t/40;ball->y=sScene->py+(sScene->my-sScene->py)*t/40-Sin(t*128/40,24);}
        if(t==40){mew->invisible=TRUE;PlaySE(SE_BALL_OPEN);}
        if(t>40){ball->x=sScene->mx;ball->y=sScene->my+12;ball->x2=(t%45<12)?Sin(t*24,2):0;}
        if(t==90 || t==135 || t==180)PlaySE(SE_BALL_TRAY_ENTER);
        if(t==225)
        {
            if(sScene->captureSuccess)
            {
                u8 result=GiveMonToPlayer(&sScene->enemy);
                if(result==MON_CANT_GIVE){Finish(taskId,sFull);return;}
                FlagSet(FLAG_MEW_TRUCK_CAUGHT);
                GetSetPokedexFlag(SpeciesToNationalPokedexNum(SPECIES_MEW),FLAG_SET_CAUGHT);
                PlayFanfare(MUS_LEVEL_UP);sScene->captured=TRUE;
                gSprites[sScene->partner].invisible=TRUE;
                StartSpriteAnim(&gSprites[gPlayerAvatar.spriteId],GetMoveDirectionAnimNum(DIR_NORTH));
                gSprites[gPlayerAvatar.spriteId].animPaused=FALSE;
            }
            else
            {
                DestroySprite(ball);sScene->ball=MAX_SPRITES;mew->invisible=FALSE;
                sScene->state=1;sScene->frame=0;sScene->enemyCooldown=120;
            }
        }
        if(t>=225 && t<=300 && sScene->captured)
        {
            gSprites[gPlayerAvatar.spriteId].y2=sScene->redY+54*(300-t)/75;
            if(t<245)OrbitSparks(sScene->px,sScene->py,245-t,t*8);
            else SparksOff();
        }
        if(t>300 && sScene->captured)Finish(taskId,sCaught);
        return;
    }
    // Reveal: no battle logic runs before BeginDuel.
    if(t==120){FadeOutMapMusic(4);PlaySE(SE_M_STRENGTH);}
    if(t==180)
    {
        SetTruckTiles(FALSE);truck->invisible=FALSE;
        BlendPalettes(0x0000FFFF,5,RGB(4,4,12));
    }
    if(t>=180 && t<280)
    {
        truck->x2=Sin(t*35,2);truck->y2=0;
    }
    if(t>=180 && t<390)
    {
        u8 i;
        SparksOff();
        // Sparse glints underneath the chassis, not an opaque ring over it.
        for(i=0;i<6;i++)
        {
            struct Sprite *sp=&gSprites[sScene->sparks[i]];
            sp->invisible=(t+i*9)%40>10;
            sp->x=sScene->truckX-18+i*7+Sin(t*2+i*37,2);
            sp->y=sScene->truckY+25+Sin(t*3+i*41,5);
        }
    }
    if(t==240){mew->invisible=FALSE;PlaySE(SE_M_PSYBEAM);}
    if(t>=240 && t<420)
    {
        u8 frame;
        if(t<300)frame=47-(t-240)/5;
        else if(t<360)frame=(t-300)/10;
        else frame=6+(t-360)/10;
        if(t%5==0)SetFrame(sScene->mew,frame);
        mew->y=sScene->truckY+40-(s16)(t-240)/7;
        truck->x2=t<330?Sin(t*31,1):0;truck->y2=0;
    }
    if(t==390){PlayCry_Normal(SPECIES_MEW,0);PlayNewMapMusic(MUS_ENCOUNTER_DEOXYS);StartSpriteAffineAnim(wave,0);}
    if(t>=390 && t<462)
    {
        wave->invisible=FALSE;wave->x=mew->x;wave->y=mew->y;
        if(t%6==0)SetFrame(sScene->wave,24+(t-390)/6);
        OrbitSparks(mew->x,mew->y,20+(t-390),t*5);
        SetCameraPanning(Sin(t*60,1),0);
    }
    if(t==462){wave->invisible=TRUE;SparksOff();SetCameraPanning(0,0);}
    if(t>=420 && t<510)
    {
        mew->x=sScene->truckX+Sin((t-420)*128/90,35);
        mew->y=sScene->truckY-6+Cos((t-420)*128/90,20);
        if(t%6==0)SetFrame(sScene->mew,12+(t/6)%12);
        truck->x2=0;truck->y2=0;
    }
    if(t==510)
    {
        SetTruckTiles(TRUE);truck->invisible=TRUE;FreeSpritePaletteByTag(TAG_TRUCK);
        StartSpriteAnim(&gSprites[gPlayerAvatar.spriteId],GetMoveDirectionAnimNum(DIR_SOUTH));
        gSprites[gPlayerAvatar.spriteId].animPaused=FALSE;
    }
    if(t>=510 && t<600)
    {
        gSprites[gPlayerAvatar.spriteId].y2=sScene->redY+54*(t-510)/90;
        // Settle beside the trainer, then cross the dock into the encounter.
        mew->x=sScene->truckX+(184-sScene->truckX)*(t-510)/90;
        mew->y=sScene->truckY-26+(72-sScene->truckY+26)*(t-510)/90+Sin(t*4,2);
        if(t%6==0)SetFrame(sScene->mew,12+(t/6)%12);
    }
    if(t==600)
    {
        gSprites[gPlayerAvatar.spriteId].y2=sScene->redY+54;
        StartSpriteAnim(&gSprites[gPlayerAvatar.spriteId],GetFaceDirectionAnimNum(DIR_NORTH));
        BlendPalettes(0xFFFFFFFF,0,RGB_BLACK);BeginDuel();
    }
}
