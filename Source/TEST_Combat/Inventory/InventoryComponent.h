// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InventoryItem.h"
#include "InventoryComponent.generated.h"

class UItemDefinition;
struct FStreamableHandle;

/**
 * Describes one not-yet-server-confirmed mutation applied to the client-side prediction
 * shadow copy (see UInventoryComponent::PredictedItems). Plain internal bookkeeping, never
 * replicated or exposed to Blueprint, so it is a plain struct rather than a USTRUCT.
 */
enum class EPredictedInventoryOp : uint8
{
	Add,
	Remove,
	Use,
	Move,
	SplitStack,
	MergeStack,
	Drop,

	// Materializes an item at a specific, already-known slot (unlike Add, which lets the
	// server pick a slot). Used by UEquipmentComponent when predicting an unequip: the UI
	// already knows which empty inventory slot the item is being unequipped into, the same way
	// MoveItem already knows both its source and destination slots.
	InsertAt,

	// Carries a new UInventoryComponent::BackpackBonusSlots value in this op's Quantity field.
	// Used by UEquipmentComponent when predicting an equip/unequip of a backpack, so the
	// inventory grid can resize optimistically instead of waiting on server confirmation.
	CapacityChange,
};

struct FPredictedInventoryOp
{
	int32 PredictionID = 0;
	EPredictedInventoryOp OpType = EPredictedInventoryOp::Add;
	FPrimaryAssetId ItemID;
	int32 SlotA = INDEX_NONE;
	int32 SlotB = INDEX_NONE;
	int32 Quantity = 0;
};

/**
 * Replicated, server-authoritative inventory of fixed slots.
 *
 * === Placement: PlayerState, not Pawn ===
 * This component is meant to be added to APlayerState, not APawn/ACharacter. Tradeoffs:
 *   - PlayerState persists across a player's pawn being destroyed and respawned (and across
 *     seamless travel within a session), so the inventory - and any client-side prediction
 *     state built up for it - survives death instead of being destroyed with the pawn and
 *     having to be rebuilt/re-replicated from scratch.
 *   - PlayerState already replicates to every client (for scoreboards etc.), which is
 *     convenient for multiplayer UI, but means inventory contents would be sent to clients
 *     who have no business seeing another player's items unless replication is explicitly
 *     narrowed - see GetLifetimeReplicatedProps, which conditions InventoryList on
 *     COND_OwnerOnly for exactly this reason.
 *   - A pawn-hosted inventory would be simpler to reason about for single-life games (no
 *     "which PlayerState owns this pawn right now" indirection) and would get relevancy/
 *     dormancy tuning "for free" from the pawn's existing network settings, but loses
 *     everything above on death. For a respawn-based multiplayer game, PlayerState is the
 *     correct home.
 *
 * === Relevancy / dormancy note (only applies if reused off PlayerState) ===
 * A component has no network relevancy of its own; it inherits whatever its owning Actor
 * has. PlayerState is always relevant to every client by engine default, so that is not a
 * concern here. If this component is ever reused on a world Actor instead (a loot chest,
 * an NPC corpse with lootable inventory), that actor's relevancy/dormancy settings become
 * this inventory's relevancy/dormancy settings - e.g. give it a sensible
 * NetCullDistanceSquared and consider NetDormancy (DORM_DormantAll, flushed with
 * FlushNetDormancy on interaction) so distant containers do not consume server CPU and
 * bandwidth replicating to clients who cannot reach them, unlike the always-relevant
 * PlayerState case this component was designed against.
 *
 * === Replication flow ===
 * See FInventoryList's comment in InventoryItem.h for the FastArraySerializer delta-replication
 * mechanics. Short version: server mutates InventoryList.Items and calls MarkItemDirty /
 * MarkArrayDirty; clients receive a delta and the engine invokes
 * PostReplicatedAdd/Change/PreReplicatedRemove on FInventoryList, which forward to
 * NotifySlotReplicated() below to broadcast OnInventoryChanged and (owning client only)
 * reconcile prediction.
 *
 * === Client-side prediction flow ===
 * FastArraySerializer callbacks only fire once the server's change has round-tripped back
 * to the client, which is too slow for responsive UI (drag-drop, use-item feedback, etc.).
 * To cover that gap, the public API (AddItem/RemoveItem/UseItem/MoveItem/SplitStack/
 * MergeStack/DropItem) is safe to call on the owning client: it applies an optimistic,
 * client-only change to PredictedItems immediately (broadcasting
 * OnPredictedInventoryChanged), tags the change with a PredictionID, and then fires the
 * matching Server_* RPC carrying that same PredictionID.
 *   - AUTHORITATIVE STATE lives only in InventoryList.Items and only the server ever
 *     mutates it directly.
 *   - PREDICTED STATE lives only in PredictedItems and only the owning client ever mutates
 *     it directly; it exists purely so that client's UI has something to draw immediately.
 * The server replies with Client_AckPrediction(PredictionID, bSuccess). Success just drops
 * the pending op (the real replication will confirm the same result shortly). Failure - or
 * any fresh authoritative replication arriving at all - triggers RebuildPredictedState(),
 * which resets PredictedItems to a copy of the last known authoritative InventoryList.Items
 * and replays every still-pending predicted op on top, in order. This makes reconciliation
 * self-correcting (it does not depend on acks and replication arriving in a particular
 * order) rather than trying to hand-roll an exact inverse for every op type.
 *
 * === RPC validation / anti-spam flow ===
 * Every mutation is server-only: the public API never touches InventoryList.Items directly,
 * only Server_* RPC implementations do. Each Server_* RPC has a real _Validate that rejects
 * structurally bad requests (out-of-range slot indices, non-positive quantities, malformed
 * PredictionIDs) before the request is even dispatched to its _Implementation - a client
 * that sends invalid values is treated as cheating/hacked and the engine closes its
 * connection, so _Validate must never fail for a legitimate, merely-inconvenient request
 * (e.g. "inventory full") - that kind of business-rule rejection happens inside
 * _Implementation instead, which replies with Client_AckPrediction(..., false).
 * Server_TryBeginSlotOp/Server_EndSlotOp additionally guard each slot with a short cooldown
 * (MinSlotOperationInterval) plus a pending-op set, so two Server_UseItem calls fired back
 * to back for the same slot before the first has replicated back to the client (e.g. from a
 * modified client bypassing the local prediction guard, or a future async/channeled item
 * use) cannot both succeed and double-consume the stack.
 */
