// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "GameplayTagContainer.h"
#include "AnimNotifyState_WeaponTrace.generated.h"

class UStaticMeshComponent;

/**
 * Placed on a swing/attack Anim Montage to define the active hit-detection window. Each tick
 * while playing, sphere-traces from TraceStartSocket to TraceEndSocket on the named weapon mesh
 * component (found on the animated actor by component name - see WeaponMeshComponentName) and
 * sends HitEventTag as a Gameplay Event to the SWINGING actor's own ASC for each newly-hit actor,
 * carrying the hit actor as the event's Target. GA_SwordSwing (or any melee ability waiting on
 * HitEventTag via UAbilityTask_WaitGameplayEvent) reacts to that rather than this notify state
 * applying damage itself - keeps hit-detection (an animation-timing concern) decoupled from
 * damage application (a GAS/ability concern), and lets one notify state be reused by any melee
 * ability that cares about HitEventTag.
 *
 * Authority-only: mirrors this project's Inventory/Equipment convention of never trusting a
 * client to report its own hits (see UEquipmentComponent's class comment) - NotifyTick no-ops
 * unless the animated actor HasAuthority(). GAS's own montage replication means the server plays
 * this same montage (and therefore ticks this same notify) for every character, including a
 * remote client's pawn, so this does not require any extra plumbing to work correctly for
 * non-host players.
 *
 * De-dupes hits per swing: an actor already hit during this NotifyState's active window is not
 * hit again until NotifyBegin resets the tracked set (i.e. the next time the notify plays).
 */
UCLASS()
class TEST_COMBAT_API UAnimNotifyState_WeaponTrace : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	UAnimNotifyState_WeaponTrace();

	// Name of the StaticMeshComponent on the animated actor to trace against (e.g. "WeaponMesh" -
	// see the equipment visual-attach component of the same name on BP_ThirdPersonCharacter).
	UPROPERTY(EditAnywhere, Category = "Weapon Trace")
	FName WeaponMeshComponentName = TEXT("WeaponMesh");

	// Socket on the weapon mesh the trace starts from (nearer the hilt). Falls back to the
	// component's own location if the mesh has no such socket.
	UPROPERTY(EditAnywhere, Category = "Weapon Trace")
	FName TraceStartSocket = TEXT("BladeStart");

	// Socket on the weapon mesh the trace ends at (the tip). Falls back to tracing from the start
	// point extended forward by FallbackBladeLength if the mesh has no such socket.
	UPROPERTY(EditAnywhere, Category = "Weapon Trace")
	FName TraceEndSocket = TEXT("BladeEnd");

	UPROPERTY(EditAnywhere, Category = "Weapon Trace", meta = (ClampMin = "0"))
	float FallbackBladeLength = 90.0f;

	UPROPERTY(EditAnywhere, Category = "Weapon Trace", meta = (ClampMin = "0"))
	float TraceRadius = 8.0f;

	UPROPERTY(EditAnywhere, Category = "Weapon Trace")
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Pawn;

	UPROPERTY(EditAnywhere, Category = "Weapon Trace")
	FGameplayTag HitEventTag;

	UPROPERTY(EditAnywhere, Category = "Weapon Trace")
	bool bDrawDebugTrace = false;

	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyTick(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float FrameDeltaTime, const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;

private:
	// Per-animating-mesh state, since this UAnimNotifyState instance (the CDO placed in the
	// montage asset) is shared across every actor/instance playing it. Keyed by the mesh
	// component actually playing the montage, reset in NotifyBegin and cleared in NotifyEnd.
	TMap<TWeakObjectPtr<USkeletalMeshComponent>, TArray<TWeakObjectPtr<AActor>>> HitActorsThisSwing;

	UStaticMeshComponent* FindWeaponMesh(AActor* Owner) const;
};
