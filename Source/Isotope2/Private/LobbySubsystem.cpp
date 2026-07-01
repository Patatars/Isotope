#include "LobbySubsystem.h"

#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"

#include "Online/OnlineServices.h"
#include "Online/Auth.h"
#include "Online/Lobbies.h"
#include "Online/SessionsEOSGS.h"
#include "Online/SessionsEOSGSTypes.h"
#include "eos_connect_types.h"
#include "eos_sdk.h"
#include "eos_lobby.h"
#include "EOSShared.h"
#include "IEOSSDKManager.h"
#include <Online/OnlineIdEOSGS.h>

DEFINE_LOG_CATEGORY(LogLobbySubsystem);

using namespace UE::Online;

namespace
{
	static EOS_ProductUserId GetProductUserIdFromAccountId(const FAccountId& AccountId)
	{
		return GetProductUserId(AccountId);
	}

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

// =============================================================================
// --- СИСТЕМНЫЙ ЖИЗНЕННЫЙ ЦИКЛ ПОДСИСТЕМЫ (INITIALIZE / DEINITIALIZE) ---
// =============================================================================

void ULobbySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogLobbySubsystem, Log, TEXT("Initializing"));
	UWorld* World = GetWorld();
	if (World && World->GetNetMode() != NM_DedicatedServer)
	{
		InitializeOnlineServices();
		ResolveLocalAccountFromAuthCache();
	}
}

void ULobbySubsystem::Deinitialize()
{
	UE_LOG(LogLobbySubsystem, Log, TEXT("Deinitializing"));
	UILobbyJoinRequestedHandle.Unbind();
	LobbyJoinedHandle.Unbind();
	LobbyLeftHandle.Unbind();
	LobbyUpdatedHandle.Unbind();
	LobbyMemberJoinedHandle.Unbind();
	LobbyMemberLeftHandle.Unbind();
	LobbyMemberUpdatedHandle.Unbind();
	LobbyLeaderChangedHandle.Unbind();

	if (LeaveLobbyRequestedNotificationId != EOS_INVALID_NOTIFICATIONID)
	{
		IEOSSDKManager* SDKManager = IEOSSDKManager::Get();
		if (SDKManager)
		{
			TArray<IEOSPlatformHandlePtr> Platforms = SDKManager->GetActivePlatforms();
			if (Platforms.Num() > 0 && Platforms[0].IsValid())
			{
				EOS_HPlatform PlatformHandle = *Platforms[0];
				if (PlatformHandle)
				{
					EOS_HLobby LobbyHandle = EOS_Platform_GetLobbyInterface(PlatformHandle);
					if (LobbyHandle)
					{
						EOS_Lobby_RemoveNotifyLeaveLobbyRequested(LobbyHandle, LeaveLobbyRequestedNotificationId);
						LeaveLobbyRequestedNotificationId = EOS_INVALID_NOTIFICATIONID;
					}
				}
			}
		}
	}

	bHasCachedLobby = false;
	CachedLobby = FIsotopeLobbyBP{};
	LobbyAttributeUpdatedDelegates.Empty();
	LobbyMemberAttributeUpdatedDelegates.Empty();

	Services.Reset();
	Auth.Reset();
	Lobbies.Reset();
	ExternalUI.Reset();
	UserInfo.Reset();
	Social.Reset();
	Presence.Reset();
	Sessions.Reset();

	Super::Deinitialize();
}

void ULobbySubsystem::InitializeOnlineServices()
{
	Services = UE::Online::GetServices();
	if (!Services.IsValid())
	{
		UE_LOG(LogLobbySubsystem, Error, TEXT("Online Services are not available"));
		OnOnlineError.Broadcast(TEXT("OnlineServices not available"));
		return;
	}

	Auth = Services->GetAuthInterface();
	Lobbies = Services->GetLobbiesInterface();
	ExternalUI = Services->GetExternalUIInterface();
	UserInfo = Services->GetUserInfoInterface();
	Social = Services->GetSocialInterface();
	Presence = Services->GetPresenceInterface();
	Sessions = Services->GetSessionsInterface();

	bServicesReady = Auth.IsValid() && Lobbies.IsValid();
	if (!bServicesReady)
	{
		UE_LOG(LogLobbySubsystem, Error, TEXT("Required Online Services interfaces are missing"));
		OnOnlineError.Broadcast(TEXT("Required Online Services interfaces are missing"));
		return;
	}


	UILobbyJoinRequestedHandle = Lobbies->OnUILobbyJoinRequested().Add(this, &ULobbySubsystem::HandleUILobbyJoinRequested);
	LobbyJoinedHandle = Lobbies->OnLobbyJoined().Add(this, &ULobbySubsystem::HandleNativeLobbyJoined);
	LobbyLeftHandle = Lobbies->OnLobbyLeft().Add(this, &ULobbySubsystem::HandleNativeLobbyLeft);
	LobbyUpdatedHandle = Lobbies->OnLobbyAttributesChanged().Add(this, &ULobbySubsystem::HandleNativeLobbyAttributesChanged);
	LobbyMemberJoinedHandle = Lobbies->OnLobbyMemberJoined().Add(this, &ULobbySubsystem::HandleNativeLobbyMemberJoined);
	LobbyMemberLeftHandle = Lobbies->OnLobbyMemberLeft().Add(this, &ULobbySubsystem::HandleNativeLobbyMemberLeft);
	LobbyMemberUpdatedHandle = Lobbies->OnLobbyMemberAttributesChanged().Add(this, &ULobbySubsystem::HandleNativeLobbyMemberAttributesChanged);
	LobbyLeaderChangedHandle = Lobbies->OnLobbyLeaderChanged().Add(this, &ULobbySubsystem::HandleNativeLobbyLeaderChanged);

	IEOSSDKManager* SDKManager = IEOSSDKManager::Get();
	if (SDKManager)
	{
		TArray<IEOSPlatformHandlePtr> Platforms = SDKManager->GetActivePlatforms();
		if (Platforms.Num() > 0 && Platforms[0].IsValid())
		{
			EOS_HPlatform PlatformHandle = *Platforms[0];
			if (PlatformHandle)
			{
				EOS_HLobby LobbyHandle = EOS_Platform_GetLobbyInterface(PlatformHandle);
				if (LobbyHandle)
				{
					EOS_Lobby_AddNotifyLeaveLobbyRequestedOptions Options = {};
					Options.ApiVersion = EOS_LOBBY_ADDNOTIFYLEAVELOBBYREQUESTED_API_LATEST;

					LeaveLobbyRequestedNotificationId = EOS_Lobby_AddNotifyLeaveLobbyRequested(
						LobbyHandle,
						&Options,
						this,
						&ULobbySubsystem::OnLeaveLobbyRequestedCallback
					);
				}
			}
		}
	}
}

