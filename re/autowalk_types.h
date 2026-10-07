#ifndef KCD2_AUTOWALK_RE_TYPES_H
#define KCD2_AUTOWALK_RE_TYPES_H

typedef unsigned char aw_u8;
typedef unsigned int aw_u32;
typedef unsigned long long aw_u64;

typedef struct { float x, y; } AW_Vec2;
typedef struct { float x, y, z; } AW_Vec3;
typedef struct { float x, y, z; } AW_Ang3;
typedef struct { float w, x, y, z; } AW_Quat;
typedef struct { aw_u64 begin, end, capacity; } AW_Vector64;

typedef struct {
    AW_Vec2 m_stick;
    float m_delta;
    float m_negOne;
    aw_u32 m_zero10;
    aw_u32 _pad14;
    void* m_pUser;
} AW_S_HorseFollowStick; /* 0x20 */

typedef struct {
    AW_Vec3 m_hit;
    AW_Vec3 m_along;
    void* m_pFrom;
    void* m_pTo;
    float m_yawFrom;
    float m_yawTo;
    aw_u8 m_hasHit;
    aw_u8 m_failed;
    aw_u8 _pad32[6];
} AW_S_HorseMagnetismSample; /* 0x38 */

typedef struct {
    void* m_pHorseData;
    aw_u64 m_stickUser;
    AW_S_HorseFollowStick m_stick;
    AW_Vector64 m_pathA;
    AW_Vector64 m_pathB;
    aw_u8 m_latched;
    aw_u8 _pad61[7];
    void* m_pMagnetism;
    aw_u32 m_mode;
    aw_u32 _pad74;
    aw_u64 m_reserved78;
} AW_S_HorseRoadFollow; /* 0x80 */

typedef struct {
    void* vptr;
    aw_u64 m_reserved08;
    aw_u64 m_reserved10;
    aw_u64 m_reserved18;
    AW_Vector64 m_path;
    void* m_pHorseData;
    aw_u32 m_zero40;
    aw_u8 m_flag44;
    aw_u8 _pad45[3];
} AW_S_AutoController; /* 0x48 */

typedef struct {
    void* magnetism_vptr;
    void* rider_modifier_vptr;
    void* m_pPlayer;
    void* m_pHorseData;
    float m_deactivateTime;
    float m_reactivateTime;
    float m_hintTime;
    aw_u8 m_flags;
    aw_u8 _pad2D[3];
} AW_S_OnPressController; /* 0x30 */

typedef struct {
    void* m_pOwner;
    AW_Ang3 m_lookAngles;
    AW_Quat m_lookQuat;
    AW_Quat m_viewRotation;
    AW_Quat m_flatYawQuat;
    float m_flatYaw;
    float m_viewPitch;
    AW_Ang3 m_lookDeltaTransient;
    AW_Ang3 m_lookDeltaPending;
    AW_Ang3 m_lookDeltaCarried;
    aw_u8 _pad70[0x0C];
    AW_Ang3 m_lookDeltaRequest;
    AW_Ang3 m_lookAngleAccum;
    aw_u8 m_lookClamped;
    aw_u8 _pad95[3];
} AW_C_ActorPhysicsState; /* 0x98 */

typedef struct {
    aw_u8 _pad000[0xF8];
    void* m_pHorse;
    void* m_pMove;
    aw_u8 _pad108[8];
    float m_pseudoSpeed;
    float m_prevPseudoSpeed;
    float m_speed;
    aw_u8 _pad11C[8];
    aw_u8 m_backwards;
    aw_u8 m_fastSlowDown;
    aw_u8 _pad126[2];
    float m_yawSmoothed;
    float m_yawVel;
    float m_magnetYaw;
    aw_u8 _pad134[4];
    aw_u8 m_magnetismLive;
    aw_u8 _pad139[3];
    AW_Vec3 m_magnetHit;
    aw_u8 _pad148[0x10];
    AW_S_HorseRoadFollow m_roadFollow;
    aw_u8 _pad1D8[0x2F0];
} AW_S_HorseData; /* 0x4C8 */

#endif
