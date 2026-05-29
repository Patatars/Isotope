#include "AsyncAction_CreateLobby1.h"
#include "LobbyBackendSubsystem.h"
#include "Engine/GameInstance.h"

UAsyncAction_CreateLobby1* UAsyncAction_CreateLobby1::CreateBackendLobby(UObject* WorldContextObject, const FString& EosLobbyId, const FString& HostEosId, int32 MaxPlayers)
{
    UAsyncAction_CreateLobby1* Node = NewObject<UAsyncAction_CreateLobby1>();
    Node->WorldContext = WorldContextObject;
    Node->EosLobbyId = EosLobbyId;
    Node->HostEosId = HostEosId;
    Node->MaxPlayers = MaxPlayers;

    // Обязательно регистрируем, чтобы сборщик мусора не убил ноду до ответа сервера
    Node->RegisterWithGameInstance(WorldContextObject);
    return Node;
}

void UAsyncAction_CreateLobby1::Activate()
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

    // Вызываем нашу сабсистему!
    Backend->CreateLobby(
        EosLobbyId,
        HostEosId,
        MaxPlayers,
        FOnBackendRequestComplete::CreateUObject(this, &UAsyncAction_CreateLobby1::OnRequestComplete)
    );
}

void UAsyncAction_CreateLobby1::OnRequestComplete(bool bSuccess, const FString& Response)
{
    if (bSuccess)
    {
        OnSuccess.Broadcast(Response);
    }
    else
    {
        OnFailure.Broadcast(Response);
    }

    // Завершаем работу ноды
    SetReadyToDestroy();
}