// Fill out your copyright notice in the Description page of Project Settings.

#include "InventoryComponent.h"
#include "ItemDefinition.h"
#include "ItemPickupInterface.h"
#include "Net/UnrealNetwork.h"
#include "Engine/AssetManager.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/Pawn.h"
#include "TimerManager.h"

UInventoryComponent::UInventoryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UInventoryComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// COND_OwnerOnly: unlike a Pawn, a PlayerState replicates to every client by default
	// (scoreboards etc. need it), but another player's item contents are private - only the
	// connection that owns this PlayerState should ever receive InventoryList.
	DOREPLIFETIME_CONDITION(UInventoryComponent, InventoryList, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UInventoryComponent, BackpackBonusSlots, COND_OwnerOnly);
}

void UInventoryComponent::BeginPlay()
{
	Super::BeginPlay();

	InventoryList.OwnerComponent = this;
	PredictedItems = InventoryList.Items;
}

void UInventoryComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Clear the FastArray's back-pointer first: the replication system can still invoke
	// PostReplicatedAdd/Change/PreReplicatedRemove on InventoryList while this component is
	// tearing down (e.g. a last delta processed as the connection closes), and those
	// callbacks must not dereference a half-destroyed UInventoryComponent.
	InventoryList.OwnerComponent = nullptr;

	// Cancel any async loads this component kicked off. Their completion callbacks capture
	// `this`; left uncancelled they could fire after both this component and any UI bound
	// to its delegates are gone.
	for (const TSharedPtr<FStreamableHandle>& Handle : ActiveStreamingHandles)
	{
		if (Handle.IsValid())
		{
			Handle->CancelHandle();
		}
	}
	ActiveStreamingHandles.Empty();

	// Explicitly drop UI bindings rather than relying on the delegate objects simply being
	// destroyed alongside the component, so any dangling references are cut at a known point.
	OnInventoryChanged.Clear();
	OnPredictedInventoryChanged.Clear();

	Super::EndPlay(EndPlayReason);
}

// ==================== Public API ====================

bool UInventoryComponent::AddItem(FPrimaryAssetId ItemID, int32 Quantity)
{
	if (!ItemID.IsValid() || Quantity <= 0 || Quantity > MaxQuantityPerRequest)
	{
		return false;
	}

	int32 PredictionID = 0;
	if (ShouldPredictLocally())
	{
		PredictionID = AllocatePredictionID();

		FPredictedInventoryOp Op;
		Op.PredictionID = PredictionID;
		Op.OpType = EPredictedInventoryOp::Add;
		Op.ItemID = ItemID;
		Op.Quantity = Quantity;
		PendingPredictions.Add(Op);

		// Slot assignment is server-owned (stacking rules / first-free-slot search), so
		// this rebuild will not place a new stack anywhere - it exists so UI bound to
		// OnPredictedInventoryChanged can show an "item incoming" cue immediately. See
		// ApplyPredictedOpToArray's Add case.
		RebuildPredictedState();
	}


	Server_AddItem(ItemID, Quantity, PredictionID);
	return true;
}

bool UInventoryComponent::RemoveItem(int32 SlotIndex, int32 Quantity)
{
	if (!IsValidSlotIndex(SlotIndex) || Quantity <= 0)
	{
		return false;
	}

	int32 PredictionID = 0;
	if (ShouldPredictLocally())
	{
		if (IsSlotPendingPrediction(SlotIndex))
		{
			return false;
		}

		PredictionID = AllocatePredictionID();

		FPredictedInventoryOp Op;
		Op.PredictionID = PredictionID;
		Op.OpType = EPredictedInventoryOp::Remove;
		Op.SlotA = SlotIndex;
		Op.Quantity = Quantity;
		PendingPredictions.Add(Op);
		RebuildPredictedState();
	}

	Server_RemoveItem(SlotIndex, Quantity, PredictionID);
	return true;
}

