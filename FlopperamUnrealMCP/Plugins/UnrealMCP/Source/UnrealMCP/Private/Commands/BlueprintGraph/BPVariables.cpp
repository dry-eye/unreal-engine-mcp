#include "Commands/BlueprintGraph/BPVariables.h"
#include "Commands/EpicUnrealMCPCommonUtils.h"
#include "Engine/Blueprint.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "EditorSubsystem.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "PropertyEditorModule.h"
#include "Modules/ModuleManager.h"

TSharedPtr<FJsonObject> FBPVariables::CreateVariable(const TSharedPtr<FJsonObject>& Params)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

    FString BlueprintName = Params->GetStringField(TEXT("blueprint_name"));
    FString VariableName = Params->GetStringField(TEXT("variable_name"));
    FString VariableType = Params->GetStringField(TEXT("variable_type"));

    // is_editable and is_public both map to CPF_Edit (Instance Editable). The read-side
    // API reports this flag as `is_editable`, so we accept that name as the primary write
    // alias and keep `is_public` for backward compatibility.
    bool IsEditable = false;
    if (Params->HasField(TEXT("is_editable")))
    {
        IsEditable = Params->GetBoolField(TEXT("is_editable"));
    }
    else if (Params->HasField(TEXT("is_public")))
    {
        IsEditable = Params->GetBoolField(TEXT("is_public"));
    }
    FString Tooltip = Params->HasField(TEXT("tooltip")) ? Params->GetStringField(TEXT("tooltip")) : TEXT("");
    FString Category = Params->HasField(TEXT("category")) ? Params->GetStringField(TEXT("category")) : TEXT("Default");
    FString SubtypeClassPath = Params->HasField(TEXT("variable_subtype_class"))
        ? Params->GetStringField(TEXT("variable_subtype_class"))
        : TEXT("");
    const bool bIsArray = Params->HasField(TEXT("is_array"))
        ? Params->GetBoolField(TEXT("is_array"))
        : false;

    UBlueprint* Blueprint = FEpicUnrealMCPCommonUtils::FindBlueprint(BlueprintName);

    if (!Blueprint)
    {
        Result->SetBoolField("success", false);
        Result->SetStringField("error", "Blueprint not found");
        return Result;
    }

    FEdGraphPinType VarType;
    FString TypeError;
    if (!ResolvePinType(VariableType, SubtypeClassPath, VarType, TypeError))
    {
        Result->SetBoolField("success", false);
        Result->SetStringField("error", TypeError);
        return Result;
    }
    if (bIsArray)
    {
        VarType.ContainerType = EPinContainerType::Array;
    }
    FName VarName = FName(*VariableName);

    if (FBlueprintEditorUtils::AddMemberVariable(Blueprint, VarName, VarType))
    {
        FBPVariableDescription& Variable = Blueprint->NewVariables.Last();
        Variable.FriendlyName = VariableName;
        Variable.Category = FText::FromString(Category);
        Variable.PropertyFlags = CPF_BlueprintVisible | CPF_BlueprintReadOnly;
        if (IsEditable)
        {
            Variable.PropertyFlags |= CPF_Edit;
        }

        if (!Tooltip.IsEmpty())
        {
            Variable.SetMetaData(FBlueprintMetadata::MD_Tooltip, Tooltip);
        }

        if (Params->HasField(TEXT("default_value")))
        {
            SetDefaultValue(Variable, Params->Values.FindRef("default_value"));
        }

        Blueprint->MarkPackageDirty();

        // Force immediate refresh of the Blueprint editor
        FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

        // Force asset registry update
        if (GEditor)
        {
            // Note: Asset registry notifications removed for UE5.5 compatibility
            // FAssetRegistryModule::AssetRegistryHelpers::GetAssetRegistry().AssetCreated(Blueprint);

            // Broadcast compilation event to refresh all editors
            // GEditor->BroadcastBlueprintCompiled(Blueprint); // Removed for UE5.5 compatibility

            // Additional refresh for property windows
            FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
            PropertyModule.NotifyCustomizationModuleChanged();
        }

        FKismetEditorUtilities::CompileBlueprint(Blueprint);

        Result->SetBoolField("success", true);

        TSharedPtr<FJsonObject> VarInfo = MakeShared<FJsonObject>();
        VarInfo->SetStringField("name", VariableName);
        VarInfo->SetStringField("type", VariableType);
        if (!SubtypeClassPath.IsEmpty())
        {
            VarInfo->SetStringField("subtype_class", SubtypeClassPath);
        }
        VarInfo->SetBoolField("is_editable", IsEditable);
        VarInfo->SetBoolField("is_public", IsEditable);
        VarInfo->SetBoolField("is_array", bIsArray);
        VarInfo->SetStringField("category", Category);

        Result->SetObjectField("variable", VarInfo);
    }
    else
    {
        Result->SetBoolField("success", false);
        Result->SetStringField("error", "Failed to create variable");
    }

    return Result;
}

