#include "ServerSessionSubsystem.h"

#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Online/OnlineIdEOSGS.h"

using namespace UE::Online;

void UServerSessionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	InitializeOnlineServices();
}

void UServerSessionSubsystem::Deinitialize()
{
	DestroyServerSession();

	if (SessionModificationHandle)
	{
		EOS_SessionModification_Release(SessionModificationHandle);
		SessionModificationHandle = nullptr;
	}

	EOSSessionsHandle = nullptr;
	Services.Reset();

	Super::Deinitialize();
}

void UServerSessionSubsystem::InitializeOnlineServices()
{
	Services = UE::Online::GetServices();

	if (!Services.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("ServerSessionSubsystem: Online Services not available"));
		return;
	}

	InitEOSSDKHandles();
}

bool UServerSessionSubsystem::InitEOSSDKHandles()
{
	IEOSSDKManager* SDKManager = IEOSSDKManager::Get();
	if (!SDKManager)
	{
		UE_LOG(LogTemp, Error, TEXT("ServerSessionSubsystem: EOS SDK Manager not available"));
		return false;
	}

	TArray<IEOSPlatformHandlePtr> Platforms = SDKManager->GetActivePlatforms();
	if (Platforms.Num() <= 0 || !Platforms[0].IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("ServerSessionSubsystem: No active EOS platform handles"));
		return false;
	}

	EOS_HPlatform PlatformHandle = *Platforms[0];
	if (!PlatformHandle)
	{
		UE_LOG(LogTemp, Error, TEXT("ServerSessionSubsystem: EOS platform handle is null"));
		return false;
	}

	EOSSessionsHandle = EOS_Platform_GetSessionsInterface(PlatformHandle);
	if (!EOSSessionsHandle)
	{
		UE_LOG(LogTemp, Error, TEXT("ServerSessionSubsystem: EOS Sessions handle is null"));
		return false;
	}

	UE_LOG(LogTemp, Log, TEXT("ServerSessionSubsystem: EOS Sessions handle ready"));
	return true;
}

void UServerSessionSubsystem::CreateServerSession(int32 MaxPlayers)
{
	if (!EOSSessionsHandle && !InitEOSSDKHandles())
	{
		UE_LOG(LogTemp, Error, TEXT("CreateServerSession: EOS Sessions handle invalid"));
		OnServerSessionCreated.Broadcast(TEXT(""));
		return;
	}

	CreateNativeEOSSession(MaxPlayers);
}