UCLASS(ClassGroup = (Inventory), meta = (BlueprintSpawnableComponent))
class TEST_COMBAT_API UInventoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UInventoryComponent();

	//~ Begin UActorComponent interface
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End UActorComponent interface

	// ==================== Public API (client- and server-safe entry points) ====================
	// On the owning client these apply an optimistic prediction and then issue the matching
	// Server_* RPC. On the server (or for a locally-authoritative standalone game) they
	// mutate authoritative state directly. Called from Blueprint or from other C++ (input
	// handling, UI widgets, etc.) - callers should not call the Server_* RPCs directly.

	UFUNCTION(BlueprintCallable, Category = "Inventory")
	bool AddItem(FPrimaryAssetId ItemID, int32 Quantity);

	UFUNCTION(BlueprintCallable, Category = "Inventory")
	bool RemoveItem(int32 SlotIndex, int32 Quantity);

	UFUNCTION(BlueprintCallable, Category = "Inventory")
	bool UseItem(int32 SlotIndex);

	UFUNCTION(BlueprintCallable, Category = "Inventory")
	bool MoveItem(int32 SourceSlot, int32 DestSlot);

	UFUNCTION(BlueprintCallable, Category = "Inventory")
	bool SplitStack(int32 SourceSlot, int32 DestSlot, int32 SplitQuantity);

	UFUNCTION(BlueprintCallable, Category = "Inventory")
	bool MergeStack(int32 SourceSlot, int32 DestSlot);

	UFUNCTION(BlueprintCallable, Category = "Inventory")
	bool DropItem(int32 SlotIndex, int32 Quantity);

	// Read-only views for UI. Prefer GetPredictedItems() for the owning client's own
	// inventory widgets (immediate feedback); use GetAuthoritativeItems() for anything that
	// must reflect only server-confirmed truth (e.g. trade/other-player UI).
	UFUNCTION(BlueprintPure, Category = "Inventory")
	const TArray<FInventoryItem>& GetPredictedItems() const { return PredictedItems; }

	UFUNCTION(BlueprintPure, Category = "Inventory")
	const TArray<FInventoryItem>& GetAuthoritativeItems() const { return InventoryList.Items; }

	// Authoritative capacity: the fixed base plus whatever a currently-equipped backpack
	// grants (see BackpackBonusSlots). Drives IsValidSlotIndex/FindFirstFreeSlot, so
	// backpack-granted slots become usable for AddItem etc. immediately.
	UFUNCTION(BlueprintPure, Category = "Inventory")
	int32 GetNumSlots() const { return BaseNumSlots + BackpackBonusSlots; }

	// Optimistic counterpart to GetNumSlots(), for UI that wants the inventory grid to resize
	// the instant a backpack is equipped/unequipped rather than waiting on server confirmation.
	UFUNCTION(BlueprintPure, Category = "Inventory")
	int32 GetPredictedNumSlots() const { return BaseNumSlots + PredictedBackpackBonusSlots; }

	// Convenience for UI: looks up the predicted-view entry occupying SlotIndex, if any.
	// Saves every slot widget's refresh logic from looping GetPredictedItems() itself.
	UFUNCTION(BlueprintPure, Category = "Inventory")
	bool FindPredictedItemInSlot(int32 SlotIndex, FInventoryItem& OutItem) const;

	// Predicted-view counterpart to the private, authority-only FindFirstFreeSlot(): the first
	// slot with no PREDICTED entry, or INDEX_NONE if every slot (up to GetPredictedNumSlots())
	// is predicted-occupied. For equipment UI picking a DestInventorySlot for UnequipItem, so it
	// doesn't hand back a slot another still-pending predicted op already claimed.
	UFUNCTION(BlueprintPure, Category = "Inventory")
	int32 FindFirstFreePredictedSlot() const;

	// ==================== Persistence ====================
	// FInventoryItem/FInventoryList are plain UPROPERTY data (no runtime-only handles or
	// object pointers), so this is a direct copy, not a conversion - the same shape can be
	// written into a USaveGame field or round-tripped through FJsonObjectConverter.

	UFUNCTION(BlueprintCallable, Category = "Inventory|Persistence")
	FInventorySaveData BuildSaveData() const;

	// Authority-only: replaces the authoritative inventory wholesale (e.g. on load from a
	// save file) and marks the whole array dirty so it replicates to clients.
	UFUNCTION(BlueprintCallable, Category = "Inventory|Persistence")
	void LoadSaveData(const FInventorySaveData& SaveData);

	// ==================== UI delegates ====================

	// Server-confirmed changes only. Bind here for anything that must never show a value
	// the server hasn't actually accepted (trade windows, other players' inventories, etc).
	UPROPERTY(BlueprintAssignable, Category = "Inventory")
	FOnInventoryChanged OnInventoryChanged;

	// Optimistic changes on the owning client, fired the instant a predicted op is applied
	// or rolled back. Bind here for the local player's own inventory HUD/widgets.
	UPROPERTY(BlueprintAssignable, Category = "Inventory")
	FOnInventoryChanged OnPredictedInventoryChanged;

	// Called directly by this component's own Server_* RPC implementations (and by
	// UEquipmentComponent via friend access) after InventoryList.Items has ALREADY been mutated
	// in place - safe to synchronously rebuild predicted state from here. NOT called by
	// FInventoryList's replication callbacks - see NotifyReplicatedDeltaReceived for why those
	// need different handling. Public only for the same cross-translation-unit reason as that
	// function; treat both as internal and do not call them from gameplay code.
	void NotifySlotReplicated(int32 SlotIndex, bool bWasRemoved);

	// Called by FInventoryList's FastArraySerializer callbacks (PostReplicatedAdd/Change/
	// PreReplicatedRemove) via the OwnerComponent back-pointer - i.e. only on a machine
	// RECEIVING a replicated delta (a client), never on the machine that produced it.
	//
	// Deliberately NOT the same as NotifySlotReplicated: per Engine's FastArraySerializer.h,
	// FFastArraySerializer::PreReplicatedRemove fires BEFORE the array's actual element removal
	// (that happens much later in the same delta-apply call, and Epic's own doc comment on
	// these callbacks warns the array's contents are not guaranteed up to date within them) -
	// and a removal anywhere in the same delta batch leaves InventoryList.Items transiently
	// stale for the OTHER (add/change) callbacks in that same batch too. Rebuilding
	// PredictedItems synchronously from here would therefore risk snapshotting a stale array
	// (a just-removed item briefly still "present" in the predicted view - the InventoryList
	// really is correct once the whole delta finishes applying, this function just cannot read
	// it mid-delta). OnInventoryChanged is still broadcast immediately - it only relays
	// SlotIndex/bWasRemoved, not the array itself, so it's accurate regardless - but the
	// predicted-state rebuild is deferred to the next tick, by which point the delta has
	// definitely finished applying.
	void NotifyReplicatedDeltaReceived(int32 SlotIndex, bool bWasRemoved);

