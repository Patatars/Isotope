#include "IsotopePartySubsystem.h"

#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"
#include "OnlineSubsystemUtils.h"
#include "GameFramework/PlayerState.h"
#include "eos_connect_types.h"
#include "EOSShared.h"
#include "Online/OnlineServices.h" // Для GetServices() и IOnlineServicesPtr
#include "Online/Auth.h"           // Для общего интерфейса Auth
#include "Online/AuthEOS.h" 
#include <Online/OnlineIdEOSGS.h>


DEFINE_LOG_CATEGORY(LogIsotopePartySubsystem);

using namespace UE::Online;

namespace
{

	static EOS_ProductUserId GetProductUserIdFromAccountId(const FAccountId& AccountId)
	{
		return GetProductUserId(AccountId);

	}



	//static FString AccountIdToString(const FAccountId& Id)
	//{
	//	EOS_ProductUserId PUID = GetProductUserId(Id);

	//	char Buffer[EOS_PRODUCTUSERID_MAX_LENGTH + 1];
	//	int32_t BufferLen = sizeof(Buffer);
	//	EOS_ProductUserId_ToString(PUID, Buffer, &BufferLen);

	//	return FString(UTF8_TO_TCHAR(Buffer));
	//}
	static FString LobbyIdToString(const FLobbyId& Id)
	{
		return ToLogString(Id);
	}

	static FString AccountIdToString(const FAccountId& Id)
	{
		return ToLogString(Id);
	}

	static FString AttrIdToString(const FSchemaAttributeId& Id)
	{
		return Id.ToString();
	}

	static FString SchemaIdToString(const FSchemaId& Id)
	{
		return Id.ToString();
	}
}



FString UIsotopePartySubsystem::GetPlayerPUID(APlayerState* PlayerState)
{
	if (!PlayerState) return TEXT("");

	FUniqueNetIdRepl UniqueNetIdRepl = PlayerState->GetUniqueId();
	TSharedPtr<const FUniqueNetId> UniqueNetId = UniqueNetIdRepl.GetUniqueNetId();
	if (!UniqueNetId.IsValid()) return TEXT("");

	// ToString() у EOS даёт строку вида "EOS:0002a4b1c3d2..."
	FString FullId = UniqueNetId->ToString();

	// Убираем префикс "EOS:" если есть
	FString PUID;
	if (FullId.StartsWith(TEXT("EOS:")))
	{
		PUID = FullId.RightChop(4); // убираем "EOS:"
	}
	else
	{
		PUID = FullId;
	}

	return PUID;
}

void UIsotopePartySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UWorld* World = GetWorld();
	if (World && World->GetNetMode() != NM_DedicatedServer)
	{
		InitializeOnlineServices();
		StartPolling();

		// Пробуем восстановить аккаунт из кэша Auth (если пользователь уже логинился).
		ResolveLocalAccountFromAuthCache();

	}
	
}