bool UInventoryComponent::UseItem(int32 SlotIndex)
{
	if (!IsValidSlotIndex(SlotIndex))
	{
		return false;
	}

	int32 PredictionID = 0;
	if (ShouldPredictLocally())
	{
		// This is the double-consume guard from the class comment: refuse a second UseItem
		// on a slot that already has one in flight. The server-side cooldown in
		// Server_TryBeginSlotOp is the defense-in-depth backstop for a client that bypasses
		// this (modified client, or a future async/channeled item use), not the primary
		// mechanism - this check is what makes normal spam-clicking a no-op.
		if (IsSlotPendingPrediction(SlotIndex))
		{
			return false;
		}

		PredictionID = AllocatePredictionID();

		FPredictedInventoryOp Op;
		Op.PredictionID = PredictionID;
		Op.OpType = EPredictedInventoryOp::Use;
		Op.SlotA = SlotIndex;
		PendingPredictions.Add(Op);

		// Deliberately does not touch PredictedItems: whether use consumes the stack
		// depends on server-side UItemDefinition::OnItemUsed logic the client cannot
		// evaluate authoritatively. Queuing the op (for the pending-slot check above) without
		// guessing the outcome is safer than predicting a decrement that might have to
		// visibly "undo" itself.
		RebuildPredictedState();
	}

	Server_UseItem(SlotIndex, PredictionID);
	return true;
}

bool UInventoryComponent::MoveItem(int32 SourceSlot, int32 DestSlot)
{
	if (!IsValidSlotIndex(SourceSlot) || !IsValidSlotIndex(DestSlot) || SourceSlot == DestSlot)
	{
		return false;
	}

	int32 PredictionID = 0;
	if (ShouldPredictLocally())
	{
		if (IsSlotPendingPrediction(SourceSlot) || IsSlotPendingPrediction(DestSlot))
		{
			return false;
		}

		PredictionID = AllocatePredictionID();

		FPredictedInventoryOp Op;
		Op.PredictionID = PredictionID;
		Op.OpType = EPredictedInventoryOp::Move;
		Op.SlotA = SourceSlot;
		Op.SlotB = DestSlot;
		PendingPredictions.Add(Op);
		RebuildPredictedState();
	}

	Server_MoveItem(SourceSlot, DestSlot, PredictionID);
	return true;
}

bool UInventoryComponent::SplitStack(int32 SourceSlot, int32 DestSlot, int32 SplitQuantity)
{
	if (!IsValidSlotIndex(SourceSlot) || !IsValidSlotIndex(DestSlot) || SourceSlot == DestSlot || SplitQuantity <= 0)
	{
		return false;
	}

	int32 PredictionID = 0;
	if (ShouldPredictLocally())
	{
		if (IsSlotPendingPrediction(SourceSlot) || IsSlotPendingPrediction(DestSlot))
		{
			return false;
		}

		PredictionID = AllocatePredictionID();

		FPredictedInventoryOp Op;
		Op.PredictionID = PredictionID;
		Op.OpType = EPredictedInventoryOp::SplitStack;
		Op.SlotA = SourceSlot;
		Op.SlotB = DestSlot;
		Op.Quantity = SplitQuantity;
		PendingPredictions.Add(Op);
		RebuildPredictedState();
	}

	Server_SplitStack(SourceSlot, DestSlot, SplitQuantity, PredictionID);
	return true;
}

bool UInventoryComponent::MergeStack(int32 SourceSlot, int32 DestSlot)
{
	if (!IsValidSlotIndex(SourceSlot) || !IsValidSlotIndex(DestSlot) || SourceSlot == DestSlot)
	{
		return false;
	}

	int32 PredictionID = 0;
	if (ShouldPredictLocally())
	{
		if (IsSlotPendingPrediction(SourceSlot) || IsSlotPendingPrediction(DestSlot))
		{
			return false;
		}

		PredictionID = AllocatePredictionID();

		FPredictedInventoryOp Op;
		Op.PredictionID = PredictionID;
		Op.OpType = EPredictedInventoryOp::MergeStack;
		Op.SlotA = SourceSlot;
		Op.SlotB = DestSlot;
		PendingPredictions.Add(Op);
		RebuildPredictedState();
	}

	Server_MergeStack(SourceSlot, DestSlot, PredictionID);
	return true;
}

bool UInventoryComponent::DropItem(int32 SlotIndex, int32 Quantity)
{
	if (!IsValidSlotIndex(SlotIndex) || Quantity <= 0)
	{
		return false;
	}

	int32 PredictionID = 0;
	if (ShouldPredictLocally())
	{
		if (IsSlotPendingPrediction(SlotIndex))
		{
			return false;
		}

		PredictionID = AllocatePredictionID();

		FPredictedInventoryOp Op;
		Op.PredictionID = PredictionID;
		Op.OpType = EPredictedInventoryOp::Drop;
		Op.SlotA = SlotIndex;
		Op.Quantity = Quantity;
		PendingPredictions.Add(Op);
		RebuildPredictedState();
	}

	Server_DropItem(SlotIndex, Quantity, PredictionID);
	return true;
}

