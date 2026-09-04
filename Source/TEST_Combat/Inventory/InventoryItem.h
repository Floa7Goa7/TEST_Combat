// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "InventoryItem.generated.h"

class UInventoryComponent;

/**
 * Fired whenever a slot's authoritative (server-confirmed) contents change.
 * bWasRemoved distinguishes "slot cleared" from "slot added/changed" so UI doesn't
 * have to re-query the whole array to figure out what happened.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnInventoryChanged, int32, SlotIndex, bool, bWasRemoved);

/**
 * A single inventory entry. Kept intentionally "dumb" (data-only) - behavior lives on
 * UItemDefinition (see ItemDefinition.h) so this struct stays small, cheap to replicate,
 * and trivially serializable to a USaveGame / JSON (every field is a plain UPROPERTY of a
 * type FJsonObjectConverter / FMemoryWriter can already handle - no raw pointers, no
 * runtime-only handles). Do not add UObject pointer or TWeakObjectPtr members here; if a
 * piece of runtime-only state is ever needed, keep it in a side map on UInventoryComponent keyed by
 * SlotIndex instead of polluting the replicated/persisted struct.
 */
USTRUCT(BlueprintType)
struct FInventoryItem : public FFastArraySerializerItem
{
	GENERATED_BODY()

	FInventoryItem() {}

	FInventoryItem(FPrimaryAssetId InItemID, int32 InQuantity, int32 InSlotIndex)
		: ItemID(InItemID)
		, Quantity(InQuantity)
		, SlotIndex(InSlotIndex)
	{}

	// Which UItemDefinition (a UPrimaryDataAsset) this stack refers to. Resolved via
	// UAssetManager::Get().GetPrimaryAssetObject(ItemID) rather than storing a hard/soft
	// object pointer, which keeps this struct free of asset references for save-game
	// purposes and avoids forcing every item's data asset to load just to replicate a stack.
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Inventory")
	FPrimaryAssetId ItemID;

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Inventory")
	int32 Quantity = 0;

	// Fixed-slot / grid placement. INDEX_NONE means "not placed" (should not occur for
	// entries that are actually in FInventoryList::Items - the list only ever holds
	// occupied slots, see FInventoryList below).
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Inventory")
	int32 SlotIndex = INDEX_NONE;

	bool IsValidEntry() const { return ItemID.IsValid() && Quantity > 0 && SlotIndex != INDEX_NONE; }
};

/**
 * Replicated container for the inventory's contents.
 *
 * === Replication flow ===
 * FFastArraySerializer delta-replicates only the entries that changed since the last
 * acknowledged state, instead of re-sending the whole inventory whenever one stack
 * changes. Server-side mutation code (in UInventoryComponent) must call one of:
 *   - MarkItemDirty(Entry)   after mutating an existing FInventoryItem in place
 *   - MarkArrayDirty()       after a structural change (Add/RemoveAt) not covered above
 * On each client, the engine replays the delta and invokes PostReplicatedAdd /
 * PostReplicatedChange / PreReplicatedRemove for the affected entries, in that order
 * relative to the array mutation. Each of those forwards to OwnerComponent so the
 * component can broadcast FOnInventoryChanged for UI, and (on the owning client only)
 * reconcile the client-side prediction shadow copy - see UInventoryComponent's
 * "CLIENT-SIDE PREDICTION" section for that half of the flow.
 */
USTRUCT(BlueprintType)
struct FInventoryList : public FFastArraySerializer
{
	GENERATED_BODY()

	FInventoryList() {}

	UPROPERTY()
	TArray<FInventoryItem> Items;

	// Back-pointer so the FastArray callbacks (which the engine calls directly on this
	// struct, not on the owning component) can reach UInventoryComponent to broadcast
	// delegates and drive prediction reconciliation. Not a UPROPERTY: it must never be
	// replicated or serialized, and is set/cleared by UInventoryComponent's
	// BeginPlay/EndPlay (see EndPlay for why it is cleared, not left dangling).
	UInventoryComponent* OwnerComponent = nullptr;

	// --- FFastArraySerializer callbacks ---
	void PostReplicatedAdd(const TArrayView<int32>& AddedIndices, int32 FinalSize);
	void PostReplicatedChange(const TArrayView<int32>& ChangedIndices, int32 FinalSize);
	void PreReplicatedRemove(const TArrayView<int32>& RemovedIndices, int32 FinalSize);

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FFastArraySerializer::FastArrayDeltaSerialize<FInventoryItem, FInventoryList>(Items, DeltaParms, *this);
	}

	FInventoryItem* FindBySlot(int32 SlotIndex);
	const FInventoryItem* FindBySlot(int32 SlotIndex) const;
};

template<>
struct TStructOpsTypeTraits<FInventoryList> : public TStructOpsTypeTraitsBase2<FInventoryList>
{
	enum
	{
		WithNetDeltaSerializer = true,
	};
};

/**
 * Plain, engine-struct-only snapshot of an inventory's contents, suitable for embedding
 * directly in a USaveGame (UPROPERTY(SaveGame) TArray<FInventoryItem> works as-is too -
 * this wrapper exists mainly so save systems have a single named type to version) or for
 * round-tripping through FJsonObjectConverter for cross-session / cross-service transfer.
 * Deliberately holds no replication bookkeeping (ReplicationID etc. from
 * FFastArraySerializerItem are transient network state, not save data).
 */
USTRUCT(BlueprintType)
struct FInventorySaveData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Inventory")
	TArray<FInventoryItem> Items;
};