TSharedPtr<FJsonObject> FBPVariables::SetVariableProperties(const TSharedPtr<FJsonObject>& Params)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

    FString BlueprintName = Params->GetStringField(TEXT("blueprint_name"));
    FString VariableName = Params->GetStringField(TEXT("variable_name"));

    UBlueprint* Blueprint = FEpicUnrealMCPCommonUtils::FindBlueprint(BlueprintName);

    if (!Blueprint)
    {
        Result->SetBoolField("success", false);
        Result->SetStringField("error", FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
        return Result;
    }

    // Find the variable in the Blueprint
    FBPVariableDescription* VarDesc = nullptr;
    for (FBPVariableDescription& Var : Blueprint->NewVariables)
    {
        if (Var.VarName == FName(*VariableName))
        {
            VarDesc = &Var;
            break;
        }
    }

    if (!VarDesc)
    {
        Result->SetBoolField("success", false);
        Result->SetStringField("error", FString::Printf(TEXT("Variable not found: %s"), *VariableName));
        return Result;
    }

    // Track which properties were updated
    TSharedPtr<FJsonObject> UpdatedProperties = MakeShared<FJsonObject>();

    // Update var_name (rename variable)
    if (Params->HasField(TEXT("var_name")))
    {
        FString NewVarName = Params->GetStringField(TEXT("var_name"));
        VarDesc->VarName = FName(*NewVarName);
        UpdatedProperties->SetStringField("var_name", NewVarName);
    }

    // Update var_type (change variable type)
    if (Params->HasField(TEXT("var_type")))
    {
        FString TypeString = Params->GetStringField(TEXT("var_type"));
        FString SubtypeClassPath = Params->HasField(TEXT("variable_subtype_class"))
            ? Params->GetStringField(TEXT("variable_subtype_class"))
            : TEXT("");
        // is_array is only honoured alongside a var_type change. To toggle the
        // array flag on an existing variable, send both var_type (same as current)
        // and is_array=true/false.
        const bool bIsArray = Params->HasField(TEXT("is_array"))
            ? Params->GetBoolField(TEXT("is_array"))
            : false;
        FEdGraphPinType NewType;
        FString TypeError;
        if (!ResolvePinType(TypeString, SubtypeClassPath, NewType, TypeError))
        {
            Result->SetBoolField("success", false);
            Result->SetStringField("error", TypeError);
            return Result;
        }
        if (bIsArray)
        {
            NewType.ContainerType = EPinContainerType::Array;
        }
        VarDesc->VarType = NewType;
        UpdatedProperties->SetStringField("var_type", TypeString);
        if (!SubtypeClassPath.IsEmpty())
        {
            UpdatedProperties->SetStringField("variable_subtype_class", SubtypeClassPath);
        }
        if (Params->HasField(TEXT("is_array")))
        {
            UpdatedProperties->SetBoolField("is_array", bIsArray);
        }
    }

    // Update is_blueprint_writable (Set node)
    if (Params->HasField(TEXT("is_blueprint_writable")))
    {
        bool bIsWritable = Params->GetBoolField(TEXT("is_blueprint_writable"));
        if (bIsWritable)
        {
            VarDesc->PropertyFlags &= ~CPF_BlueprintReadOnly;
        }
        else
        {
            VarDesc->PropertyFlags |= CPF_BlueprintReadOnly;
        }
        UpdatedProperties->SetBoolField("is_blueprint_writable", bIsWritable);
    }

    // Update is_editable (preferred) / is_public (legacy alias) - both map to CPF_Edit
    {
        const bool bHasEditable = Params->HasField(TEXT("is_editable"));
        const bool bHasPublic = Params->HasField(TEXT("is_public"));
        if (bHasEditable || bHasPublic)
        {
            const bool bIsEditable = bHasEditable
                ? Params->GetBoolField(TEXT("is_editable"))
                : Params->GetBoolField(TEXT("is_public"));
            if (bIsEditable)
            {
                VarDesc->PropertyFlags |= CPF_Edit;
            }
            else
            {
                VarDesc->PropertyFlags &= ~CPF_Edit;
            }
            UpdatedProperties->SetBoolField("is_editable", bIsEditable);
            UpdatedProperties->SetBoolField("is_public", bIsEditable);
        }
    }

    // Update is_editable_in_instance (opposite of CPF_DisableEditOnInstance)
    if (Params->HasField(TEXT("is_editable_in_instance")))
    {
        bool bIsEditable = Params->GetBoolField(TEXT("is_editable_in_instance"));
        if (bIsEditable)
        {
            VarDesc->PropertyFlags &= ~CPF_DisableEditOnInstance;
        }
        else
        {
            VarDesc->PropertyFlags |= CPF_DisableEditOnInstance;
        }
        UpdatedProperties->SetBoolField("is_editable_in_instance", bIsEditable);
    }

    // Update is_config
    if (Params->HasField(TEXT("is_config")))
    {
        bool bIsConfig = Params->GetBoolField(TEXT("is_config"));
        if (bIsConfig)
        {
            VarDesc->PropertyFlags |= CPF_Config;
        }
        else
        {
            VarDesc->PropertyFlags &= ~CPF_Config;
        }
        UpdatedProperties->SetBoolField("is_config", bIsConfig);
    }

    // Update friendly_name
    if (Params->HasField(TEXT("friendly_name")))
    {
        FString FriendlyName = Params->GetStringField(TEXT("friendly_name"));
        VarDesc->FriendlyName = FriendlyName;
        UpdatedProperties->SetStringField("friendly_name", FriendlyName);
    }

    // Update tooltip
    if (Params->HasField(TEXT("tooltip")))
    {
        FString Tooltip = Params->GetStringField(TEXT("tooltip"));
        VarDesc->SetMetaData(FBlueprintMetadata::MD_Tooltip, *Tooltip);
        UpdatedProperties->SetStringField("tooltip", Tooltip);
    }

    // Update category
    if (Params->HasField(TEXT("category")))
    {
        FString Category = Params->GetStringField(TEXT("category"));
        VarDesc->Category = FText::FromString(Category);
        UpdatedProperties->SetStringField("category", Category);
    }

    // Update replication_enabled (Row 15 - CPF_Net flag)
    if (Params->HasField(TEXT("replication_enabled")))
    {
        bool bReplicationEnabled = Params->GetBoolField(TEXT("replication_enabled"));
        if (bReplicationEnabled)
        {
            VarDesc->PropertyFlags |= CPF_Net;
        }
        else
        {
            VarDesc->PropertyFlags &= ~CPF_Net;
        }
        UpdatedProperties->SetBoolField("replication_enabled", bReplicationEnabled);
    }

    // Update replication_condition (Row 16 - ELifetimeCondition)
    if (Params->HasField(TEXT("replication_condition")))
    {
        int32 ReplicationConditionValue = (int32)Params->GetNumberField(TEXT("replication_condition"));
        VarDesc->ReplicationCondition = (ELifetimeCondition)ReplicationConditionValue;
        UpdatedProperties->SetNumberField("replication_condition", ReplicationConditionValue);
    }

    // Update is_private (Row 7 - MD_AllowPrivateAccess metadata)
    if (Params->HasField(TEXT("is_private")))
    {
        bool bIsPrivate = Params->GetBoolField(TEXT("is_private"));
        if (bIsPrivate)
        {
            VarDesc->SetMetaData(TEXT("AllowPrivateAccess"), TEXT("true"));
        }
        else
        {
            VarDesc->RemoveMetaData(TEXT("AllowPrivateAccess"));
        }
        UpdatedProperties->SetBoolField("is_private", bIsPrivate);
    }

    // Update expose_on_spawn (metadata)
    if (Params->HasField(TEXT("expose_on_spawn")))
    {
        bool bExposeOnSpawn = Params->GetBoolField(TEXT("expose_on_spawn"));
        if (bExposeOnSpawn)
        {
            VarDesc->SetMetaData(TEXT("ExposeOnSpawn"), TEXT("true"));
        }
        else
        {
            VarDesc->RemoveMetaData(TEXT("ExposeOnSpawn"));
        }
        UpdatedProperties->SetBoolField("expose_on_spawn", bExposeOnSpawn);
    }

    // Update default_value
    if (Params->HasField(TEXT("default_value")))
    {
        SetDefaultValue(*VarDesc, Params->Values.FindRef("default_value"));
        UpdatedProperties->SetStringField("default_value", "updated");
    }

    // Update expose_to_cinematics (CPF_Interp)
    if (Params->HasField(TEXT("expose_to_cinematics")))
    {
        bool bExposeToCinematics = Params->GetBoolField(TEXT("expose_to_cinematics"));
        if (bExposeToCinematics)
        {
            VarDesc->PropertyFlags |= CPF_Interp;
        }
        else
        {
            VarDesc->PropertyFlags &= ~CPF_Interp;
        }
        UpdatedProperties->SetBoolField("expose_to_cinematics", bExposeToCinematics);
    }

    // Update slider_range_min (MD_UIMin)
    if (Params->HasField(TEXT("slider_range_min")))
    {
        FString SliderMin = Params->GetStringField(TEXT("slider_range_min"));
        VarDesc->SetMetaData(TEXT("UIMin"), *SliderMin);
        UpdatedProperties->SetStringField("slider_range_min", SliderMin);
    }

    // Update slider_range_max (MD_UIMax)
    if (Params->HasField(TEXT("slider_range_max")))
    {
        FString SliderMax = Params->GetStringField(TEXT("slider_range_max"));
        VarDesc->SetMetaData(TEXT("UIMax"), *SliderMax);
        UpdatedProperties->SetStringField("slider_range_max", SliderMax);
    }

    // Update value_range_min (MD_ClampMin)
    if (Params->HasField(TEXT("value_range_min")))
    {
        FString ClampMin = Params->GetStringField(TEXT("value_range_min"));
        VarDesc->SetMetaData(TEXT("ClampMin"), *ClampMin);
        UpdatedProperties->SetStringField("value_range_min", ClampMin);
    }

    // Update value_range_max (MD_ClampMax)
    if (Params->HasField(TEXT("value_range_max")))
    {
        FString ClampMax = Params->GetStringField(TEXT("value_range_max"));
        VarDesc->SetMetaData(TEXT("ClampMax"), *ClampMax);
        UpdatedProperties->SetStringField("value_range_max", ClampMax);
    }

    // Update units (MD_Units)
    if (Params->HasField(TEXT("units")))
    {
        FString Units = Params->GetStringField(TEXT("units"));
        VarDesc->SetMetaData(TEXT("Units"), *Units);
        UpdatedProperties->SetStringField("units", Units);
    }

    // Update bitmask (MD_Bitmask)
    if (Params->HasField(TEXT("bitmask")))
    {
        bool bIsBitmask = Params->GetBoolField(TEXT("bitmask"));
        if (bIsBitmask)
        {
            VarDesc->SetMetaData(TEXT("Bitmask"), TEXT("true"));
        }
        else
        {
            VarDesc->RemoveMetaData(TEXT("Bitmask"));
        }
        UpdatedProperties->SetBoolField("bitmask", bIsBitmask);
    }

    // Update bitmask_enum (MD_BitmaskEnum)
    if (Params->HasField(TEXT("bitmask_enum")))
    {
        FString BitmaskEnum = Params->GetStringField(TEXT("bitmask_enum"));
        VarDesc->SetMetaData(TEXT("BitmaskEnum"), *BitmaskEnum);
        UpdatedProperties->SetStringField("bitmask_enum", BitmaskEnum);
    }

    // Mark Blueprint as modified and compile
    Blueprint->MarkPackageDirty();
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

    // Force property editor refresh for metadata changes
    // This ensures Details Panel dropdowns (Units, etc.) synchronize with metadata
    if (GEditor)
    {
        FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
        PropertyModule.NotifyCustomizationModuleChanged();
    }

    FKismetEditorUtilities::CompileBlueprint(Blueprint);

    Result->SetBoolField("success", true);
    Result->SetStringField("variable_name", VariableName);
    Result->SetObjectField("properties_updated", UpdatedProperties);
    Result->SetStringField("message", "Variable properties updated successfully");

    return Result;
}