bool UInventoryComponent::FindPredictedItemInSlot(int32 SlotIndex, FInventoryItem& OutItem) const
{
	if (const FInventoryItem* Found = PredictedItems.FindByPredicate(
		[SlotIndex](const FInventoryItem& Item) { return Item.SlotIndex == SlotIndex; }))
	{
		OutItem = *Found;
		return true;
	}
	return false;
}

int32 UInventoryComponent::FindFirstFreePredictedSlot() const
{
	for (int32 SlotIndex = 0; SlotIndex < GetPredictedNumSlots(); ++SlotIndex)
	{
		FInventoryItem Unused;
		if (!FindPredictedItemInSlot(SlotIndex, Unused))
		{
			return SlotIndex;
		}
	}
	return INDEX_NONE;
}

// ==================== Persistence ====================

FInventorySaveData UInventoryComponent::BuildSaveData() const
{
	FInventorySaveData SaveData;
	SaveData.Items = InventoryList.Items;
	return SaveData;
}

void UInventoryComponent::LoadSaveData(const FInventorySaveData& SaveData)
{
	if (GetOwnerRole() != ROLE_Authority)
	{
		return;
	}

	InventoryList.Items = SaveData.Items;
	InventoryList.MarkArrayDirty();
	RebuildPredictedState();
	OnInventoryChanged.Broadcast(INDEX_NONE, false);
}

// ==================== Replication / prediction plumbing ====================

void UInventoryComponent::NotifySlotReplicated(int32 SlotIndex, bool bWasRemoved)
{
	// NOTE: FastArraySerializer's PostReplicatedAdd/Change/PreReplicatedRemove only fire on
	// machines that *received* a replicated delta - never on the authoritative machine that
	// produced it. Server_*_Implementation therefore calls this directly after mutating
	// InventoryList so the server's own local UI (a listen-server host's HUD, for example)
	// updates too; remote clients get here via FInventoryList's callbacks instead. Both paths
	// converge here so there is exactly one place that broadcasts to UI and reconciles
	// prediction.
	OnInventoryChanged.Broadcast(SlotIndex, bWasRemoved);
	RebuildPredictedState();
}

void UInventoryComponent::NotifyReplicatedDeltaReceived(int32 SlotIndex, bool bWasRemoved)
{
	// Accurate immediately regardless of the array's mid-delta state - see this function's
	// declaration comment.
	OnInventoryChanged.Broadcast(SlotIndex, bWasRemoved);

	if (!bPredictedRebuildPending)
	{
		bPredictedRebuildPending = true;
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimerForNextTick(
				FTimerDelegate::CreateUObject(this, &UInventoryComponent::ExecuteDeferredPredictedRebuild));
		}
		else
		{
			// No world (e.g. mid-teardown) - nothing will tick to run the deferred call, so
			// just do it now; there's no mid-delta race to worry about outside a live game world.
			ExecuteDeferredPredictedRebuild();
		}
	}
}

void UInventoryComponent::ExecuteDeferredPredictedRebuild()
{
	bPredictedRebuildPending = false;
	RebuildPredictedState();
}

bool UInventoryComponent::ShouldPredictLocally() const
{
	// Original: prediction only when the owner actor reports ROLE_AutonomousProxy.
	// In some ownership setups (InventoryComponent on APlayerState), the engine may show
	// ROLE_SimulatedProxy on the client even for the local player. Fall back to a
	// "owned by my local controller" check so the owning client still gets prediction.
	const bool bRoleAutonomous = GetOwnerRole() == ROLE_AutonomousProxy;
	if (bRoleAutonomous)
	{
		return true;
	}

	// If the owner is a PlayerState, check whether it belongs to the local PlayerController.
	if (const APlayerState* PS = Cast<APlayerState>(GetOwner()))
	{
		if (UWorld* World = GetWorld())
		{
			if (APlayerController* LocalPC = World->GetFirstPlayerController())
			{
				const bool bOwnedByLocalPC = LocalPC->PlayerState == PS && LocalPC->IsLocalController();
	
				return bOwnedByLocalPC;
			}
		}
	}

	return false;
}

bool UInventoryComponent::IsSlotPendingPrediction(int32 SlotIndex) const
{
	for (const FPredictedInventoryOp& Op : PendingPredictions)
	{
		if (Op.SlotA == SlotIndex || Op.SlotB == SlotIndex)
		{
			return true;
		}
	}
	return false;
}

