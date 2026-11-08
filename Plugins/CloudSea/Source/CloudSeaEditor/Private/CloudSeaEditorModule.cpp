#include "Modules/ModuleManager.h"
#include "CloudSea/CloudSeaMaskPainter.h"

class FCloudSeaEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		RegisterCloudSeaMaskPainter();
	}

	virtual void ShutdownModule() override
	{
		UnregisterCloudSeaMaskPainter();
	}
};

IMPLEMENT_MODULE(FCloudSeaEditorModule, CloudSeaEditor)
