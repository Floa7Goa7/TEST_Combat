// Fill out your copyright notice in the Description page of Project Settings.

#include "EquipmentComponent.h"
#include "EquipmentTags.h"
#include "EquipmentItemDefinition.h"
#include "InventoryComponent.h"
#include "Net/UnrealNetwork.h"
#include "Engine/AssetManager.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/Pawn.h"
#include "TimerManager.h"

UEquipmentComponent::UEquipmentComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);

	MainHandSlotTag = TAG_Item_Slot_MainHand;
	OffHandSlotTag = TAG_Item_Slot_OffHand;
	BackpackSlotTag = TAG_Item_Slot_Backpack;
	ValidEquipmentSlots = { TAG_Item_Slot_MainHand, TAG_Item_Slot_OffHand, TAG_Item_Slot_Backpack, TAG_Item_Slot_Helmet };
}

void UEquipmentComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// COND_OwnerOnly for the same reason as UInventoryComponent::InventoryList - see that
	// class' GetLifetimeReplicatedProps comment.
	DOREPLIFETIME_CONDITION(UEquipmentComponent, EquipmentList, COND_OwnerOnly);
}

void UEquipmentComponent::BeginPlay()
{
	Super::BeginPlay();

	EquipmentList.OwnerComponent = this;
	PredictedEquipment = EquipmentList.Entries;

	if (AActor* Owner = GetOwner())
	{
		InventoryComponent = Owner->FindComponentByClass<UInventoryComponent>();
	}
}

void UEquipmentComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// See UInventoryComponent::EndPlay for why the back-pointer is cleared first and streaming/
	// delegate teardown happens explicitly rather than being left to object destruction order.
	EquipmentList.OwnerComponent = nullptr;
	OnEquipmentChanged.Clear();
	OnPredictedEquipmentChanged.Clear();
	InventoryComponent = nullptr;

	Super::EndPlay(EndPlayReason);
}

// ==================== Public API ====================

bool UEquipmentComponent::EquipItem(int32 SourceInventorySlot, FGameplayTag TargetSlotTag)
{
	if (!InventoryComponent || !ValidEquipmentSlots.Contains(TargetSlotTag) || !InventoryComponent->IsValidSlotIndex(SourceInventorySlot))
	{
		return false;
	}

	int32 PredictionID = 0;
	if (ShouldPredictLocally())
	{
		if (IsSlotPendingPrediction(TargetSlotTag) || InventoryComponent->IsSlotPendingPrediction(SourceInventorySlot))
		{
			return false;
		}

		FInventoryItem SourceItem;
		if (!InventoryComponent->FindPredictedItemInSlot(SourceInventorySlot, SourceItem))
		{
			return false;
		}

		PredictionID = AllocatePredictionID();

		FPredictedEquipmentOp EquipOp;
		EquipOp.PredictionID = PredictionID;
		EquipOp.OpType = EPredictedEquipmentOp::Equip;
		EquipOp.SlotTag = TargetSlotTag;
		EquipOp.ItemID = SourceItem.ItemID;
		PendingPredictions.Add(EquipOp);

		// Cross-component prediction under the SAME PredictionID - see this class' header
		// comment. Removes the item from its source inventory slot...
		FPredictedInventoryOp RemoveOp;
		RemoveOp.PredictionID = PredictionID;
		RemoveOp.OpType = EPredictedInventoryOp::Remove;
		RemoveOp.SlotA = SourceInventorySlot;
		RemoveOp.Quantity = SourceItem.Quantity;
		InventoryComponent->PendingPredictions.Add(RemoveOp);

		// ...and if TargetSlotTag was already occupied, swaps the displaced item back into that
		// exact source slot (deterministic - see Server_EquipItem_Implementation).
		FEquipmentEntry ExistingTarget;
		if (FindPredictedItemInSlot(TargetSlotTag, ExistingTarget))
		{
			FPredictedInventoryOp InsertOp;
			InsertOp.PredictionID = PredictionID;
			InsertOp.OpType = EPredictedInventoryOp::InsertAt;
			InsertOp.ItemID = ExistingTarget.ItemID;
			InsertOp.SlotB = SourceInventorySlot;
			InsertOp.Quantity = 1;
			InventoryComponent->PendingPredictions.Add(InsertOp);
		}

		if (TargetSlotTag == BackpackSlotTag)
		{
			if (const UEquipmentItemDefinition* ItemDef = ResolveEquipmentDefinition(SourceItem.ItemID))
			{
				FPredictedInventoryOp CapacityOp;
				CapacityOp.PredictionID = PredictionID;
				CapacityOp.OpType = EPredictedInventoryOp::CapacityChange;
				CapacityOp.Quantity = ItemDef->BackpackBonusSlots;
				InventoryComponent->PendingPredictions.Add(CapacityOp);
			}
		}

		RebuildPredictedState();
		InventoryComponent->RebuildPredictedState();
	}

	Server_EquipItem(SourceInventorySlot, TargetSlotTag, PredictionID);
	return true;
}

