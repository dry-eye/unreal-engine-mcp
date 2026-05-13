#include "Commands/BlueprintGraph/Nodes/UtilityNodes.h"
#include "Commands/BlueprintGraph/Nodes/NodeCreatorUtils.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Select.h"
#include "K2Node_SpawnActorFromClass.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet/KismetMathLibrary.h"
#include "Json.h"

namespace
{
    // Resolve a textual class reference to its UClass. Accepts full package paths
    // (preferred, e.g. "/Script/WheeledVehicleCoreRuntime.VehicleAutoTestLibrary"),
    // and falls back to a short-name lookup so "VehicleAutoTestLibrary" still works.
    UClass* ResolveTargetClass(const FString& Path)
    {
        if (Path.IsEmpty())
        {
            return nullptr;
        }
        if (UClass* Loaded = LoadObject<UClass>(nullptr, *Path))
        {
            return Loaded;
        }
        // Editor builds can fall back to a global search by short name.
        return FindFirstObject<UClass>(*Path, EFindFirstObjectOptions::NativeFirst);
    }
}

UK2Node* FUtilityNodeCreator::CreatePrintNode(UEdGraph* Graph, const TSharedPtr<FJsonObject>& Params)
{
	if (!Graph || !Params.IsValid())
	{
		return nullptr;
	}

	UK2Node_CallFunction* PrintNode = NewObject<UK2Node_CallFunction>(Graph);
	if (!PrintNode)
	{
		return nullptr;
	}

	UFunction* PrintFunc = UKismetSystemLibrary::StaticClass()->FindFunctionByName(
		GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, PrintString)
	);

	if (!PrintFunc)
	{
		return nullptr;
	}

	// Set function reference BEFORE initialization
	PrintNode->SetFromFunction(PrintFunc);

	double PosX, PosY;
	FNodeCreatorUtils::ExtractNodePosition(Params, PosX, PosY);
	PrintNode->NodePosX = static_cast<int32>(PosX);
	PrintNode->NodePosY = static_cast<int32>(PosY);

	Graph->AddNode(PrintNode, true, false);
	FNodeCreatorUtils::InitializeK2Node(PrintNode, Graph);

	// Set message if provided AFTER initialization
	FString Message;
	if (Params->TryGetStringField(TEXT("message"), Message))
	{
		UEdGraphPin* InStringPin = PrintNode->FindPin(TEXT("InString"));
		if (InStringPin)
		{
			InStringPin->DefaultValue = Message;
		}
	}

	return PrintNode;
}

