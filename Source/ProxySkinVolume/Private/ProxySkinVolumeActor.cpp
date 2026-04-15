#include "ProxySkinVolumeActor.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Components/BoxComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SphereComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GeometryScript/MeshAssetFunctions.h"
#include "GeometryScript/MeshBooleanFunctions.h"
#include "GeometryScript/MeshPrimitiveFunctions.h"
#include "GeometryScript/MeshSimplifyFunctions.h"
#include "GeometryScript/MeshTransformFunctions.h"
#include "IAssetTools.h"
#include "IMeshReductionManagerModule.h"
#include "Materials/MaterialInterface.h"
#include "MeshMerge/MeshMergingSettings.h"
#include "MeshMerge/MeshProxySettings.h"
#include "MeshMergeModule.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeExit.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"
#include "UDynamicMesh.h"

DEFINE_LOG_CATEGORY(LogProxySkinVolume);

namespace
{
	const FName ProxySkinTempActorTag(TEXT("ProxySkinVolume_Temp"));

	FString BuildDebugSourceName(const AActor* OwnerActor, const UActorComponent* Component, int32 OptionalIndex = INDEX_NONE)
	{
		const FString ActorName = OwnerActor ? OwnerActor->GetActorNameOrLabel() : TEXT("UnknownActor");
		const FString ComponentName = Component ? Component->GetName() : TEXT("UnknownComponent");
		if (OptionalIndex != INDEX_NONE)
		{
			return FString::Printf(TEXT("%s.%s[%d]"), *ActorName, *ComponentName, OptionalIndex);
		}
		return FString::Printf(TEXT("%s.%s"), *ActorName, *ComponentName);
	}
}

AProxySkinVolume::AProxySkinVolume()
{
	PrimaryActorTick.bCanEverTick = false;
	bIsEditorOnlyActor = true;
	bRunConstructionScriptOnDrag = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	VolumeBox = CreateDefaultSubobject<UBoxComponent>(TEXT("VolumeBox"));
	VolumeBox->SetupAttachment(Root);
	VolumeBox->SetBoxExtent(FVector(1500.0f, 1500.0f, 800.0f));
	VolumeBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	VolumeBox->SetGenerateOverlapEvents(false);
	VolumeBox->ShapeColor = FColor(60, 220, 120, 255);

	PivotSphere = CreateDefaultSubobject<USphereComponent>(TEXT("PivotSphere"));
	PivotSphere->SetupAttachment(Root);
	PivotSphere->SetSphereRadius(20.0f);
	PivotSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PivotSphere->SetGenerateOverlapEvents(false);
	PivotSphere->ShapeColor = FColor(255, 210, 30, 255);

	OutputFolder.Path = TEXT("/Game/Generated");
}

