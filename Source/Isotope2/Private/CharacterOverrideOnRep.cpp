// CharacterOverrideOnRep.cpp
#include "CharacterOverrideOnRep.h"
#include "GameFramework/PlayerState.h"
#include "Engine/Engine.h"

ACharacterOverrideOnRep::ACharacterOverrideOnRep()
{
    PrimaryActorTick.bCanEverTick = false;
}

void ACharacterOverrideOnRep::BeginPlay()
{
    Super::BeginPlay();
}

void ACharacterOverrideOnRep::OnRep_PlayerState()
{
    Super::OnRep_PlayerState();

    // Клиент вызывает событие
    OnReplicatedPlayerState();

    // Если это клиент-овнер, вызываем RPC чтобы уведомить сервер
    if (IsLocallyControlled() && !HasAuthority())
    {
        Server_NotifyPlayerStateInitialized();
    }
}

void ACharacterOverrideOnRep::OnReplicatedPlayerState_Implementation()
{
    UE_LOG(LogTemp, Log, TEXT("OnReplicatedPlayerState called on %s Role=%d"), *GetName(), (int32)GetLocalRole());
    // Логика BP и/или C++, одинаково для сервера и клиента
}

bool ACharacterOverrideOnRep::Server_NotifyPlayerStateInitialized_Validate()
{
    return true; // Добавьте валидацию, если нужно
}

void ACharacterOverrideOnRep::Server_NotifyPlayerStateInitialized_Implementation()
{
    // Сервер вызывает то же событие для общей обработки
    OnReplicatedPlayerState();
}