// =============================================================================
// --- ИНТЕРФЕЙС АВТОРИЗАЦИИ (LOGIN / LOGOUT / CACHE) ---
// =============================================================================

void ULobbySubsystem::Login()
{
	UE_LOG(LogLobbySubsystem, Log, TEXT("Login requested"));
	if (!Auth.IsValid())
	{
		OnLoginFailed.Broadcast(TEXT("Auth interface not initialized"));
		return;
	}

	if (ResolveLocalAccountFromAuthCache())
	{
		return;
	}

	FString AuthType, AuthLogin, AuthPassword;
	const TCHAR* CmdLine = FCommandLine::Get();

	bool bHasType = FParse::Value(CmdLine, TEXT("AUTH_TYPE="), AuthType);
	bool bHasLogin = FParse::Value(CmdLine, TEXT("AUTH_LOGIN="), AuthLogin);
	bool bHasPassword = FParse::Value(CmdLine, TEXT("AUTH_PASSWORD="), AuthPassword);

	if (bHasType && bHasLogin && bHasPassword)
	{
		UGameInstance* GI = GetGameInstance();
		if (!GI) return;

		ULocalPlayer* LocalPlayer = GI->GetFirstGamePlayer();
		if (!LocalPlayer) return;

		LocalPlatformUserId = LocalPlayer->GetPlatformUserId();

		FAuthLogin::Params Params;
		Params.PlatformUserId = LocalPlatformUserId;
		Params.CredentialsType = FName(*AuthType);
		Params.CredentialsId = AuthLogin;
		Params.CredentialsToken = TVariant<FString, UE::Online::FExternalAuthToken>(TInPlaceType<FString>(), AuthPassword);

		Auth->Login(MoveTemp(Params))
			.OnComplete(this, [this](const TOnlineResult<FAuthLogin>& Result)
				{
					if (Result.IsOk())
					{
						const auto& AccountInfo = Result.GetOkValue().AccountInfo;
						LocalAccountId = AccountInfo->AccountId;
						LocalAccountIdString = AccountIdToString(LocalAccountId);
						bLoggedIn = true;
						UE_LOG(LogLobbySubsystem, Log, TEXT("Login succeeded. AccountId=%s"), *LocalAccountIdString);
						OnLoginSuccess.Broadcast(LocalAccountIdString);
					}
					else
					{
						FString ErrorStr = Result.GetErrorValue().GetLogString();
						UE_LOG(LogLobbySubsystem, Error, TEXT("Login failed: %s"), *ErrorStr);
						OnLoginFailed.Broadcast(ErrorStr);
					}
				});
	}
	else
	{
		UE_LOG(LogLobbySubsystem, Log, TEXT("Opening EOS login overlay"));
		ShowLoginOverlay();
	}
}

void ULobbySubsystem::Logout()
{
	UE_LOG(LogLobbySubsystem, Log, TEXT("Logout requested. AccountId=%s"), *LocalAccountIdString);
	bLoggedIn = false;
	LocalAccountId = FAccountId{};
	LocalAccountIdString.Reset();

	bHasCachedLobby = false;
	CachedNativeLobbyId = FLobbyId{};
	CachedLobby = FIsotopeLobbyBP{};

	OnLogout.Broadcast(TEXT("Logged out"));
}

bool ULobbySubsystem::ResolveLocalAccountFromAuthCache()
{
	if (!Auth.IsValid()) return false;

	UGameInstance* GI = GetGameInstance();
	if (!GI) return false;

	ULocalPlayer* LocalPlayer = GI->GetFirstGamePlayer();
	if (!LocalPlayer) return false;

	LocalPlatformUserId = LocalPlayer->GetPlatformUserId();

	FAuthGetLocalOnlineUserByPlatformUserId::Params Params;
	Params.PlatformUserId = LocalPlatformUserId;

	const TOnlineResult<FAuthGetLocalOnlineUserByPlatformUserId> Result = Auth->GetLocalOnlineUserByPlatformUserId(MoveTemp(Params));
	if (!Result.IsOk()) return false;

	const FAuthGetLocalOnlineUserByPlatformUserId::Result& Value = Result.GetOkValue();
	LocalAccountId = Value.AccountInfo->AccountId;
	LocalAccountIdString = AccountIdToString(LocalAccountId);
	bLoggedIn = true;
	UE_LOG(LogLobbySubsystem, Log, TEXT("Login restored from auth cache. AccountId=%s"), *LocalAccountIdString);

	OnLoginSuccess.Broadcast(LocalAccountIdString);
	return true;
}

bool ULobbySubsystem::EnsureLoggedInAndReady(bool bAllowLoginUI)
{
	if (!bServicesReady) InitializeOnlineServices();
	if (!bServicesReady) return false;
	if (bLoggedIn && LocalAccountId.IsValid()) return true;
	if (ResolveLocalAccountFromAuthCache()) return true;

	if (bAllowLoginUI) ShowLoginOverlay();
	return false;
}

// =============================================================================
// --- ПУБЛИЧНЫЙ БЛУПРИНТОВЫЙ ИНТЕРФЕЙС ЛОББИ (CREATE / JOIN / LEAVE) ---
// =============================================================================

