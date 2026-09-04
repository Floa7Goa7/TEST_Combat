// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "EquipmentItem.h"
#include "EquipmentComponent.generated.h"

class UEquipmentItemDefinition;
class UInventoryComponent;

/**
 * Describes one not-yet-server-confirmed mutation applied to the client-side prediction shadow
 * copy (PredictedEquipment). Mirrors FPredictedInventoryOp (InventoryComponent.h) - see that
 * file for the general prediction-op rationale.
 */
enum class EPredictedEquipmentOp : uint8
{
	Equip,
	Unequip,
};

struct FPredictedEquipmentOp
{
	int32 PredictionID = 0;
	EPredictedEquipmentOp OpType = EPredictedEquipmentOp::Equip;
	FGameplayTag SlotTag;
	FPrimaryAssetId ItemID;
};

/**
 * Replicated, server-authoritative set of equipped items: MainHand, OffHand, Backpack, Helmet
 * (more armor slots to follow - see ValidEquipmentSlots).
 *
 * === Placement, replication, prediction, RPC validation ===
 * All follow exactly the same rules as UInventoryComponent (PlayerState placement so equipment
 * survives death; COND_OwnerOnly replication since another player's gear choices are private
 * the same way their inventory is; predict-then-RPC public API; _Validate only rejects
 * structurally malformed requests, business-rule rejections like "slot occupied" happen in
 * _Implementation via Client_AckPrediction(..., false)). See InventoryComponent.h's class
 * comment for the full explanation of each of those - it is not repeated here.
 *
 * === What is specific to equipment ===
 * - Slots are addressed by FGameplayTag (ValidEquipmentSlots), not a fixed int32 range, so new
 *   slots (Chest, Legs, Boots, ...) are content additions, not code changes - see EquipmentTags.h.
 * - A two-handed weapon equipped to MainHand blocks OffHand without OffHand getting its own
 *   FEquipmentEntry - see IsSlotBlocked. Equipping into a blocked/occupied-conflicting slot is
 *   an explicit rejection, never an implicit auto-unequip of whatever's in the way.
 * - Equip/unequip moves an item between this component and a sibling UInventoryComponent (also
 *   on the PlayerState, resolved once in BeginPlay). That sibling grants this class friend
 *   access (see InventoryComponent.h) specifically so:
 *     - Server_EquipItem_Implementation/Server_UnequipItem_Implementation can move the item
 *       between InventoryList.Items and this component's EquipmentList directly, reusing the
 *       inventory's own Server_TryBeginSlotOp/FindFirstFreeSlot/NotifySlotReplicated.
 *     - Client-side prediction can push a matching FPredictedInventoryOp (Remove for equip,
 *       InsertAt for unequip, plus CapacityChange for the backpack slot) onto the sibling's
 *       PendingPredictions under the SAME PredictionID this component allocated, and later
 *       resolve it there too via UInventoryComponent::ResolvePredictedOp. One PredictionID can
 *       therefore have a pending entry in both components at once; both must be resolved
 *       together, which is what Client_AckPrediction_Implementation here does.
 * - Equipping/unequipping the Backpack slot additionally writes
 *   UInventoryComponent::BackpackBonusSlots (authority) and queues a CapacityChange predicted
 *   op (client). Unequipping it is refused (business rule, not a validate failure) if any
 *   inventory item currently occupies a slot that would fall out of range - see
 *   UInventoryComponent::Authority_CanShrinkTo.
 */