void AProxySkinVolume::BakeProxySkin()
{
	FString ValidationError;
	if (!ValidateBakeContext(ValidationError))
	{
		UE_LOG(LogProxySkinVolume, Error, TEXT("Bake aborted: %s"), *ValidationError);
		return;
	}

	UWorld* World = GetWorld();
	check(World != nullptr);

	const FVector WorkspaceOrigin = VolumeBox->GetComponentLocation();

	UE_LOG(LogProxySkinVolume, Log, TEXT("Bake started for '%s'"), *GetActorNameOrLabel());
	UE_LOG(LogProxySkinVolume, Log, TEXT("ProxySkinVolume pipeline revision: PSV_R4_MERGE_ONLY"));
	if (bVerboseLogging)
	{
		UE_LOG(LogProxySkinVolume, Log, TEXT("Bake settings: MinMeshSize=%.2f, ProxyScreenSize=%.2f, MergeDistance=%.2f, HardAngle=%.2f"),
			MinMeshSizeCm, ProxyScreenSize, MergeDistance, HardAngle);
	}

	// Always clear stale transient actors from previous bakes before starting a new run.
	CleanupTemporaryActors(true);

	ON_SCOPE_EXIT
	{
		CleanupTemporaryActors(false);
	};

	TArray<AActor*> SourceActors;
	FBox ShiftedSourceBounds(EForceInit::ForceInit);
	FProxySkinCollectionStats CollectionStats;
	if (!CollectAndExpandSourceActors(WorkspaceOrigin, SourceActors, ShiftedSourceBounds, CollectionStats))
	{
		UE_LOG(LogProxySkinVolume, Error, TEXT("Bake failed: no valid mesh sources intersected VolumeBox."));
		return;
	}

	UE_LOG(LogProxySkinVolume, Log,
		TEXT("Source collection: ActorsScanned=%d, Candidates=%d, Included=%d, FilteredByBounds=%d, FilteredBySize=%d, Invalid=%d, TempActors=%d"),
		CollectionStats.NumActorsScanned,
		CollectionStats.NumCandidateComponents,
		CollectionStats.NumIncludedSources,
		CollectionStats.NumFilteredByBounds,
		CollectionStats.NumFilteredBySize,
		CollectionStats.NumInvalidComponents,
		CollectionStats.NumTempActorsCreated);

	FNativeProxyBuildResult NativeResult;
	if (!BuildNativeProxyMesh(SourceActors, ShiftedSourceBounds, NativeResult) || NativeResult.IntermediateMesh == nullptr)
	{
		UE_LOG(LogProxySkinVolume, Error, TEXT("Bake failed: native proxy/merge generation returned no mesh."));
		return;
	}

	UE_LOG(LogProxySkinVolume, Log, TEXT("Native build success: mode=%s, mesh=%s"),
		NativeResult.bUsedProxyBuilder ? TEXT("ProxyLOD") : TEXT("MergeFallback"),
		*NativeResult.IntermediateMesh->GetName());

	UDynamicMesh* WorkingMesh = nullptr;
	if (!ConvertStaticMeshToDynamic(NativeResult.IntermediateMesh, WorkingMesh) || WorkingMesh == nullptr || WorkingMesh->IsEmpty())
	{
		UE_LOG(LogProxySkinVolume, Error, TEXT("Bake failed: could not convert intermediate mesh to dynamic mesh."));
		return;
	}

	AlignUnknownProxyTransformToSources(WorkingMesh, ShiftedSourceBounds, NativeResult);
	UGeometryScriptLibrary_MeshTransformFunctions::TransformMesh(WorkingMesh, NativeResult.MeshToShiftedWorld, true, nullptr);

	if (WorkingMesh->IsEmpty())
	{
		UE_LOG(LogProxySkinVolume, Error, TEXT("Bake failed: transformed dynamic mesh is empty."));
		return;
	}

	const int32 BeforeSimplifyTriangles = WorkingMesh->GetTriangleCount();
	if (!ApplyNativeSimplification(WorkingMesh))
	{
		UE_LOG(LogProxySkinVolume, Error, TEXT("Bake failed: native simplification produced invalid mesh."));
		return;
	}
	if (bVerboseLogging)
	{
		UE_LOG(LogProxySkinVolume, Log, TEXT("Simplification: triangles %d -> %d"), BeforeSimplifyTriangles, WorkingMesh->GetTriangleCount());
	}

	SpawnOrUpdateTemporaryProxyActor(NativeResult.IntermediateMesh, NativeResult.MeshToShiftedWorld, WorkspaceOrigin);

	if (bTrimToVolume)
	{
		const int32 BeforeTrimTriangles = WorkingMesh->GetTriangleCount();
		if (!TrimDynamicMeshToVolume(WorkingMesh, WorkspaceOrigin))
		{
			UE_LOG(LogProxySkinVolume, Error, TEXT("Bake failed: boolean trim/intersection with VolumeBox failed or produced empty mesh."));
			return;
		}

		UE_LOG(LogProxySkinVolume, Log, TEXT("Boolean trim success: triangles %d -> %d"), BeforeTrimTriangles, WorkingMesh->GetTriangleCount());

		if (WorkingMesh->GetTriangleCount() <= 12)
		{
			UE_LOG(LogProxySkinVolume, Error, TEXT("Bake aborted: trim result has %d triangles (degenerate/box-like result)."), WorkingMesh->GetTriangleCount());
			return;
		}

		// Guard against a known artifact where coarse proxy output collapses to "just the trimming box".
		if (WorkingMesh->GetTriangleCount() <= 24 && VolumeBox != nullptr)
		{
			const FBox MeshBounds = GetDynamicMeshBounds(WorkingMesh);
			const FVector MeshSize = MeshBounds.GetSize();
			const FVector BoxSize = VolumeBox->GetScaledBoxExtent() * 2.0f;
			const bool bLooksLikeTrimBox =
				MeshBounds.IsValid &&
				FMath::IsNearlyEqual(MeshSize.X, BoxSize.X, 2.0f) &&
				FMath::IsNearlyEqual(MeshSize.Y, BoxSize.Y, 2.0f) &&
				FMath::IsNearlyEqual(MeshSize.Z, BoxSize.Z, 2.0f);
			if (bLooksLikeTrimBox)
			{
				UE_LOG(LogProxySkinVolume, Error, TEXT("Bake aborted: result collapsed to trim box (12 tris). Disable ProxyLOD fallback or reduce problematic giant source meshes in volume."));
				return;
			}
		}
	}
	else if (bVerboseLogging)
	{
		UE_LOG(LogProxySkinVolume, Log, TEXT("Trim step skipped because bTrimToVolume=false."));
	}

	if (!ApplyPivotSphereToDynamicMesh(WorkingMesh, WorkspaceOrigin))
	{
		UE_LOG(LogProxySkinVolume, Error, TEXT("Bake failed: could not apply PivotSphere pivot offset."));
		return;
	}

	if (WorkingMesh->IsEmpty())
	{
		UE_LOG(LogProxySkinVolume, Error, TEXT("Bake failed: final mesh is empty after pivot adjustment."));
		return;
	}

	UStaticMesh* FinalMeshAsset = nullptr;
	FString FinalObjectPath;
	if (!SaveDynamicMeshToAsset(WorkingMesh, FinalMeshAsset, FinalObjectPath) || FinalMeshAsset == nullptr)
	{
		UE_LOG(LogProxySkinVolume, Error, TEXT("Bake failed: could not create final static mesh asset."));
		return;
	}

	UE_LOG(LogProxySkinVolume, Log, TEXT("Final mesh asset created: %s"), *FinalObjectPath);

	if (bSpawnResultActor)
	{
		if (AStaticMeshActor* ResultActor = SpawnResultActor(FinalMeshAsset))
		{
			UE_LOG(LogProxySkinVolume, Log, TEXT("Result actor spawned: %s"), *ResultActor->GetActorNameOrLabel());
		}
		else
		{
			UE_LOG(LogProxySkinVolume, Warning, TEXT("bSpawnResultActor=true, but result actor could not be spawned."));
		}
	}

	UE_LOG(LogProxySkinVolume, Log, TEXT("Bake finished for '%s'"), *GetActorNameOrLabel());
}

