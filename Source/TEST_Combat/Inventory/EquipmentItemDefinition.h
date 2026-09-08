// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "ItemDefinition.h"
#include "EquipmentItemDefinition.generated.h"

class UEquipmentComponent;
class UStaticMesh;
class UAnimMontage;

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
	// common case for weapons/shields/helmets that don't need their own deformation. This is
	// what BP_ThirdPersonCharacter's equip visual-attach logic reads (via
	// LoadEquippedMeshSynchronous below) to set WeaponMesh's Static Mesh - not PickupMesh, which
	// is for the ground-dropped appearance only.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment")
	TSoftObjectPtr<UStaticMesh> EquippedMesh;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment")
	FName AttachSocketName;

	// Forces a synchronous load of EquippedMesh if not already resident - same rationale as
	// UItemDefinition::LoadIconSynchronous/LoadPickupMeshSynchronous. Use this instead of a bare
	// EquippedMesh Get anywhere a Blueprint needs the actual StaticMesh (e.g. the equip
	// visual-attach logic setting WeaponMesh's Static Mesh).
	UFUNCTION(BlueprintCallable, Category = "Equipment")
	UStaticMesh* LoadEquippedMeshSynchronous() const;

	// Selects which entry of UGA_WeaponAttack::AttackMontagesByWeaponType this item's attack
	// uses (e.g. "Item.WeaponType.Sword", "Item.WeaponType.Mace"). Left as a plain GameplayTag
	// rather than an enum so adding a new weapon type is a content change, not a C++ recompile -
	// same rationale as ItemTags above. Left unset (or unmatched in that map) falls back to
	// UGA_WeaponAttack::UnarmedMontage.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat")
	FGameplayTag WeaponTypeTag;

	// Optional per-item attack montage that takes priority over the WeaponTypeTag map lookup -
	// for a special-cased weapon whose attack shouldn't be shared with the rest of its type.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat")
	TSoftObjectPtr<UAnimMontage> AttackMontageOverride;

	// Montage Play Rate for this item's attack (whichever montage UGA_WeaponAttack ultimately
	// resolves - override or type lookup). Lets e.g. a mace swing slower than a sword without a
	// distinct montage asset or a separate cooldown system - AnimNotifyState_WeaponTrace's
	// hit-detection window scales with it for free, since its timing is relative to the
	// montage's own timeline.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat", meta = (ClampMin = "0.01"))
	float SwingSpeed = 1.0f;

	// Damage range rolled per hit for this item's attack - see UGA_WeaponAttack::
	// OnHitEventReceived, which rolls FMath::RandRange(MinDamage, MaxDamage) independently for
	// every target hit during a swing. Integers by design (whole-number damage values), even
	// though the underlying Health attribute is float - only cast to float at the one point
	// SetSetByCallerMagnitude requires it. Only meaningful for items used by an attack ability
	// (e.g. equipped to MainHand); leave at 0 for non-weapon gear.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat", meta = (ClampMin = "0"))
	int32 MinDamage = 0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat", meta = (ClampMin = "0"))
	int32 MaxDamage = 0;

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
