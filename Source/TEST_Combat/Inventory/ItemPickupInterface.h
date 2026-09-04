// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "ItemPickupInterface.generated.h"

UINTERFACE(BlueprintType)
class TEST_COMBAT_API UItemPickupInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * Implemented by whatever Actor class UItemDefinition::PickupActorClass points at.
 * UInventoryComponent::Server_DropItem spawns that class and calls this to stamp the
 * pickup with the item/quantity that was dropped, instead of the inventory system needing
 * to know anything about pickup actors' internals (mesh, interaction prompt, etc.).
 */
class TEST_COMBAT_API IItemPickupInterface
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, Category = "Inventory")
	void InitializePickup(FPrimaryAssetId ItemID, int32 Quantity);
};