void ULobbySubsystem::CreateLobby(int32 MaxMembers, EIsotopeLobbyJoinPolicy LobbyJoinPolicy)
{
	UE_LOG(LogLobbySubsystem, Log, TEXT("Create lobby requested. MaxMembers=%d JoinPolicy=%d"), MaxMembers, static_cast<int32>(LobbyJoinPolicy));
	if (!EnsureLoggedInAndReady(true))
	{
		OnOnlineError.Broadcast(TEXT("Not logged in"));
		return;
	}

	const FString LobbyCorrelationId = FGuid::NewGuid().ToString(EGuidFormats::Digits);

	FCreateLobby::Params Params;
	Params.LocalAccountId = LocalAccountId;
	Params.LocalName = FName(TEXT("Lobby"));
	Params.MaxMembers = MaxMembers;
	Params.SchemaId = FSchemaId(TEXT("GameLobby"));
	Params.JoinPolicy = static_cast<ELobbyJoinPolicy>(LobbyJoinPolicy);
	Params.bPresenceEnabled = true;

	Params.Attributes.Add(FSchemaAttributeId(TEXT("LobbyCorrelationId")), FSchemaVariant(LobbyCorrelationId));
	Params.UserAttributes = {};

	Lobbies->CreateLobby(MoveTemp(Params))
		.OnComplete([this](const TOnlineResult<FCreateLobby>& Result)
			{
				if (!Result.IsOk())
				{
					const FString ErrStr = Result.GetErrorValue().GetLogString();
					UE_LOG(LogLobbySubsystem, Error, TEXT("Create lobby failed: %s"), *ErrStr);
					OnOnlineError.Broadcast(FString::Printf(TEXT("CreateLobby failed: %s"), *ErrStr));
					return;
				}

				const FCreateLobby::Result& Value = Result.GetOkValue();
				CachedNativeLobbyId = Value.Lobby->LobbyId;
				CachedLobby = BuildLobbySnapshot(*Value.Lobby);
				bHasCachedLobby = true;
				UE_LOG(LogLobbySubsystem, Log, TEXT("Lobby created. LobbyId=%s"), *CachedLobby.LobbyId);

				OnLobbyCreated.Broadcast(CachedLobby);
			});
}

void ULobbySubsystem::JoinLobby(const FString& LobbyIdStr)
{
	if (!EnsureLoggedInAndReady(true))
	{
		OnOnlineError.Broadcast(TEXT("Not logged in"));
		return;
	}
	OnOnlineError.Broadcast(TEXT("Manual join by LobbyId not supported via string. Use invite system."));
}

void ULobbySubsystem::LeaveLobby()
{
	UE_LOG(LogLobbySubsystem, Log, TEXT("Leave lobby requested. LobbyId=%s"), *CachedLobby.LobbyId);
	if (!EnsureLoggedInAndReady(false) || !CachedNativeLobbyId.IsValid())
	{
		OnOnlineError.Broadcast(TEXT("No active lobby or not logged in"));
		return;
	}

	const FIsotopeLobbyBP Previous = CachedLobby;

	FLeaveLobby::Params Params;
	Params.LocalAccountId = LocalAccountId;
	Params.LobbyId = CachedNativeLobbyId;

	Lobbies->LeaveLobby(MoveTemp(Params));
}

// =============================================================================
// --- УПРАВЛЕНИЕ АТРИБУТАМИ ---
// =============================================================================

void ULobbySubsystem::SetLobbyAttribute(const FString& Key, const FString& Value)
{
	UE_LOG(LogLobbySubsystem, Log, TEXT("Set lobby string attribute requested. Key=%s"), *Key);
	if (!EnsureLoggedInAndReady(false) || !CachedNativeLobbyId.IsValid()) return;
	FModifyLobbyAttributes::Params Params;
	Params.LocalAccountId = LocalAccountId;
	Params.LobbyId = CachedNativeLobbyId;
	Params.UpdatedAttributes.Add(FSchemaAttributeId(*Key), FSchemaVariant(Value));
	Lobbies->ModifyLobbyAttributes(MoveTemp(Params));
}

void ULobbySubsystem::SetLobbyAttributeInt(const FString& Key, int64 Value)
{
	UE_LOG(LogLobbySubsystem, Log, TEXT("Set lobby int attribute requested. Key=%s Value=%lld"), *Key, Value);
	if (!EnsureLoggedInAndReady(false) || !CachedNativeLobbyId.IsValid()) return;
	FModifyLobbyAttributes::Params Params;
	Params.LocalAccountId = LocalAccountId;
	Params.LobbyId = CachedNativeLobbyId;
	Params.UpdatedAttributes.Add(FSchemaAttributeId(*Key), FSchemaVariant(Value));
	Lobbies->ModifyLobbyAttributes(MoveTemp(Params));
}

void ULobbySubsystem::SetLobbyAttributeDouble(const FString& Key, double Value)
{
	if (!EnsureLoggedInAndReady(false) || !CachedNativeLobbyId.IsValid()) return;
	FModifyLobbyAttributes::Params Params;
	Params.LocalAccountId = LocalAccountId;
	Params.LobbyId = CachedNativeLobbyId;
	Params.UpdatedAttributes.Add(FSchemaAttributeId(*Key), FSchemaVariant(Value));
	Lobbies->ModifyLobbyAttributes(MoveTemp(Params));
}

void ULobbySubsystem::SetLobbyAttributeBool(const FString& Key, bool Value)
{
	if (!EnsureLoggedInAndReady(false) || !CachedNativeLobbyId.IsValid()) return;
	FModifyLobbyAttributes::Params Params;
	Params.LocalAccountId = LocalAccountId;
	Params.LobbyId = CachedNativeLobbyId;
	Params.UpdatedAttributes.Add(FSchemaAttributeId(*Key), FSchemaVariant(Value));
	Lobbies->ModifyLobbyAttributes(MoveTemp(Params));
}

void ULobbySubsystem::SetMemberAttribute(const FString& Key, const FString& Value)
{
	UE_LOG(LogLobbySubsystem, Log, TEXT("Set member string attribute requested. Key=%s"), *Key);
	if (!EnsureLoggedInAndReady(false) || !CachedNativeLobbyId.IsValid()) return;
	FModifyLobbyMemberAttributes::Params Params;
	Params.LocalAccountId = LocalAccountId;
	Params.LobbyId = CachedNativeLobbyId;
	Params.UpdatedAttributes.Add(FSchemaAttributeId(*Key), FSchemaVariant(Value));
	Lobbies->ModifyLobbyMemberAttributes(MoveTemp(Params));
}