void UIsotopePartySubsystem::HandleUILobbyJoinRequested(const UE::Online::FUILobbyJoinRequested& EventParams)
{
	// Проверяем, что пришедший результат успешный
	if (EventParams.Result.IsOk())
	{
		// GetOkValue() возвращает TSharedRef<const FLobby> напрямую
		const TSharedRef<const UE::Online::FLobby>& LobbyRef = EventParams.Result.GetOkValue();

		// Получаем LobbyId и переводим его в FString
		const FString LobbyIdStr = LobbyIdToString(LobbyRef->LobbyId);

		// Вызываем блупринт-диспатчер
		OnLobbyInviteAccepted.Broadcast(LobbyIdStr);

		// Автоматически джойнимся в лобби
		if (!LocalAccountId.IsValid())
		{
			UE_LOG(LogIsotopePartySubsystem, Error, TEXT("Cannot join lobby: not logged in"));
			OnOnlineError.Broadcast(TEXT("Cannot join lobby: not logged in"));
			return;
		}

		// Если мы уже в лобби — сначала выходим из него
		if (bHasCachedNativeLobbyId)
		{
			UE_LOG(LogIsotopePartySubsystem, Log, TEXT("Leaving current lobby before joining new one"));

			FLeaveLobby::Params LeaveParams;
			LeaveParams.LocalAccountId = LocalAccountId;
			LeaveParams.LobbyId = CachedNativeLobbyId;

			bHasCachedLobby = false;
			bHasCachedNativeLobbyId = false;
			CachedLobby = FIsotopeLobbyBP{};
			CachedNativeLobbyId = FLobbyId{};

			// Дожидаемся завершения leave, потом джойнимся
			Lobbies->LeaveLobby(MoveTemp(LeaveParams))
				.OnComplete([this, LobbyRef, LobbyIdStr](const TOnlineResult<FLeaveLobby>& LeaveResult)
					{
						if (!LeaveResult.IsOk())
						{
							UE_LOG(LogIsotopePartySubsystem, Warning,
								TEXT("Leave failed before join: %s"), *LeaveResult.GetErrorValue().GetLogString());
						}

						// Теперь джойнимся в новое лобби
						FJoinLobby::Params JoinParams;
						JoinParams.LocalAccountId = LocalAccountId;
						JoinParams.LobbyId = LobbyRef->LobbyId;
						JoinParams.LocalName = FName(TEXT("Lobby"));
						JoinParams.bPresenceEnabled = true;
						JoinParams.UserAttributes = {};

						Lobbies->JoinLobby(MoveTemp(JoinParams))
							.OnComplete([this, LobbyIdStr](const TOnlineResult<FJoinLobby>& Result)
								{
									if (!Result.IsOk())
									{
										const FString ErrStr = Result.GetErrorValue().GetLogString();
										UE_LOG(LogIsotopePartySubsystem, Error,
											TEXT("JoinLobby failed for %s: %s"), *LobbyIdStr, *ErrStr);
										OnOnlineError.Broadcast(FString::Printf(TEXT("Join failed: %s"), *ErrStr));
										return;
									}

									const FJoinLobby::Result& Value = Result.GetOkValue();
									CachedNativeLobbyId = Value.Lobby->LobbyId;
									bHasCachedNativeLobbyId = true;

									const FIsotopeLobbyBP Snapshot = BuildLobbySnapshot(*Value.Lobby);
									CachedLobby = Snapshot;
									bHasCachedLobby = true;

									UE_LOG(LogIsotopePartySubsystem, Log,
										TEXT("Successfully joined lobby %s"), *LobbyIdStr);
									OnLobbyJoined.Broadcast(Snapshot);
								});
					});
			return;
		}

		// Если не в лобби — сразу джойнимся
		FJoinLobby::Params JoinParams;
		JoinParams.LocalAccountId = LocalAccountId;
		JoinParams.LobbyId = LobbyRef->LobbyId;
		JoinParams.LocalName = FName(TEXT("Lobby"));
		JoinParams.bPresenceEnabled = true;
		JoinParams.UserAttributes = {};

		Lobbies->JoinLobby(MoveTemp(JoinParams))
			.OnComplete([this, LobbyIdStr](const TOnlineResult<FJoinLobby>& Result)
				{
					if (!Result.IsOk())
					{
						const FString ErrStr = Result.GetErrorValue().GetLogString();
						UE_LOG(LogIsotopePartySubsystem, Error,
							TEXT("JoinLobby failed for %s: %s"), *LobbyIdStr, *ErrStr);
						OnOnlineError.Broadcast(FString::Printf(TEXT("Join failed: %s"), *ErrStr));
						return;
					}

					const FJoinLobby::Result& Value = Result.GetOkValue();
					CachedNativeLobbyId = Value.Lobby->LobbyId;
					bHasCachedNativeLobbyId = true;

					const FIsotopeLobbyBP Snapshot = BuildLobbySnapshot(*Value.Lobby);
					CachedLobby = Snapshot;
					bHasCachedLobby = true;

					UE_LOG(LogIsotopePartySubsystem, Log,
						TEXT("Successfully joined lobby %s"), *LobbyIdStr);
					OnLobbyJoined.Broadcast(Snapshot);
				});
	}
	else
	{
		const FString ErrStr = EventParams.Result.GetErrorValue().GetLogString();
		UE_LOG(LogIsotopePartySubsystem, Error, TEXT("UI Lobby Join Request failed: %s"), *ErrStr);
		OnOnlineError.Broadcast(FString::Printf(TEXT("UI Join Request failed: %s"), *ErrStr));
	}
}



void UIsotopePartySubsystem::Deinitialize()
{
	StopPolling();

	UILobbyJoinRequestedHandle.Unbind();
	bHasCachedLobby = false;
	bHasCachedNativeLobbyId = false;
	CachedLobby = FIsotopeLobbyBP{};

	Services.Reset();
	Auth.Reset();
	Lobbies.Reset();
	ExternalUI.Reset();
	UserInfo.Reset();
	Social.Reset();
	Presence.Reset();

	Super::Deinitialize();
}

void UIsotopePartySubsystem::InitializeOnlineServices()
{
	Services = UE::Online::GetServices();
	if (!Services.IsValid())
	{
		OnOnlineError.Broadcast(TEXT("OnlineServices not available"));
		return;
	}

	Auth = Services->GetAuthInterface();
	Lobbies = Services->GetLobbiesInterface();
	ExternalUI = Services->GetExternalUIInterface();
	UserInfo = Services->GetUserInfoInterface();
	Social = Services->GetSocialInterface();
	Presence = Services->GetPresenceInterface();

	bServicesReady = Auth.IsValid() && Lobbies.IsValid();
	if (!bServicesReady)
	{
		OnOnlineError.Broadcast(TEXT("Required Online Services interfaces are missing"));
		return;
	}
	UILobbyJoinRequestedHandle = Lobbies->OnUILobbyJoinRequested().Add(this, &UIsotopePartySubsystem::HandleUILobbyJoinRequested);
}

void UIsotopePartySubsystem::StartPolling()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			PollTimerHandle,
			this,
			&UIsotopePartySubsystem::PollLobbyState,
			0.5f,
			true
		);
	}
}

