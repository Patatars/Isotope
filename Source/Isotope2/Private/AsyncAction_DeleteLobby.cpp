#include "AsyncAction_DeleteLobby.h"
#include "LobbyBackendSubsystem.h"
#include "Engine/GameInstance.h"

UAsyncAction_DeleteLobby* UAsyncAction_DeleteLobby::DeleteBackendLobby(UObject* WorldContextObject, const FString& BackendLobbyId)
{
    UAsyncAction_DeleteLobby* Node = NewObject<UAsyncAction_DeleteLobby>();
    Node->WorldContext = WorldContextObject;
    Node->BackendLobbyId = BackendLobbyId;
    Node->RegisterWithGameInstance(WorldContextObject);
    return Node;
}

void UAsyncAction_DeleteLobby::Activate()
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

    Backend->DeleteLobby(BackendLobbyId, FOnBackendRequestComplete::CreateUObject(this, &UAsyncAction_DeleteLobby::OnRequestComplete));
}

void UAsyncAction_DeleteLobby::OnRequestComplete(bool bSuccess, const FString& Response)
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