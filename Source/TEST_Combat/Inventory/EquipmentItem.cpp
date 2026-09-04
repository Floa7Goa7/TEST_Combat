// Fill out your copyright notice in the Description page of Project Settings.

#include "EquipmentItem.h"
#include "EquipmentComponent.h"

// These three are invoked by the replication system on machines that RECEIVE a delta for this
// array - never on the authoritative machine that produced it. See FInventoryList's identical
// comment in InventoryItem.cpp; the same reasoning applies here.

void FEquipmentList::PostReplicatedAdd(const TArrayView<int32>& AddedIndices, int32 FinalSize)
{
	if (!OwnerComponent)
	{
		return;
	}
	for (const int32 Index : AddedIndices)
	{
		if (Entries.IsValidIndex(Index))
		{
			OwnerComponent->NotifyReplicatedDeltaReceived(Entries[Index].SlotTag, false);
		}
	}
}

void FEquipmentList::PostReplicatedChange(const TArrayView<int32>& ChangedIndices, int32 FinalSize)
{
	if (!OwnerComponent)
	{
		return;
	}
	for (const int32 Index : ChangedIndices)
	{
		if (Entries.IsValidIndex(Index))
		{
			OwnerComponent->NotifyReplicatedDeltaReceived(Entries[Index].SlotTag, false);
		}
	}
}

void FEquipmentList::PreReplicatedRemove(const TArrayView<int32>& RemovedIndices, int32 FinalSize)
{
	if (!OwnerComponent)
	{
		return;
	}
	for (const int32 Index : RemovedIndices)
	{
		if (Entries.IsValidIndex(Index))
		{
			// Called BEFORE the entry is actually removed, so SlotTag is still readable here.
			// See NotifyReplicatedDeltaReceived's comment for why this can't synchronously
			// rebuild predicted state at this point.
			OwnerComponent->NotifyReplicatedDeltaReceived(Entries[Index].SlotTag, true);
		}
	}
}

FEquipmentEntry* FEquipmentList::FindBySlotTag(FGameplayTag SlotTag)
{
	return Entries.FindByPredicate([SlotTag](const FEquipmentEntry& Entry) { return Entry.SlotTag == SlotTag; });
}

const FEquipmentEntry* FEquipmentList::FindBySlotTag(FGameplayTag SlotTag) const
{
	return Entries.FindByPredicate([SlotTag](const FEquipmentEntry& Entry) { return Entry.SlotTag == SlotTag; });
}