UCLASS(ClassGroup = (Inventory), meta = (BlueprintSpawnableComponent))
class TEST_COMBAT_API UEquipmentComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UEquipmentComponent();

	//~ Begin UActorComponent interface
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End UActorComponent interface

	// ==================== Config ====================

	// Every slot tag this component will accept. Defaults to the four built-in slots (see
	// EquipmentTags.h); extend this (in a Blueprint subclass' defaults, or per-instance on the
	// PlayerState) to add Chest/Legs/Boots/etc. later without touching this class.
	UPROPERTY(EditDefaultsOnly, Category = "Equipment")
	TArray<FGameplayTag> ValidEquipmentSlots;

	UPROPERTY(EditDefaultsOnly, Category = "Equipment")
	FGameplayTag MainHandSlotTag;

	UPROPERTY(EditDefaultsOnly, Category = "Equipment")
	FGameplayTag OffHandSlotTag;

	UPROPERTY(EditDefaultsOnly, Category = "Equipment")
	FGameplayTag BackpackSlotTag;

	// ==================== Public API (client- and server-safe entry points) ====================
	// Same predict-then-RPC shape as UInventoryComponent's public API - see that class' header
	// comment ("Client-side prediction flow") for the general mechanics.

	// Moves whatever occupies SourceInventorySlot (on the sibling UInventoryComponent) into
	// TargetSlotTag. If TargetSlotTag is already occupied, the two items swap: the previously
	// equipped item goes back into SourceInventorySlot.
	UFUNCTION(BlueprintCallable, Category = "Equipment")
	bool EquipItem(int32 SourceInventorySlot, FGameplayTag TargetSlotTag);

	// Moves whatever occupies SlotTag back into DestInventorySlot (must be empty). Symmetric
	// with EquipItem: the destination inventory slot is UI-known ahead of time, the same way
	// UInventoryComponent::MoveItem already knows both its source and destination slots.
	UFUNCTION(BlueprintCallable, Category = "Equipment")
	bool UnequipItem(FGameplayTag SlotTag, int32 DestInventorySlot);

	UFUNCTION(BlueprintPure, Category = "Equipment")
	const TArray<FEquipmentEntry>& GetPredictedEquipment() const { return PredictedEquipment; }

	UFUNCTION(BlueprintPure, Category = "Equipment")
	const TArray<FEquipmentEntry>& GetAuthoritativeEquipment() const { return EquipmentList.Entries; }

	UFUNCTION(BlueprintPure, Category = "Equipment")
	bool FindPredictedItemInSlot(FGameplayTag SlotTag, FEquipmentEntry& OutEntry) const;

	// True for OffHand while a two-handed weapon occupies MainHand in the PREDICTED view (the
	// view UI should be asking this against, same rationale as GetPredictedItems() elsewhere).
	UFUNCTION(BlueprintPure, Category = "Equipment")
	bool IsSlotBlocked(FGameplayTag SlotTag) const;

	// ==================== Persistence ====================

	UFUNCTION(BlueprintCallable, Category = "Equipment|Persistence")
	FEquipmentSaveData BuildSaveData() const;

	UFUNCTION(BlueprintCallable, Category = "Equipment|Persistence")
	void LoadSaveData(const FEquipmentSaveData& SaveData);

	// ==================== UI delegates ====================

	UPROPERTY(BlueprintAssignable, Category = "Equipment")
	FOnEquipmentChanged OnEquipmentChanged;

	UPROPERTY(BlueprintAssignable, Category = "Equipment")
	FOnEquipmentChanged OnPredictedEquipmentChanged;

	// Called directly by this component's own Server_* RPC implementations after EquipmentList
	// has ALREADY been mutated in place - safe to synchronously rebuild predicted state from
	// here. Public only for the same cross-translation-unit reason as
	// UInventoryComponent::NotifySlotReplicated; treat as internal.
	void NotifySlotReplicated(FGameplayTag SlotTag, bool bWasRemoved);

	// Called by FEquipmentList's FastArraySerializer callbacks (a machine RECEIVING a
	// replicated delta, i.e. a client) - see
	// UInventoryComponent::NotifyReplicatedDeltaReceived's comment for exactly why this can't
	// just call NotifySlotReplicated: FFastArraySerializer::PreReplicatedRemove fires before
	// the array's actual element removal, so rebuilding predicted state synchronously from here
	// risks snapshotting a still-stale EquipmentList. Broadcasts OnEquipmentChanged immediately
	// (accurate regardless) and defers the predicted-state rebuild to the next tick.
	void NotifyReplicatedDeltaReceived(FGameplayTag SlotTag, bool bWasRemoved);

protected:
	UPROPERTY(EditDefaultsOnly, Category = "Equipment")
	float MinSlotOperationInterval = 0.1f;

	UPROPERTY(Replicated)
	FEquipmentList EquipmentList;

	UFUNCTION(Server, Reliable, WithValidation)
	void Server_EquipItem(int32 SourceInventorySlot, FGameplayTag TargetSlotTag, int32 PredictionID);

	UFUNCTION(Server, Reliable, WithValidation)
	void Server_UnequipItem(FGameplayTag SlotTag, int32 DestInventorySlot, int32 PredictionID);

	UFUNCTION(Client, Reliable)
	void Client_AckPrediction(int32 PredictionID, bool bSuccess);

private:
	// Cached once in BeginPlay: the sibling UInventoryComponent this equipment moves items to
	// and from. Both components are expected on the same owning Actor (PlayerState).
	UPROPERTY()
	UInventoryComponent* InventoryComponent = nullptr;

	TArray<FEquipmentEntry> PredictedEquipment;
	TArray<FPredictedEquipmentOp> PendingPredictions;
	int32 NextPredictionID = 1;

	// See UInventoryComponent's identical pair for the rationale.
	bool bPredictedRebuildPending = false;
	void ExecuteDeferredPredictedRebuild();

	bool ShouldPredictLocally() const;
	int32 AllocatePredictionID() { return NextPredictionID++; }
	bool IsSlotPendingPrediction(FGameplayTag SlotTag) const;
	void ApplyPredictedOpToArray(TArray<FEquipmentEntry>& Entries, const FPredictedEquipmentOp& Op) const;
	void RebuildPredictedState();
	void ResolvePredictedOp(int32 PredictionID);

	TSet<FGameplayTag> SlotsWithPendingServerOperation;
	TMap<FGameplayTag, double> LastSlotOperationServerTime;
	bool Server_TryBeginSlotOp(FGameplayTag SlotTag);
	void Server_EndSlotOp(FGameplayTag SlotTag);

	UEquipmentItemDefinition* ResolveEquipmentDefinition(const FPrimaryAssetId& ItemID) const;

	// The pawn possessing this equipment's owning PlayerState, if any, else the owning
	// PlayerState itself - same "prefer the pawn" resolution UInventoryComponent uses for
	// UseItem/DropItem, passed to UEquipmentItemDefinition::OnEquipped/OnUnequipped as Wearer.
	AActor* ResolveWearerActor() const;

	// True iff SlotTag == OffHandSlotTag and MainHand currently (authoritatively) holds a
	// two-handed item. Authority-side counterpart to the public, predicted-view IsSlotBlocked.
	bool Authority_IsSlotBlocked(FGameplayTag SlotTag) const;

	friend struct FEquipmentList;
};