namespace
{
    // Resolve a textual class/struct/enum reference to its UObject. Accepts full
    // package paths (preferred, e.g. "/Script/Engine.SplineComponent") and falls
    // back to a short-name lookup.
    template <typename TObject>
    TObject* ResolveTypeObject(const FString& Path)
    {
        if (Path.IsEmpty())
        {
            return nullptr;
        }
        TObject* Resolved = LoadObject<TObject>(nullptr, *Path);
        if (!Resolved)
        {
            // Short name fallback (e.g. "SplineComponent", "Pawn").
            Resolved = FindFirstObject<TObject>(*Path, EFindFirstObjectOptions::NativeFirst);
        }
        return Resolved;
    }
}

bool FBPVariables::ResolvePinType(
    const FString& TypeString,
    const FString& SubtypeClassPath,
    FEdGraphPinType& OutPinType,
    FString& OutError)
{
    OutPinType = FEdGraphPinType();
    OutError.Reset();

    auto RequiresSubtype = [&](const TCHAR* Kind, const TCHAR* Example) -> bool
    {
        if (SubtypeClassPath.IsEmpty())
        {
            OutError = FString::Printf(
                TEXT("variable_type='%s' requires variable_subtype_class (%s e.g. %s)"),
                *TypeString, Kind, Example);
            return false;
        }
        return true;
    };

    if (TypeString.Equals(TEXT("bool"), ESearchCase::IgnoreCase))
    {
        OutPinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
    }
    else if (TypeString.Equals(TEXT("int"), ESearchCase::IgnoreCase)
        || TypeString.Equals(TEXT("int32"), ESearchCase::IgnoreCase)
        || TypeString.Equals(TEXT("integer"), ESearchCase::IgnoreCase))
    {
        OutPinType.PinCategory = UEdGraphSchema_K2::PC_Int;
    }
    else if (TypeString.Equals(TEXT("int64"), ESearchCase::IgnoreCase))
    {
        OutPinType.PinCategory = UEdGraphSchema_K2::PC_Int64;
    }
    else if (TypeString.Equals(TEXT("byte"), ESearchCase::IgnoreCase))
    {
        OutPinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
    }
    else if (TypeString.Equals(TEXT("float"), ESearchCase::IgnoreCase)
        || TypeString.Equals(TEXT("real"), ESearchCase::IgnoreCase))
    {
        OutPinType.PinCategory = UEdGraphSchema_K2::PC_Real;
        OutPinType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
    }
    else if (TypeString.Equals(TEXT("double"), ESearchCase::IgnoreCase))
    {
        OutPinType.PinCategory = UEdGraphSchema_K2::PC_Real;
        OutPinType.PinSubCategory = UEdGraphSchema_K2::PC_Double;
    }
    else if (TypeString.Equals(TEXT("string"), ESearchCase::IgnoreCase))
    {
        OutPinType.PinCategory = UEdGraphSchema_K2::PC_String;
    }
    else if (TypeString.Equals(TEXT("name"), ESearchCase::IgnoreCase))
    {
        OutPinType.PinCategory = UEdGraphSchema_K2::PC_Name;
    }
    else if (TypeString.Equals(TEXT("text"), ESearchCase::IgnoreCase))
    {
        OutPinType.PinCategory = UEdGraphSchema_K2::PC_Text;
    }
    else if (TypeString.Equals(TEXT("vector"), ESearchCase::IgnoreCase))
    {
        OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
        OutPinType.PinSubCategoryObject = TBaseStructure<FVector>::Get();
    }
    else if (TypeString.Equals(TEXT("rotator"), ESearchCase::IgnoreCase))
    {
        OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
        OutPinType.PinSubCategoryObject = TBaseStructure<FRotator>::Get();
    }
    else if (TypeString.Equals(TEXT("transform"), ESearchCase::IgnoreCase))
    {
        OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
        OutPinType.PinSubCategoryObject = TBaseStructure<FTransform>::Get();
    }
    else if (TypeString.Equals(TEXT("object"), ESearchCase::IgnoreCase))
    {
        if (!RequiresSubtype(TEXT("object reference"), TEXT("/Script/Engine.SplineComponent")))
        {
            return false;
        }
        UClass* SubClass = ResolveTypeObject<UClass>(SubtypeClassPath);
        if (!SubClass)
        {
            OutError = FString::Printf(
                TEXT("Could not resolve variable_subtype_class '%s' to a UClass"),
                *SubtypeClassPath);
            return false;
        }
        OutPinType.PinCategory = UEdGraphSchema_K2::PC_Object;
        OutPinType.PinSubCategoryObject = SubClass;
    }
    else if (TypeString.Equals(TEXT("class"), ESearchCase::IgnoreCase))
    {
        if (!RequiresSubtype(TEXT("class reference"), TEXT("/Script/Engine.Pawn")))
        {
            return false;
        }
        UClass* SubClass = ResolveTypeObject<UClass>(SubtypeClassPath);
        if (!SubClass)
        {
            OutError = FString::Printf(
                TEXT("Could not resolve variable_subtype_class '%s' to a UClass"),
                *SubtypeClassPath);
            return false;
        }
        OutPinType.PinCategory = UEdGraphSchema_K2::PC_Class;
        OutPinType.PinSubCategoryObject = SubClass;
    }
    else if (TypeString.Equals(TEXT("soft_object"), ESearchCase::IgnoreCase)
        || TypeString.Equals(TEXT("softobject"), ESearchCase::IgnoreCase))
    {
        if (!RequiresSubtype(TEXT("soft object reference"), TEXT("/Script/Engine.Texture2D")))
        {
            return false;
        }
        UClass* SubClass = ResolveTypeObject<UClass>(SubtypeClassPath);
        if (!SubClass)
        {
            OutError = FString::Printf(
                TEXT("Could not resolve variable_subtype_class '%s' to a UClass"),
                *SubtypeClassPath);
            return false;
        }
        OutPinType.PinCategory = UEdGraphSchema_K2::PC_SoftObject;
        OutPinType.PinSubCategoryObject = SubClass;
    }
    else if (TypeString.Equals(TEXT("soft_class"), ESearchCase::IgnoreCase)
        || TypeString.Equals(TEXT("softclass"), ESearchCase::IgnoreCase))
    {
        if (!RequiresSubtype(TEXT("soft class reference"), TEXT("/Script/Engine.Pawn")))
        {
            return false;
        }
        UClass* SubClass = ResolveTypeObject<UClass>(SubtypeClassPath);
        if (!SubClass)
        {
            OutError = FString::Printf(
                TEXT("Could not resolve variable_subtype_class '%s' to a UClass"),
                *SubtypeClassPath);
            return false;
        }
        OutPinType.PinCategory = UEdGraphSchema_K2::PC_SoftClass;
        OutPinType.PinSubCategoryObject = SubClass;
    }
    else if (TypeString.Equals(TEXT("interface"), ESearchCase::IgnoreCase))
    {
        if (!RequiresSubtype(TEXT("interface"), TEXT("/Script/MyModule.MyInterface")))
        {
            return false;
        }
        UClass* SubClass = ResolveTypeObject<UClass>(SubtypeClassPath);
        if (!SubClass)
        {
            OutError = FString::Printf(
                TEXT("Could not resolve variable_subtype_class '%s' to a UClass"),
                *SubtypeClassPath);
            return false;
        }
        OutPinType.PinCategory = UEdGraphSchema_K2::PC_Interface;
        OutPinType.PinSubCategoryObject = SubClass;
    }
    else if (TypeString.Equals(TEXT("struct"), ESearchCase::IgnoreCase))
    {
        if (!RequiresSubtype(TEXT("struct"), TEXT("/Script/Engine.HitResult")))
        {
            return false;
        }
        UScriptStruct* SubStruct = ResolveTypeObject<UScriptStruct>(SubtypeClassPath);
        if (!SubStruct)
        {
            OutError = FString::Printf(
                TEXT("Could not resolve variable_subtype_class '%s' to a UScriptStruct"),
                *SubtypeClassPath);
            return false;
        }
        OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
        OutPinType.PinSubCategoryObject = SubStruct;
    }
    else if (TypeString.Equals(TEXT("enum"), ESearchCase::IgnoreCase))
    {
        if (!RequiresSubtype(TEXT("enum"), TEXT("/Script/Engine.ETraceTypeQuery")))
        {
            return false;
        }
        UEnum* SubEnum = ResolveTypeObject<UEnum>(SubtypeClassPath);
        if (!SubEnum)
        {
            OutError = FString::Printf(
                TEXT("Could not resolve variable_subtype_class '%s' to a UEnum"),
                *SubtypeClassPath);
            return false;
        }
        OutPinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
        OutPinType.PinSubCategoryObject = SubEnum;
    }
    else
    {
        OutError = FString::Printf(
            TEXT("Unsupported variable_type: '%s'. Supported: bool, int, int64, byte, float, double, "
                 "string, name, text, vector, rotator, transform, object, class, soft_object, "
                 "soft_class, interface, struct, enum"),
            *TypeString);
        return false;
    }

    return true;
}

void FBPVariables::SetDefaultValue(FBPVariableDescription& Variable, const TSharedPtr<FJsonValue>& Value)
{
    if (!Value.IsValid())
    {
        return;
    }

    FString StringValue;

    // Convert JSON value to string representation for default value
    if (Value->Type == EJson::String)
    {
        StringValue = Value->AsString();
    }
    else if (Value->Type == EJson::Number)
    {
        StringValue = FString::Printf(TEXT("%g"), Value->AsNumber());
    }
    else if (Value->Type == EJson::Boolean)
    {
        StringValue = Value->AsBool() ? TEXT("true") : TEXT("false");
    }
    else if (Value->Type == EJson::Null)
    {
        StringValue = TEXT("");
    }
    else
    {
        // For complex types, convert to empty string
        StringValue = TEXT("");
    }

    // Update Variable.DefaultValue for Blueprint display
    Variable.DefaultValue = StringValue;
}