void UIsotopePartySubsystem::QueryDisplayName(const FString& AccountIdStr)
{
	if (!EnsureLoggedInAndReady(false))
	{
		OnDisplayNameFailed.Broadcast(TEXT("Not logged in"));
		return;
	}

	if (!UserInfo.IsValid())
	{
		OnDisplayNameFailed.Broadcast(TEXT("UserInfo interface not available"));
		return;
	}

	// Конвертируем FString обратно в FAccountId
	// В UE 5.6 нет прямого FromString, используем GetLocalOnlineUserByPlatformUserId
	// для чужих аккаунтов — ищем через GetJoinedLobbies
	TSharedPtr<const FLobby> NativeLobby;
	if (!GetCurrentJoinedLobbyNative(NativeLobby) || !NativeLobby.IsValid())
	{
		OnDisplayNameFailed.Broadcast(TEXT("No active lobby to resolve AccountId from"));
		return;
	}

	// Ищем нужный FAccountId среди членов лобби
	FAccountId TargetAccountId;
	bool bFound = false;
	for (const TPair<FAccountId, TSharedRef<const FLobbyMember>>& Pair : NativeLobby->Members)
	{
		if (AccountIdToString(Pair.Key) == AccountIdStr)
		{
			TargetAccountId = Pair.Key;
			bFound = true;
			break;
		}
	}

	if (!bFound)
	{
		OnDisplayNameFailed.Broadcast(FString::Printf(
			TEXT("AccountId %s not found in current lobby"), *AccountIdStr));
		return;
	}

	FQueryUserInfo::Params QueryParams;
	QueryParams.LocalAccountId = LocalAccountId;
	QueryParams.AccountIds = { TargetAccountId };

	UserInfo->QueryUserInfo(MoveTemp(QueryParams))
		.OnComplete([this, TargetAccountId, AccountIdStr](const TOnlineResult<FQueryUserInfo>& Result)
			{
				if (!Result.IsOk())
				{
					const FString ErrStr = Result.GetErrorValue().GetLogString();
					UE_LOG(LogIsotopePartySubsystem, Error,
						TEXT("QueryDisplayName failed for %s: %s"), *AccountIdStr, *ErrStr);
					OnDisplayNameFailed.Broadcast(ErrStr);
					return;
				}

				FGetUserInfo::Params GetParams;
				GetParams.LocalAccountId = LocalAccountId;
				GetParams.AccountId = TargetAccountId;

				const TOnlineResult<FGetUserInfo> InfoResult = UserInfo->GetUserInfo(MoveTemp(GetParams));
				if (!InfoResult.IsOk())
				{
					const FString ErrStr = InfoResult.GetErrorValue().GetLogString();
					UE_LOG(LogIsotopePartySubsystem, Error,
						TEXT("GetUserInfo failed for %s: %s"), *AccountIdStr, *ErrStr);
					OnDisplayNameFailed.Broadcast(ErrStr);
					return;
				}

				const FString DisplayName = InfoResult.GetOkValue().UserInfo->DisplayName;
				UE_LOG(LogIsotopePartySubsystem, Log,
					TEXT("DisplayName for %s: %s"), *AccountIdStr, *DisplayName);
				OnDisplayNameReady.Broadcast(AccountIdStr, DisplayName);
			});
}

void UIsotopePartySubsystem::QueryAllMemberDisplayNames()
{
	if (!bHasCachedLobby)
	{
		OnDisplayNameFailed.Broadcast(TEXT("No active lobby"));
		return;
	}

	// Просто вызываем QueryDisplayName для каждого члена лобби.
	// OnDisplayNameReady стрельнёт по одному разу на каждого.
	for (const FIsotopeLobbyMemberBP& Member : CachedLobby.Members)
	{
		QueryDisplayName(Member.AccountId);
	}
}

void UIsotopePartySubsystem::StopPolling()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PollTimerHandle);
	}
}

bool UIsotopePartySubsystem::ResolveLocalAccountFromAuthCache()
{
	if (!Auth.IsValid())
	{
		return false;
	}

	UGameInstance* GI = GetGameInstance();
	if (!GI)
	{
		return false;
	}

	ULocalPlayer* LocalPlayer = GI->GetFirstGamePlayer();
	if (!LocalPlayer)
	{
		return false;
	}

	LocalPlatformUserId = LocalPlayer->GetPlatformUserId();

	FAuthGetLocalOnlineUserByPlatformUserId::Params Params;
	Params.PlatformUserId = LocalPlatformUserId;

	const TOnlineResult<FAuthGetLocalOnlineUserByPlatformUserId> Result =
		Auth->GetLocalOnlineUserByPlatformUserId(MoveTemp(Params));

	if (!Result.IsOk())
	{
		return false;
	}

	// AccountInfo — TSharedRef, никогда не null, проверка IsValid не нужна
	const FAuthGetLocalOnlineUserByPlatformUserId::Result& Value = Result.GetOkValue();
	LocalAccountId = Value.AccountInfo->AccountId;
	LocalAccountIdString = AccountIdToString(LocalAccountId);
	bLoggedIn = true;

	OnLoginSuccess.Broadcast(LocalAccountIdString);
	return true;
}

bool UIsotopePartySubsystem::EnsureLoggedInAndReady(bool bAllowLoginUI)
{
	if (!bServicesReady)
	{
		InitializeOnlineServices();
	}

	if (!bServicesReady)
	{
		return false;
	}

	if (bLoggedIn && LocalAccountId.IsValid())
	{
		return true;
	}

	if (ResolveLocalAccountFromAuthCache())
	{
		return true;
	}

	if (bAllowLoginUI)
	{
		ShowLoginOverlay();
	}

	return false;
}

void UIsotopePartySubsystem::LoginDeveloper()
{
	// Dev-auth: запускай редактор/игру с аргументами:
	// -AUTH_TYPE="developer" -AUTH_LOGIN="localhost:<PORT>" -AUTH_PASSWORD="<NAME>"
	// Тогда ResolveLocalAccountFromAuthCache найдёт уже залогиненного пользователя.
	if (ResolveLocalAccountFromAuthCache())
	{
		return;
	}

	// Fallback — показываем Login UI платформы.
	ShowLoginOverlay();
}