void AProxySkinVolume::ClearTempActors()
{
	CleanupTemporaryActors(true);
	UE_LOG(LogProxySkinVolume, Log, TEXT("Temporary ProxySkinVolume actors cleared."));
}

bool AProxySkinVolume::ValidateBakeContext(FString& OutError) const
{
	if (GetWorld() == nullptr)
	{
		OutError = TEXT("World context is invalid.");
		return false;
	}

	if (VolumeBox == nullptr)
	{
		OutError = TEXT("VolumeBox component is missing.");
		return false;
	}

	if (PivotSphere == nullptr)
	{
		OutError = TEXT("PivotSphere component is missing.");
		return false;
	}

	const FBox VolumeBounds = GetVolumeWorldAABB();
	if (!VolumeBounds.IsValid)
	{
		OutError = TEXT("VolumeBox bounds are invalid.");
		return false;
	}

	return true;
}

bool AProxySkinVolume::CollectAndExpandSourceActors(const FVector& WorkspaceOrigin, TArray<AActor*>& OutSourceActors, FBox& OutShiftedSourceBounds, FProxySkinCollectionStats& OutStats)
{
	UWorld* World = GetWorld();
	if (World == nullptr || VolumeBox == nullptr)
	{
		return false;
	}

	const FBox VolumeWorldAABB = GetVolumeWorldAABB();
	if (!VolumeWorldAABB.IsValid)
	{
		return false;
	}

	OutSourceActors.Reset();
	OutShiftedSourceBounds = FBox(EForceInit::ForceInit);
	OutStats = FProxySkinCollectionStats{};

	auto AddCandidateFromTransform = [&](AActor* OwnerActor, UStaticMeshComponent* MaterialSource, UStaticMesh* StaticMesh, const FTransform& SourceWorldTransform, int32 OptionalInstanceIndex)
	{
		++OutStats.NumCandidateComponents;

		if (!StaticMesh || SourceWorldTransform.ContainsNaN())
		{
			++OutStats.NumInvalidComponents;
			return;
		}

		const FBox SourceWorldBounds = StaticMesh->GetBoundingBox().TransformBy(SourceWorldTransform);
		bool bFilteredByBounds = false;
		bool bFilteredBySize = false;
		if (!IsWorldBoundsValidForCollection(SourceWorldBounds, VolumeWorldAABB, bFilteredByBounds, bFilteredBySize))
		{
			if (bFilteredByBounds)
			{
				++OutStats.NumFilteredByBounds;
			}
			if (bFilteredBySize)
			{
				++OutStats.NumFilteredBySize;
			}
			return;
		}

		TArray<TObjectPtr<UMaterialInterface>> SourceMaterials;
		if (MaterialSource != nullptr)
		{
			const int32 MaterialCount = MaterialSource->GetNumMaterials();
			SourceMaterials.Reserve(MaterialCount);
			for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
			{
				SourceMaterials.Add(MaterialSource->GetMaterial(MaterialIndex));
			}
		}

		const FString DebugName = BuildDebugSourceName(OwnerActor, MaterialSource, OptionalInstanceIndex);
		AStaticMeshActor* TempActor = SpawnTemporaryStaticMeshActor(StaticMesh, SourceWorldTransform, SourceMaterials, DebugName, WorkspaceOrigin);
		if (!TempActor)
		{
			++OutStats.NumInvalidComponents;
			return;
		}

		OutSourceActors.Add(TempActor);
		OutShiftedSourceBounds += SourceWorldBounds.ShiftBy(-WorkspaceOrigin);
		++OutStats.NumIncludedSources;
		++OutStats.NumTempActorsCreated;
	};

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* CandidateActor = *It;
		if (!IsValid(CandidateActor) || CandidateActor == this)
		{
			continue;
		}

		if (CandidateActor->Tags.Contains(ProxySkinTempActorTag))
		{
			continue;
		}

		++OutStats.NumActorsScanned;

		if (AStaticMeshActor* StaticMeshActor = Cast<AStaticMeshActor>(CandidateActor))
		{
			UStaticMeshComponent* StaticMeshComponent = StaticMeshActor->GetStaticMeshComponent();
			if (StaticMeshComponent && StaticMeshComponent->GetStaticMesh())
			{
				AddCandidateFromTransform(
					CandidateActor,
					StaticMeshComponent,
					StaticMeshComponent->GetStaticMesh(),
					StaticMeshComponent->GetComponentTransform(),
					INDEX_NONE);
			}
			continue;
		}

		if (bIncludeBlueprintStaticMeshComponents)
		{
			TInlineComponentArray<UStaticMeshComponent*> StaticMeshComponents(CandidateActor);
			for (UStaticMeshComponent* StaticMeshComponent : StaticMeshComponents)
			{
				if (!StaticMeshComponent || !StaticMeshComponent->GetStaticMesh())
				{
					++OutStats.NumInvalidComponents;
					continue;
				}

				// ISM/HISM are handled in dedicated expansion below.
				if (Cast<UInstancedStaticMeshComponent>(StaticMeshComponent) != nullptr)
				{
					continue;
				}

				AddCandidateFromTransform(
					CandidateActor,
					StaticMeshComponent,
					StaticMeshComponent->GetStaticMesh(),
					StaticMeshComponent->GetComponentTransform(),
					INDEX_NONE);
			}
		}

		if (bIncludeInstancedStaticMeshes)
		{
			TInlineComponentArray<UInstancedStaticMeshComponent*> InstancedComponents(CandidateActor);
			for (UInstancedStaticMeshComponent* InstancedComponent : InstancedComponents)
			{
				if (!InstancedComponent || !InstancedComponent->GetStaticMesh())
				{
					++OutStats.NumInvalidComponents;
					continue;
				}

				const int32 InstanceCount = InstancedComponent->GetInstanceCount();
				for (int32 InstanceIndex = 0; InstanceIndex < InstanceCount; ++InstanceIndex)
				{
					FTransform InstanceWorldTransform;
					if (!InstancedComponent->GetInstanceTransform(InstanceIndex, InstanceWorldTransform, true))
					{
						++OutStats.NumInvalidComponents;
						continue;
					}

					AddCandidateFromTransform(
						CandidateActor,
						InstancedComponent,
						InstancedComponent->GetStaticMesh(),
						InstanceWorldTransform,
						InstanceIndex);
				}
			}
		}
	}

	return OutSourceActors.Num() > 0;
}