bool UEquipmentComponent::UnequipItem(FGameplayTag SlotTag, int32 DestInventorySlot)
{
	if (!InventoryComponent || !ValidEquipmentSlots.Contains(SlotTag) || !InventoryComponent->IsValidSlotIndex(DestInventorySlot))
	{
		return false;
	}

	// Mirrors the same bWouldStrandItems check in Server_UnequipItem_Implementation - refused
	// here too so the client never optimistically predicts a capacity shrink the server is
	// certain to reject, which would otherwise strand an item's slot widget out of range for a
	// round trip before RebuildPredictedState() corrected it.
	if (SlotTag == BackpackSlotTag && !InventoryComponent->Authority_CanShrinkTo(InventoryComponent->BaseNumSlots))
	{
		return false;
	}

	int32 PredictionID = 0;
	if (ShouldPredictLocally())
	{
		if (IsSlotPendingPrediction(SlotTag) || InventoryComponent->IsSlotPendingPrediction(DestInventorySlot))
		{
			return false;
		}

		FEquipmentEntry Entry;
		if (!FindPredictedItemInSlot(SlotTag, Entry))
		{
			return false;
		}

		PredictionID = AllocatePredictionID();

		FPredictedEquipmentOp UnequipOp;
		UnequipOp.PredictionID = PredictionID;
		UnequipOp.OpType = EPredictedEquipmentOp::Unequip;
		UnequipOp.SlotTag = SlotTag;
		PendingPredictions.Add(UnequipOp);

		FPredictedInventoryOp InsertOp;
		InsertOp.PredictionID = PredictionID;
		InsertOp.OpType = EPredictedInventoryOp::InsertAt;
		InsertOp.ItemID = Entry.ItemID;
		InsertOp.SlotB = DestInventorySlot;
		InsertOp.Quantity = 1;
		InventoryComponent->PendingPredictions.Add(InsertOp);

		if (SlotTag == BackpackSlotTag)
		{
			FPredictedInventoryOp CapacityOp;
			CapacityOp.PredictionID = PredictionID;
			CapacityOp.OpType = EPredictedInventoryOp::CapacityChange;
			CapacityOp.Quantity = 0;
			InventoryComponent->PendingPredictions.Add(CapacityOp);
		}

		RebuildPredictedState();
		InventoryComponent->RebuildPredictedState();
	}

	Server_UnequipItem(SlotTag, DestInventorySlot, PredictionID);
	return true;
}

bool UEquipmentComponent::FindPredictedItemInSlot(FGameplayTag SlotTag, FEquipmentEntry& OutEntry) const
{
	if (const FEquipmentEntry* Found = PredictedEquipment.FindByPredicate(
		[SlotTag](const FEquipmentEntry& Entry) { return Entry.SlotTag == SlotTag; }))
	{
		OutEntry = *Found;
		return true;
	}
	return false;
}