void UIsotopePartySubsystem::AutoLoginFromCommandLine()
{
	if (!bServicesReady)
	{
		InitializeOnlineServices();
	}

	if (!bServicesReady || !Auth.IsValid())
	{
		OnLoginFailed.Broadcast(TEXT("Online services not ready"));
		return;
	}

	// Сначала проверяем кэш — вдруг EOS SDK уже подхватил аргументы сам.
	if (ResolveLocalAccountFromAuthCache())
	{
		return;
	}

	// Читаем аргументы из командной строки.
	// Формат: -AUTH_TYPE=developer -AUTH_LOGIN=localhost:6547 -AUTH_PASSWORD=Daun2
	const FString& CmdLine = FCommandLine::Get();

	FString AuthType, AuthLogin, AuthPassword;
	FParse::Value(*CmdLine, TEXT("AUTH_TYPE="), AuthType);
	FParse::Value(*CmdLine, TEXT("AUTH_LOGIN="), AuthLogin);
	FParse::Value(*CmdLine, TEXT("AUTH_PASSWORD="), AuthPassword);

	if (AuthType.IsEmpty())
	{
		UE_LOG(LogIsotopePartySubsystem, Warning,
			TEXT("AutoLoginFromCommandLine: аргументы не найдены, фолбэк на LoginDeveloper."));
		LoginDeveloper();
		return;
	}

	UE_LOG(LogIsotopePartySubsystem, Log,
		TEXT("AutoLoginFromCommandLine: AUTH_TYPE=%s AUTH_LOGIN=%s"), *AuthType, *AuthLogin);

	// Резолвим PlatformUserId для первого локального игрока.
	UGameInstance* GI = GetGameInstance();
	if (!GI)
	{
		OnLoginFailed.Broadcast(TEXT("GameInstance unavailable"));
		return;
	}
	ULocalPlayer* LocalPlayer = GI->GetFirstGamePlayer();
	if (!LocalPlayer)
	{
		OnLoginFailed.Broadcast(TEXT("No local player found"));
		return;
	}
	LocalPlatformUserId = LocalPlayer->GetPlatformUserId();

	FAuthLogin::Params Params;
	Params.PlatformUserId = LocalPlatformUserId;
	Params.CredentialsType = FName(*AuthType);         // FName, не FString
	Params.CredentialsId = AuthLogin;
	Params.CredentialsToken = TVariant<FString, UE::Online::FExternalAuthToken>(TInPlaceType<FString>(), AuthPassword); // TVariant

	Auth->Login(MoveTemp(Params))
		.OnComplete([this](const TOnlineResult<FAuthLogin>& Result)
			{
				if (!Result.IsOk())
				{
					const FString ErrStr = Result.GetErrorValue().GetLogString();
					UE_LOG(LogIsotopePartySubsystem, Error,
						TEXT("AutoLoginFromCommandLine: Login failed — %s"), *ErrStr);
					OnLoginFailed.Broadcast(ErrStr);
					return;
				}

				const FAuthLogin::Result& Value = Result.GetOkValue();
				LocalAccountId = Value.AccountInfo->AccountId;
				LocalAccountIdString = ToLogString(LocalAccountId);
				bLoggedIn = true;

				UE_LOG(LogIsotopePartySubsystem, Log,
					TEXT("AutoLoginFromCommandLine: успешно — %s"), *LocalAccountIdString);
				OnLoginSuccess.Broadcast(LocalAccountIdString);
			});
}

void UIsotopePartySubsystem::Logout()
{
	bLoggedIn = false;
	LocalAccountId = FAccountId{};
	LocalAccountIdString.Reset();

	bHasCachedLobby = false;
	bHasCachedNativeLobbyId = false;
	CachedLobby = FIsotopeLobbyBP{};
	CachedNativeLobbyId = FLobbyId{};

	OnLogout.Broadcast(TEXT("Logged out"));
}

void UIsotopePartySubsystem::CreateLobby(int32 MaxMembers, EIsotopeLobbyJoinPolicy LobbyJoinPolicy)
{

	if (!EnsureLoggedInAndReady(true))
	{
		OnOnlineError.Broadcast(TEXT("Not logged in"));
		return;
	}

	const FString UniqueMatchId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);


	FCreateLobby::Params Params;
	Params.LocalAccountId = LocalAccountId;
	Params.LocalName = FName(TEXT("Lobby"));
	Params.MaxMembers = MaxMembers;
	Params.SchemaId = FSchemaId(TEXT("GameLobby"));
	Params.JoinPolicy = static_cast<ELobbyJoinPolicy>(LobbyJoinPolicy);
	Params.bPresenceEnabled = true;
	Params.Attributes.Add(
		FSchemaAttributeId(TEXT("MatchId")),
		FSchemaVariant(UniqueMatchId)
	);
	Params.UserAttributes = {};

	Lobbies->CreateLobby(MoveTemp(Params))
		.OnComplete([this](const TOnlineResult<FCreateLobby>& Result)
			{
				if (!Result.IsOk())
				{
					const FString ErrStr = Result.GetErrorValue().GetLogString();
					UE_LOG(LogIsotopePartySubsystem, Error,
						TEXT("CreateLobby failed: %s"), *ErrStr);
					OnOnlineError.Broadcast(FString::Printf(TEXT("CreateLobby failed: %s"),
						*Result.GetErrorValue().GetLogString()));
					return;
				}

				const FCreateLobby::Result& Value = Result.GetOkValue();
				// Сохраняем нативный LobbyId — он нужен для дальнейших вызовов API
				CachedNativeLobbyId = Value.Lobby->LobbyId;
				bHasCachedNativeLobbyId = true;

				const FIsotopeLobbyBP Snapshot = BuildLobbySnapshot(*Value.Lobby);
				CachedLobby = Snapshot;
				bHasCachedLobby = true;

				OnLobbyCreated.Broadcast(Snapshot);
			});
}

