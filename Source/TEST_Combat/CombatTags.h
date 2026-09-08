// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "NativeGameplayTags.h"

// Native tags for the melee combat system (GA_WeaponAttack, AnimNotifyState_WeaponTrace). See
// EquipmentTags.h for the rationale on declaring these natively instead of via a GameplayTags
// .ini / the editor's Tag Manager.
TEST_COMBAT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Ability_WeaponAttack);

// Sent by AnimNotifyState_WeaponTrace (server-only) to the swinging actor's own ASC for each
// newly-hit target during the notify's active window; GA_WeaponAttack waits on this via
// UAbilityTask_WaitGameplayEvent to know when/who to apply damage to.
TEST_COMBAT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Event_Weapon_Hit);

// Set-by-Caller tag GA_WeaponAttack writes its (currently flat, see GA_WeaponAttack's class
// comment) damage amount under. GE_SwordDamage's Health modifier should read its magnitude from
// this tag.
TEST_COMBAT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Data_Damage);

// Added as a loose gameplay tag to an actor's ASC by UCombatAttributeSet when its Health reaches
// 0 (see PostGameplayEffectExecute), removed by ResetForRespawn. GA_WeaponAttack (and any future
// ability that shouldn't run while dead) lists this in ActivationBlockedTags, so a dead actor is
// automatically refused activation with no per-ability dead-check needed. Replicates to every
// client for free as part of the ASC's own tag container - no separate plumbing required for
// remote clients to see another actor's dead state.
TEST_COMBAT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_State_Dead);
