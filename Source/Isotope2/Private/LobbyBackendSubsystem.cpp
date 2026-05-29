#include "LobbyBackendSubsystem.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "Engine/NetDriver.h"
void ULobbyBackendSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    ReloadConfig();

    UE_LOG(LogTemp, Log, TEXT("LobbyBackendSubsystem initialized"));
}

void ULobbyBackendSubsystem::Deinitialize()
{
    StopAutoPolling();
    if (!CurrentPollingLobbyId.IsEmpty()) CancelMatchmaking(CurrentPollingLobbyId, {});
    Super::Deinitialize();
}

// ... 
// [ ����� �������� ��� ��������� ���� ������� CreateLobby, GetLobby, DeleteLobby, SendRequest, OnResponseReceived ]
// ...

void ULobbyBackendSubsystem::CreateLobby(const FString& EosLobbyId, const FString& HostEosId, int32 MaxPlayers, FOnBackendRequestComplete OnComplete)
{
    TSharedPtr<FJsonObject> Json = MakeShareable(new FJsonObject);
    Json->SetStringField("eos_lobby_id", EosLobbyId);
    Json->SetStringField("host_eos_id", HostEosId);
    Json->SetNumberField("max_players", MaxPlayers);
    Json->SetStringField("state", "waiting");

    FString Body;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Body);
    FJsonSerializer::Serialize(Json.ToSharedRef(), Writer);

    SendRequest("POST", "/lobbies", Body, OnComplete);
}

void ULobbyBackendSubsystem::GetLobby(const FString& BackendLobbyId, FOnBackendRequestComplete OnComplete)
{
    SendRequest("GET", "/lobbies/" + BackendLobbyId, "", OnComplete);
}

void ULobbyBackendSubsystem::DeleteLobby(const FString& BackendLobbyId, FOnBackendRequestComplete OnComplete)
{
    SendRequest("DELETE", "/lobbies/" + BackendLobbyId, "", OnComplete);
}

void ULobbyBackendSubsystem::SendRequest(const FString& Verb, const FString& Endpoint, const FString& Body, FOnBackendRequestComplete OnComplete)
{
    TSharedRef<IHttpRequest> Request = FHttpModule::Get().CreateRequest();

    Request->SetURL(BaseUrl + Endpoint);
    Request->SetVerb(Verb);
    Request->SetHeader("Content-Type", "application/json");

    if (!AuthToken.IsEmpty())
    {
        Request->SetHeader("X-API-Token",  AuthToken);
    }

    if (!Body.IsEmpty())
    {
        Request->SetContentAsString(Body);
    }

    Request->OnProcessRequestComplete().BindUObject(this, &ULobbyBackendSubsystem::OnResponseReceived, OnComplete);
    Request->ProcessRequest();
}

void ULobbyBackendSubsystem::OnResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSuccess, FOnBackendRequestComplete OnComplete)
{
    if (!bSuccess || !Response.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("Backend request failed: no response"));
        OnComplete.ExecuteIfBound(false, "");
        return;
    }

    const int32 Code = Response->GetResponseCode();
    const FString ResponseStr = Response->GetContentAsString();
    const bool bOk = Code >= 200 && Code < 300;

    OnComplete.ExecuteIfBound(bOk, ResponseStr);
}

// ---------------- MATCHMAKING ----------------