void ULobbySubsystem::SetMemberAttributeInt(const FString& Key, int64 Value)
{
	if (!EnsureLoggedInAndReady(false) || !CachedNativeLobbyId.IsValid()) return;
	FModifyLobbyMemberAttributes::Params Params;
	Params.LocalAccountId = LocalAccountId;
	Params.LobbyId = CachedNativeLobbyId;
	Params.UpdatedAttributes.Add(FSchemaAttributeId(*Key), FSchemaVariant(Value));
	Lobbies->ModifyLobbyMemberAttributes(MoveTemp(Params));
}

void ULobbySubsystem::SetMemberAttributeDouble(const FString& Key, double Value)
{
	if (!EnsureLoggedInAndReady(false) || !CachedNativeLobbyId.IsValid()) return;
	FModifyLobbyMemberAttributes::Params Params;
	Params.LocalAccountId = LocalAccountId;
	Params.LobbyId = CachedNativeLobbyId;
	Params.UpdatedAttributes.Add(FSchemaAttributeId(*Key), FSchemaVariant(Value));
	Lobbies->ModifyLobbyMemberAttributes(MoveTemp(Params));
}

void ULobbySubsystem::SetMemberAttributeBool(const FString& Key, bool Value)
{
	if (!EnsureLoggedInAndReady(false) || !CachedNativeLobbyId.IsValid()) return;
	FModifyLobbyMemberAttributes::Params Params;
	Params.LocalAccountId = LocalAccountId;
	Params.LobbyId = CachedNativeLobbyId;
	Params.UpdatedAttributes.Add(FSchemaAttributeId(*Key), FSchemaVariant(Value));
	Lobbies->ModifyLobbyMemberAttributes(MoveTemp(Params));
}

void ULobbySubsystem::OnLobbyAttributeUpdated(const FString& Name, FOnLobbyAttributeUpdatedDelegate OnUpdated)
{
	if (Name.IsEmpty() || !OnUpdated.IsBound()) return;
	LobbyAttributeUpdatedDelegates.FindOrAdd(Name).AddUnique(OnUpdated);
}

void ULobbySubsystem::RemoveLobbyAttributeUpdated(const FString& Name, FOnLobbyAttributeUpdatedDelegate OnUpdated)
{
	TArray<FOnLobbyAttributeUpdatedDelegate>* Delegates = LobbyAttributeUpdatedDelegates.Find(Name);
	if (!Delegates) return;

	Delegates->Remove(OnUpdated);
	if (Delegates->IsEmpty())
	{
		LobbyAttributeUpdatedDelegates.Remove(Name);
	}
}

void ULobbySubsystem::OnLobbyMemberAttributeUpdated(const FString& Name, FOnLobbyMemberAttributeUpdatedDelegate OnUpdated)
{
	if (Name.IsEmpty() || !OnUpdated.IsBound()) return;
	LobbyMemberAttributeUpdatedDelegates.FindOrAdd(Name).AddUnique(OnUpdated);
}

void ULobbySubsystem::RemoveLobbyMemberAttributeUpdated(const FString& Name, FOnLobbyMemberAttributeUpdatedDelegate OnUpdated)
{
	TArray<FOnLobbyMemberAttributeUpdatedDelegate>* Delegates = LobbyMemberAttributeUpdatedDelegates.Find(Name);
	if (!Delegates) return;

	Delegates->Remove(OnUpdated);
	if (Delegates->IsEmpty())
	{
		LobbyMemberAttributeUpdatedDelegates.Remove(Name);
	}
}

// =============================================================================
// --- ПОЛУЧЕНИЕ СОСТОЯНИЯ ---
// =============================================================================

bool ULobbySubsystem::GetCurrentLobby(FIsotopeLobbyBP& OutLobby) const
{
	if (!bHasCachedLobby) return false;
	OutLobby = CachedLobby;
	return true;
}

bool ULobbySubsystem::GetLobbyMembers(TArray<FIsotopeLobbyMemberBP>& OutMembers) const
{
	if (!bHasCachedLobby) return false;
	OutMembers = CachedLobby.Members;
	return true;
}

bool ULobbySubsystem::GetCurrentJoinedLobbyNative(TSharedPtr<const FLobby>& OutLobby) const
{
	if (!Lobbies.IsValid() || !LocalAccountId.IsValid()) return false;

	FGetJoinedLobbies::Params Params;
	Params.LocalAccountId = LocalAccountId;

	const TOnlineResult<FGetJoinedLobbies> Result = Lobbies->GetJoinedLobbies(MoveTemp(Params));
	if (!Result.IsOk() || Result.GetOkValue().Lobbies.Num() == 0) return false;

	OutLobby = Result.GetOkValue().Lobbies[0];
	return OutLobby.IsValid();
}

// =============================================================================
// --- UI OVERLAYS ---
// =============================================================================

void ULobbySubsystem::ShowFriendsOverlay()
{
	if (!EnsureLoggedInAndReady(true) || !ExternalUI.IsValid()) return;

	FExternalUIShowFriendsUI::Params Params;
	Params.LocalAccountId = LocalAccountId;
	ExternalUI->ShowFriendsUI(MoveTemp(Params));
}

void ULobbySubsystem::ShowLoginOverlay()
{
	if (!ExternalUI.IsValid()) return;

	UGameInstance* GI = GetGameInstance();
	ULocalPlayer* LocalPlayer = GI ? GI->GetFirstGamePlayer() : nullptr;
	if (!LocalPlayer) return;

	FExternalUIShowLoginUI::Params Params;
	Params.PlatformUserId = LocalPlayer->GetPlatformUserId();
	Params.Scopes = {};

	ExternalUI->ShowLoginUI(MoveTemp(Params));
}

// =============================================================================
// --- OSSv2 HANDLERS ---
// =============================================================================

