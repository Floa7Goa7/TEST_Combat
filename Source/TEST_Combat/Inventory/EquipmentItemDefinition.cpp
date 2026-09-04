// Fill out your copyright notice in the Description page of Project Settings.

#include "EquipmentItemDefinition.h"

void UEquipmentItemDefinition::OnEquipped_Implementation(AActor* Wearer, UEquipmentComponent* OwningEquipment)
{
	// No stat/ability effects yet - see the class comment. Subclasses (or a Blueprint override
	// of this event) add GAS GameplayEffect application, mesh attachment, etc. here.
}

void UEquipmentItemDefinition::OnUnequipped_Implementation(AActor* Wearer, UEquipmentComponent* OwningEquipment)
{
}