void ULobbyBackendSubsystem::StartMatchmaking(const FString& LobbyId, const TArray<FString>& PlayerIds, EMatchFormat MatchFormat, FOnMatchmakingQueueResponse OnQueueResponse, FOnMatchFoundDelegate OnMatchFound)
{
    TSharedPtr<FJsonObject> Json = MakeShareable(new FJsonObject);
    Json->SetStringField("lobby_id", LobbyId);

    TArray<TSharedPtr<FJsonValue>> PlayerIdsArray;
    for (const FString& PlayerId : PlayerIds)
    {
        PlayerIdsArray.Add(MakeShareable(new FJsonValueString(PlayerId)));
    }
    Json->SetArrayField("player_ids", PlayerIdsArray);


    FString MatchFormatStr = StaticEnum<EMatchFormat>()->GetNameStringByValue((int64)MatchFormat).ToLower();
    Json->SetStringField("match_format", MatchFormatStr);

    FString Body;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Body);
    FJsonSerializer::Serialize(Json.ToSharedRef(), Writer);

    CurrentMatchFoundCallback = OnMatchFound;

    SendMatchmakingRequest("POST", "/matchmaking/queue", Body,
        FOnMatchmakingInternal::CreateLambda([this, LobbyId, OnQueueResponse](bool bSuccess, const FString& Response)
        {
            // Вызываем callback на ответ от бэка
            OnQueueResponse.ExecuteIfBound(bSuccess, Response);

            if (bSuccess)
            {
                // Запрос успешно отправлен, сохраняем lobby_id и начинаем автоматический поллинг
                CurrentPollingLobbyId = LobbyId;
                StartAutoPolling(CurrentPollingLobbyId);
                UE_LOG(LogTemp, Log, TEXT("Matchmaking started, polling for match..."));
            }
            else
            {
                // Ошибка при добавлении в очередь - очищаем callback
                UE_LOG(LogTemp, Error, TEXT("Failed to join matchmaking queue: %s"), *Response);
                CurrentMatchFoundCallback.Unbind();
            }
        }));
}

void ULobbyBackendSubsystem::StartCustomGame(const FString& LobbyId, const TArray<FString>& Team1PlayerIds, const TArray<FString>& Team2PlayerIds, FOnMatchmakingQueueResponse OnQueueResponse, FOnMatchFoundDelegate OnMatchFound)
{
    TSharedPtr<FJsonObject> Json = MakeShareable(new FJsonObject);
    Json->SetStringField("lobby_id", LobbyId);

    // Формируем массив команд
    TArray<TSharedPtr<FJsonValue>> TeamsArray;

    // Team 1
    TArray<TSharedPtr<FJsonValue>> Team1Array;
    for (const FString& PlayerId : Team1PlayerIds)
    {
        Team1Array.Add(MakeShareable(new FJsonValueString(PlayerId)));
    }
    TeamsArray.Add(MakeShareable(new FJsonValueArray(Team1Array)));

    // Team 2
    TArray<TSharedPtr<FJsonValue>> Team2Array;
    for (const FString& PlayerId : Team2PlayerIds)
    {
        Team2Array.Add(MakeShareable(new FJsonValueString(PlayerId)));
    }
    TeamsArray.Add(MakeShareable(new FJsonValueArray(Team2Array)));

    Json->SetArrayField("teams", TeamsArray);

    FString Body;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Body);
    FJsonSerializer::Serialize(Json.ToSharedRef(), Writer);

    // Сохраняем callback для вызова после поллинга
    CurrentMatchFoundCallback = OnMatchFound;

    SendMatchmakingRequest("POST", "/custom/start", Body,
        FOnMatchmakingInternal::CreateLambda([this, LobbyId, OnQueueResponse](bool bSuccess, const FString& Response)
        {
            // Вызываем callback на ответ от бэка
            OnQueueResponse.ExecuteIfBound(bSuccess, Response);

            if (bSuccess)
            {
                // Запрос успешно отправлен, сохраняем lobby_id и начинаем автоматический поллинг
                CurrentPollingLobbyId = LobbyId;
                StartAutoPolling(CurrentPollingLobbyId);
                UE_LOG(LogTemp, Log, TEXT("Custom game started, polling for server..."));
            }
            else
            {
                // Ошибка при старте игры
                UE_LOG(LogTemp, Error, TEXT("Failed to start custom game: %s"), *Response);
                CurrentMatchFoundCallback.Unbind();
            }
        }));
}

void ULobbyBackendSubsystem::CancelMatchmaking(const FString& LobbyId, FOnMatchmakingQueueResponse OnComplete)
{
    TSharedPtr<FJsonObject> Json = MakeShareable(new FJsonObject);
    Json->SetStringField("lobby_id", LobbyId);

    FString Body;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Body);
    FJsonSerializer::Serialize(Json.ToSharedRef(), Writer);

    // Останавливаем поллинг
    StopAutoPolling();

    // Очищаем сохранённый callback
    CurrentMatchFoundCallback.Unbind();

    SendMatchmakingRequest("POST", "/matchmaking/cancel", Body,
        FOnMatchmakingInternal::CreateLambda([OnComplete](bool bSuccess, const FString& Response)
        {
            OnComplete.ExecuteIfBound(bSuccess, Response);
        }));
    CurrentPollingLobbyId.Empty();
}