void UInventoryComponent::ApplyPredictedOpToArray(TArray<FInventoryItem>& Items, const FPredictedInventoryOp& Op) const
{
	auto FindBySlot = [&Items](int32 SlotIndex) -> FInventoryItem*
	{
		return Items.FindByPredicate([SlotIndex](const FInventoryItem& Item) { return Item.SlotIndex == SlotIndex; });
	};

	switch (Op.OpType)
	{
	case EPredictedInventoryOp::Add:
		// See AddItem()'s comment: slot assignment is server-owned and intentionally not
		// guessed here.
		break;

	case EPredictedInventoryOp::Use:
		// See UseItem()'s comment: consumption outcome is not predictable client-side.
		break;

	case EPredictedInventoryOp::Remove:
	case EPredictedInventoryOp::Drop:
		if (FInventoryItem* Item = FindBySlot(Op.SlotA))
		{
			Item->Quantity -= Op.Quantity;
			if (Item->Quantity <= 0)
			{
				const int32 SlotA = Op.SlotA;
				Items.RemoveAll([SlotA](const FInventoryItem& I) { return I.SlotIndex == SlotA; });
			}
		}
		break;

	case EPredictedInventoryOp::Move:
	{
		FInventoryItem* AtA = FindBySlot(Op.SlotA);
		FInventoryItem* AtB = FindBySlot(Op.SlotB);
		if (AtA) { AtA->SlotIndex = Op.SlotB; }
		if (AtB) { AtB->SlotIndex = Op.SlotA; }
		break;
	}

	case EPredictedInventoryOp::SplitStack:
		if (FInventoryItem* Source = FindBySlot(Op.SlotA))
		{
			if (Source->Quantity > Op.Quantity && !FindBySlot(Op.SlotB))
			{
				Source->Quantity -= Op.Quantity;
				Items.Add(FInventoryItem(Source->ItemID, Op.Quantity, Op.SlotB));
			}
		}
		break;

	case EPredictedInventoryOp::MergeStack:
		if (FInventoryItem* Source = FindBySlot(Op.SlotA))
		{
			if (FInventoryItem* Dest = FindBySlot(Op.SlotB))
			{
				if (Dest->ItemID == Source->ItemID)
				{
					Dest->Quantity += Source->Quantity;
					const int32 SlotA = Op.SlotA;
					Items.RemoveAll([SlotA](const FInventoryItem& I) { return I.SlotIndex == SlotA; });
				}
			}
		}
		break;

	case EPredictedInventoryOp::InsertAt:
		// Queued by UEquipmentComponent when predicting an unequip. Op.SlotB is the
		// destination inventory slot (already known to the caller, same as MoveItem's dest).
		if (!FindBySlot(Op.SlotB))
		{
			Items.Add(FInventoryItem(Op.ItemID, Op.Quantity, Op.SlotB));
		}
		break;

	case EPredictedInventoryOp::CapacityChange:
		// Handled separately in RebuildPredictedState (it affects PredictedBackpackBonusSlots,
		// not the Items array), nothing to do here.
		break;
	}
}

void UInventoryComponent::RebuildPredictedState()
{
	// Always start from the last known AUTHORITATIVE snapshot and replay whatever
	// PREDICTED ops are still outstanding on top of it. This is what makes reconciliation
	// self-correcting: it does not matter whether this was triggered by a fresh
	// authoritative replication, a successful ack, or a failed one - the result is always
	// "confirmed state + remaining optimistic overlay", never a hand-rolled inverse of one
	// specific op.
	PredictedItems = InventoryList.Items;
	PredictedBackpackBonusSlots = BackpackBonusSlots;
	for (const FPredictedInventoryOp& Op : PendingPredictions)
	{
		ApplyPredictedOpToArray(PredictedItems, Op);
		if (Op.OpType == EPredictedInventoryOp::CapacityChange)
		{
			PredictedBackpackBonusSlots = Op.Quantity;
		}
	}

	// A rebuild can touch several slots at once, so there is no single meaningful
	// SlotIndex to report; INDEX_NONE tells UI to refresh broadly.
	OnPredictedInventoryChanged.Broadcast(INDEX_NONE, false);
}

// ==================== Server-side anti-spam guards ====================

