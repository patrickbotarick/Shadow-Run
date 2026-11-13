#pragma once

#include "GameFramework/SaveGame.h"
#include "ShadowProfile.generated.h"

UCLASS()
class UShadowProfile : public USaveGame
{
    GENERATED_BODY()

public:
    UPROPERTY()
    int32 Version = 1;
    UPROPERTY()
    int32 Credits = 0;
    UPROPERTY()
    int32 BestScore = 0;
    UPROPERTY()
    int32 DamageLevel = 0;
    UPROPERTY()
    int32 ReloadLevel = 0;
    UPROPERTY()
    int32 HealthLevel = 0;
    UPROPERTY()
    int32 ShieldLevel = 0;
};