void UIsotopePartySubsystem::JoinLobby(const FString& LobbyIdStr)
{
	if (!EnsureLoggedInAndReady(true))
	{
		OnOnlineError.Broadcast(TEXT("Not logged in"));
		return;
	}

	// Эта функция оставлена для возможности ручного джойна из блупринтов,
	// но в UE 5.6 нет прямого способа конвертировать FString обратно в FLobbyId.
	// Рекомендуется использовать автоматический джойн через HandleUILobbyJoinRequested,
	// который срабатывает при принятии инвайта через social overlay.

	UE_LOG(LogIsotopePartySubsystem, Warning,
		TEXT("JoinLobby: Manual join by LobbyId string is not supported in UE 5.6. Use invite system instead."));

	OnOnlineError.Broadcast(TEXT("Manual join by LobbyId not supported. Use invite system."));
}

void UIsotopePartySubsystem::LeaveLobby()
{
	if (!EnsureLoggedInAndReady(false))
	{
		OnOnlineError.Broadcast(TEXT("Not logged in"));
		return;
	}

	if (!bHasCachedNativeLobbyId)
	{
		OnOnlineError.Broadcast(TEXT("No active lobby"));
		return;
	}

	const FIsotopeLobbyBP Previous = CachedLobby;

	FLeaveLobby::Params Params;
	Params.LocalAccountId = LocalAccountId;
	Params.LobbyId = CachedNativeLobbyId; // используем нативный Id

	Lobbies->LeaveLobby(MoveTemp(Params));

	bHasCachedLobby = false;
	bHasCachedNativeLobbyId = false;
	CachedLobby = FIsotopeLobbyBP{};
	CachedNativeLobbyId = FLobbyId{};

	OnLobbyLeft.Broadcast(Previous);
}

void UIsotopePartySubsystem::SetLobbyAttribute(const FString& Key, const FString& Value)
{
	if (!EnsureLoggedInAndReady(false))
	{
		OnOnlineError.Broadcast(TEXT("Not logged in"));
		return;
	}

	if (!bHasCachedNativeLobbyId)
	{
		OnOnlineError.Broadcast(TEXT("No active lobby"));
		return;
	}

	FModifyLobbyAttributes::Params Params;
	Params.LocalAccountId = LocalAccountId;
	Params.LobbyId = CachedNativeLobbyId;
	Params.RemovedAttributes = {};
	Params.UpdatedAttributes.Add(FSchemaAttributeId(*Key), FSchemaVariant(Value));

	Lobbies->ModifyLobbyAttributes(MoveTemp(Params));
}

void UIsotopePartySubsystem::SetLobbyAttributeInt(const FString& Key, int64 Value)
{
	if (!EnsureLoggedInAndReady(false))
	{
		OnOnlineError.Broadcast(TEXT("Not logged in"));
		return;
	}

	if (!bHasCachedNativeLobbyId)
	{
		OnOnlineError.Broadcast(TEXT("No active lobby"));
		return;
	}

	FModifyLobbyAttributes::Params Params;
	Params.LocalAccountId = LocalAccountId;
	Params.LobbyId = CachedNativeLobbyId;
	Params.RemovedAttributes = {};
	Params.UpdatedAttributes.Add(FSchemaAttributeId(*Key), FSchemaVariant(Value));

	Lobbies->ModifyLobbyAttributes(MoveTemp(Params));
}

void UIsotopePartySubsystem::SetLobbyAttributeDouble(const FString& Key, double Value)
{
	if (!EnsureLoggedInAndReady(false))
	{
		OnOnlineError.Broadcast(TEXT("Not logged in"));
		return;
	}

	if (!bHasCachedNativeLobbyId)
	{
		OnOnlineError.Broadcast(TEXT("No active lobby"));
		return;
	}

	FModifyLobbyAttributes::Params Params;
	Params.LocalAccountId = LocalAccountId;
	Params.LobbyId = CachedNativeLobbyId;
	Params.RemovedAttributes = {};
	Params.UpdatedAttributes.Add(FSchemaAttributeId(*Key), FSchemaVariant(Value));

	Lobbies->ModifyLobbyAttributes(MoveTemp(Params));
}

void UIsotopePartySubsystem::SetLobbyAttributeBool(const FString& Key, bool Value)
{
	if (!EnsureLoggedInAndReady(false))
	{
		OnOnlineError.Broadcast(TEXT("Not logged in"));
		return;
	}

	if (!bHasCachedNativeLobbyId)
	{
		OnOnlineError.Broadcast(TEXT("No active lobby"));
		return;
	}

	FModifyLobbyAttributes::Params Params;
	Params.LocalAccountId = LocalAccountId;
	Params.LobbyId = CachedNativeLobbyId;
	Params.RemovedAttributes = {};
	Params.UpdatedAttributes.Add(FSchemaAttributeId(*Key), FSchemaVariant(Value));

	Lobbies->ModifyLobbyAttributes(MoveTemp(Params));
}