void ULobbyBackendSubsystem::SendMatchmakingRequest(const FString& Verb, const FString& Endpoint, const FString& Body, FOnMatchmakingInternal OnComplete)
{
    TSharedRef<IHttpRequest> Request = FHttpModule::Get().CreateRequest();

    Request->SetURL(BaseUrl + Endpoint);
    Request->SetVerb(Verb);
    Request->SetHeader("Content-Type", "application/json");

    if (!AuthToken.IsEmpty())
    {
        Request->SetHeader("X-API-Token", AuthToken);
    }

    if (!Body.IsEmpty())
    {
        Request->SetContentAsString(Body);
    }

    Request->OnProcessRequestComplete().BindUObject(this, &ULobbyBackendSubsystem::OnMatchmakingResponseReceived, OnComplete);
    Request->ProcessRequest();
}

void ULobbyBackendSubsystem::OnMatchmakingResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSuccess, FOnMatchmakingInternal OnComplete)
{
    if (!bSuccess || !Response.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("Matchmaking request failed: no response"));
        OnComplete.ExecuteIfBound(false, "");
        return;
    }

    const int32 Code = Response->GetResponseCode();
    const FString ResponseStr = Response->GetContentAsString();
    const bool bOk = Code >= 200 && Code < 300;

    OnComplete.ExecuteIfBound(bOk, ResponseStr);
}

// ---------------- AUTO POLLING ----------------

void ULobbyBackendSubsystem::StartAutoPolling(const FString& LobbyId)
{
    // ���� ������� ��� ����, ��������� ������
    StopAutoPolling();
    UWorld* World = GetWorld();
    if (World)
    {
        // ��������� ������, ������� ����� �������� �������� DoPollTick()
        World->GetTimerManager().SetTimer(PollingTimerHandle, this, &ULobbyBackendSubsystem::DoPollTick, PollingInterval, true, 0);
        UE_LOG(LogTemp, Log, TEXT("Started auto-polling matchmaking status for LobbyId: %s"), *LobbyId);

        // �����������: ����� ������� ������ ���, �� ��������� ���������
        //DoPollTick();
    }
}

void ULobbyBackendSubsystem::StopAutoPolling()
{
    UWorld* World = GetWorld();
    if (World && PollingTimerHandle.IsValid())
    {
        World->GetTimerManager().ClearTimer(PollingTimerHandle);
        PollingTimerHandle.Invalidate();
        UE_LOG(LogTemp, Log, TEXT("Stopped auto-polling matchmaking status."));
    }
}

void ULobbyBackendSubsystem::DoPollTick()
{
    if (CurrentPollingLobbyId.IsEmpty())
    {
        StopAutoPolling();
        return;
    }

    TSharedRef<IHttpRequest> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(BaseUrl + "/matchmaking/status/" + CurrentPollingLobbyId);
    Request->SetVerb("GET");
    Request->SetHeader("Content-Type", "application/json");

    if (!AuthToken.IsEmpty())
    {
        Request->SetHeader("X-API-Token", AuthToken);
    }

    Request->OnProcessRequestComplete().BindUObject(this, &ULobbyBackendSubsystem::OnPollResponseReceived);
    Request->ProcessRequest();
}

