// IsotopePartyManager.cpp
#include "IsotopePartyManager.h"
#include "Engine/World.h"

UWorld* UIsotopePartyManager::GetWorld() const
{
    // Если это дефолтный класс движка или у него нет родителя — мира нет
    if (HasAnyFlags(RF_ClassDefaultObject) || !GetOuter())
    {
        return nullptr;
    }

    // А вот тут магия: раз Outer = GameInstance, мы берем мир у него!
    return GetOuter()->GetWorld();
}

void UIsotopePartyManager::SendPartyInvite_Implementation()
{

}

void UIsotopePartyManager::StartAutologin_Implementation()
{
    // Базовая реализация (можно оставить пустой, переопределим в BP)
}

void UIsotopePartyManager::CreatePartyLobby_Implementation()
{
}

void UIsotopePartyManager::LeavePartyLobby_Implementation()
{
}

void UIsotopePartyManager::MatchmakingStart_Implementation()
{
}

void UIsotopePartyManager::MatchmakingCancel_Implementation()
{
}

FString UIsotopePartyManager::GetPartyConnectString_Implementation()
{
    return TEXT("");
}

bool UIsotopePartyManager::IsPartyLeader_Implementation()
{
    return true; // По умолчанию соло-игрок всегда лидер своего пати
}