void UServerSessionSubsystem::CreateNativeEOSSession(int32 MaxPlayers)
{
	if (!EOSSessionsHandle)
	{
		UE_LOG(LogTemp, Error, TEXT("CreateNativeEOSSession: EOSSessionsHandle invalid"));
		OnServerSessionCreated.Broadcast(TEXT(""));
		return;
	}

	const int32 SafeMaxPlayers = FMath::Max(1, MaxPlayers);

	EOS_Sessions_CreateSessionModificationOptions CreateOptions = {};
	CreateOptions.ApiVersion = EOS_SESSIONS_CREATESESSIONMODIFICATION_API_LATEST;

	FTCHARToUTF8 SessionNameUtf8(NativeSessionName);
	FTCHARToUTF8 BucketIdUtf8(NativeBucketId);

	CreateOptions.SessionName = SessionNameUtf8.Get();
	CreateOptions.BucketId = BucketIdUtf8.Get();
	CreateOptions.MaxPlayers = static_cast<uint32_t>(SafeMaxPlayers);
	CreateOptions.bPresenceEnabled = EOS_FALSE;
	CreateOptions.LocalUserId = nullptr;

	EOS_HSessionModification NewModificationHandle = nullptr;

	const EOS_EResult CreateResult = EOS_Sessions_CreateSessionModification(
		EOSSessionsHandle,
		&CreateOptions,
		&NewModificationHandle
	);

	if (CreateResult != EOS_EResult::EOS_Success)
	{
		UE_LOG(LogTemp, Error, TEXT("EOS_Sessions_CreateSessionModification failed: %d"),
			static_cast<int32>(CreateResult));

		OnServerSessionCreated.Broadcast(TEXT(""));
		return;
	}

	SessionModificationHandle = NewModificationHandle;

	EOS_SessionModification_SetPermissionLevelOptions PermissionOptions = {};
	PermissionOptions.ApiVersion = EOS_SESSIONMODIFICATION_SETPERMISSIONLEVEL_API_LATEST;
	PermissionOptions.PermissionLevel = EOS_EOnlineSessionPermissionLevel::EOS_OSPF_PublicAdvertised;

	EOS_SessionModification_SetPermissionLevel(SessionModificationHandle, &PermissionOptions);

	// allow joining while match is already running
	EOS_SessionModification_SetJoinInProgressAllowedOptions JoinOptions = {};
	JoinOptions.ApiVersion = EOS_SESSIONMODIFICATION_SETJOININPROGRESSALLOWED_API_LATEST;
	JoinOptions.bAllowJoinInProgress = EOS_TRUE;

	EOS_SessionModification_SetJoinInProgressAllowed(SessionModificationHandle, &JoinOptions);

	EOS_SessionModification_SetInvitesAllowedOptions InvitesOptions = {};
	InvitesOptions.ApiVersion = EOS_SESSIONMODIFICATION_SETINVITESALLOWED_API_LATEST;
	InvitesOptions.bInvitesAllowed = EOS_TRUE;

	EOS_SessionModification_SetInvitesAllowed(SessionModificationHandle, &InvitesOptions);

	EOS_SessionModification_SetHostAddressOptions HostOptions = {};
	HostOptions.ApiVersion = EOS_SESSIONMODIFICATION_SETHOSTADDRESS_API_LATEST;

	FString HostAddress;
	if (!FParse::Value(FCommandLine::Get(), TEXT("SessionHostAddress="), HostAddress))
	{
		HostAddress = TEXT("127.0.0.1:7777");
	}

	FTCHARToUTF8 HostAddressUtf8(*HostAddress);
	HostOptions.HostAddress = HostAddressUtf8.Get();

	EOS_SessionModification_SetHostAddress(SessionModificationHandle, &HostOptions);

	UE_LOG(LogTemp, Log, TEXT("Creating native EOS server session. HostAddress=%s"), *HostAddress);

	// debug/portal marker
	AddStringAttribute(
		SessionModificationHandle,
		"SessionType",
		"Session",
		EOS_ESessionAttributeAdvertisementType::EOS_SAAT_Advertise
	);

	EOS_Sessions_UpdateSessionOptions UpdateOptions = {};
	UpdateOptions.ApiVersion = EOS_SESSIONS_UPDATESESSION_API_LATEST;
	UpdateOptions.SessionModificationHandle = SessionModificationHandle;

	EOS_Sessions_UpdateSession(
		EOSSessionsHandle,
		&UpdateOptions,
		this,
		&UServerSessionSubsystem::OnUpdateSessionComplete
	);
}

bool UServerSessionSubsystem::AddStringAttribute(
	EOS_HSessionModification ModificationHandle,
	const char* Key,
	const char* Value,
	EOS_ESessionAttributeAdvertisementType AdvertisementType
)
{
	if (!ModificationHandle || !Key || !Value)
	{
		return false;
	}

	EOS_Sessions_AttributeData AttributeData = {};
	AttributeData.ApiVersion = EOS_SESSIONS_ATTRIBUTEDATA_API_LATEST;
	AttributeData.Key = Key;
	AttributeData.ValueType = EOS_ESessionAttributeType::EOS_AT_STRING;
	AttributeData.Value.AsUtf8 = Value;

	EOS_SessionModification_AddAttributeOptions Options = {};
	Options.ApiVersion = EOS_SESSIONMODIFICATION_ADDATTRIBUTE_API_LATEST;
	Options.SessionAttribute = &AttributeData;
	Options.AdvertisementType = AdvertisementType;

	const EOS_EResult Result = EOS_SessionModification_AddAttribute(
		ModificationHandle,
		&Options
	);

	if (Result != EOS_EResult::EOS_Success)
	{
		UE_LOG(LogTemp, Warning, TEXT("EOS_SessionModification_AddAttribute failed for key [%s]: %d"),
			UTF8_TO_TCHAR(Key),
			static_cast<int32>(Result));

		return false;
	}

	return true;
}

void EOS_CALL UServerSessionSubsystem::OnUpdateSessionComplete(
	const EOS_Sessions_UpdateSessionCallbackInfo* Data
)
{
	if (!Data || !Data->ClientData)
	{
		return;
	}

	UServerSessionSubsystem* This = static_cast<UServerSessionSubsystem*>(Data->ClientData);
	if (!This)
	{
		return;
	}

	if (Data->ResultCode == EOS_EResult::EOS_Success)
	{
		This->CurrentSessionId = UTF8_TO_TCHAR(Data->SessionId);

		UE_LOG(LogTemp, Log, TEXT("Native EOS server session created. SessionId=%s"),
			*This->CurrentSessionId);

		This->OnServerSessionCreated.Broadcast(This->CurrentSessionId);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("EOS_Sessions_UpdateSession failed: %d"),
			static_cast<int32>(Data->ResultCode));

		This->CurrentSessionId.Empty();
		This->OnServerSessionCreated.Broadcast(TEXT(""));
	}

	if (This->SessionModificationHandle)
	{
		EOS_SessionModification_Release(This->SessionModificationHandle);
		This->SessionModificationHandle = nullptr;
	}
}