bool UInventoryComponent::Server_TryBeginSlotOp(int32 SlotIndex)
{
	if (SlotsWithPendingServerOperation.Contains(SlotIndex))
	{
		return false;
	}

	if (const double* LastTime = LastSlotOperationServerTime.Find(SlotIndex))
	{
		const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
		if (Now - *LastTime < MinSlotOperationInterval)
		{
			return false;
		}
	}

	SlotsWithPendingServerOperation.Add(SlotIndex);
	return true;
}

void UInventoryComponent::Server_EndSlotOp(int32 SlotIndex)
{
	SlotsWithPendingServerOperation.Remove(SlotIndex);
	LastSlotOperationServerTime.Add(SlotIndex, GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0);
}

UItemDefinition* UInventoryComponent::ResolveItemDefinition(const FPrimaryAssetId& ItemID) const
{
	if (!ItemID.IsValid())
	{
		return nullptr;
	}

	// Assumes the item's data asset is already resident (Asset Manager preload rule, or
	// loaded earlier in the game's own loading flow) rather than loading it synchronously
	// here, which would stall whatever thread is processing this RPC. A miss is treated as
	// "unknown item" by every caller of this function.
	UObject* Asset = UAssetManager::Get().GetPrimaryAssetObject(ItemID);
	return Cast<UItemDefinition>(Asset);
}

int32 UInventoryComponent::FindFirstFreeSlot() const
{
	for (int32 SlotIndex = 0; SlotIndex < GetNumSlots(); ++SlotIndex)
	{
		if (!InventoryList.FindBySlot(SlotIndex))
		{
			return SlotIndex;
		}
	}
	return INDEX_NONE;
}

bool UInventoryComponent::Authority_CanShrinkTo(int32 NewNumSlots) const
{
	for (const FInventoryItem& Item : InventoryList.Items)
	{
		if (Item.SlotIndex >= NewNumSlots)
		{
			return false;
		}
	}
	return true;
}

// ==================== Server RPCs ====================
// Every _Validate below only checks that the request is well-formed (in-range indices,
// positive quantities, sane upper bounds). A _Validate failure makes the engine close the
// sending connection, so it must never fail for a request that is merely inconvenient
// (inventory full, wrong item type, etc.) - those are business-rule rejections handled in
// _Implementation via Client_AckPrediction(..., false).

bool UInventoryComponent::Server_AddItem_Validate(FPrimaryAssetId ItemID, int32 Quantity, int32 PredictionID)
{
	return ItemID.IsValid() && Quantity > 0 && Quantity <= MaxQuantityPerRequest;
}

void UInventoryComponent::Server_AddItem_Implementation(FPrimaryAssetId ItemID, int32 Quantity, int32 PredictionID)
{
	const UItemDefinition* ItemDef = ResolveItemDefinition(ItemID);
	if (!ItemDef)
	{
	
			// Well-formed FPrimaryAssetId, but not a registered item (e.g. stale ID from
			// content that was removed) - a legitimate business-rule rejection, not proof of
			// cheating, so this is handled here rather than in _Validate.
			Client_AckPrediction(PredictionID, false);
			return;
		}

		int32 Remaining = Quantity;

		// Fill existing, under-cap stacks of the same item before opening new slots.
		for (FInventoryItem& Item : InventoryList.Items)
		{
			if (Remaining <= 0)
			{
				break;
			}
			if (Item.ItemID == ItemID && Item.Quantity < ItemDef->MaxStackSize)
			{
				const int32 AddAmount = FMath::Min(Remaining, ItemDef->MaxStackSize - Item.Quantity);
				Item.Quantity += AddAmount;
				Remaining -= AddAmount;
				InventoryList.MarkItemDirty(Item);
				NotifySlotReplicated(Item.SlotIndex, false);
	
			}
		}

		while (Remaining > 0)
		{
			const int32 FreeSlot = FindFirstFreeSlot();
			if (FreeSlot == INDEX_NONE)
			{
	
				break; // Inventory full; whatever did not fit is simply not added.
			}

			const int32 StackQuantity = FMath::Min(Remaining, ItemDef->MaxStackSize);
			InventoryList.Items.Add(FInventoryItem(ItemID, StackQuantity, FreeSlot));
			InventoryList.MarkArrayDirty();
			NotifySlotReplicated(FreeSlot, false);
			Remaining -= StackQuantity;
		}

		Client_AckPrediction(PredictionID, Remaining < Quantity);
}

bool UInventoryComponent::Server_RemoveItem_Validate(int32 SlotIndex, int32 Quantity, int32 PredictionID)
{
	return IsValidSlotIndex(SlotIndex) && Quantity > 0 && Quantity <= MaxQuantityPerRequest;
}