void ULobbySubsystem::HandleUILobbyJoinRequested(const UE::Online::FUILobbyJoinRequested& EventParams)
{
	if (!EventParams.Result.IsOk()) return;

	const TSharedRef<const FLobby>& TargetLobbyRef = EventParams.Result.GetOkValue();
	const FString TargetLobbyIdStr = LobbyIdToString(TargetLobbyRef->LobbyId);

	OnLobbyInviteAccepted.Broadcast(TargetLobbyIdStr);

	if (!LocalAccountId.IsValid())
	{
		UE_LOG(LogLobbySubsystem, Warning, TEXT("Invite accepted, but not logged in."));
		return;
	}

	TWeakObjectPtr<ULobbySubsystem> WeakThis(this);

	auto ExecuteJoin = [WeakThis, TargetLobbyRef]()
		{
			if (!WeakThis.IsValid()) return;

			FJoinLobby::Params JoinParams;
			JoinParams.LocalAccountId = WeakThis->LocalAccountId;
			JoinParams.LobbyId = TargetLobbyRef->LobbyId;
			JoinParams.LocalName = FName(TEXT("Lobby"));
			JoinParams.bPresenceEnabled = true;

			WeakThis->Lobbies->JoinLobby(MoveTemp(JoinParams))
				.OnComplete([WeakThis](const TOnlineResult<FJoinLobby>& Result)
					{
						if (!Result.IsOk() && WeakThis.IsValid())
						{
							FString ErrorStr = Result.GetErrorValue().GetLogString();
							WeakThis->OnOnlineError.Broadcast(FString::Printf(TEXT("JoinLobby failed: %s"), *ErrorStr));
						}
					});
		};

	FGetJoinedLobbies::Params GetParams;
	GetParams.LocalAccountId = LocalAccountId;
	const TOnlineResult<FGetJoinedLobbies> JoinedResult = Lobbies->GetJoinedLobbies(MoveTemp(GetParams));

	if (JoinedResult.IsOk() && JoinedResult.GetOkValue().Lobbies.Num() > 0)
	{
		FLeaveLobby::Params LeaveParams;
		LeaveParams.LocalAccountId = LocalAccountId;
		LeaveParams.LobbyId = JoinedResult.GetOkValue().Lobbies[0]->LobbyId;

		bHasCachedLobby = false;
		CachedNativeLobbyId = FLobbyId{};
		CachedLobby = FIsotopeLobbyBP{};

		Lobbies->LeaveLobby(MoveTemp(LeaveParams)).OnComplete([WeakThis, ExecuteJoin](const TOnlineResult<FLeaveLobby>&)
			{
				if (WeakThis.IsValid())
				{
					ExecuteJoin();
				}
			});
	}
	else
	{
		ExecuteJoin();
	}
}

void ULobbySubsystem::HandleNativeLobbyJoined(const UE::Online::FLobbyJoined& EventParams)
{
	CachedNativeLobbyId = EventParams.Lobby->LobbyId;
	CachedLobby = BuildLobbySnapshot(*EventParams.Lobby);
	bHasCachedLobby = true;
	UE_LOG(LogLobbySubsystem, Log, TEXT("Lobby joined. LobbyId=%s Members=%d"), *CachedLobby.LobbyId, CachedLobby.Members.Num());

	// Бросаем OnLobbyJoined ТОЛЬКО если мы не владелец лобби (т.е. присоединились к кому-то)
	if (EventParams.Lobby->OwnerAccountId != LocalAccountId)
	{
		OnLobbyJoined.Broadcast(CachedLobby);
	}

	QueryMemberDisplayName(LocalAccountId);
}

void ULobbySubsystem::HandleNativeLobbyLeft(const UE::Online::FLobbyLeft& EventParams)
{
	if (!bHasCachedLobby)
	{
		return;
	}
	FIsotopeLobbyBP OldLobby = CachedLobby;
	UE_LOG(LogLobbySubsystem, Log, TEXT("Lobby left. LobbyId=%s"), *OldLobby.LobbyId);
	bHasCachedLobby = false;
	CachedNativeLobbyId = FLobbyId{};
	CachedLobby = FIsotopeLobbyBP{};
	OnLobbyLeft.Broadcast(OldLobby);
}

void ULobbySubsystem::HandleNativeLobbyAttributesChanged(const UE::Online::FLobbyAttributesChanged& EventParams)
{
	CachedLobby = BuildLobbySnapshot(*EventParams.Lobby);
	UE_LOG(LogLobbySubsystem, Log, TEXT("Lobby attributes changed. LobbyId=%s Attributes=%d"), *CachedLobby.LobbyId, CachedLobby.Attributes.Num());
	OnLobbyAttributesChanged.Broadcast(CachedLobby);

	for (const TPair<FSchemaAttributeId, FSchemaVariant>& AddedAttribute : EventParams.AddedAttributes)
	{
		BroadcastLobbyAttributeUpdated(AddedAttribute.Key, AddedAttribute.Value);
	}
	for (const TPair<FSchemaAttributeId, TPair<FSchemaVariant, FSchemaVariant>>& ChangedAttribute : EventParams.ChangedAttributes)
	{
		BroadcastLobbyAttributeUpdated(ChangedAttribute.Key, ChangedAttribute.Value.Value);
	}
}

void ULobbySubsystem::HandleNativeLobbyMemberJoined(const UE::Online::FLobbyMemberJoined& EventParams)
{
	CachedLobby = BuildLobbySnapshot(*EventParams.Lobby);
	FIsotopeLobbyMemberBP MemberSnapshot = BuildMemberSnapshot(*EventParams.Member);
	UE_LOG(LogLobbySubsystem, Log, TEXT("Lobby member joined. LobbyId=%s PUID=%s Members=%d"), *CachedLobby.LobbyId, *MemberSnapshot.AccountId, CachedLobby.Members.Num());
	OnLobbyMemberJoined.Broadcast(CachedLobby, MemberSnapshot);
	QueryMemberDisplayName(EventParams.Member->AccountId);
}

void ULobbySubsystem::HandleNativeLobbyMemberLeft(const UE::Online::FLobbyMemberLeft& EventParams)
{
	FIsotopeLobbyMemberBP MemberSnapshot = BuildMemberSnapshot(*EventParams.Member);
	CachedLobby = BuildLobbySnapshot(*EventParams.Lobby);
	UE_LOG(LogLobbySubsystem, Log, TEXT("Lobby member left. LobbyId=%s PUID=%s Members=%d"), *CachedLobby.LobbyId, *MemberSnapshot.AccountId, CachedLobby.Members.Num());
	OnLobbyMemberLeft.Broadcast(CachedLobby, MemberSnapshot);
}

