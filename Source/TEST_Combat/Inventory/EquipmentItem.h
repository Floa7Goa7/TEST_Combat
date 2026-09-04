// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "EquipmentItem.generated.h"

class UEquipmentComponent;

/**
 * Fired whenever a slot's authoritative (server-confirmed) contents change.
 * Mirrors FOnInventoryChanged (see InventoryItem.h) but keyed by the equipment slot's
 * FGameplayTag instead of an int32 slot index.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnEquipmentChanged, FGameplayTag, SlotTag, bool, bWasRemoved);

/**
 * A single equipped item. Kept data-only for the same reasons as FInventoryItem (see
 * InventoryItem.h) - cheap to replicate/serialize, no runtime-only handles.
 *
 * Deliberately has no Quantity field: gear does not stack (an item equipped here should have
 * UItemDefinition::MaxStackSize == 1). A two-handed weapon equipped to MainHand does not get a
 * second entry for OffHand - see UEquipmentComponent::IsSlotBlocked, which derives OffHand's
 * blocked state from the MainHand entry's item definition instead of duplicating the entry.
 */
USTRUCT(BlueprintType)
struct FEquipmentEntry : public FFastArraySerializerItem
{
	GENERATED_BODY()

	FEquipmentEntry() {}

	FEquipmentEntry(FGameplayTag InSlotTag, FPrimaryAssetId InItemID)
		: SlotTag(InSlotTag)
		, ItemID(InItemID)
	{}

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Equipment")
	FGameplayTag SlotTag;

	// Resolved the same way as FInventoryItem::ItemID - via UAssetManager, not a hard/soft
	// object pointer. See InventoryItem.h for the rationale.
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Equipment")
	FPrimaryAssetId ItemID;

	bool IsValidEntry() const { return SlotTag.IsValid() && ItemID.IsValid(); }
};

/**
 * Replicated container for equipped items. Structurally identical to FInventoryList (see its
 * comment in InventoryItem.h for the full replication-flow explanation) - entries are just
 * addressed by FGameplayTag instead of an int32 slot index.
 */
USTRUCT(BlueprintType)
struct FEquipmentList : public FFastArraySerializer
{
	GENERATED_BODY()

	FEquipmentList() {}

	UPROPERTY()
	TArray<FEquipmentEntry> Entries;

	// Back-pointer so the FastArray callbacks can reach UEquipmentComponent. Not a UPROPERTY -
	// must never replicate/serialize. Set/cleared in BeginPlay/EndPlay, same as FInventoryList.
	UEquipmentComponent* OwnerComponent = nullptr;

	void PostReplicatedAdd(const TArrayView<int32>& AddedIndices, int32 FinalSize);
	void PostReplicatedChange(const TArrayView<int32>& ChangedIndices, int32 FinalSize);
	void PreReplicatedRemove(const TArrayView<int32>& RemovedIndices, int32 FinalSize);

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FFastArraySerializer::FastArrayDeltaSerialize<FEquipmentEntry, FEquipmentList>(Entries, DeltaParms, *this);
	}

	FEquipmentEntry* FindBySlotTag(FGameplayTag SlotTag);
	const FEquipmentEntry* FindBySlotTag(FGameplayTag SlotTag) const;
};

template<>
struct TStructOpsTypeTraits<FEquipmentList> : public TStructOpsTypeTraitsBase2<FEquipmentList>
{
	enum
	{
		WithNetDeltaSerializer = true,
	};
};

/**
 * Plain snapshot of an equipment set, suitable for a USaveGame field. Mirrors
 * FInventorySaveData (InventoryItem.h) for the same reasons.
 */
USTRUCT(BlueprintType)
struct FEquipmentSaveData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Equipment")
	TArray<FEquipmentEntry> Entries;
};
