// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "NativeGameplayTags.h"

// Native slot-identity tags for UEquipmentComponent. Declared natively (rather than via a
// GameplayTags .ini / the editor's Tag Manager) so they self-register at module load with no
// content-side setup required, while still being fully visible/usable in tag pickers.
//
// Adding a new equipment slot later (Chest, Legs, Boots, ...) is: declare/define one more tag
// pair here, then add it to UEquipmentComponent::ValidEquipmentSlots - no other code changes.
TEST_COMBAT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Item_Slot_MainHand);
TEST_COMBAT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Item_Slot_OffHand);
TEST_COMBAT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Item_Slot_Backpack);
TEST_COMBAT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Item_Slot_Helmet);