protected:
	// Number of fixed slots before any backpack bonus. FInventoryItem::SlotIndex must satisfy
	// 0 <= SlotIndex < GetNumSlots().
	UPROPERTY(EditDefaultsOnly, Category = "Inventory")
	int32 BaseNumSlots = 20;

	// Extra slots granted by a currently-equipped backpack (0 if none equipped). Authority-only
	// to write: set by UEquipmentComponent's server RPCs via friend access, never by this
	// component directly. COND_OwnerOnly for the same privacy reason as InventoryList (see
	// GetLifetimeReplicatedProps).
	UPROPERTY(Replicated)
	int32 BackpackBonusSlots = 0;

	// Sanity ceiling on any single Quantity value, independent of a given item's
	// UItemDefinition::MaxStackSize - guards against integer-overflow-style abuse from a
	// modified client before an item definition has even been resolved.
	UPROPERTY(EditDefaultsOnly, Category = "Inventory")
	int32 MaxQuantityPerRequest = 9999;

	// Minimum time between two server-processed operations on the same slot; see
	// Server_TryBeginSlotOp.
	UPROPERTY(EditDefaultsOnly, Category = "Inventory")
	float MinSlotOperationInterval = 0.1f;

	UPROPERTY(Replicated)
	FInventoryList InventoryList;

	// ==================== Server RPCs: the only code allowed to mutate InventoryList.Items ====================
	// PredictionID is client-generated (see AllocatePredictionID) and echoed back via
	// Client_AckPrediction; pass 0 when called from non-predicting contexts (server-side
	// game logic invoking these directly on behalf of an NPC, for example - there is no
	// prediction to reconcile in that case).

	UFUNCTION(Server, Reliable, WithValidation)
	void Server_AddItem(FPrimaryAssetId ItemID, int32 Quantity, int32 PredictionID);

	UFUNCTION(Server, Reliable, WithValidation)
	void Server_RemoveItem(int32 SlotIndex, int32 Quantity, int32 PredictionID);

	UFUNCTION(Server, Reliable, WithValidation)
	void Server_UseItem(int32 SlotIndex, int32 PredictionID);

	UFUNCTION(Server, Reliable, WithValidation)
	void Server_MoveItem(int32 SourceSlot, int32 DestSlot, int32 PredictionID);

	UFUNCTION(Server, Reliable, WithValidation)
	void Server_SplitStack(int32 SourceSlot, int32 DestSlot, int32 SplitQuantity, int32 PredictionID);

	UFUNCTION(Server, Reliable, WithValidation)
	void Server_MergeStack(int32 SourceSlot, int32 DestSlot, int32 PredictionID);

	UFUNCTION(Server, Reliable, WithValidation)
	void Server_DropItem(int32 SlotIndex, int32 Quantity, int32 PredictionID);

	// Reliable client RPC: tells the owning client's prediction layer whether PredictionID
	// succeeded. See the class comment's "Client-side prediction flow" section.
	UFUNCTION(Client, Reliable)
	void Client_AckPrediction(int32 PredictionID, bool bSuccess);