bool UEquipmentComponent::IsSlotBlocked(FGameplayTag SlotTag) const
{
	if (SlotTag != OffHandSlotTag)
	{
		return false;
	}

	FEquipmentEntry MainHandEntry;
	if (!FindPredictedItemInSlot(MainHandSlotTag, MainHandEntry))
	{
		return false;
	}

	const UEquipmentItemDefinition* ItemDef = ResolveEquipmentDefinition(MainHandEntry.ItemID);
	return ItemDef && ItemDef->bTwoHanded;
}

// ==================== Persistence ====================

FEquipmentSaveData UEquipmentComponent::BuildSaveData() const
{
	FEquipmentSaveData SaveData;
	SaveData.Entries = EquipmentList.Entries;
	return SaveData;
}

void UEquipmentComponent::LoadSaveData(const FEquipmentSaveData& SaveData)
{
	if (GetOwnerRole() != ROLE_Authority)
	{
		return;
	}

	EquipmentList.Entries = SaveData.Entries;
	EquipmentList.MarkArrayDirty();

	// Keep the sibling inventory's capacity consistent with whatever backpack (if any) the
	// loaded data equips - LoadSaveData bypasses Server_EquipItem, so nothing else does this.
	if (InventoryComponent)
	{
		const FEquipmentEntry* BackpackEntry = EquipmentList.FindBySlotTag(BackpackSlotTag);
		const UEquipmentItemDefinition* BackpackDef = BackpackEntry ? ResolveEquipmentDefinition(BackpackEntry->ItemID) : nullptr;
		InventoryComponent->BackpackBonusSlots = BackpackDef ? BackpackDef->BackpackBonusSlots : 0;
	}

	RebuildPredictedState();
	OnEquipmentChanged.Broadcast(FGameplayTag(), false);
}

// ==================== Replication / prediction plumbing ====================

void UEquipmentComponent::NotifySlotReplicated(FGameplayTag SlotTag, bool bWasRemoved)
{
	// See UInventoryComponent::NotifySlotReplicated for why both the replication-callback path
	// and this component's own server-local calls converge here.
	OnEquipmentChanged.Broadcast(SlotTag, bWasRemoved);
	RebuildPredictedState();
}

void UEquipmentComponent::NotifyReplicatedDeltaReceived(FGameplayTag SlotTag, bool bWasRemoved)
{
	OnEquipmentChanged.Broadcast(SlotTag, bWasRemoved);

	if (!bPredictedRebuildPending)
	{
		bPredictedRebuildPending = true;
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimerForNextTick(
				FTimerDelegate::CreateUObject(this, &UEquipmentComponent::ExecuteDeferredPredictedRebuild));
		}
		else
		{
			ExecuteDeferredPredictedRebuild();
		}
	}
}

void UEquipmentComponent::ExecuteDeferredPredictedRebuild()
{
	bPredictedRebuildPending = false;
	RebuildPredictedState();
}

bool UEquipmentComponent::ShouldPredictLocally() const
{
	return GetOwnerRole() == ROLE_AutonomousProxy;
}

bool UEquipmentComponent::IsSlotPendingPrediction(FGameplayTag SlotTag) const
{
	for (const FPredictedEquipmentOp& Op : PendingPredictions)
	{
		if (Op.SlotTag == SlotTag)
		{
			return true;
		}
	}
	return false;
}

void UEquipmentComponent::ApplyPredictedOpToArray(TArray<FEquipmentEntry>& Entries, const FPredictedEquipmentOp& Op) const
{
	auto FindBySlot = [&Entries](FGameplayTag SlotTag) -> FEquipmentEntry*
	{
		return Entries.FindByPredicate([SlotTag](const FEquipmentEntry& Entry) { return Entry.SlotTag == SlotTag; });
	};

	switch (Op.OpType)
	{
	case EPredictedEquipmentOp::Equip:
		if (FEquipmentEntry* Existing = FindBySlot(Op.SlotTag))
		{
			Existing->ItemID = Op.ItemID;
		}
		else
		{
			Entries.Add(FEquipmentEntry(Op.SlotTag, Op.ItemID));
		}
		break;

	case EPredictedEquipmentOp::Unequip:
	{
		const FGameplayTag SlotTag = Op.SlotTag;
		Entries.RemoveAll([SlotTag](const FEquipmentEntry& Entry) { return Entry.SlotTag == SlotTag; });
		break;
	}
	}
}

