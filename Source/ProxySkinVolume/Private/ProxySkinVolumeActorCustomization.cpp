#include "ProxySkinVolumeActorCustomization.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"

#define LOCTEXT_NAMESPACE "ProxySkinVolumeActorCustomization"

TSharedRef<IDetailCustomization> FProxySkinVolumeActorCustomization::MakeInstance()
{
	return MakeShared<FProxySkinVolumeActorCustomization>();
}

void FProxySkinVolumeActorCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	const TCHAR* HiddenCategories[] =
	{
		TEXT("Proxy Skin|00 Actions"),
		TEXT("Transform"),
		TEXT("Actor"),
		TEXT("Tick"),
		TEXT("Component Tick"),
		TEXT("ComponentTick"),
		TEXT("Rendering"),
		TEXT("Physics"),
		TEXT("Collision"),
		TEXT("LOD"),
		TEXT("Cooking"),
		TEXT("HLOD"),
		TEXT("Activation"),
		TEXT("Tags"),
		TEXT("Replication"),
		TEXT("Input"),
		TEXT("Navigation"),
		TEXT("Asset User Data"),
		TEXT("AssetUserData"),
		TEXT("World Partition"),
		TEXT("Data Layers"),
		TEXT("Mobile"),
		TEXT("Ray Tracing"),
		TEXT("Virtual Texture"),
		TEXT("Root"),
		TEXT("VolumeBox"),
		TEXT("PivotSphere"),
		TEXT("VolumeBillboard")
	};
	for (const TCHAR* CategoryName : HiddenCategories)
	{
		DetailBuilder.HideCategory(CategoryName);
	}

	IDetailCategoryBuilder& ActionsCategory = DetailBuilder.EditCategory(
		TEXT("Proxy Skin Actions"),
		LOCTEXT("ProxySkinActionsCategory", "Proxy Skin Actions"),
		ECategoryPriority::Important);
	ActionsCategory.SetSortOrder(-100000);
}

#undef LOCTEXT_NAMESPACE
