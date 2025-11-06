// ControllerOverrideOnRep.cpp
#include "ControllerOverrideOnRep.h"
#include "GameFramework/PlayerState.h"
#include "Engine/Engine.h"

ACharacterControllerOverrideOnRep::ACharacterControllerOverrideOnRep()
{
    bReplicates = true;
}

void ACharacterControllerOverrideOnRep::BeginPlay()
{
    Super::BeginPlay();
}

void ACharacterControllerOverrideOnRep::OnRep_PlayerState()
{
    Super::OnRep_PlayerState();

    // Клиент вызывает событие
    OnReplicatedPlayerState();

    // Если клиент - владелец и не сервер — вызываем RPC
    if (IsLocalController() && !HasAuthority())
    {
        Server_NotifyPlayerStateInitialized();
    }
}

void ACharacterControllerOverrideOnRep::OnReplicatedPlayerState_Implementation()
{
    UE_LOG(LogTemp, Log, TEXT("OnReplicatedPlayerState called on %s Role=%d"), *GetName(), (int32)GetLocalRole());
    // Общая логика, доступная в BP
}

bool ACharacterControllerOverrideOnRep::Server_NotifyPlayerStateInitialized_Validate()
{
    return true;
}

void ACharacterControllerOverrideOnRep::Server_NotifyPlayerStateInitialized_Implementation()
{
    // Сервер вызывает то же событие
    OnReplicatedPlayerState();
}