void UEquipmentComponent::RebuildPredictedState()
{
	// Same "authoritative snapshot + replay pending ops" invariant as
	// UInventoryComponent::RebuildPredictedState - see that function's comment.
	PredictedEquipment = EquipmentList.Entries;
	for (const FPredictedEquipmentOp& Op : PendingPredictions)
	{
		ApplyPredictedOpToArray(PredictedEquipment, Op);
	}

	OnPredictedEquipmentChanged.Broadcast(FGameplayTag(), false);
}

void UEquipmentComponent::ResolvePredictedOp(int32 PredictionID)
{
	if (PredictionID == 0)
	{
		return;
	}

	PendingPredictions.RemoveAll(
		[PredictionID](const FPredictedEquipmentOp& Op) { return Op.PredictionID == PredictionID; });

	RebuildPredictedState();
}

// ==================== Server-side anti-spam guards ====================

bool UEquipmentComponent::Server_TryBeginSlotOp(FGameplayTag SlotTag)
{
	if (SlotsWithPendingServerOperation.Contains(SlotTag))
	{
		return false;
	}

	if (const double* LastTime = LastSlotOperationServerTime.Find(SlotTag))
	{
		const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
		if (Now - *LastTime < MinSlotOperationInterval)
		{
			return false;
		}
	}

	SlotsWithPendingServerOperation.Add(SlotTag);
	return true;
}

void UEquipmentComponent::Server_EndSlotOp(FGameplayTag SlotTag)
{
	SlotsWithPendingServerOperation.Remove(SlotTag);
	LastSlotOperationServerTime.Add(SlotTag, GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0);
}

UEquipmentItemDefinition* UEquipmentComponent::ResolveEquipmentDefinition(const FPrimaryAssetId& ItemID) const
{
	if (!ItemID.IsValid())
	{
		return nullptr;
	}

	UObject* Asset = UAssetManager::Get().GetPrimaryAssetObject(ItemID);
	return Cast<UEquipmentItemDefinition>(Asset);
}

AActor* UEquipmentComponent::ResolveWearerActor() const
{
	AActor* Wearer = GetOwner();
	if (const APlayerState* PS = Cast<APlayerState>(GetOwner()))
	{
		if (APawn* Pawn = PS->GetPawn())
		{
			Wearer = Pawn;
		}
	}
	return Wearer;
}

bool UEquipmentComponent::Authority_IsSlotBlocked(FGameplayTag SlotTag) const
{
	if (SlotTag != OffHandSlotTag)
	{
		return false;
	}

	const FEquipmentEntry* MainHandEntry = EquipmentList.FindBySlotTag(MainHandSlotTag);
	if (!MainHandEntry)
	{
		return false;
	}

	const UEquipmentItemDefinition* ItemDef = ResolveEquipmentDefinition(MainHandEntry->ItemID);
	return ItemDef && ItemDef->bTwoHanded;
}

// ==================== Server RPCs ====================
// See InventoryComponent.cpp's equivalent comment: _Validate only rejects structurally
// malformed requests. Range-checking SourceInventorySlot/DestInventorySlot against the
// sibling inventory's current (dynamic, backpack-dependent) capacity is therefore done in
// _Implementation, not _Validate - that capacity isn't something a request should be able to
// get "structurally wrong" in a way worth disconnecting over.

bool UEquipmentComponent::Server_EquipItem_Validate(int32 SourceInventorySlot, FGameplayTag TargetSlotTag, int32 PredictionID)
{
	return TargetSlotTag.IsValid() && SourceInventorySlot >= 0;
}