void ULobbySubsystem::HandleNativeLobbyMemberAttributesChanged(const UE::Online::FLobbyMemberAttributesChanged& EventParams)
{
	CachedLobby = BuildLobbySnapshot(*EventParams.Lobby);
	FIsotopeLobbyMemberBP MemberSnapshot = BuildMemberSnapshot(*EventParams.Member);
	OnLobbyMemberAttributesChanged.Broadcast(CachedLobby, MemberSnapshot);

	for (const TPair<FSchemaAttributeId, FSchemaVariant>& AddedAttribute : EventParams.AddedAttributes)
	{
		BroadcastLobbyMemberAttributeUpdated(MemberSnapshot, AddedAttribute.Key, AddedAttribute.Value);
	}
	for (const TPair<FSchemaAttributeId, TPair<FSchemaVariant, FSchemaVariant>>& ChangedAttribute : EventParams.ChangedAttributes)
	{
		BroadcastLobbyMemberAttributeUpdated(MemberSnapshot, ChangedAttribute.Key, ChangedAttribute.Value.Value);
	}
}

void ULobbySubsystem::HandleNativeLobbyLeaderChanged(const UE::Online::FLobbyLeaderChanged& EventParams)
{
	CachedLobby = BuildLobbySnapshot(*EventParams.Lobby);
	FIsotopeLobbyMemberBP LeaderMember;
	for (const FIsotopeLobbyMemberBP& M : CachedLobby.Members)
	{
		if (M.AccountId == GetNativeEOSProductUserId(EventParams.Lobby->OwnerAccountId))
		{
			LeaderMember = M;
			break;
		}
	}
	OnLobbyLeaderChanged.Broadcast(CachedLobby, LeaderMember);
}

// =============================================================================
// --- EOS SDK OVERLAY BUTTON (LEAVE PARTY) ---
// =============================================================================

void EOS_CALL ULobbySubsystem::OnLeaveLobbyRequestedCallback(const EOS_Lobby_LeaveLobbyRequestedCallbackInfo* Data)
{
	if (!Data || !Data->ClientData) return;

	ULobbySubsystem* This = static_cast<ULobbySubsystem*>(Data->ClientData);
	if (This) This->HandleEOSLeaveLobbyRequested(Data->LobbyId);
}

void ULobbySubsystem::HandleEOSLeaveLobbyRequested(const char* LobbyId)
{
	if (!LocalAccountId.IsValid() || !CachedNativeLobbyId.IsValid()) return;

	const FIsotopeLobbyBP PreviousLobby = CachedLobby;

	FLeaveLobby::Params Params;
	Params.LocalAccountId = LocalAccountId;
	Params.LobbyId = CachedNativeLobbyId;


	Lobbies->LeaveLobby(MoveTemp(Params));

}

void ULobbySubsystem::ConnectToSessionById(const FString& SessionIdStr, const FString& JoinTicket)
{
	UE_LOG(LogLobbySubsystem, Log, TEXT("Session connection requested. SessionId=%s"), *SessionIdStr);
	if (SessionIdStr.IsEmpty())
	{
		OnSessionJoinFailed.Broadcast(SessionIdStr, TEXT("SessionId is empty"));
		return;
	}
	UE_LOG(LogLobbySubsystem, Log, TEXT("Online Services initialized"));

	if (JoinTicket.IsEmpty())
	{
		OnSessionJoinFailed.Broadcast(SessionIdStr, TEXT("JoinTicket is empty"));
		return;
	}

	if (!EnsureLoggedInAndReady(true))
	{
		OnSessionJoinFailed.Broadcast(SessionIdStr, TEXT("Not logged in"));
		return;
	}

	if (!Sessions.IsValid())
	{
		if (!bServicesReady)
		{
			InitializeOnlineServices();
		}

		if (!Sessions.IsValid())
		{
			OnSessionJoinFailed.Broadcast(SessionIdStr, TEXT("Sessions interface not available"));
			return;
		}
	}

	// UE 5.6 OSSv2: если у тебя этот метод называется иначе,
	// см. Online/OnlineId.h / Online/Sessions.h.
	const FOnlineSessionId SessionId = FOnlineSessionIdRegistryEOSGS::GetRegistered(
		Services->GetServicesProvider()).BasicRegistry.FindOrAddHandle(SessionIdStr);
	const TOnlineResult<FGetSessionById> CachedSessionResult = Sessions->GetSessionById({ SessionId });
	if (!CachedSessionResult.IsOk())
	{
		OnSessionJoinStarted.Broadcast(SessionIdStr);

		FFindSessions::Params FindParams;
		FindParams.LocalAccountId = LocalAccountId;
		FindParams.MaxResults = 1;
		FindParams.SessionId = SessionId;

		TWeakObjectPtr<ULobbySubsystem> WeakThis(this);
		Sessions->FindSessions(MoveTemp(FindParams))
			.OnComplete(this, [WeakThis, SessionIdStr, JoinTicket](const TOnlineResult<FFindSessions>& FindResult)
				{
					if (!WeakThis.IsValid())
					{
						return;
					}

					ULobbySubsystem* This = WeakThis.Get();
					if (!FindResult.IsOk())
					{
						const FString ErrorStr = FindResult.GetErrorValue().GetLogString();
						UE_LOG(LogLobbySubsystem, Error, TEXT("FindSessions failed: %s"), *ErrorStr);
						This->OnSessionJoinFailed.Broadcast(SessionIdStr, ErrorStr);
						return;
					}

					if (FindResult.GetOkValue().FoundSessionIds.IsEmpty())
					{
						This->OnSessionJoinFailed.Broadcast(SessionIdStr, TEXT("Session not found"));
						return;
					}

					This->ConnectToSessionById(SessionIdStr, JoinTicket);
				});
		return;
	}

	OnSessionJoinStarted.Broadcast(SessionIdStr);

	FJoinSession::Params JoinParams;
	JoinParams.LocalAccountId = LocalAccountId;
	JoinParams.SessionId = SessionId;
	JoinParams.SessionName = FName(TEXT("GameSession"));

	TWeakObjectPtr<ULobbySubsystem> WeakThis(this);

	Sessions->JoinSession(MoveTemp(JoinParams))
		.OnComplete(this, [WeakThis, SessionId, SessionIdStr, JoinTicket](const TOnlineResult<FJoinSession>& Result)
			{
				if (!WeakThis.IsValid())
				{
					return;
				}

				ULobbySubsystem* This = WeakThis.Get();

				if (!Result.IsOk())
				{
					const FString ErrorStr = Result.GetErrorValue().GetLogString();

					UE_LOG(LogLobbySubsystem, Error, TEXT("JoinSession failed: %s"), *ErrorStr);
					This->OnSessionJoinFailed.Broadcast(SessionIdStr, ErrorStr);
					return;
				}

				const TOnlineResult<FGetSessionById> SessionResult = This->Sessions->GetSessionById({ SessionId });
				if (!SessionResult.IsOk())
				{
					const FString ErrorStr = SessionResult.GetErrorValue().GetLogString();

					UE_LOG(LogLobbySubsystem, Error, TEXT("GetSessionById failed: %s"), *ErrorStr);
					This->OnSessionJoinFailed.Broadcast(SessionIdStr, ErrorStr);
					return;
				}

				const FCustomSessionSetting* HostAddressSetting =
					SessionResult.GetOkValue().Session->GetSessionSettings().CustomSettings.Find(EOSGS_HOST_ADDRESS_ATTRIBUTE_KEY);

				if (!HostAddressSetting || HostAddressSetting->Data.GetType() != ESchemaAttributeType::String)
				{
					This->OnSessionJoinFailed.Broadcast(SessionIdStr, TEXT("Session host address is missing"));
					return;
				}

				const FString ConnectString = HostAddressSetting->Data.GetString();

				if (ConnectString.IsEmpty())
				{
					This->OnSessionJoinFailed.Broadcast(SessionIdStr, TEXT("Session host address is empty"));
					return;
				}

				UE_LOG(LogLobbySubsystem, Log, TEXT("Session joined. ConnectString=%s"), *ConnectString);

				UGameInstance* GI = This->GetGameInstance();
				APlayerController* PC = GI ? GI->GetFirstLocalPlayerController() : nullptr;

				if (!PC)
				{
					This->OnSessionJoinFailed.Broadcast(SessionIdStr, TEXT("No local PlayerController for ClientTravel"));
					return;
				}

				This->OnSessionJoinSucceeded.Broadcast(SessionIdStr, ConnectString);
				const FString TravelUrl = FString::Printf(
					TEXT("%s?JoinTicket=%s"),
					*ConnectString,
					*JoinTicket);
				PC->ClientTravel(TravelUrl, TRAVEL_Absolute);
			});
}

