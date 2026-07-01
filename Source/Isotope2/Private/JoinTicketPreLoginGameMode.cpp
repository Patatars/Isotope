#include "JoinTicketPreLoginGameMode.h"

#include "Kismet/GameplayStatics.h"
#include "Online/OnlineIdEOSGS.h"

DEFINE_LOG_CATEGORY_STATIC(LogJoinTicketPreLogin, Log, All);

namespace
{
	FString GetConnectionPUID(const FUniqueNetIdRepl& UniqueId)
	{
		if (!UniqueId.IsValid() || !UniqueId.IsV2())
		{
			return TEXT("");
		}

		const EOS_ProductUserId ProductUserId = UE::Online::GetProductUserId(UniqueId.GetV2());
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
}

void AJoinTicketPreLoginGameMode::PreLoginAsync(
	const FString& Options,
	const FString& Address,
	const FUniqueNetIdRepl& UniqueId,
	const FOnPreLoginCompleteDelegate& OnComplete)
{
	const FString JoinTicket = UGameplayStatics::ParseOption(Options, TEXT("JoinTicket"));
	if (FApp::GetBuildConfiguration() == EBuildConfiguration::Development) {
		Super::PreLoginAsync(
			Options,
			Address,
			UniqueId,
			OnComplete);
		return;
	}
	if (JoinTicket.IsEmpty())
	{
		UE_LOG(LogJoinTicketPreLogin, Warning, TEXT("PreLogin rejected: join ticket is missing. Address=%s"), *Address);
		OnComplete.ExecuteIfBound(TEXT("Join ticket is required"));
		return;
	}

	const FString ConnectionPUID = GetConnectionPUID(UniqueId);
	if (ConnectionPUID.IsEmpty())
	{
		UE_LOG(LogJoinTicketPreLogin, Warning, TEXT("PreLogin rejected: EOS identity is missing. Address=%s"), *Address);
		OnComplete.ExecuteIfBound(TEXT("EOS identity is required"));
		return;
	}

	const int32 RequestId = NextPreLoginRequestId++;
	PendingPreLogins.Add(RequestId, { Options, Address, UniqueId, OnComplete });
	UE_LOG(LogJoinTicketPreLogin, Log, TEXT("Validation dispatched to Blueprint. RequestId=%d PUID=%s Address=%s"), RequestId, *ConnectionPUID, *Address);
	ValidateJoinTicket(RequestId, JoinTicket, ConnectionPUID);
}

void AJoinTicketPreLoginGameMode::CompleteJoinTicketPreLogin(
	const int32 RequestId,
	const bool bAllowLogin,
	const FString& Error)
{
	FPendingPreLogin PendingPreLogin;
	if (!PendingPreLogins.RemoveAndCopyValue(RequestId, PendingPreLogin))
	{
		UE_LOG(LogJoinTicketPreLogin, Warning, TEXT("Unknown or completed validation. RequestId=%d"), RequestId);
		return;
	}

	if (!bAllowLogin)
	{
		UE_LOG(LogJoinTicketPreLogin, Warning, TEXT("PreLogin rejected by Blueprint. RequestId=%d Error=%s"), RequestId, *Error);
		PendingPreLogin.Completion.ExecuteIfBound(
			Error.IsEmpty() ? TEXT("Join ticket validation failed") : Error);
		return;
	}

	UE_LOG(LogJoinTicketPreLogin, Log, TEXT("Join ticket accepted. RequestId=%d"), RequestId);

	Super::PreLoginAsync(
		PendingPreLogin.Options,
		PendingPreLogin.Address,
		PendingPreLogin.UniqueId,
		PendingPreLogin.Completion);
}

void AJoinTicketPreLoginGameMode::ValidateJoinTicket_Implementation(
	const int32 RequestId,
	const FString& ConnectionPUID,
	const FString& JoinTicket)
{
	CompleteJoinTicketPreLogin(RequestId, false, TEXT("Join ticket validation is not implemented"));
}