void UIsotopePartySubsystem::SetMemberAttribute(const FString& Key, const FString& Value)
{
	if (!EnsureLoggedInAndReady(false))
	{
		OnOnlineError.Broadcast(TEXT("Not logged in"));
		return;
	}

	if (!bHasCachedNativeLobbyId)
	{
		OnOnlineError.Broadcast(TEXT("No active lobby"));
		return;
	}

	FModifyLobbyMemberAttributes::Params Params;
	Params.LocalAccountId = LocalAccountId;
	Params.LobbyId = CachedNativeLobbyId;
	Params.RemovedAttributes = {};
	Params.UpdatedAttributes.Add(FSchemaAttributeId(*Key), FSchemaVariant(Value));

	Lobbies->ModifyLobbyMemberAttributes(MoveTemp(Params));
}

void UIsotopePartySubsystem::SetMemberAttributeInt(const FString& Key, int64 Value)
{
	if (!EnsureLoggedInAndReady(false))
	{
		OnOnlineError.Broadcast(TEXT("Not logged in"));
		return;
	}

	if (!bHasCachedNativeLobbyId)
	{
		OnOnlineError.Broadcast(TEXT("No active lobby"));
		return;
	}

	FModifyLobbyMemberAttributes::Params Params;
	Params.LocalAccountId = LocalAccountId;
	Params.LobbyId = CachedNativeLobbyId;
	Params.RemovedAttributes = {};
	Params.UpdatedAttributes.Add(FSchemaAttributeId(*Key), FSchemaVariant(Value));

	Lobbies->ModifyLobbyMemberAttributes(MoveTemp(Params));
}

void UIsotopePartySubsystem::SetMemberAttributeDouble(const FString& Key, double Value)
{
	if (!EnsureLoggedInAndReady(false))
	{
		OnOnlineError.Broadcast(TEXT("Not logged in"));
		return;
	}

	if (!bHasCachedNativeLobbyId)
	{
		OnOnlineError.Broadcast(TEXT("No active lobby"));
		return;
	}

	FModifyLobbyMemberAttributes::Params Params;
	Params.LocalAccountId = LocalAccountId;
	Params.LobbyId = CachedNativeLobbyId;
	Params.RemovedAttributes = {};
	Params.UpdatedAttributes.Add(FSchemaAttributeId(*Key), FSchemaVariant(Value));

	Lobbies->ModifyLobbyMemberAttributes(MoveTemp(Params));
}

void UIsotopePartySubsystem::SetMemberAttributeBool(const FString& Key, bool Value)
{
	if (!EnsureLoggedInAndReady(false))
	{
		OnOnlineError.Broadcast(TEXT("Not logged in"));
		return;
	}

	if (!bHasCachedNativeLobbyId)
	{
		OnOnlineError.Broadcast(TEXT("No active lobby"));
		return;
	}

	FModifyLobbyMemberAttributes::Params Params;
	Params.LocalAccountId = LocalAccountId;
	Params.LobbyId = CachedNativeLobbyId;
	Params.RemovedAttributes = {};
	Params.UpdatedAttributes.Add(FSchemaAttributeId(*Key), FSchemaVariant(Value));

	Lobbies->ModifyLobbyMemberAttributes(MoveTemp(Params));
}

bool UIsotopePartySubsystem::GetCurrentLobby(FIsotopeLobbyBP& OutLobby) const
{
	if (!bHasCachedLobby)
	{
		return false;
	}

	OutLobby = CachedLobby;
	return true;
}

bool UIsotopePartySubsystem::GetLobbyMembers(TArray<FIsotopeLobbyMemberBP>& OutMembers) const
{
	if (!bHasCachedLobby)
	{
		return false;
	}

	OutMembers = CachedLobby.Members;
	return true;
}

void UIsotopePartySubsystem::ShowFriendsOverlay()
{
	if (!ExternalUI.IsValid())
	{
		OnOnlineError.Broadcast(TEXT("ExternalUI interface not available"));
		return;
	}

	UGameInstance* GI = GetGameInstance();
	if (!GI)
	{
		OnOnlineError.Broadcast(TEXT("GameInstance unavailable"));
		return;
	}

	ULocalPlayer* LocalPlayer = GI->GetFirstGamePlayer();
	if (!LocalPlayer)
	{
		OnOnlineError.Broadcast(TEXT("No local player found"));
		return;
	}

	FExternalUIShowFriendsUI::Params Params;
	ExternalUI->ShowFriendsUI(MoveTemp(Params));

	OnInviteUIOpened.Broadcast(TEXT("Friends UI opened"));
}

void UIsotopePartySubsystem::ShowLoginOverlay()
{
	if (!ExternalUI.IsValid())
	{
		OnOnlineError.Broadcast(TEXT("ExternalUI interface not available"));
		return;
	}

	UGameInstance* GI = GetGameInstance();
	if (!GI)
	{
		OnOnlineError.Broadcast(TEXT("GameInstance unavailable"));
		return;
	}

	ULocalPlayer* LocalPlayer = GI->GetFirstGamePlayer();
	if (!LocalPlayer)
	{
		OnOnlineError.Broadcast(TEXT("No local player found"));
		return;
	}

	FExternalUIShowLoginUI::Params Params;
	Params.PlatformUserId = LocalPlayer->GetPlatformUserId();
	Params.Scopes = {};

	ExternalUI->ShowLoginUI(MoveTemp(Params));
	OnInviteUIOpened.Broadcast(TEXT("Login UI opened"));
}

