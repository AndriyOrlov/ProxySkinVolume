#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "GameFramework/Actor.h"
#include "ProxySkinVolumeActor.generated.h"

class AStaticMeshActor;
class UBillboardComponent;
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
	hidecategories = (Actor, Input, Collision, Replication, Rendering, Physics, LOD, Cooking, HLOD, Navigation, AssetUserData, ComponentTick, Tags, Activation),
	meta = (PrioritizeCategories = "Proxy Skin Actions Proxy Skin|00 Actions Proxy Skin|01 Settings Proxy Skin|02 Output Proxy Skin|03 Debug Proxy Skin|90 Components"))
class PROXYSKINVOLUME_API AProxySkinVolume : public AActor
{
	GENERATED_BODY()

public:
	AProxySkinVolume();
	virtual void Tick(float DeltaSeconds) override;
	virtual bool ShouldTickIfViewportsOnly() const override;

	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Proxy Skin Actions", meta = (DisplayName = "Bake Proxy Skin"))
	void BakeProxySkin();

	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Proxy Skin Actions", meta = (DisplayName = "Clear Temp Actors"))
	void ClearTempActors();

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Proxy Skin|90 Components")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Proxy Skin|90 Components")
	TObjectPtr<UBoxComponent> VolumeBox;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Proxy Skin|90 Components")
	TObjectPtr<USphereComponent> PivotSphere;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Proxy Skin|90 Components")
	TObjectPtr<UBillboardComponent> VolumeBillboard;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Proxy Skin|01 Settings", meta = (ClampMin = "0.0"))
	float MinMeshSizeCm = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Proxy Skin|01 Settings", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Rejects extremely large source bounds relative to VolumeBox. 0 disables this safeguard."))
	float MaxBoundsToVolumeRatio = 1000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Proxy Skin|01 Settings", meta = (ClampMin = "1.0", ClampMax = "1200.0"))
	float ProxyScreenSize = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Proxy Skin|01 Settings", meta = (ClampMin = "0.0"))
	float MergeDistance = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Proxy Skin|01 Settings", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float HardAngle = 80.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Proxy Skin|01 Settings", meta = (DisplayName = "Include Static Mesh Actors", ToolTip = "Collects AStaticMeshActor instances inside VolumeBox."))
	bool bIncludeStaticMeshActors = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Proxy Skin|01 Settings")
	bool bIncludeBlueprintStaticMeshComponents = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Proxy Skin|01 Settings", meta = (DisplayName = "Include ISM Components", ToolTip = "Collects plain UInstancedStaticMeshComponent (non-HISM, non-Foliage)."))
	bool bIncludeInstancedStaticMeshes = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Proxy Skin|01 Settings", meta = (DisplayName = "Include HISM Components", ToolTip = "Collects hierarchical instanced static mesh components."))
	bool bIncludeHierarchicalInstancedStaticMeshes = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Proxy Skin|01 Settings", meta = (DisplayName = "Include Foliage Components", ToolTip = "Collects foliage instanced static mesh components (usually owned by InstancedFoliageActor)."))
	bool bIncludeFoliageInstancedStaticMeshes = false;

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Proxy Skin|01 Settings", meta = (DisplayName = "Use Voxel Remesh + Decimate"))
	bool bUseVoxelRemeshDecimate = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Proxy Skin|01 Settings", meta = (ClampMin = "16", ClampMax = "512", EditCondition = "bUseVoxelRemeshDecimate", EditConditionHides, DisplayName = "Voxel Grid Resolution"))
	int32 VoxelGridResolution = 96;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Proxy Skin|01 Settings", meta = (ClampMin = "64", ClampMax = "2000000", EditCondition = "bUseVoxelRemeshDecimate", EditConditionHides, DisplayName = "Voxel Decimate Target Triangles"))
	int32 VoxelDecimateTargetTriangles = 25000;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Proxy Skin|01 Settings", meta = (EditCondition = "bUseVoxelRemeshDecimate", EditConditionHides, DisplayName = "Voxel Use Volume Bounds"))
	bool bVoxelUseVolumeBounds = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Proxy Skin|01 Settings", meta = (ClampMin = "0.0", ClampMax = "500.0", EditCondition = "bUseVoxelRemeshDecimate", EditConditionHides, DisplayName = "Voxel Bounds Padding (cm)"))
	float VoxelBoundsPaddingCm = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Proxy Skin|03 Debug")
	bool bKeepTempProxyActor = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Proxy Skin|03 Debug")
	bool bKeepTempSourceActors = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Proxy Skin|03 Debug")
	bool bAllowProxyLODFallback = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Proxy Skin|03 Debug", meta = (DisplayName = "Preview Volume Wireframe"))
	bool bPreviewVolumeWireframe = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Proxy Skin|03 Debug", meta = (DisplayName = "Preview Mesh Edges In Volume"))
	bool bPreviewMeshEdgesInVolume = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Proxy Skin|03 Debug", meta = (ClampMin = "100", ClampMax = "40000", DisplayName = "Max Preview Edge Lines"))
	int32 MaxPreviewEdgeLines = 4000;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Proxy Skin|03 Debug", meta = (ClampMin = "0.1", ClampMax = "10.0", DisplayName = "Preview Wire Thickness"))
	float PreviewWireThickness = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Proxy Skin|03 Debug", meta = (DisplayName = "Preview Only When Selected"))
	bool bPreviewOnlyWhenSelected = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Proxy Skin|03 Debug", meta = (DisplayName = "Volume Wire Color"))
	FColor VolumeWireColor = FColor(62, 255, 172);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Proxy Skin|03 Debug", meta = (DisplayName = "Pivot Wire Color"))
	FColor PivotWireColor = FColor(255, 190, 32);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Proxy Skin|03 Debug", meta = (DisplayName = "Edge Wire Color"))
	FColor EdgeWireColor = FColor(0, 170, 255);

