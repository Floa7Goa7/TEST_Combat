// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "NativeGameplayTags.h"

// Native tags for the melee combat system (GA_SwordSwing, AnimNotifyState_WeaponTrace). See
// EquipmentTags.h for the rationale on declaring these natively instead of via a GameplayTags
// .ini / the editor's Tag Manager.
TEST_COMBAT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Ability_SwordSwing);

// Sent by AnimNotifyState_WeaponTrace (server-only) to the swinging actor's own ASC for each
// newly-hit target during the notify's active window; GA_SwordSwing waits on this via
// UAbilityTask_WaitGameplayEvent to know when/who to apply damage to.
TEST_COMBAT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Event_Weapon_Hit);

// Set-by-Caller tag GA_SwordSwing writes its (currently flat, see GA_SwordSwing's class comment)
// damage amount under. GE_SwordDamage's Health modifier should read its magnitude from this tag.
TEST_COMBAT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Data_Damage);