bool UIsotopePartySubsystem::GetCurrentJoinedLobbyNative(TSharedPtr<const FLobby>& OutLobby) const
{
	if (!Lobbies.IsValid() || !LocalAccountId.IsValid())
	{
		return false;
	}

	FGetJoinedLobbies::Params Params;
	Params.LocalAccountId = LocalAccountId;

	const TOnlineResult<FGetJoinedLobbies> Result = Lobbies->GetJoinedLobbies(MoveTemp(Params));
	if (!Result.IsOk())
	{
		return false;
	}

	const FGetJoinedLobbies::Result& Value = Result.GetOkValue();
	if (Value.Lobbies.Num() == 0)
	{
		return false;
	}

	OutLobby = Value.Lobbies[0];
	return OutLobby.IsValid();
}

void UIsotopePartySubsystem::PollLobbyState()
{
	if (!bLoggedIn || !LocalAccountId.IsValid() || !Lobbies.IsValid())
	{
		return;
	}

	FGetJoinedLobbies::Params Params;
	Params.LocalAccountId = LocalAccountId;

	const TOnlineResult<FGetJoinedLobbies> Result = Lobbies->GetJoinedLobbies(MoveTemp(Params));
	if (!Result.IsOk())
	{
		return;
	}

	const FGetJoinedLobbies::Result& Value = Result.GetOkValue();

	if (Value.Lobbies.Num() == 0)
	{
		if (bHasCachedLobby)
		{
			FIsotopeLobbyBP OldLobby = CachedLobby;
			bHasCachedLobby = false;
			bHasCachedNativeLobbyId = false;
			CachedLobby = FIsotopeLobbyBP{};
			CachedNativeLobbyId = FLobbyId{};
			OnLobbyLeft.Broadcast(OldLobby);
		}
		return;
	}

	const TSharedRef<const FLobby>& NativeLobbyRef = Value.Lobbies[0];
	const FIsotopeLobbyBP NewLobby = BuildLobbySnapshot(*NativeLobbyRef);

	if (!bHasCachedLobby)
	{
		// Обновляем нативный Id при первом обнаружении лобби
		CachedNativeLobbyId = NativeLobbyRef->LobbyId;
		bHasCachedNativeLobbyId = true;

		CachedLobby = NewLobby;
		bHasCachedLobby = true;
		OnLobbyJoined.Broadcast(CachedLobby);
		return;
	}

	if (!LobbyEquals(CachedLobby, NewLobby))
	{
		BroadcastLobbyDelta(CachedLobby, NewLobby);
		CachedNativeLobbyId = NativeLobbyRef->LobbyId;
		CachedLobby = NewLobby;
	}
}

FIsotopeAttribute UIsotopePartySubsystem::ConvertVariantToAttribute(const FString& Key, const FSchemaVariant& Variant) const
{
	FIsotopeAttribute Result;
	Result.Key = Key;

	if (Variant.VariantData.IsType<FString>())
	{
		Result.Type = EIsotopeAttributeType::String;
		Result.AsString = Variant.VariantData.Get<FString>();
	}
	else if (Variant.VariantData.IsType<int64>())
	{
		Result.Type = EIsotopeAttributeType::Int64;
		Result.AsInt = Variant.VariantData.Get<int64>();
		Result.AsString = FString::Printf(TEXT("%lld"), Result.AsInt);
	}
	else if (Variant.VariantData.IsType<double>())
	{
		Result.Type = EIsotopeAttributeType::Double;
		Result.AsDouble = Variant.VariantData.Get<double>();
		Result.AsString = FString::Printf(TEXT("%f"), Result.AsDouble);
	}
	else if (Variant.VariantData.IsType<bool>())
	{
		Result.Type = EIsotopeAttributeType::Bool;
		Result.AsBool = Variant.VariantData.Get<bool>();
		Result.AsString = Result.AsBool ? TEXT("true") : TEXT("false");
	}
	else
	{
		Result.Type = EIsotopeAttributeType::String;
		Result.AsString = LexToString(Variant);
	}

	return Result;
}

FIsotopeLobbyBP UIsotopePartySubsystem::BuildLobbySnapshot(const FLobby& NativeLobby) const
{
	FIsotopeLobbyBP Out;
	Out.LobbyId = FString("Test");
	Out.OwnerAccountId = AccountIdToString(NativeLobby.OwnerAccountId);
	Out.LocalName = NativeLobby.LocalName.ToString();
	Out.SchemaId = SchemaIdToString(NativeLobby.SchemaId);
	Out.MaxMembers = NativeLobby.MaxMembers;
	Out.JoinPolicy = LexToString(NativeLobby.JoinPolicy);

	

	for (const TPair<FSchemaAttributeId, FSchemaVariant>& Pair : NativeLobby.Attributes)
	{
		FString Key = AttrIdToString(Pair.Key);
		Out.Attributes.Add(Key, ConvertVariantToAttribute(Key, Pair.Value));
	}

	if (const FIsotopeAttribute* MatchId = Out.Attributes.Find(TEXT("MatchId")))
	{
		Out.LobbyId = MatchId->AsString;
	}

	for (const TPair<FAccountId, TSharedRef<const FLobbyMember>>& Pair : NativeLobby.Members)
	{
		Out.Members.Add(BuildMemberSnapshot(*Pair.Value));
	}

	Out.Members.Sort([](const FIsotopeLobbyMemberBP& A, const FIsotopeLobbyMemberBP& B)
		{
			return A.AccountId < B.AccountId;
		});

	return Out;
}