void UEquipmentComponent::Server_EquipItem_Implementation(int32 SourceInventorySlot, FGameplayTag TargetSlotTag, int32 PredictionID)
{
	if (!InventoryComponent || !ValidEquipmentSlots.Contains(TargetSlotTag) || !InventoryComponent->IsValidSlotIndex(SourceInventorySlot))
	{
		Client_AckPrediction(PredictionID, false);
		return;
	}

	if (!Server_TryBeginSlotOp(TargetSlotTag))
	{
		Client_AckPrediction(PredictionID, false);
		return;
	}
	if (!InventoryComponent->Server_TryBeginSlotOp(SourceInventorySlot))
	{
		Server_EndSlotOp(TargetSlotTag);
		Client_AckPrediction(PredictionID, false);
		return;
	}

	FInventoryItem* SourceItem = InventoryComponent->InventoryList.FindBySlot(SourceInventorySlot);
	UEquipmentItemDefinition* ItemDef = SourceItem ? ResolveEquipmentDefinition(SourceItem->ItemID) : nullptr;

	// SourceItem->Quantity == 1: gear doesn't stack. Guards against a stack of >1 (which
	// shouldn't be reachable via normal play if MaxStackSize is authored correctly, but is not
	// otherwise re-checked here) silently collapsing into a single equipped item.
	const bool bValidSource = SourceItem && ItemDef && SourceItem->Quantity == 1 && ItemDef->EquipmentSlotTag == TargetSlotTag;

	// Two-handed occupancy rules - explicit rejection, no implicit auto-unequip of whatever is
	// in the way (see this class' header comment).
	const bool bTwoHandedConflict =
		(TargetSlotTag == MainHandSlotTag && ItemDef && ItemDef->bTwoHanded && EquipmentList.FindBySlotTag(OffHandSlotTag)) ||
		(TargetSlotTag == OffHandSlotTag && Authority_IsSlotBlocked(OffHandSlotTag));

	if (!bValidSource || bTwoHandedConflict)
	{
		Server_EndSlotOp(TargetSlotTag);
		InventoryComponent->Server_EndSlotOp(SourceInventorySlot);
		Client_AckPrediction(PredictionID, false);
		return;
	}

	const FPrimaryAssetId NewItemID = SourceItem->ItemID;

	InventoryComponent->InventoryList.Items.RemoveAll(
		[SourceInventorySlot](const FInventoryItem& Item) { return Item.SlotIndex == SourceInventorySlot; });
	InventoryComponent->InventoryList.MarkArrayDirty();

	FEquipmentEntry* ExistingTarget = EquipmentList.FindBySlotTag(TargetSlotTag);
	FPrimaryAssetId DisplacedItemID;
	const bool bHadDisplaced = ExistingTarget != nullptr;
	if (bHadDisplaced)
	{
		DisplacedItemID = ExistingTarget->ItemID;
		ExistingTarget->ItemID = NewItemID;
		EquipmentList.MarkItemDirty(*ExistingTarget);
	}
	else
	{
		EquipmentList.Entries.Add(FEquipmentEntry(TargetSlotTag, NewItemID));
		EquipmentList.MarkArrayDirty();
	}

	if (bHadDisplaced)
	{
		// Swap: the previously-equipped item goes back into the exact slot the new item came
		// from - deterministic, no "find a free slot" ambiguity.
		InventoryComponent->InventoryList.Items.Add(FInventoryItem(DisplacedItemID, 1, SourceInventorySlot));
		InventoryComponent->InventoryList.MarkArrayDirty();
	}

	InventoryComponent->NotifySlotReplicated(SourceInventorySlot, !bHadDisplaced);
	NotifySlotReplicated(TargetSlotTag, false);

	if (TargetSlotTag == BackpackSlotTag)
	{
		InventoryComponent->BackpackBonusSlots = ItemDef->BackpackBonusSlots;
		InventoryComponent->NotifySlotReplicated(INDEX_NONE, false);
	}

	AActor* Wearer = ResolveWearerActor();
	ItemDef->OnEquipped(Wearer, this);
	if (bHadDisplaced)
	{
		if (UEquipmentItemDefinition* DisplacedDef = ResolveEquipmentDefinition(DisplacedItemID))
		{
			DisplacedDef->OnUnequipped(Wearer, this);
		}
	}

	Server_EndSlotOp(TargetSlotTag);
	InventoryComponent->Server_EndSlotOp(SourceInventorySlot);
	Client_AckPrediction(PredictionID, true);
}

