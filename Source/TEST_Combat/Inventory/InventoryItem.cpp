// Fill out your copyright notice in the Description page of Project Settings.

#include "InventoryItem.h"
#include "InventoryComponent.h"

// These three are invoked by the replication system on machines that RECEIVE a delta for
// this array - never on the authoritative machine that produced it (see
// UInventoryComponent::NotifySlotReplicated for how the server's own local UI still gets
// updated). OwnerComponent is null after EndPlay clears it, which these guard against since
// a straggling delta can still arrive mid-teardown.

void FInventoryList::PostReplicatedAdd(const TArrayView<int32>& AddedIndices, int32 FinalSize)
{
	if (!OwnerComponent)
	{
		return;
	}
	for (const int32 Index : AddedIndices)
	{
		if (Items.IsValidIndex(Index))
		{
			OwnerComponent->NotifyReplicatedDeltaReceived(Items[Index].SlotIndex, false);
		}
	}
}

void FInventoryList::PostReplicatedChange(const TArrayView<int32>& ChangedIndices, int32 FinalSize)
{
	if (!OwnerComponent)
	{
		return;
	}
	for (const int32 Index : ChangedIndices)
	{
		if (Items.IsValidIndex(Index))
		{
			OwnerComponent->NotifyReplicatedDeltaReceived(Items[Index].SlotIndex, false);
		}
	}
}

void FInventoryList::PreReplicatedRemove(const TArrayView<int32>& RemovedIndices, int32 FinalSize)
{
	if (!OwnerComponent)
	{
		return;
	}
	for (const int32 Index : RemovedIndices)
	{
		if (Items.IsValidIndex(Index))
		{
			// Called BEFORE the entry is actually removed from Items, so SlotIndex is still
			// readable here - this is the last point at which it is. See
			// NotifyReplicatedDeltaReceived's comment for why this can't synchronously rebuild
			// predicted state from Items at this point.
			OwnerComponent->NotifyReplicatedDeltaReceived(Items[Index].SlotIndex, true);
		}
	}
}

FInventoryItem* FInventoryList::FindBySlot(int32 SlotIndex)
{
	return Items.FindByPredicate([SlotIndex](const FInventoryItem& Item) { return Item.SlotIndex == SlotIndex; });
}

const FInventoryItem* FInventoryList::FindBySlot(int32 SlotIndex) const
{
	return Items.FindByPredicate([SlotIndex](const FInventoryItem& Item) { return Item.SlotIndex == SlotIndex; });
}
