// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ItemPickupInterface.h"
#include "ItemPickupActor.generated.h"

class UStaticMeshComponent;
struct FStreamableHandle;

/**
 * Generic, reusable world pickup: one class (or a thin Blueprint subclass of it, for VFX/sound/
 * interaction-prompt polish) instead of a bespoke Blueprint per item. UInventoryComponent::
 * Server_DropItem spawns whatever UItemDefinition::PickupActorClass points at and calls
 * InitializePickup with the dropped item's identity (see ItemPickupInterface.h) - this class
 * resolves that item's UItemDefinition::PickupMesh (a soft reference) and applies it to
 * PickupMeshComponent, so a new item needs only a mesh assigned on its data asset, never a new
 * actor/Blueprint.
 */
UCLASS()
class TEST_COMBAT_API AItemPickupActor : public AActor, public IItemPickupInterface
{
	GENERATED_BODY()

public:
	AItemPickupActor();

	UFUNCTION(BlueprintPure, Category = "Inventory")
	FPrimaryAssetId GetItemID() const { return ItemID; }

	UFUNCTION(BlueprintPure, Category = "Inventory")
	int32 GetQuantity() const { return Quantity; }

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Inventory")
	UStaticMeshComponent* PickupMeshComponent;

	//~ Begin IItemPickupInterface
	virtual void InitializePickup_Implementation(FPrimaryAssetId InItemID, int32 InQuantity) override;
	//~ End IItemPickupInterface

	//~ Begin AActor interface
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End AActor interface

	// Set once by InitializePickup; identifies what this world actor represents so pickup/
	// interaction logic (Blueprint or C++, not this class' concern) knows what to grant.
	// BlueprintReadOnly requires at least protected access (UHT rejects it on private members).
	UPROPERTY(BlueprintReadOnly, Category = "Inventory")
	FPrimaryAssetId ItemID;

	UPROPERTY(BlueprintReadOnly, Category = "Inventory")
	int32 Quantity = 0;

private:
	void OnPickupMeshLoaded();

	// Held so EndPlay can cancel it - see UInventoryComponent::ActiveStreamingHandles for the
	// same rationale (an in-flight load's completion callback captures `this`).
	TSharedPtr<FStreamableHandle> MeshLoadHandle;
};