bool UEquipmentComponent::Server_UnequipItem_Validate(FGameplayTag SlotTag, int32 DestInventorySlot, int32 PredictionID)
{
	return SlotTag.IsValid() && DestInventorySlot >= 0;
}

void UEquipmentComponent::Server_UnequipItem_Implementation(FGameplayTag SlotTag, int32 DestInventorySlot, int32 PredictionID)
{
	if (!InventoryComponent || !InventoryComponent->IsValidSlotIndex(DestInventorySlot))
	{
		Client_AckPrediction(PredictionID, false);
		return;
	}

	if (!Server_TryBeginSlotOp(SlotTag))
	{
		Client_AckPrediction(PredictionID, false);
		return;
	}
	if (!InventoryComponent->Server_TryBeginSlotOp(DestInventorySlot))
	{
		Server_EndSlotOp(SlotTag);
		Client_AckPrediction(PredictionID, false);
		return;
	}

	FEquipmentEntry* Entry = EquipmentList.FindBySlotTag(SlotTag);
	const bool bDestOccupied = InventoryComponent->InventoryList.FindBySlot(DestInventorySlot) != nullptr;

	// Refusing to shrink capacity while it would strand an item (rather than destroying it) is
	// a business-rule rejection, same tier as "inventory full" elsewhere in this system.
	const bool bWouldStrandItems = SlotTag == BackpackSlotTag
		&& !InventoryComponent->Authority_CanShrinkTo(InventoryComponent->BaseNumSlots);

	if (!Entry || bDestOccupied || bWouldStrandItems)
	{
		Server_EndSlotOp(SlotTag);
		InventoryComponent->Server_EndSlotOp(DestInventorySlot);
		Client_AckPrediction(PredictionID, false);
		return;
	}

	const FPrimaryAssetId ItemID = Entry->ItemID;
	UEquipmentItemDefinition* ItemDef = ResolveEquipmentDefinition(ItemID);

	EquipmentList.Entries.RemoveAll([SlotTag](const FEquipmentEntry& E) { return E.SlotTag == SlotTag; });
	EquipmentList.MarkArrayDirty();
	NotifySlotReplicated(SlotTag, true);

	InventoryComponent->InventoryList.Items.Add(FInventoryItem(ItemID, 1, DestInventorySlot));
	InventoryComponent->InventoryList.MarkArrayDirty();
	InventoryComponent->NotifySlotReplicated(DestInventorySlot, false);

	if (SlotTag == BackpackSlotTag)
	{
		InventoryComponent->BackpackBonusSlots = 0;
		InventoryComponent->NotifySlotReplicated(INDEX_NONE, false);
	}

	if (ItemDef)
	{
		ItemDef->OnUnequipped(ResolveWearerActor(), this);
	}

	Server_EndSlotOp(SlotTag);
	InventoryComponent->Server_EndSlotOp(DestInventorySlot);
	Client_AckPrediction(PredictionID, true);
}

void UEquipmentComponent::Client_AckPrediction_Implementation(int32 PredictionID, bool bSuccess)
{
	// One PredictionID can have pending entries in this component AND the sibling inventory
	// (see the header comment) - both must be resolved together.
	ResolvePredictedOp(PredictionID);
	if (InventoryComponent)
	{
		InventoryComponent->ResolvePredictedOp(PredictionID);
	}
}