AStaticMeshActor* AProxySkinVolume::SpawnTemporaryStaticMeshActor(UStaticMesh* StaticMesh, const FTransform& SourceWorldTransform, const TArray<TObjectPtr<UMaterialInterface>>& SourceMaterials, const FString& DebugName, const FVector& WorkspaceOrigin)
{
	UWorld* World = GetWorld();
	if (World == nullptr || StaticMesh == nullptr)
	{
		return nullptr;
	}

	FTransform ShiftedTransform = SourceWorldTransform;
	ShiftedTransform.AddToTranslation(-WorkspaceOrigin);
	if (ShiftedTransform.ContainsNaN())
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Name = MakeUniqueObjectName(World, AStaticMeshActor::StaticClass(), TEXT("PSV_TempSource"));
	SpawnParameters.ObjectFlags = RF_Transient | RF_TextExportTransient;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AStaticMeshActor* TempActor = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), ShiftedTransform, SpawnParameters);
	if (!TempActor)
	{
		return nullptr;
	}

	TempActor->SetFlags(RF_Transient);
	TempActor->Tags.AddUnique(ProxySkinTempActorTag);
	TempActor->bIsEditorOnlyActor = true;
	TempActor->SetActorEnableCollision(false);
	TempActor->SetActorHiddenInGame(true);
	TempActor->SetIsTemporarilyHiddenInEditor(true);
	TempActor->SetActorLabel(FString::Printf(TEXT("PSV_Temp_%s"), *SanitizeAssetToken(DebugName)), false);

	UStaticMeshComponent* TempComponent = TempActor->GetStaticMeshComponent();
	if (!TempComponent)
	{
		TempActor->Destroy();
		return nullptr;
	}

	TempComponent->SetFlags(RF_Transient);
	TempComponent->SetStaticMesh(StaticMesh);
	TempComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TempComponent->SetGenerateOverlapEvents(false);
	TempComponent->SetWorldTransform(ShiftedTransform);

	for (int32 MaterialIndex = 0; MaterialIndex < SourceMaterials.Num(); ++MaterialIndex)
	{
		TempComponent->SetMaterial(MaterialIndex, SourceMaterials[MaterialIndex]);
	}

	TemporarySourceActors.Add(TempActor);
	return TempActor;
}

bool AProxySkinVolume::IsWorldBoundsValidForCollection(const FBox& WorldBounds, const FBox& VolumeWorldAABB, bool& bOutFilteredByBounds, bool& bOutFilteredBySize) const
{
	bOutFilteredByBounds = false;
	bOutFilteredBySize = false;

	if (!WorldBounds.IsValid)
	{
		bOutFilteredByBounds = true;
		return false;
	}

	if (!VolumeWorldAABB.Intersect(WorldBounds))
	{
		bOutFilteredByBounds = true;
		return false;
	}

	const FVector BoundsSize = WorldBounds.GetSize();
	const float MaxAxis = FMath::Max3(BoundsSize.X, BoundsSize.Y, BoundsSize.Z);
	if (MaxAxis < FMath::Max(0.0f, MinMeshSizeCm))
	{
		bOutFilteredBySize = true;
		return false;
	}

	return true;
}

bool AProxySkinVolume::BuildNativeProxyMesh(const TArray<AActor*>& SourceActors, const FBox& ShiftedSourceBounds, FNativeProxyBuildResult& OutResult)
{
	OutResult = FNativeProxyBuildResult{};

	if (SourceActors.IsEmpty())
	{
		return false;
	}

	// Stability-first path for heavy scenes: merge source geometry, then simplify.
	if (BuildMergedMeshFallback(SourceActors, OutResult))
	{
		return true;
	}

	UE_LOG(LogProxySkinVolume, Error, TEXT("MergeComponentsToStaticMesh failed. ProxyLOD fallback is disabled in this revision to prevent box artifacts."));
	return false;
}