FIsotopeLobbyMemberBP UIsotopePartySubsystem::BuildMemberSnapshot(const FLobbyMember& NativeMember) const
{
	FIsotopeLobbyMemberBP Out;

	Out.AccountId = AccountIdToString(NativeMember.AccountId);
	Out.bIsLocalMember = NativeMember.bIsLocalMember;

	for (const TPair<FSchemaAttributeId, FSchemaVariant>& Pair : NativeMember.Attributes)
	{
		FString Key = AttrIdToString(Pair.Key);
		Out.Attributes.Add(Key, ConvertVariantToAttribute(Key, Pair.Value));
	}

	return Out;
}

bool UIsotopePartySubsystem::AttributeEquals(const FIsotopeAttribute& A, const FIsotopeAttribute& B)
{
	if (A.Key != B.Key || A.Type != B.Type)
	{
		return false;
	}

	switch (A.Type)
	{
	case EIsotopeAttributeType::String:
		return A.AsString == B.AsString;
	case EIsotopeAttributeType::Int64:
		return A.AsInt == B.AsInt;
	case EIsotopeAttributeType::Double:
		return FMath::IsNearlyEqual(A.AsDouble, B.AsDouble);
	case EIsotopeAttributeType::Bool:
		return A.AsBool == B.AsBool;
	default:
		return false;
	}
}

bool UIsotopePartySubsystem::AttributeMapEquals(
	const TMap<FString, FIsotopeAttribute>& A,
	const TMap<FString, FIsotopeAttribute>& B)
{
	if (A.Num() != B.Num()) return false;

	for (const TPair<FString, FIsotopeAttribute>& Pair : A)
	{
		const FIsotopeAttribute* Other = B.Find(Pair.Key);
		if (!Other || !AttributeEquals(Pair.Value, *Other))
		{
			return false;
		}
	}

	return true;
}

bool UIsotopePartySubsystem::MemberEquals(const FIsotopeLobbyMemberBP& A, const FIsotopeLobbyMemberBP& B)
{
	return A.AccountId == B.AccountId
		&& A.bIsLocalMember == B.bIsLocalMember
		&& AttributeMapEquals(A.Attributes, B.Attributes);
}

bool UIsotopePartySubsystem::LobbyEquals(const FIsotopeLobbyBP& A, const FIsotopeLobbyBP& B)
{
	if (A.LobbyId != B.LobbyId ||
		A.OwnerAccountId != B.OwnerAccountId ||
		A.LocalName != B.LocalName ||
		A.SchemaId != B.SchemaId ||
		A.MaxMembers != B.MaxMembers ||
		A.JoinPolicy != B.JoinPolicy ||
		!AttributeMapEquals(A.Attributes, B.Attributes) ||
		A.Members.Num() != B.Members.Num())
	{
		return false;
	}

	for (int32 i = 0; i < A.Members.Num(); ++i)
	{
		if (!MemberEquals(A.Members[i], B.Members[i]))
		{
			return false;
		}
	}

	return true;
}

void UIsotopePartySubsystem::BroadcastLobbyDelta(const FIsotopeLobbyBP& OldLobby, const FIsotopeLobbyBP& NewLobby)
{
	if (OldLobby.LobbyId != NewLobby.LobbyId)
	{
		OnLobbyLeft.Broadcast(OldLobby);
		OnLobbyJoined.Broadcast(NewLobby);
		return;
	}

	if (!AttributeMapEquals(OldLobby.Attributes, NewLobby.Attributes) ||
		OldLobby.MaxMembers != NewLobby.MaxMembers ||
		OldLobby.JoinPolicy != NewLobby.JoinPolicy ||
		OldLobby.OwnerAccountId != NewLobby.OwnerAccountId)
	{
		OnLobbyAttributesChanged.Broadcast(NewLobby);

		// Проверяем, стали ли мы овнером
		if (OldLobby.OwnerAccountId != NewLobby.OwnerAccountId &&
			NewLobby.OwnerAccountId == LocalAccountIdString)
		{
			UE_LOG(LogIsotopePartySubsystem, Log, TEXT("Became lobby owner"));
			OnBecameLobbyOwner.Broadcast(TEXT("You are now the lobby owner"));
		}
	}

	// Detect member joins / leaves / attribute changes
	TMap<FString, FIsotopeLobbyMemberBP> OldMembers;
	TMap<FString, FIsotopeLobbyMemberBP> NewMembers;

	for (const FIsotopeLobbyMemberBP& M : OldLobby.Members) { OldMembers.Add(M.AccountId, M); }
	for (const FIsotopeLobbyMemberBP& M : NewLobby.Members) { NewMembers.Add(M.AccountId, M); }

	for (const TPair<FString, FIsotopeLobbyMemberBP>& Pair : NewMembers)
	{
		if (!OldMembers.Contains(Pair.Key))
		{
			OnLobbyMemberJoined.Broadcast(NewLobby, Pair.Value);
		}
		else if (!MemberEquals(OldMembers[Pair.Key], Pair.Value))
		{
			OnLobbyMemberAttributesChanged.Broadcast(NewLobby, Pair.Value);
		}
	}

	for (const TPair<FString, FIsotopeLobbyMemberBP>& Pair : OldMembers)
	{
		if (!NewMembers.Contains(Pair.Key))
		{
			OnLobbyMemberLeft.Broadcast(NewLobby, Pair.Value);
		}
	}
}