void UServerSessionSubsystem::DestroyServerSession()
{
	if (!EOSSessionsHandle || CurrentSessionId.IsEmpty())
	{
		OnServerSessionDestroyed.Broadcast(false);
		return;
	}

	EOS_Sessions_DestroySessionOptions Options = {};
	Options.ApiVersion = EOS_SESSIONS_DESTROYSESSION_API_LATEST;

	FTCHARToUTF8 SessionNameUtf8(NativeSessionName);
	Options.SessionName = SessionNameUtf8.Get();

	EOS_Sessions_DestroySession(
		EOSSessionsHandle,
		&Options,
		this,
		&UServerSessionSubsystem::OnDestroySessionComplete
	);
}

void EOS_CALL UServerSessionSubsystem::OnDestroySessionComplete(
	const EOS_Sessions_DestroySessionCallbackInfo* Data
)
{
	if (!Data || !Data->ClientData)
	{
		return;
	}

	UServerSessionSubsystem* This = static_cast<UServerSessionSubsystem*>(Data->ClientData);
	if (!This)
	{
		return;
	}

	const bool bSuccess = Data->ResultCode == EOS_EResult::EOS_Success;

	if (bSuccess)
	{
		UE_LOG(LogTemp, Log, TEXT("Native EOS server session destroyed"));
		This->CurrentSessionId.Empty();
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("EOS_Sessions_DestroySession failed: %d"),
			static_cast<int32>(Data->ResultCode));
	}

	This->OnServerSessionDestroyed.Broadcast(bSuccess);
}

FString UServerSessionSubsystem::GetPlayerUniqueNetIdString(APlayerState* PlayerState)
{
	if (!PlayerState)
	{
		return TEXT("");
	}

	const FUniqueNetIdRepl UniqueId = PlayerState->GetUniqueId();

	if (!UniqueId.IsValid())
	{
		return TEXT("");
	}

	return UniqueId.ToString();
}

FString UServerSessionSubsystem::GetPlayerPUID(APlayerState* PlayerState)
{
	if (!PlayerState)
	{
		return TEXT("");
	}

	const FUniqueNetIdRepl UniqueId = PlayerState->GetUniqueId();
	if (!UniqueId.IsValid() || !UniqueId.IsV2())
	{
		return TEXT("");
	}

	const EOS_ProductUserId ProductUserId = GetProductUserId(UniqueId.GetV2());
	if (!ProductUserId)
	{
		return TEXT("");
	}

	char Buffer[EOS_PRODUCTUSERID_MAX_LENGTH + 1];
	int32 BufferLength = sizeof(Buffer);
	if (EOS_ProductUserId_ToString(ProductUserId, Buffer, &BufferLength) != EOS_EResult::EOS_Success)
	{
		return TEXT("");
	}

	return UTF8_TO_TCHAR(Buffer);
}

bool UServerSessionSubsystem::IsPlayerInCurrentSession(APlayerState* PlayerState) const
{
	if (!EOSSessionsHandle || CurrentSessionId.IsEmpty())
	{
		return false;
	}

	const FString PlayerPUID = GetPlayerPUID(PlayerState);
	if (PlayerPUID.IsEmpty())
	{
		return false;
	}

	const EOS_ProductUserId ProductUserId = EOS_ProductUserId_FromString(TCHAR_TO_UTF8(*PlayerPUID));
	if (EOS_ProductUserId_IsValid(ProductUserId) == EOS_FALSE)
	{
		return false;
	}

	EOS_Sessions_IsUserInSessionOptions Options = {};
	Options.ApiVersion = EOS_SESSIONS_ISUSERINSESSION_API_LATEST;

	FTCHARToUTF8 SessionNameUtf8(NativeSessionName);
	Options.SessionName = SessionNameUtf8.Get();
	Options.TargetUserId = ProductUserId;

	return EOS_Sessions_IsUserInSession(EOSSessionsHandle, &Options) == EOS_EResult::EOS_Success;
}
