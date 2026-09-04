// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "ItemDefinition.generated.h"

class AActor;
class UInventoryComponent;
class UTexture2D;
class UStaticMesh;

/**
 * Static, designer-authored data for one kind of item (e.g. "Health Potion", "Iron Sword").
 * One asset per item type; FInventoryItem::ItemID (an FPrimaryAssetId) points at an
 * instance of this class via the Asset Manager, rather than the inventory holding a hard
 * reference to it - that keeps replicated/save-game item entries tiny (an ID, not a UObject)
 * and lets item data be added or changed without touching the inventory component.
 *
 * NOTE: for GetPrimaryAssetId() below to resolve, register the "Item" primary asset type
 * in Project Settings -> Asset Manager (or DefaultGame.ini's
 * [/Script/Engine.AssetManagerSettings] PrimaryAssetTypesToScan) pointing at the folder
 * these data assets live in.
 *
 * === Extension point ===
 * UItemDefinition is deliberately not abstract - a plain data-only item (e.g. crafting
 * material) can use it directly. Items with behavior (consumables, weapons, quest items)
 * should subclass it (UConsumableItemDefinition, UWeaponItemDefinition, ...) and override
 * OnItemUsed_Implementation, rather than the base FInventoryItem struct growing an
 * ever-larger union of type-specific fields. This mirrors how GAS keeps per-ability data on
 * the ability class, not on the activation record.
 */
UCLASS(BlueprintType)
class TEST_COMBAT_API UItemDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item")
	FText DisplayName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item", meta = (MultiLine = "true"))
	FText Description;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item")
	TSoftObjectPtr<UTexture2D> Icon;

	// World appearance when this item is dropped. Soft reference so an item whose pickup is
	// never actually spawned this session (never dropped/looted) doesn't pull its mesh into
	// memory - see AItemPickupActor::InitializePickup_Implementation, which async-loads this on
	// demand. One generic PickupActorClass (see below) can therefore serve every item: only
	// this field needs to differ per item, not a whole Blueprint.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item")
	TSoftObjectPtr<UStaticMesh> PickupMesh;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item", meta = (ClampMin = "1"))
	int32 MaxStackSize = 1;

	// Category / flags as tags (e.g. Item.Category.Weapon, Item.Flag.Consumable,
	// Item.Flag.QuestItem) instead of an enum, so new categories/flags don't require
	// recompiling C++ or touching this class - just add tags to the project's tag table.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item")
	FGameplayTagContainer ItemTags;

	// Actor class spawned by UInventoryComponent::Server_DropItem. Should implement
	// IItemPickupInterface; see ItemPickupInterface.h.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item")
	TSubclassOf<AActor> PickupActorClass;

	//~ Begin UPrimaryDataAsset interface
	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
	//~ End UPrimaryDataAsset interface

	// Resolves ItemID to its UItemDefinition, forcing a synchronous load if it is not already in
	// memory. Unlike UAssetManager::GetPrimaryAssetObject (what the Blueprint "Get Object from
	// Primary Asset Id" node wraps), which only returns an already-loaded object and silently
	// returns None otherwise, this guarantees a real object (or a genuine null for an invalid/
	// unregistered ItemID) - use this from Blueprint UI instead of chaining Get Primary Asset
	// Object -> Cast, which intermittently "Cast Failed"s purely because nothing loaded the
	// asset yet.
	UFUNCTION(BlueprintCallable, Category = "Item", meta = (DisplayName = "Load Item Definition"))
	static UItemDefinition* LoadItemDefinitionSynchronous(FPrimaryAssetId ItemID);

	// Extension point for item-specific behavior. Base implementation does nothing and
	// returns false ("not consumed"); subclasses override to implement heal-on-use,
	// equip-on-use, quest-turn-in-on-use, etc. Called from server-authoritative code
	// (UInventoryComponent::Server_UseItem_Implementation) - do not assume this runs on
	// the client. Call as ItemDef->OnItemUsed(...) (the plain declared name), not
	// Execute_OnItemUsed - that indirection is only generated for UINTERFACE events.
	UFUNCTION(BlueprintNativeEvent, Category = "Item")
	bool OnItemUsed(AActor* UsingActor, UInventoryComponent* OwningInventory, int32 SlotIndex);
	virtual bool OnItemUsed_Implementation(AActor* UsingActor, UInventoryComponent* OwningInventory, int32 SlotIndex);
};