bool AProxySkinVolume::TryBuildProxyLODMesh(const TArray<AActor*>& SourceActors, FNativeProxyBuildResult& OutResult)
{
	IMeshReductionManagerModule* ReductionManager = FModuleManager::LoadModulePtr<IMeshReductionManagerModule>(TEXT("MeshReductionInterface"));
	if (ReductionManager == nullptr || ReductionManager->GetMeshMergingInterface() == nullptr)
	{
		if (bVerboseLogging)
		{
			UE_LOG(LogProxySkinVolume, Warning, TEXT("MeshReductionInterface does not expose a mesh merging implementation (ProxyLOD plugin unavailable)."));
		}
		return false;
	}

	const IMeshMergeUtilities& MeshMergeUtilities = FModuleManager::LoadModuleChecked<IMeshMergeModule>(TEXT("MeshMergeUtilities")).GetUtilities();

	FMeshProxySettings ProxySettings;
	ProxySettings.ScreenSize = FMath::Clamp(FMath::RoundToInt(ProxyScreenSize), 1, 1200);
	ProxySettings.bOverrideVoxelSize = true;
	ProxySettings.VoxelSize = VolumeBox
		? FMath::Max(10.0f, (VolumeBox->GetScaledBoxExtent().GetMax() * 2.0f) / 128.0f)
		: 100.0f;
	ProxySettings.MergeDistance = FMath::Max(0.0f, MergeDistance);
	ProxySettings.bUseHardAngleThreshold = true;
	ProxySettings.HardAngleThreshold = FMath::Clamp(HardAngle, 0.0f, 180.0f);
	ProxySettings.bCreateCollision = false;
	ProxySettings.bAllowDistanceField = false;
	ProxySettings.bComputeLightMapResolution = false;
	ProxySettings.bGenerateLightmapUVs = false;
	ProxySettings.bSupportRayTracing = false;
	ProxySettings.bCalculateCorrectLODModel = true;

	TArray<UObject*> CallbackAssets;
	const FGuid JobGuid = FGuid::NewGuid();
	const FCreateProxyDelegate CompletionDelegate = FCreateProxyDelegate::CreateLambda(
		[&CallbackAssets, JobGuid](const FGuid InGuid, TArray<UObject*>& InAssets)
		{
			if (InGuid == JobGuid)
			{
				CallbackAssets = InAssets;
			}
		});

	const float TransitionScreenSize = FMath::Clamp(ProxyScreenSize / 1200.0f, 0.01f, 1.0f);
	MeshMergeUtilities.CreateProxyMesh(
		SourceActors,
		ProxySettings,
		GetTransientPackage(),
		TEXT("ProxySkinVolumeIntermediate"),
		JobGuid,
		CompletionDelegate,
		false,
		TransitionScreenSize);

	for (UObject* Asset : CallbackAssets)
	{
		if (UStaticMesh* IntermediateStaticMesh = Cast<UStaticMesh>(Asset))
		{
			OutResult.IntermediateMesh = IntermediateStaticMesh;
			OutResult.MeshToShiftedWorld = FTransform::Identity;
			OutResult.bTransformIsExact = false;
			OutResult.bUsedProxyBuilder = true;
			return true;
		}
	}

	return false;
}

bool AProxySkinVolume::BuildMergedMeshFallback(const TArray<AActor*>& SourceActors, FNativeProxyBuildResult& OutResult)
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return false;
	}

	const IMeshMergeUtilities& MeshMergeUtilities = FModuleManager::LoadModuleChecked<IMeshMergeModule>(TEXT("MeshMergeUtilities")).GetUtilities();

	TArray<UPrimitiveComponent*> ComponentsToMerge;
	for (AActor* SourceActor : SourceActors)
	{
		if (!IsValid(SourceActor))
		{
			continue;
		}

		TInlineComponentArray<UStaticMeshComponent*> StaticMeshComponents(SourceActor);
		for (UStaticMeshComponent* Component : StaticMeshComponents)
		{
			if (Component && Component->GetStaticMesh())
			{
				ComponentsToMerge.Add(Component);
			}
		}
	}

	if (ComponentsToMerge.IsEmpty())
	{
		if (bVerboseLogging)
		{
			UE_LOG(LogProxySkinVolume, Warning, TEXT("Merge fallback skipped: ComponentsToMerge is empty."));
		}
		return false;
	}

	if (bVerboseLogging)
	{
		UE_LOG(LogProxySkinVolume, Log, TEXT("Merge fallback: attempting to merge %d components."), ComponentsToMerge.Num());
	}

	FMeshMergingSettings MergeSettings;
	MergeSettings.LODSelectionType = EMeshLODSelectionType::SpecificLOD;
	MergeSettings.SpecificLOD = 0;
	MergeSettings.bPivotPointAtZero = false;
	MergeSettings.bGenerateLightMapUV = false;
	MergeSettings.bComputedLightMapResolution = false;
	MergeSettings.TargetLightMapResolution = 64;
	MergeSettings.bMergeMaterials = false;
	MergeSettings.bMergePhysicsData = false;
	MergeSettings.bMergeMeshSockets = false;
	MergeSettings.bBakeVertexDataToMesh = true;
	MergeSettings.bUseLandscapeCulling = false;
	MergeSettings.bIncludeImposters = false;
	MergeSettings.bSupportRayTracing = false;
	MergeSettings.bAllowDistanceField = false;

	TArray<UObject*> MergedAssets;
	FVector MergedActorLocation = FVector::ZeroVector;
	MeshMergeUtilities.MergeComponentsToStaticMesh(
		ComponentsToMerge,
		World,
		MergeSettings,
		nullptr,
		GetTransientPackage(),
		TEXT("ProxySkinVolumeIntermediate"),
		MergedAssets,
		MergedActorLocation,
		FMath::Clamp(ProxyScreenSize / 1200.0f, 0.01f, 1.0f),
		true);

	if (bVerboseLogging)
	{
		UE_LOG(LogProxySkinVolume, Log, TEXT("Merge fallback returned %d assets."), MergedAssets.Num());
	}

	for (UObject* Asset : MergedAssets)
	{
		if (UStaticMesh* IntermediateStaticMesh = Cast<UStaticMesh>(Asset))
		{
			OutResult.IntermediateMesh = IntermediateStaticMesh;
			OutResult.MeshToShiftedWorld = FTransform(MergedActorLocation);
			OutResult.bTransformIsExact = true;
			OutResult.bUsedProxyBuilder = false;
			return true;
		}
	}

	if (bVerboseLogging)
	{
		UE_LOG(LogProxySkinVolume, Warning, TEXT("Merge fallback did not return a UStaticMesh asset."));
	}

	return false;
}

