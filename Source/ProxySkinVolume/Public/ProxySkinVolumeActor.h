#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "GameFramework/Actor.h"
#include "ProxySkinVolumeActor.generated.h"

class AStaticMeshActor;
class UBoxComponent;
class UDynamicMesh;
class UMaterialInterface;
class USceneComponent;
class USphereComponent;
class UStaticMesh;

DECLARE_LOG_CATEGORY_EXTERN(LogProxySkinVolume, Log, All);

UCLASS(
	BlueprintType,
	Blueprintable,
	meta = (PrioritizeCategories = "Proxy Skin|00 Actions Proxy Skin|01 Settings Proxy Skin|02 Output Proxy Skin|03 Debug Proxy Skin|90 Components"))
class PROXYSKINVOLUME_API AProxySkinVolume : public AActor
{
	GENERATED_BODY()

public:
	AProxySkinVolume();

	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Proxy Skin|00 Actions", meta = (DisplayName = "Bake Proxy Skin"))
	void BakeProxySkin();

	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Proxy Skin|00 Actions", meta = (DisplayName = "Clear Temp Actors"))
	void ClearTempActors();

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Proxy Skin|90 Components")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Proxy Skin|90 Components")
	TObjectPtr<UBoxComponent> VolumeBox;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Proxy Skin|90 Components")
	TObjectPtr<USphereComponent> PivotSphere;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Proxy Skin|01 Settings", meta = (ClampMin = "0.0"))
	float MinMeshSizeCm = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Proxy Skin|01 Settings", meta = (ClampMin = "1.0", ClampMax = "1200.0"))
	float ProxyScreenSize = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Proxy Skin|01 Settings", meta = (ClampMin = "0.0"))
	float MergeDistance = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Proxy Skin|01 Settings", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float HardAngle = 80.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Proxy Skin|01 Settings")
	bool bIncludeBlueprintStaticMeshComponents = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Proxy Skin|01 Settings")
	bool bIncludeInstancedStaticMeshes = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Proxy Skin|01 Settings")
	bool bDeleteTemporaryActors = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Proxy Skin|01 Settings")
	bool bSpawnResultActor = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Proxy Skin|01 Settings")
	bool bVerboseLogging = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Proxy Skin|02 Output")
	FDirectoryPath OutputFolder;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Proxy Skin|02 Output")
	FString OutputAssetNamePrefix = TEXT("SM_VolumeProxySkin");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Proxy Skin|01 Settings")
	bool bTrimToVolume = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Proxy Skin|03 Debug")
	bool bKeepTempProxyActor = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Proxy Skin|03 Debug")
	bool bKeepTempSourceActors = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Proxy Skin|03 Debug")
	bool bAllowProxyLODFallback = false;

private:
	struct FProxySkinCollectionStats
	{
		int32 NumActorsScanned = 0;
		int32 NumCandidateComponents = 0;
		int32 NumIncludedSources = 0;
		int32 NumFilteredByBounds = 0;
		int32 NumFilteredBySize = 0;
		int32 NumInvalidComponents = 0;
		int32 NumTempActorsCreated = 0;
	};

	struct FNativeProxyBuildResult
	{
		TObjectPtr<UStaticMesh> IntermediateMesh = nullptr;
		FTransform MeshToShiftedWorld = FTransform::Identity;
		bool bTransformIsExact = false;
		bool bUsedProxyBuilder = false;
	};

	bool ValidateBakeContext(FString& OutError) const;
	bool CollectAndExpandSourceActors(const FVector& WorkspaceOrigin, TArray<AActor*>& OutSourceActors, FBox& OutShiftedSourceBounds, FProxySkinCollectionStats& OutStats);
	AStaticMeshActor* SpawnTemporaryStaticMeshActor(UStaticMesh* StaticMesh, const FTransform& SourceWorldTransform, const TArray<TObjectPtr<UMaterialInterface>>& SourceMaterials, const FString& DebugName, const FVector& WorkspaceOrigin);
	bool IsWorldBoundsValidForCollection(const FBox& WorldBounds, const FBox& VolumeWorldAABB, bool& bOutFilteredByBounds, bool& bOutFilteredBySize) const;

	bool BuildNativeProxyMesh(const TArray<AActor*>& SourceActors, const FBox& ShiftedSourceBounds, FNativeProxyBuildResult& OutResult);
	bool TryBuildProxyLODMesh(const TArray<AActor*>& SourceActors, FNativeProxyBuildResult& OutResult);
	bool BuildMergedMeshFallback(const TArray<AActor*>& SourceActors, FNativeProxyBuildResult& OutResult);

	bool ConvertStaticMeshToDynamic(UStaticMesh* SourceMesh, UDynamicMesh*& OutDynamicMesh) const;
	bool ApplyNativeSimplification(UDynamicMesh* DynamicMesh) const;
	FBox GetDynamicMeshBounds(const UDynamicMesh* DynamicMesh) const;
	void AlignUnknownProxyTransformToSources(UDynamicMesh* DynamicMesh, const FBox& ShiftedSourceBounds, FNativeProxyBuildResult& InOutResult) const;
	bool TrimDynamicMeshToVolume(UDynamicMesh* DynamicMesh, const FVector& WorkspaceOrigin) const;
	bool ApplyPivotSphereToDynamicMesh(UDynamicMesh* DynamicMesh, const FVector& WorkspaceOrigin) const;
	bool SaveDynamicMeshToAsset(UDynamicMesh* DynamicMesh, UStaticMesh*& OutStaticMesh, FString& OutObjectPath) const;
	AStaticMeshActor* SpawnResultActor(UStaticMesh* StaticMeshAsset) const;
	void SpawnOrUpdateTemporaryProxyActor(UStaticMesh* IntermediateMesh, const FTransform& MeshToShiftedWorld, const FVector& WorkspaceOrigin);
	void CleanupTemporaryActors(bool bForceDeleteAll);

	FBox GetVolumeWorldAABB() const;
	static FString SanitizeAssetToken(const FString& InToken);

	UPROPERTY(Transient)
	TArray<TObjectPtr<AStaticMeshActor>> TemporarySourceActors;

	UPROPERTY(Transient)
	TObjectPtr<AStaticMeshActor> TemporaryProxyActor;
};