void UInventoryComponent::Server_RemoveItem_Implementation(int32 SlotIndex, int32 Quantity, int32 PredictionID)
{
	if (!Server_TryBeginSlotOp(SlotIndex))
	{
		Client_AckPrediction(PredictionID, false);
		return;
	}

	FInventoryItem* Item = InventoryList.FindBySlot(SlotIndex);
	if (!Item || Item->Quantity < Quantity)
	{
		Server_EndSlotOp(SlotIndex);
		Client_AckPrediction(PredictionID, false);
		return;
	}

	Item->Quantity -= Quantity;
	if (Item->Quantity <= 0)
	{
		InventoryList.Items.RemoveAll([SlotIndex](const FInventoryItem& I) { return I.SlotIndex == SlotIndex; });
		InventoryList.MarkArrayDirty();
		NotifySlotReplicated(SlotIndex, true);
	}
	else
	{
		InventoryList.MarkItemDirty(*Item);
		NotifySlotReplicated(SlotIndex, false);
	}

	Server_EndSlotOp(SlotIndex);
	Client_AckPrediction(PredictionID, true);
}

bool UInventoryComponent::Server_UseItem_Validate(int32 SlotIndex, int32 PredictionID)
{
	return IsValidSlotIndex(SlotIndex);
}

void UInventoryComponent::Server_UseItem_Implementation(int32 SlotIndex, int32 PredictionID)
{
	if (!Server_TryBeginSlotOp(SlotIndex))
	{
		// Exactly the race called out in the class comment: a second UseItem for this slot
		// arrived while the first was still pending/cooling down. Reject outright rather
		// than risk consuming the stack twice.
		Client_AckPrediction(PredictionID, false);
		return;
	}

	FInventoryItem* Item = InventoryList.FindBySlot(SlotIndex);
	UItemDefinition* ItemDef = Item ? ResolveItemDefinition(Item->ItemID) : nullptr;
	if (!Item || !ItemDef)
	{
		Server_EndSlotOp(SlotIndex);
		Client_AckPrediction(PredictionID, false);
		return;
	}

	AActor* UsingActor = GetOwner();
	if (const APlayerState* PS = Cast<APlayerState>(GetOwner()))
	{
		if (APawn* Pawn = PS->GetPawn())
		{
			UsingActor = Pawn;
		}
	}

	// Plain-name call, not Execute_OnItemUsed: for a BlueprintNativeEvent declared on a
	// concrete UCLASS (as opposed to a UINTERFACE - see IItemPickupInterface below for that
	// case), UHT wires Blueprint-override dispatch into the function's own declared name.
	const bool bConsumed = ItemDef->OnItemUsed(UsingActor, this, SlotIndex);
	if (bConsumed)
	{
		Item->Quantity -= 1;
		if (Item->Quantity <= 0)
		{
			InventoryList.Items.RemoveAll([SlotIndex](const FInventoryItem& I) { return I.SlotIndex == SlotIndex; });
			InventoryList.MarkArrayDirty();
			NotifySlotReplicated(SlotIndex, true);
		}
		else
		{
			InventoryList.MarkItemDirty(*Item);
			NotifySlotReplicated(SlotIndex, false);
		}
	}

	Server_EndSlotOp(SlotIndex);
	Client_AckPrediction(PredictionID, true);
}

bool UInventoryComponent::Server_MoveItem_Validate(int32 SourceSlot, int32 DestSlot, int32 PredictionID)
{
	return IsValidSlotIndex(SourceSlot) && IsValidSlotIndex(DestSlot) && SourceSlot != DestSlot;
}

void UInventoryComponent::Server_MoveItem_Implementation(int32 SourceSlot, int32 DestSlot, int32 PredictionID)
{
	if (!Server_TryBeginSlotOp(SourceSlot))
	{
		Client_AckPrediction(PredictionID, false);
		return;
	}
	if (!Server_TryBeginSlotOp(DestSlot))
	{
		Server_EndSlotOp(SourceSlot);
		Client_AckPrediction(PredictionID, false);
		return;
	}

	FInventoryItem* SourceItem = InventoryList.FindBySlot(SourceSlot);
	if (!SourceItem)
	{
		Server_EndSlotOp(SourceSlot);
		Server_EndSlotOp(DestSlot);
		Client_AckPrediction(PredictionID, false);
		return;
	}

	// Slot swap: whatever (if anything) currently occupies DestSlot trades places with
	// SourceItem instead of being overwritten.
	FInventoryItem* DestItem = InventoryList.FindBySlot(DestSlot);
	SourceItem->SlotIndex = DestSlot;
	InventoryList.MarkItemDirty(*SourceItem);
	if (DestItem)
	{
		DestItem->SlotIndex = SourceSlot;
		InventoryList.MarkItemDirty(*DestItem);
	}

	NotifySlotReplicated(SourceSlot, false);
	NotifySlotReplicated(DestSlot, false);

	Server_EndSlotOp(SourceSlot);
	Server_EndSlotOp(DestSlot);
	Client_AckPrediction(PredictionID, true);
}