public:
	// Authority-only: true iff no item currently occupies a slot >= NewNumSlots. Used by
	// UEquipmentComponent before shrinking capacity (unequipping a backpack) so items are never
	// silently stranded/destroyed - the unequip is refused instead until those slots are freed.
	UFUNCTION(BlueprintPure, Category = "Inventory")
	bool Authority_CanShrinkTo(int32 NewNumSlots) const;

private:
	// ==================== Client-side prediction state ====================
	// Only ever populated/read on the owning (autonomous proxy / standalone) connection;
	// meaningless on the server or on simulated proxies for other players' PlayerStates.
	TArray<FInventoryItem> PredictedItems;
	TArray<FPredictedInventoryOp> PendingPredictions;
	int32 PredictedBackpackBonusSlots = 0;
	int32 NextPredictionID = 1;

	// Coalesces however many NotifyReplicatedDeltaReceived calls land in one frame (a single
	// delta can touch several slots) into a single deferred RebuildPredictedState() next tick -
	// see that function's comment for why the rebuild can't happen synchronously there.
	bool bPredictedRebuildPending = false;
	void ExecuteDeferredPredictedRebuild();

	// True only for the owning client's autonomous proxy - the one role where a mutation
	// needs a predict-then-RPC round trip. Authority (server, and standalone games, which
	// are locally authoritative) mutates directly; a simulated proxy has no business
	// calling mutating API on an inventory it does not own.
	bool ShouldPredictLocally() const;
	int32 AllocatePredictionID() { return NextPredictionID++; }
	bool IsSlotPendingPrediction(int32 SlotIndex) const;
	void ApplyPredictedOpToArray(TArray<FInventoryItem>& Items, const FPredictedInventoryOp& Op) const;

	// ChangedSlotA/B let a caller that knows exactly which slot(s) its own op touched (Move,
	// SplitStack, MergeStack, Remove, Use, Drop - all take slot indices as parameters already)
	// report that precisely, so bound UI can refresh just those slots instead of every slot in
	// the inventory. Left at INDEX_NONE (the default) for callers that can't pin a slot - AddItem
	// (the server picks the destination slot), and any reconciliation/authoritative-driven
	// rebuild, where the set of predicted items that shifted isn't necessarily just one op's
	// slots - which broadcasts INDEX_NONE to tell UI to refresh broadly, same as before.
	void RebuildPredictedState(int32 ChangedSlotA = INDEX_NONE, int32 ChangedSlotB = INDEX_NONE);

	// ==================== Server-side anti-spam guards ====================
	TSet<int32> SlotsWithPendingServerOperation;
	TMap<int32, double> LastSlotOperationServerTime;
	bool Server_TryBeginSlotOp(int32 SlotIndex);
	void Server_EndSlotOp(int32 SlotIndex);

	// Streamable handles for any async loads (e.g. item icon/definition prefetch) kicked
	// off by this component. Held here purely so EndPlay can cancel them; an in-flight
	// load whose completion lambda captures `this` would otherwise fire after this
	// component (and any UI bound to it) has been destroyed.
	TArray<TSharedPtr<FStreamableHandle>> ActiveStreamingHandles;

	bool IsValidSlotIndex(int32 SlotIndex) const { return SlotIndex >= 0 && SlotIndex < GetNumSlots(); }
	UItemDefinition* ResolveItemDefinition(const FPrimaryAssetId& ItemID) const;
	int32 FindFirstFreeSlot() const;

	// Shared guts of Client_AckPrediction_Implementation: drop the matching pending op (if any)
	// and rebuild predicted state. Factored out so UEquipmentComponent can resolve a
	// cross-component PredictionID against this inventory's PendingPredictions too - see the
	// "CLIENT-SIDE PREDICTION" class comment and UEquipmentComponent::Server_EquipItem for why
	// one PredictionID can have a pending op here as well as in the equipment component.
	void ResolvePredictedOp(int32 PredictionID);

	friend struct FInventoryList;

	// Grants UEquipmentComponent's server RPCs direct access to InventoryList, PendingPredictions,
	// FindFirstFreeSlot/IsValidSlotIndex, Server_TryBeginSlotOp/EndSlotOp, NotifySlotReplicated
	// and RebuildPredictedState, so equipping/unequipping can move an item between the two
	// components' authoritative and predicted state without InventoryComponent growing a
	// parallel public "internal, don't call this" API. See the class comment's "RPC validation"
	// section: this friend is the one other place besides this class' own Server_* RPCs that is
	// allowed to mutate InventoryList.Items, and it does so under the same authority-only rules.
	friend class UEquipmentComponent;
};