bool AProxySkinVolume::ConvertStaticMeshToDynamic(UStaticMesh* SourceMesh, UDynamicMesh*& OutDynamicMesh) const
{
	OutDynamicMesh = nullptr;

	if (SourceMesh == nullptr)
	{
		return false;
	}

	UDynamicMesh* DynamicMesh = NewObject<UDynamicMesh>(GetTransientPackage(), NAME_None, RF_Transient);
	if (DynamicMesh == nullptr)
	{
		return false;
	}

	FGeometryScriptCopyMeshFromAssetOptions AssetOptions;
	FGeometryScriptMeshReadLOD RequestedLOD;
	RequestedLOD.LODType = EGeometryScriptLODType::SourceModel;
	RequestedLOD.LODIndex = 0;

	EGeometryScriptOutcomePins Outcome = EGeometryScriptOutcomePins::Failure;
	UGeometryScriptLibrary_StaticMeshFunctions::CopyMeshFromStaticMeshV2(
		SourceMesh,
		DynamicMesh,
		AssetOptions,
		RequestedLOD,
		Outcome,
		true,
		nullptr);

	if (Outcome != EGeometryScriptOutcomePins::Success || DynamicMesh->IsEmpty())
	{
		return false;
	}

	OutDynamicMesh = DynamicMesh;
	return true;
}

bool AProxySkinVolume::ApplyNativeSimplification(UDynamicMesh* DynamicMesh) const
{
	if (DynamicMesh == nullptr || DynamicMesh->IsEmpty())
	{
		return false;
	}

	const int32 CurrentTriangleCount = DynamicMesh->GetTriangleCount();
	if (CurrentTriangleCount < 64)
	{
		return true;
	}

	// ProxyScreenSize in [1..1200] drives a practical simplification ratio.
	const float SimplificationRatio = FMath::Clamp(ProxyScreenSize / 1200.0f, 0.05f, 1.0f);
	const int32 TargetTriangleCount = FMath::Clamp(
		FMath::RoundToInt(static_cast<float>(CurrentTriangleCount) * SimplificationRatio),
		64,
		CurrentTriangleCount - 1);

	if (TargetTriangleCount >= CurrentTriangleCount)
	{
		return true;
	}

	UGeometryScriptLibrary_MeshSimplifyFunctions::ApplyEditorSimplifyToTriangleCount(
		DynamicMesh,
		TargetTriangleCount,
		nullptr);

	return !DynamicMesh->IsEmpty();
}

FBox AProxySkinVolume::GetDynamicMeshBounds(const UDynamicMesh* DynamicMesh) const
{
	FBox OutBounds(EForceInit::ForceInit);

	if (DynamicMesh == nullptr || DynamicMesh->IsEmpty())
	{
		return OutBounds;
	}

	DynamicMesh->ProcessMesh(
		[&OutBounds](const UE::Geometry::FDynamicMesh3& MeshRef)
		{
			const UE::Geometry::FAxisAlignedBox3d MeshBounds = MeshRef.GetBounds(true);
			if (MeshBounds.IsEmpty())
			{
				return;
			}

			OutBounds = FBox((FVector)MeshBounds.Min, (FVector)MeshBounds.Max);
		});

	return OutBounds;
}

void AProxySkinVolume::AlignUnknownProxyTransformToSources(UDynamicMesh* DynamicMesh, const FBox& ShiftedSourceBounds, FNativeProxyBuildResult& InOutResult) const
{
	if (DynamicMesh == nullptr || InOutResult.bTransformIsExact)
	{
		return;
	}

	const FBox DynamicBounds = GetDynamicMeshBounds(DynamicMesh);
	if (!ShiftedSourceBounds.IsValid || !DynamicBounds.IsValid)
	{
		return;
	}

	const FVector AlignmentDelta = ShiftedSourceBounds.GetCenter() - DynamicBounds.GetCenter();
	InOutResult.MeshToShiftedWorld.AddToTranslation(AlignmentDelta);

	if (bVerboseLogging)
	{
		UE_LOG(LogProxySkinVolume, Log, TEXT("Applied center alignment for proxy mesh transform. Delta=%s"), *AlignmentDelta.ToString());
	}
}