// =============================================================================
// --- HELPERS / SNAPSHOTS ---
// =============================================================================

void ULobbySubsystem::QueryMemberDisplayName(const UE::Online::FAccountId& AccountId)
{
	if (!UserInfo.IsValid() || !LocalAccountId.IsValid()) return;

	FQueryUserInfo::Params QueryParams;
	QueryParams.LocalAccountId = LocalAccountId;
	QueryParams.AccountIds = { AccountId };

	UserInfo->QueryUserInfo(MoveTemp(QueryParams))
		.OnComplete(this, [this, AccountId](const TOnlineResult<FQueryUserInfo>& Result)
			{
				FString AccountIdStr = GetNativeEOSProductUserId(AccountId);

				if (!Result.IsOk())
				{
					UE_LOG(LogLobbySubsystem, Error, TEXT("QueryUserInfo failed for %s: %s"),
						*AccountIdStr, *Result.GetErrorValue().GetLogString());
					return;
				}

				FGetUserInfo::Params GetParams;
				GetParams.LocalAccountId = LocalAccountId;
				GetParams.AccountId = AccountId;

				const TOnlineResult<FGetUserInfo> InfoResult = UserInfo->GetUserInfo(MoveTemp(GetParams));
				if (InfoResult.IsOk())
				{
					FString DisplayName = InfoResult.GetOkValue().UserInfo->DisplayName;
					if (bHasCachedLobby)
					{
						for (FIsotopeLobbyMemberBP& Member : CachedLobby.Members)
						{
							if (Member.AccountId == AccountIdStr)
							{
								Member.DisplayName = DisplayName;
								break;
							}
						}
					}
					OnDisplayNameReady.Broadcast(AccountIdStr, DisplayName);
				}
			});
}

FIsotopeLobbyBP ULobbySubsystem::BuildLobbySnapshot(const FLobby& NativeLobby) const
{
	FIsotopeLobbyBP Out;

	Out.LobbyId.Empty();

	Out.OwnerAccountId = GetNativeEOSProductUserId(NativeLobby.OwnerAccountId);
	if (Out.OwnerAccountId.IsEmpty())
	{
		Out.OwnerAccountId = AccountIdToString(NativeLobby.OwnerAccountId);
	}

	Out.LocalName = NativeLobby.LocalName.ToString();
	Out.SchemaId = SchemaIdToString(NativeLobby.SchemaId);
	Out.MaxMembers = NativeLobby.MaxMembers;
	Out.JoinPolicy = LexToString(NativeLobby.JoinPolicy);

	for (const TPair<FSchemaAttributeId, FSchemaVariant>& Pair : NativeLobby.Attributes)
	{
		FString Key = AttrIdToString(Pair.Key);
		Out.Attributes.Add(Key, ConvertVariantToAttribute(Key, Pair.Value));

		if (Pair.Key.ToString() == TEXT("LobbyCorrelationId") && Pair.Value.VariantData.IsType<FString>())
		{
			Out.LobbyId = Pair.Value.VariantData.Get<FString>();
		}
	}

	for (const TPair<FAccountId, TSharedRef<const FLobbyMember>>& Pair : NativeLobby.Members)
	{
		Out.Members.Add(BuildMemberSnapshot(*Pair.Value));
	}

	Out.Members.Sort([](const FIsotopeLobbyMemberBP& A, const FIsotopeLobbyMemberBP& B)
		{
			if (A.bIsLocalMember && !B.bIsLocalMember) return true;
			if (!A.bIsLocalMember && B.bIsLocalMember) return false;
			return A.AccountId < B.AccountId;
		});
	return Out;
}

