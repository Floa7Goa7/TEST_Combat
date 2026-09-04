// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "ItemDefinition.h"
#include "EquipmentItemDefinition.generated.h"

class UEquipmentComponent;
class UStaticMesh;

/**
 * Data for a wearable/wieldable item (weapon, shield, backpack, armor piece). Subclasses
 * UItemDefinition per that class' documented extension point - see ItemDefinition.h.
 *
 * MaxStackSize (inherited) should be set to 1 on every asset of this class: gear does not
 * stack. This is designer-authored data, not something a modified client could abuse (equip
 * requests are validated against the resolved item definition server-side, not against
 * whatever Quantity a client claims), so it is left as a content authoring convention rather
 * than a hard runtime check - consistent with how the rest of this codebase trusts
 * designer-authored UItemDefinition data.
 */
UCLASS(BlueprintType)
class TEST_COMBAT_API UEquipmentItemDefinition : public UItemDefinition
{
	GENERATED_BODY()

public:
	// Which UEquipmentComponent slot this item natively equips to. For a two-handed weapon,
	// this is the MainHand slot tag - bTwoHanded below is what additionally blocks OffHand.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment")
	FGameplayTag EquipmentSlotTag;

	// Only meaningful when EquipmentSlotTag is the MainHand slot. When true, equipping this
	// item also blocks the OffHand slot (see UEquipmentComponent::IsSlotBlocked) instead of
	// occupying it with a second, duplicate entry.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment")
	bool bTwoHanded = false;

	// Only meaningful when EquipmentSlotTag is the Backpack slot. Extra inventory slots granted
	// while this item is equipped - see UInventoryComponent::BackpackBonusSlots.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment", meta = (ClampMin = "0"))
	int32 BackpackBonusSlots = 0;

	// Worn appearance, distinct from UItemDefinition::PickupMesh (a ground-lying item and a
	// worn one are different presentation concerns, even though small/simple items may point
	// both fields at the same mesh asset). Static mesh, not skeletal: attached to a socket on
	// the wearer's skeleton (AttachSocketName) rather than driven by its own skeleton - the
	// common case for weapons/shields/helmets that don't need their own deformation. Not yet
	// consumed by any code - wiring it up to actually attach on equip is a follow-up.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment")
	TSoftObjectPtr<UStaticMesh> EquippedMesh;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment")
	FName AttachSocketName;

	// Extension points for item-specific equip behavior (e.g. granting a GAS GameplayEffect for
	// stat modifiers) - mirrors UItemDefinition::OnItemUsed's pattern. Base implementation does
	// nothing; called from server-authoritative code
	// (UEquipmentComponent::Server_EquipItem_Implementation / Server_UnequipItem_Implementation)
	// after the equip/unequip has already been applied to FEquipmentList.
	UFUNCTION(BlueprintNativeEvent, Category = "Equipment")
	void OnEquipped(AActor* Wearer, UEquipmentComponent* OwningEquipment);
	virtual void OnEquipped_Implementation(AActor* Wearer, UEquipmentComponent* OwningEquipment);

	UFUNCTION(BlueprintNativeEvent, Category = "Equipment")
	void OnUnequipped(AActor* Wearer, UEquipmentComponent* OwningEquipment);
	virtual void OnUnequipped_Implementation(AActor* Wearer, UEquipmentComponent* OwningEquipment);
};