bool AProxySkinVolume::TrimDynamicMeshToVolume(UDynamicMesh* DynamicMesh, const FVector& WorkspaceOrigin) const
{
	if (DynamicMesh == nullptr || DynamicMesh->IsEmpty() || VolumeBox == nullptr)
	{
		return false;
	}

	UDynamicMesh* ToolMesh = NewObject<UDynamicMesh>(GetTransientPackage(), NAME_None, RF_Transient);
	if (ToolMesh == nullptr)
	{
		return false;
	}

	// Use unscaled extent here because component transform already carries scale.
	const FVector BoxExtent = VolumeBox->GetUnscaledBoxExtent();
	const FBox LocalBox(-BoxExtent, BoxExtent);

	FTransform ShiftedBoxTransform = VolumeBox->GetComponentTransform();
	ShiftedBoxTransform.AddToTranslation(-WorkspaceOrigin);

	FGeometryScriptPrimitiveOptions PrimitiveOptions;
	PrimitiveOptions.PolygroupMode = EGeometryScriptPrimitivePolygroupMode::PerFace;
	PrimitiveOptions.bFlipOrientation = false;

	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBoundingBox(
		ToolMesh,
		PrimitiveOptions,
		ShiftedBoxTransform,
		LocalBox,
		0,
		0,
		0,
		nullptr);

	if (ToolMesh->IsEmpty())
	{
		return false;
	}

	FGeometryScriptMeshBooleanOptions BooleanOptions;
	BooleanOptions.bFillHoles = true;
	BooleanOptions.bSimplifyOutput = true;
	BooleanOptions.SimplifyPlanarTolerance = 0.01f;
	BooleanOptions.bAllowEmptyResult = true;
	BooleanOptions.OutputTransformSpace = EGeometryScriptBooleanOutputSpace::TargetTransformSpace;

	UGeometryScriptLibrary_MeshBooleanFunctions::ApplyMeshBoolean(
		DynamicMesh,
		FTransform::Identity,
		ToolMesh,
		FTransform::Identity,
		EGeometryScriptBooleanOperation::Intersection,
		BooleanOptions,
		nullptr);

	return !DynamicMesh->IsEmpty();
}

bool AProxySkinVolume::ApplyPivotSphereToDynamicMesh(UDynamicMesh* DynamicMesh, const FVector& WorkspaceOrigin) const
{
	if (DynamicMesh == nullptr || PivotSphere == nullptr)
	{
		return false;
	}

	const FVector PivotShifted = PivotSphere->GetComponentLocation() - WorkspaceOrigin;
	UGeometryScriptLibrary_MeshTransformFunctions::TranslateMesh(DynamicMesh, -PivotShifted, nullptr);
	return true;
}

bool AProxySkinVolume::SaveDynamicMeshToAsset(UDynamicMesh* DynamicMesh, UStaticMesh*& OutStaticMesh, FString& OutObjectPath) const
{
	OutStaticMesh = nullptr;
	OutObjectPath.Empty();

	if (DynamicMesh == nullptr || DynamicMesh->IsEmpty())
	{
		return false;
	}

	FString OutputFolderPath = OutputFolder.Path;
	if (OutputFolderPath.IsEmpty())
	{
		OutputFolderPath = TEXT("/Game/Generated");
	}
	if (!OutputFolderPath.StartsWith(TEXT("/Game")))
	{
		UE_LOG(LogProxySkinVolume, Warning, TEXT("OutputFolder '%s' is invalid. Falling back to /Game/Generated."), *OutputFolderPath);
		OutputFolderPath = TEXT("/Game/Generated");
	}
	while (OutputFolderPath.EndsWith(TEXT("/")))
	{
		OutputFolderPath.LeftChopInline(1, EAllowShrinking::No);
	}

	const FString BasePrefix = OutputAssetNamePrefix.IsEmpty() ? TEXT("SM_VolumeProxySkin") : SanitizeAssetToken(OutputAssetNamePrefix);
	const FString ActorToken = SanitizeAssetToken(GetActorNameOrLabel());
	const FString BaseAssetName = FString::Printf(TEXT("%s_%s"), *BasePrefix, *ActorToken);
	const FString BasePackageName = OutputFolderPath / BaseAssetName;

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	FString UniquePackageName;
	FString UniqueAssetName;
	AssetToolsModule.Get().CreateUniqueAssetName(BasePackageName, TEXT(""), UniquePackageName, UniqueAssetName);

	UPackage* AssetPackage = CreatePackage(*UniquePackageName);
	if (AssetPackage == nullptr)
	{
		return false;
	}

	UStaticMesh* NewStaticMesh = NewObject<UStaticMesh>(AssetPackage, *UniqueAssetName, RF_Public | RF_Standalone | RF_Transactional);
	if (NewStaticMesh == nullptr)
	{
		return false;
	}

	FGeometryScriptCopyMeshToAssetOptions WriteOptions;
	WriteOptions.bEnableRecomputeNormals = true;
	WriteOptions.bEnableRecomputeTangents = true;
	WriteOptions.bEnableRemoveDegenerates = true;
	WriteOptions.GenerateLightmapUVs = EGeometryScriptGenerateLightmapUVOptions::DoNotGenerateLightmapUVs;
	WriteOptions.bEmitTransaction = false;

	FGeometryScriptMeshWriteLOD TargetLOD;
	TargetLOD.LODIndex = 0;
	TargetLOD.bWriteHiResSource = false;

	EGeometryScriptOutcomePins Outcome = EGeometryScriptOutcomePins::Failure;
	UGeometryScriptLibrary_StaticMeshFunctions::CopyMeshToStaticMesh(
		DynamicMesh,
		NewStaticMesh,
		WriteOptions,
		TargetLOD,
		Outcome,
		true,
		nullptr);

	if (Outcome != EGeometryScriptOutcomePins::Success)
	{
		return false;
	}

	FAssetRegistryModule::AssetCreated(NewStaticMesh);
	NewStaticMesh->MarkPackageDirty();
	AssetPackage->MarkPackageDirty();

	OutStaticMesh = NewStaticMesh;
	OutObjectPath = NewStaticMesh->GetPathName();
	return true;
}

