// Fill out your copyright notice in the Description page of Project Settings.

#include "AnimNotifyState_WeaponTrace.h"
#include "CombatTags.h"
#include "AbilitySystemComponent.h"
#include "GameplayEffectTypes.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"

UAnimNotifyState_WeaponTrace::UAnimNotifyState_WeaponTrace()
{
	HitEventTag = TAG_Event_Weapon_Hit;
}

void UAnimNotifyState_WeaponTrace::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);

	if (MeshComp)
	{
		HitActorsThisSwing.FindOrAdd(MeshComp).Reset();
	}
}

void UAnimNotifyState_WeaponTrace::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);

	if (MeshComp)
	{
		HitActorsThisSwing.Remove(MeshComp);
	}
}

void UAnimNotifyState_WeaponTrace::NotifyTick(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float FrameDeltaTime, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyTick(MeshComp, Animation, FrameDeltaTime, EventReference);

	if (!MeshComp)
	{
		return;
	}

	AActor* Owner = MeshComp->GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return;
	}

	UStaticMeshComponent* WeaponMesh = FindWeaponMesh(Owner);
	if (!WeaponMesh)
	{
		return;
	}

	const FVector Start = WeaponMesh->DoesSocketExist(TraceStartSocket)
		? WeaponMesh->GetSocketLocation(TraceStartSocket)
		: WeaponMesh->GetComponentLocation();

	// The fallback direction deliberately uses the OWNER's forward vector, not WeaponMesh's own
	// (WeaponMesh->GetForwardVector() would seem the obvious choice, but WeaponMesh's rotation is
	// authored purely to make a bladed weapon's own sockets line up visually when equipped -
	// nothing has ever validated what direction its raw forward axis points, since a socketed
	// weapon (the sword) never reaches this branch at all. Exercised for the first time by
	// unarmed punching (no mesh assigned = no sockets = always this branch), it turned out to
	// point back into the character rather than away from the hand. The owning actor's forward
	// vector has no such authoring assumption baked in - it is simply "the direction this
	// character is facing," which is what an unarmed strike should reach towards regardless of
	// whatever WeaponMesh's rotation happens to be.
	const FVector End = WeaponMesh->DoesSocketExist(TraceEndSocket)
		? WeaponMesh->GetSocketLocation(TraceEndSocket)
		: Start + Owner->GetActorForwardVector() * FallbackBladeLength;

	TArray<FHitResult> Hits;
	const FCollisionShape Shape = FCollisionShape::MakeSphere(TraceRadius);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(WeaponTrace), false, Owner);
	Owner->GetWorld()->SweepMultiByChannel(Hits, Start, End, FQuat::Identity, TraceChannel, Shape, Params);

	TArray<TWeakObjectPtr<AActor>>& AlreadyHit = HitActorsThisSwing.FindOrAdd(MeshComp);

	UAbilitySystemComponent* OwnerASC = Owner->FindComponentByClass<UAbilitySystemComponent>();

	for (const FHitResult& Hit : Hits)
	{
		AActor* HitActor = Hit.GetActor();
		if (!HitActor || HitActor == Owner || AlreadyHit.Contains(HitActor))
		{
			continue;
		}
		AlreadyHit.Add(HitActor);

		if (OwnerASC)
		{
			FGameplayEventData EventData;
			EventData.EventTag = HitEventTag;
			EventData.Instigator = Owner;
			EventData.Target = HitActor;
			OwnerASC->HandleGameplayEvent(HitEventTag, &EventData);
		}
	}

	if (bDrawDebugTrace)
	{
		DrawDebugLine(Owner->GetWorld(), Start, End, FColor::Red, false, 0.5f, 0, 1.5f);
	}
}

UStaticMeshComponent* UAnimNotifyState_WeaponTrace::FindWeaponMesh(AActor* Owner) const
{
	if (!Owner)
	{
		return nullptr;
	}

	for (UActorComponent* Component : Owner->GetComponents())
	{
		if (Component && Component->GetFName() == WeaponMeshComponentName)
		{
			return Cast<UStaticMeshComponent>(Component);
		}
	}
	return nullptr;
}