bool UInventoryComponent::Server_SplitStack_Validate(int32 SourceSlot, int32 DestSlot, int32 SplitQuantity, int32 PredictionID)
{
	return IsValidSlotIndex(SourceSlot) && IsValidSlotIndex(DestSlot) && SourceSlot != DestSlot
		&& SplitQuantity > 0 && SplitQuantity <= MaxQuantityPerRequest;
}

void UInventoryComponent::Server_SplitStack_Implementation(int32 SourceSlot, int32 DestSlot, int32 SplitQuantity, int32 PredictionID)
{
	if (!Server_TryBeginSlotOp(SourceSlot))
	{
		Client_AckPrediction(PredictionID, false);
		return;
	}
	if (!Server_TryBeginSlotOp(DestSlot))
	{
		Server_EndSlotOp(SourceSlot);
		Client_AckPrediction(PredictionID, false);
		return;
	}

	FInventoryItem* SourceItem = InventoryList.FindBySlot(SourceSlot);
	const bool bDestOccupied = InventoryList.FindBySlot(DestSlot) != nullptr;

	if (!SourceItem || bDestOccupied || SourceItem->Quantity <= SplitQuantity)
	{
		Server_EndSlotOp(SourceSlot);
		Server_EndSlotOp(DestSlot);
		Client_AckPrediction(PredictionID, false);
		return;
	}

	SourceItem->Quantity -= SplitQuantity;
	InventoryList.MarkItemDirty(*SourceItem);
	InventoryList.Items.Add(FInventoryItem(SourceItem->ItemID, SplitQuantity, DestSlot));
	InventoryList.MarkArrayDirty();

	NotifySlotReplicated(SourceSlot, false);
	NotifySlotReplicated(DestSlot, false);

	Server_EndSlotOp(SourceSlot);
	Server_EndSlotOp(DestSlot);
	Client_AckPrediction(PredictionID, true);
}

bool UInventoryComponent::Server_MergeStack_Validate(int32 SourceSlot, int32 DestSlot, int32 PredictionID)
{
	return IsValidSlotIndex(SourceSlot) && IsValidSlotIndex(DestSlot) && SourceSlot != DestSlot;
}

void UInventoryComponent::Server_MergeStack_Implementation(int32 SourceSlot, int32 DestSlot, int32 PredictionID)
{
	if (!Server_TryBeginSlotOp(SourceSlot))
	{
		Client_AckPrediction(PredictionID, false);
		return;
	}
	if (!Server_TryBeginSlotOp(DestSlot))
	{
		Server_EndSlotOp(SourceSlot);
		Client_AckPrediction(PredictionID, false);
		return;
	}

	FInventoryItem* SourceItem = InventoryList.FindBySlot(SourceSlot);
	FInventoryItem* DestItem = InventoryList.FindBySlot(DestSlot);

	if (!SourceItem || !DestItem || SourceItem->ItemID != DestItem->ItemID)
	{
		Server_EndSlotOp(SourceSlot);
		Server_EndSlotOp(DestSlot);
		Client_AckPrediction(PredictionID, false);
		return;
	}

	const UItemDefinition* ItemDef = ResolveItemDefinition(DestItem->ItemID);
	const int32 MaxStack = ItemDef ? ItemDef->MaxStackSize : TNumericLimits<int32>::Max();
	const int32 MoveQuantity = FMath::Min(SourceItem->Quantity, MaxStack - DestItem->Quantity);

	if (MoveQuantity <= 0)
	{
		Server_EndSlotOp(SourceSlot);
		Server_EndSlotOp(DestSlot);
		Client_AckPrediction(PredictionID, false);
		return;
	}

	DestItem->Quantity += MoveQuantity;
	InventoryList.MarkItemDirty(*DestItem);
	SourceItem->Quantity -= MoveQuantity;

	if (SourceItem->Quantity <= 0)
	{
		InventoryList.Items.RemoveAll([SourceSlot](const FInventoryItem& I) { return I.SlotIndex == SourceSlot; });
		InventoryList.MarkArrayDirty();
		NotifySlotReplicated(SourceSlot, true);
	}
	else
	{
		InventoryList.MarkItemDirty(*SourceItem);
		NotifySlotReplicated(SourceSlot, false);
	}

	NotifySlotReplicated(DestSlot, false);

	Server_EndSlotOp(SourceSlot);
	Server_EndSlotOp(DestSlot);
	Client_AckPrediction(PredictionID, true);
}