FIsotopeLobbyMemberBP ULobbySubsystem::BuildMemberSnapshot(const FLobbyMember& NativeMember) const
{
	FIsotopeLobbyMemberBP Out;
	Out.AccountId = GetNativeEOSProductUserId(NativeMember.AccountId);
	Out.bIsLocalMember = NativeMember.bIsLocalMember;
	Out.DisplayName = Out.AccountId;

	if (UserInfo.IsValid() && LocalAccountId.IsValid())
	{
		FGetUserInfo::Params GetParams;
		GetParams.LocalAccountId = LocalAccountId;
		GetParams.AccountId = NativeMember.AccountId;

		const TOnlineResult<FGetUserInfo> InfoResult = UserInfo->GetUserInfo(MoveTemp(GetParams));
		if (InfoResult.IsOk())
		{
			Out.DisplayName = InfoResult.GetOkValue().UserInfo->DisplayName;
		}
	}

	for (const TPair<FSchemaAttributeId, FSchemaVariant>& Pair : NativeMember.Attributes)
	{
		FString Key = AttrIdToString(Pair.Key);
		Out.Attributes.Add(Key, ConvertVariantToAttribute(Key, Pair.Value));
	}

	return Out;
}

FIsotopeAttribute ULobbySubsystem::ConvertVariantToAttribute(const FString& Key, const FSchemaVariant& Variant) const
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

void ULobbySubsystem::BroadcastLobbyAttributeUpdated(const FSchemaAttributeId& AttributeId, const FSchemaVariant& Value)
{
	const FString Name = AttrIdToString(AttributeId);
	TArray<FOnLobbyAttributeUpdatedDelegate>* Delegates = LobbyAttributeUpdatedDelegates.Find(Name);
	if (!Delegates) return;
	Delegates->RemoveAll([](const FOnLobbyAttributeUpdatedDelegate& Delegate) { return !Delegate.IsBound(); });
	if (Delegates->IsEmpty())
	{
		LobbyAttributeUpdatedDelegates.Remove(Name);
		return;
	}

	const FIsotopeAttribute Attribute = ConvertVariantToAttribute(Name, Value);
	const TArray<FOnLobbyAttributeUpdatedDelegate> DelegatesCopy = *Delegates;
	for (const FOnLobbyAttributeUpdatedDelegate& Delegate : DelegatesCopy)
	{
		Delegate.ExecuteIfBound(Attribute);
	}
}

void ULobbySubsystem::BroadcastLobbyMemberAttributeUpdated(const FIsotopeLobbyMemberBP& Member, const FSchemaAttributeId& AttributeId, const FSchemaVariant& Value)
{
	const FString Name = AttrIdToString(AttributeId);
	TArray<FOnLobbyMemberAttributeUpdatedDelegate>* Delegates = LobbyMemberAttributeUpdatedDelegates.Find(Name);
	if (!Delegates) return;
	Delegates->RemoveAll([](const FOnLobbyMemberAttributeUpdatedDelegate& Delegate) { return !Delegate.IsBound(); });
	if (Delegates->IsEmpty())
	{
		LobbyMemberAttributeUpdatedDelegates.Remove(Name);
		return;
	}

	const FIsotopeAttribute Attribute = ConvertVariantToAttribute(Name, Value);
	const TArray<FOnLobbyMemberAttributeUpdatedDelegate> DelegatesCopy = *Delegates;
	for (const FOnLobbyMemberAttributeUpdatedDelegate& Delegate : DelegatesCopy)
	{
		Delegate.ExecuteIfBound(Member, Attribute);
	}
}

FString ULobbySubsystem::GetPlayerPUID(APlayerState* PlayerState) const
{
	if (!PlayerState) return TEXT("");

	const FUniqueNetIdRepl UniqueNetIdRepl = PlayerState->GetUniqueId();
	if (!UniqueNetIdRepl.IsValid() || !UniqueNetIdRepl.IsV2()) return TEXT("");

	return GetNativeEOSProductUserId(UniqueNetIdRepl.GetV2());
}

void ULobbySubsystem::QueryExternalAuthCredential(FOnExternalAuthCredentialReady Completion)
{
	if (!EnsureLoggedInAndReady(false) || !Auth.IsValid())
	{
		Completion.ExecuteIfBound(false, TEXT(""), TEXT(""), TEXT("EOS Auth is unavailable"));
		return;
	}

	FAuthQueryExternalAuthToken::Params Params;
	Params.LocalAccountId = LocalAccountId;
	Params.Method = EExternalAuthTokenMethod::Primary;

	Auth->QueryExternalAuthToken(MoveTemp(Params))
		.OnComplete(this, [Completion](const TOnlineResult<FAuthQueryExternalAuthToken>& Result) mutable
			{
				if (!Result.IsOk())
				{
					Completion.ExecuteIfBound(false, TEXT(""), TEXT(""), Result.GetErrorValue().GetLogString());
					return;
				}

				const FExternalAuthToken& Token = Result.GetOkValue().ExternalAuthToken;
				Completion.ExecuteIfBound(true, Token.Type.ToString(), Token.Data, TEXT(""));
			});
}

FString ULobbySubsystem::GetNativeEOSProductUserId(const UE::Online::FAccountId& AccountId) const
{
	EOS_ProductUserId PUID = GetProductUserIdFromAccountId(AccountId);
	if (!PUID) return TEXT("");

	char Buffer[EOS_PRODUCTUSERID_MAX_LENGTH + 1];
	int32 BufferLen = sizeof(Buffer);

	EOS_EResult Result = EOS_ProductUserId_ToString(PUID, Buffer, &BufferLen);
	if (Result == EOS_EResult::EOS_Success)
	{
		return FString(UTF8_TO_TCHAR(Buffer));
	}

	return TEXT("");
}