AStaticMeshActor* AProxySkinVolume::SpawnResultActor(UStaticMesh* StaticMeshAsset) const
{
	if (StaticMeshAsset == nullptr || GetWorld() == nullptr || PivotSphere == nullptr)
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	const FVector PivotWorldLocation = PivotSphere->GetComponentLocation();
	AStaticMeshActor* ResultActor = GetWorld()->SpawnActor<AStaticMeshActor>(
		AStaticMeshActor::StaticClass(),
		PivotWorldLocation,
		FRotator::ZeroRotator,
		SpawnParameters);

	if (ResultActor == nullptr)
	{
		return nullptr;
	}

	UStaticMeshComponent* ResultComponent = ResultActor->GetStaticMeshComponent();
	if (ResultComponent == nullptr)
	{
		ResultActor->Destroy();
		return nullptr;
	}

	ResultComponent->SetStaticMesh(StaticMeshAsset);
	ResultComponent->SetMobility(EComponentMobility::Static);
	ResultComponent->SetHiddenInGame(true, true);
	ResultActor->SetActorHiddenInGame(true);

	const FString ResultActorName = FString::Printf(TEXT("ProxySkin_%s"), *SanitizeAssetToken(GetActorNameOrLabel()));
	ResultActor->SetActorLabel(ResultActorName, false);
	ResultActor->SetFolderPath(GetFolderPath());

	return ResultActor;
}

void AProxySkinVolume::SpawnOrUpdateTemporaryProxyActor(UStaticMesh* IntermediateMesh, const FTransform& MeshToShiftedWorld, const FVector& WorkspaceOrigin)
{
	if (!bKeepTempProxyActor || IntermediateMesh == nullptr || GetWorld() == nullptr)
	{
		return;
	}

	if (IsValid(TemporaryProxyActor))
	{
		TemporaryProxyActor->Destroy();
		TemporaryProxyActor = nullptr;
	}

	FTransform MeshToWorld = MeshToShiftedWorld;
	MeshToWorld.AddToTranslation(WorkspaceOrigin);

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Name = MakeUniqueObjectName(GetWorld(), AStaticMeshActor::StaticClass(), TEXT("PSV_TempProxy"));
	SpawnParameters.ObjectFlags = RF_Transient | RF_TextExportTransient;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AStaticMeshActor* TempProxy = GetWorld()->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), MeshToWorld, SpawnParameters);
	if (!TempProxy)
	{
		return;
	}

	TempProxy->SetFlags(RF_Transient);
	TempProxy->Tags.AddUnique(ProxySkinTempActorTag);
	TempProxy->bIsEditorOnlyActor = true;
	TempProxy->SetActorEnableCollision(false);
	TempProxy->SetActorLabel(FString::Printf(TEXT("PSV_TempProxy_%s"), *SanitizeAssetToken(GetActorNameOrLabel())), false);

	if (UStaticMeshComponent* TempProxyComponent = TempProxy->GetStaticMeshComponent())
	{
		TempProxyComponent->SetStaticMesh(IntermediateMesh);
		TempProxyComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	TemporaryProxyActor = TempProxy;
}

void AProxySkinVolume::CleanupTemporaryActors(bool bForceDeleteAll)
{
	int32 DeletedSourceActors = 0;
	int32 DeletedProxyActors = 0;

	const bool bDeleteSources = bForceDeleteAll || (bDeleteTemporaryActors && !bKeepTempSourceActors);
	if (bDeleteSources)
	{
		for (AStaticMeshActor* TempActor : TemporarySourceActors)
		{
			if (IsValid(TempActor))
			{
				TempActor->Destroy();
				++DeletedSourceActors;
			}
		}
		TemporarySourceActors.Reset();
	}

	const bool bDeleteProxy = bForceDeleteAll || (bDeleteTemporaryActors && !bKeepTempProxyActor);
	if (bDeleteProxy && IsValid(TemporaryProxyActor))
	{
		TemporaryProxyActor->Destroy();
		TemporaryProxyActor = nullptr;
		++DeletedProxyActors;
	}

	if ((DeletedSourceActors > 0 || DeletedProxyActors > 0) && bVerboseLogging)
	{
		UE_LOG(LogProxySkinVolume, Log, TEXT("Cleanup temp actors: deleted source=%d, deleted proxy=%d"), DeletedSourceActors, DeletedProxyActors);
	}
}

FBox AProxySkinVolume::GetVolumeWorldAABB() const
{
	if (VolumeBox == nullptr)
	{
		return FBox(EForceInit::ForceInit);
	}

	const FVector Extent = VolumeBox->GetScaledBoxExtent();
	const FBox LocalBox(-Extent, Extent);
	return LocalBox.TransformBy(VolumeBox->GetComponentTransform());
}

FString AProxySkinVolume::SanitizeAssetToken(const FString& InToken)
{
	FString OutToken = InToken;
	for (TCHAR& Char : OutToken)
	{
		if (!(FChar::IsAlnum(Char) || Char == TEXT('_')))
		{
			Char = TEXT('_');
		}
	}

	while (OutToken.Contains(TEXT("__")))
	{
		OutToken.ReplaceInline(TEXT("__"), TEXT("_"));
	}

	OutToken.TrimStartAndEndInline();
	if (OutToken.IsEmpty())
	{
		OutToken = TEXT("ProxySkin");
	}

	return OutToken;
}
