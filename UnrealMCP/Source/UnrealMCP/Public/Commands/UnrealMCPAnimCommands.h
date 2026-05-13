#pragma once

#include "CoreMinimal.h"
#include "Json.h"

class FUnrealMCPAnimCommands
{
public:
    FUnrealMCPAnimCommands();
    TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
    TSharedPtr<FJsonObject> HandleReadAnimBlueprint(const TSharedPtr<FJsonObject>& Params);
};
