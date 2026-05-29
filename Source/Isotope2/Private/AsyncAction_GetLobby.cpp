#include "AsyncAction_GetLobby.h"
#include "LobbyBackendSubsystem.h"
#include "Engine/GameInstance.h"

UAsyncAction_GetLobby* UAsyncAction_GetLobby::GetBackendLobby(UObject* WorldContextObject, const FString& BackendLobbyId)
{
    UAsyncAction_GetLobby* Node = NewObject<UAsyncAction_GetLobby>();
    Node->WorldContext = WorldContextObject;
    Node->BackendLobbyId = BackendLobbyId;
    Node->RegisterWithGameInstance(WorldContextObject);
    return Node;
}

void UAsyncAction_GetLobby::Activate()
{
    if (!WorldContext || !WorldContext->GetWorld())
    {
        OnFailure.Broadcast("Error: No World Context");
        SetReadyToDestroy();
        return;
    }

    UGameInstance* GI = WorldContext->GetWorld()->GetGameInstance();
    ULobbyBackendSubsystem* Backend = GI ? GI->GetSubsystem<ULobbyBackendSubsystem>() : nullptr;

    if (!Backend)
    {
        OnFailure.Broadcast("Error: Subsystem missing");
        SetReadyToDestroy();
        return;
    }

    Backend->GetLobby(BackendLobbyId, FOnBackendRequestComplete::CreateUObject(this, &UAsyncAction_GetLobby::OnRequestComplete));
}

void UAsyncAction_GetLobby::OnRequestComplete(bool bSuccess, const FString& Response)
{
    if (bSuccess)
    {
        OnSuccess.Broadcast(Response);
    }
    else
    {
        OnFailure.Broadcast(Response);
    }
    SetReadyToDestroy();
}