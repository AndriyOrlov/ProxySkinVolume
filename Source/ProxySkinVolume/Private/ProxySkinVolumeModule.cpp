#include "ProxySkinVolumeModule.h"

#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "ProxySkinVolumeActorCustomization.h"

void FProxySkinVolumeModule::StartupModule()
{
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
	PropertyEditorModule.RegisterCustomClassLayout(
		TEXT("ProxySkinVolume"),
		FOnGetDetailCustomizationInstance::CreateStatic(&FProxySkinVolumeActorCustomization::MakeInstance));
	PropertyEditorModule.NotifyCustomizationModuleChanged();
}

void FProxySkinVolumeModule::ShutdownModule()
{
	if (FModuleManager::Get().IsModuleLoaded(TEXT("PropertyEditor")))
	{
		FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
		PropertyEditorModule.UnregisterCustomClassLayout(TEXT("ProxySkinVolume"));
		PropertyEditorModule.NotifyCustomizationModuleChanged();
	}
}

IMPLEMENT_MODULE(FProxySkinVolumeModule, ProxySkinVolume)
