#pragma once

#include "CoreMinimal.h"

#include "IsotopeError.generated.h"

USTRUCT(BlueprintType)
struct FIsotopeError
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Isotope|Online")
	FString Method;

	UPROPERTY(BlueprintReadOnly, Category = "Isotope|Online")
	FString Error;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnIsotopeError, const FIsotopeError&, Error);