UK2Node* FUtilityNodeCreator::CreateCallFunctionNode(UEdGraph* Graph, const TSharedPtr<FJsonObject>& Params)
{
	if (!Graph || !Params.IsValid())
	{
		return nullptr;
	}

	// Get target function name
	FString TargetFunction;
	if (!Params->TryGetStringField(TEXT("target_function"), TargetFunction))
	{
		return nullptr;
	}

	UK2Node_CallFunction* CallNode = NewObject<UK2Node_CallFunction>(Graph);
	if (!CallNode)
	{
		return nullptr;
	}

	// Resolve the call target. Priority:
	//   1. Explicit target_class param (loads UBlueprintFunctionLibrary or any UClass).
	//   2. Common engine libraries as fallback (KismetSystemLibrary, KismetMathLibrary).
	//   3. Self-member on the Blueprint that owns this graph (matches the BP editor's
	//      "Call Function on Self" behavior).
	// Accept both "target_class" (preferred) and "target_blueprint" (alias) so callers
	// don't need to know which key name the C++ layer expects.
	FString ClassName;
	const bool bHasTargetClass =
		(Params->TryGetStringField(TEXT("target_class"),     ClassName) && !ClassName.IsEmpty()) ||
		(Params->TryGetStringField(TEXT("target_blueprint"), ClassName) && !ClassName.IsEmpty());

	UFunction* TargetFunc = nullptr;
	UClass* ResolvedClass = nullptr;
	if (bHasTargetClass)
	{
		ResolvedClass = ResolveTargetClass(ClassName);
		if (!ResolvedClass)
		{
			// Surface a useful diagnostic into the log; AddNode wraps this in
			// "Failed to create CallFunction node" via the caller's CreateErrorResponse.
			UE_LOG(LogTemp, Warning,
				TEXT("CallFunction: target_class '%s' could not be resolved to a UClass."),
				*ClassName);
			return nullptr;
		}
		TargetFunc = ResolvedClass->FindFunctionByName(FName(*TargetFunction));
		if (!TargetFunc)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("CallFunction: function '%s' not found on target_class '%s' (%s)."),
				*TargetFunction, *ClassName, *ResolvedClass->GetPathName());
			return nullptr;
		}
	}
	else
	{
		TargetFunc = UKismetSystemLibrary::StaticClass()->FindFunctionByName(FName(*TargetFunction));
		if (!TargetFunc)
		{
			TargetFunc = UKismetMathLibrary::StaticClass()->FindFunctionByName(FName(*TargetFunction));
		}

		// Fall back to a self-member call on the owning Blueprint so callers can still
		// reach Blueprint-defined functions / inherited UFUNCTIONs without naming a class.
		if (!TargetFunc)
		{
			if (UBlueprint* OwningBP = Cast<UBlueprint>(Graph->GetOuter()))
			{
				if (UClass* BPClass = OwningBP->GeneratedClass ? OwningBP->GeneratedClass : OwningBP->SkeletonGeneratedClass)
				{
					TargetFunc = BPClass->FindFunctionByName(FName(*TargetFunction));
					if (TargetFunc)
					{
						ResolvedClass = BPClass;
					}
				}
			}
		}
	}

	if (!TargetFunc)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("CallFunction: function '%s' not found in engine libraries or self; pass target_class for external libraries."),
			*TargetFunction);
		return nullptr;
	}

	// Set function reference BEFORE initialization. SetFromFunction wires the external
	// reference (Class + Name), allocates pins from the function signature, and honors
	// metadata like WorldContext/Latent/Static.
	CallNode->SetFromFunction(TargetFunc);

	double PosX, PosY;
	FNodeCreatorUtils::ExtractNodePosition(Params, PosX, PosY);
	CallNode->NodePosX = static_cast<int32>(PosX);
	CallNode->NodePosY = static_cast<int32>(PosY);

	Graph->AddNode(CallNode, true, false);
	FNodeCreatorUtils::InitializeK2Node(CallNode, Graph);

	return CallNode;
}

UK2Node* FUtilityNodeCreator::CreateSelectNode(UEdGraph* Graph, const TSharedPtr<FJsonObject>& Params)
{
	if (!Graph || !Params.IsValid())
	{
		return nullptr;
	}

	UK2Node_Select* SelectNode = NewObject<UK2Node_Select>(Graph);
	if (!SelectNode)
	{
		return nullptr;
	}

	double PosX, PosY;
	FNodeCreatorUtils::ExtractNodePosition(Params, PosX, PosY);
	SelectNode->NodePosX = static_cast<int32>(PosX);
	SelectNode->NodePosY = static_cast<int32>(PosY);

	Graph->AddNode(SelectNode, true, false);
	FNodeCreatorUtils::InitializeK2Node(SelectNode, Graph);

	return SelectNode;
}

UK2Node* FUtilityNodeCreator::CreateSpawnActorNode(UEdGraph* Graph, const TSharedPtr<FJsonObject>& Params)
{
	if (!Graph || !Params.IsValid())
	{
		return nullptr;
	}

	UK2Node_SpawnActorFromClass* SpawnActorNode = NewObject<UK2Node_SpawnActorFromClass>(Graph);
	if (!SpawnActorNode)
	{
		return nullptr;
	}

	double PosX, PosY;
	FNodeCreatorUtils::ExtractNodePosition(Params, PosX, PosY);
	SpawnActorNode->NodePosX = static_cast<int32>(PosX);
	SpawnActorNode->NodePosY = static_cast<int32>(PosY);

	Graph->AddNode(SpawnActorNode, true, false);
	FNodeCreatorUtils::InitializeK2Node(SpawnActorNode, Graph);

	return SpawnActorNode;
}