bool UInventoryComponent::Server_DropItem_Validate(int32 SlotIndex, int32 Quantity, int32 PredictionID)
{
	return IsValidSlotIndex(SlotIndex) && Quantity > 0 && Quantity <= MaxQuantityPerRequest;
}

void UInventoryComponent::Server_DropItem_Implementation(int32 SlotIndex, int32 Quantity, int32 PredictionID)
{
	if (!Server_TryBeginSlotOp(SlotIndex))
	{
		Client_AckPrediction(PredictionID, false);
		return;
	}

	FInventoryItem* Item = InventoryList.FindBySlot(SlotIndex);
	if (!Item || Item->Quantity < Quantity)
	{
		Server_EndSlotOp(SlotIndex);
		Client_AckPrediction(PredictionID, false);
		return;
	}

	const FPrimaryAssetId DroppedItemID = Item->ItemID;
	UItemDefinition* ItemDef = ResolveItemDefinition(DroppedItemID);

	Item->Quantity -= Quantity;
	const bool bSlotEmptied = Item->Quantity <= 0;
	if (bSlotEmptied)
	{
		InventoryList.Items.RemoveAll([SlotIndex](const FInventoryItem& I) { return I.SlotIndex == SlotIndex; });
		InventoryList.MarkArrayDirty();
	}
	else
	{
		InventoryList.MarkItemDirty(*Item);
	}
	NotifySlotReplicated(SlotIndex, bSlotEmptied);

	// Spawn the world pickup. Placement is intentionally simple (owning pawn's transform) -
	// throwing/offsetting it is presentation, not an inventory-system concern.
	if (ItemDef && ItemDef->PickupActorClass && GetWorld())
	{
		AActor* SpawnTransformSource = GetOwner();
		if (const APlayerState* PS = Cast<APlayerState>(GetOwner()))
		{
			if (APawn* Pawn = PS->GetPawn())
			{
				SpawnTransformSource = Pawn;
			}
		}

		if (SpawnTransformSource)
		{
			FActorSpawnParameters SpawnParams;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

			AActor* Pickup = GetWorld()->SpawnActor<AActor>(
				ItemDef->PickupActorClass,
				SpawnTransformSource->GetActorLocation(),
				SpawnTransformSource->GetActorRotation(),
				SpawnParams);

			if (Pickup && Pickup->GetClass()->ImplementsInterface(UItemPickupInterface::StaticClass()))
			{
				IItemPickupInterface::Execute_InitializePickup(Pickup, DroppedItemID, Quantity);
			}
		}
	}

	Server_EndSlotOp(SlotIndex);
	Client_AckPrediction(PredictionID, true);
}

void UInventoryComponent::Client_AckPrediction_Implementation(int32 PredictionID, bool bSuccess)
{
	// Rebuild regardless of bSuccess: on success the authoritative replication for this
	// change should already be on its way (or has arrived), so this just drops the
	// now-redundant predicted op; on failure it rolls the optimistic change back. Either
	// way the result is "last known authoritative state + still-outstanding predictions",
	// never a bespoke undo of this one op - see RebuildPredictedState.
	ResolvePredictedOp(PredictionID);
}

void UInventoryComponent::ResolvePredictedOp(int32 PredictionID)
{
	if (PredictionID == 0)
	{
		// Non-predicting caller (authority-local or misuse path) - nothing was queued.
		return;
	}

	// RemoveAll, not "find first and remove one": every one of this component's own public API
	// calls queues exactly one op per PredictionID, but UEquipmentComponent can queue several
	// (e.g. Remove + InsertAt + CapacityChange for an equip that swaps a backpack) under the
	// same cross-component PredictionID - see EquipmentComponent.h's class comment.
	PendingPredictions.RemoveAll(
		[PredictionID](const FPredictedInventoryOp& Op) { return Op.PredictionID == PredictionID; });

	RebuildPredictedState();
}