private:
	struct FProxySkinCollectionStats
	{
		int32 NumActorsScanned = 0;
		int32 NumCandidateComponents = 0;
		int32 NumIncludedSources = 0;
		int32 NumFilteredByType = 0;
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
	bool ApplyOptionalVoxelRemeshDecimate(UDynamicMesh* DynamicMesh, const FVector& WorkspaceOrigin) const;
	bool ApplyPivotSphereToDynamicMesh(UDynamicMesh* DynamicMesh, const FVector& WorkspaceOrigin) const;
	bool SaveDynamicMeshToAsset(UDynamicMesh* DynamicMesh, UStaticMesh*& OutStaticMesh, FString& OutObjectPath) const;
	AStaticMeshActor* SpawnResultActor(UStaticMesh* StaticMeshAsset) const;
	void SpawnOrUpdateTemporaryProxyActor(UStaticMesh* IntermediateMesh, const FTransform& MeshToShiftedWorld, const FVector& WorkspaceOrigin);
	void CleanupTemporaryActors(bool bForceDeleteAll);
	void DrawEditorPreview() const;
	void DrawVolumeAndPivotPreview() const;
	void DrawMeshEdgesPreview() const;
	bool ShouldDrawEditorPreview() const;
	static bool ClipLineSegmentToAABB(FVector& InOutStart, FVector& InOutEnd, const FVector& MinBounds, const FVector& MaxBounds);
	void DrawStaticMeshEdgesClippedToVolume(UStaticMesh* StaticMesh, const FTransform& MeshToWorld, const FTransform& VolumeWorldTransform, const FVector& VolumeLocalExtents, int32& InOutDrawCount) const;

	FBox GetVolumeWorldAABB() const;
	static FString SanitizeAssetToken(const FString& InToken);

	UPROPERTY(Transient)
	TArray<TObjectPtr<AStaticMeshActor>> TemporarySourceActors;

	UPROPERTY(Transient)
	TObjectPtr<AStaticMeshActor> TemporaryProxyActor;
};
