#include "Modules/ModuleManager.h"
#include "MCNetworkProtocol.h"
#include "Misc/NetworkVersion.h"

class FMessControlModule final : public FDefaultGameModuleImpl
{
public:
    virtual void StartupModule() override
    {
        FDefaultGameModuleImpl::StartupModule();
        FNetworkVersion::RegisterNetworkCustomVersion(MCNetworkProtocol::FoundationGuid,
            MCNetworkProtocol::FoundationVersion,MCNetworkProtocol::CompatibleFoundationVersion,
            TEXT("MessControlNetworkFoundation"));
    }
};
IMPLEMENT_PRIMARY_GAME_MODULE(FMessControlModule, MessControl, "MessControl");
