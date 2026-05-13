#pragma once

#include "CoreMinimal.h"
#include "Json.h"

class FUnrealMCPPCGCommands
{
public:
    FUnrealMCPPCGCommands();
    TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
    TSharedPtr<FJsonObject> HandleReadPCGGraph(const TSharedPtr<FJsonObject>& Params);
};