void ULobbyBackendSubsystem::OnPollResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSuccess)
{
    if (!bSuccess || !Response.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("Polling request failed: no response"));
        return;
    }

    const int32 Code = Response->GetResponseCode();
    const FString ResponseStr = Response->GetContentAsString();
    const bool bOk = Code >= 200 && Code < 300;

    if (bOk)
    {
        TSharedPtr<FJsonObject> JsonObject;
        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseStr);

        if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
        {
            FString StatusStr;
            if (JsonObject->TryGetStringField(TEXT("status"), StatusStr))
            {
                if (StatusStr == "found" || StatusStr == "matched" || StatusStr == "ready")
                {
                    // Матч найден!
                    UE_LOG(LogTemp, Log, TEXT("Match found! Stopping polling."));
                    StopAutoPolling();

                    // Парсим данные матча
                    FMatchData MatchData = ParseMatchData(ResponseStr);

                    // Вызываем сохранённый callback
                    CurrentMatchFoundCallback.ExecuteIfBound(true, MatchData);
                    CurrentMatchFoundCallback.Unbind();

                    // Также вызываем event для Blueprint
                    OnMatchFoundEvent.Broadcast(true, MatchData);
                }
                else if (StatusStr == "failed" || StatusStr == "error" || StatusStr == "cancelled")
                {
                    // Ошибка/отмена
                    UE_LOG(LogTemp, Warning, TEXT("Matchmaking failed/cancelled on server. Stopping polling."));
                    StopAutoPolling();

                    FMatchData EmptyMatchData;
                    EmptyMatchData.AdditionalData = ResponseStr;

                    // Вызываем callback с ошибкой
                    CurrentMatchFoundCallback.ExecuteIfBound(false, EmptyMatchData);
                    CurrentMatchFoundCallback.Unbind();

                    OnMatchFoundEvent.Broadcast(false, EmptyMatchData);
                }
                // Иначе статус "waiting" - продолжаем поллинг
            }
        }
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Polling request returned error code %d"), Code);
        if (Code == 404 || Code == 401 || Code == 403)
        {
            StopAutoPolling();

            FMatchData EmptyMatchData;
            EmptyMatchData.AdditionalData = ResponseStr;

            CurrentMatchFoundCallback.ExecuteIfBound(false, EmptyMatchData);
            CurrentMatchFoundCallback.Unbind();
            OnMatchFoundEvent.Broadcast(false, EmptyMatchData);
        }
    }
}

void ULobbyBackendSubsystem::ReportServerReady()
{
    UWorld* World = GetWorld();
    UE_LOG(LogTemp, Warning, TEXT("ReportServerReady called NetMode is %d."), static_cast<int32>(World->GetNetMode()));

    if (World && World->GetNetMode() != NM_DedicatedServer)
    {
        UE_LOG(LogTemp, Warning, TEXT("ReportServerReady called but this is NOT a Dedicated Server. Skipping."));
        return;
    }

    FString MatchId;
    // Парсим -MatchId=... из аргументов командной строки, которую передал Server Manager
    if (FParse::Value(FCommandLine::Get(), TEXT("MatchId="), MatchId))
    {
        UE_LOG(LogTemp, Log, TEXT("SERVER: Reporting ready for MatchId: %s"), *MatchId);

        FString Endpoint = "/matchmaking/servers/" + MatchId + "/ready";

        // Используем твою же функцию для отправки запроса на ММ (BaseUrl должен быть http://localhost:8000)
        SendRequest("POST", Endpoint, "", FOnBackendRequestComplete::CreateLambda([](bool bSuccess, const FString& ResponseStr)
            {
                if (bSuccess)
                {
                    UE_LOG(LogTemp, Log, TEXT("SERVER: Matchmaking Service successfully notified. Status is now READY."));
                }
                else
                {
                    UE_LOG(LogTemp, Error, TEXT("SERVER: Failed to notify Matchmaking Service! Response: %s"), *ResponseStr);
                }
            }));
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("SERVER: Critical Error! -MatchId parameter was not found in command line arguments!"));
    }
}

FMatchData ULobbyBackendSubsystem::ParseMatchData(const FString& JsonResponse)
{
    FMatchData MatchData;

    TSharedPtr<FJsonObject> JsonObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonResponse);

    if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
    {
        JsonObject->TryGetStringField(TEXT("match_id"), MatchData.MatchId);
        JsonObject->TryGetStringField(TEXT("server_ip"), MatchData.ServerIP);
        int32 Port = 0;
        if (JsonObject->TryGetNumberField(TEXT("server_port"), Port))
        {
            MatchData.ServerPort = Port;
        }

        // Сохраняем весь JSON как дополнительные данные
        MatchData.AdditionalData = JsonResponse;
    }

    return MatchData;
}