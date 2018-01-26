#include <shlobj.h>
#include "f4se/PluginAPI.h"

#include "f4se_common/f4se_version.h"
#include "f4se_common/SafeWrite.h"

#include "f4se/ScaleformValue.h"
#include "f4se/ScaleformCallbacks.h"
#include "f4se/GameEvents.h"

#include "Config.h"
#include "rva/RVA.h"

#include "Utils.h"
#include "Globals.h"
#include "Settings.h"

#include "ExtraEvents.h"

IDebugLog gLog;
PluginHandle g_pluginHandle = kPluginHandle_Invalid;

F4SEScaleformInterface *g_scaleform = nullptr;
F4SEMessagingInterface *g_messaging = nullptr;

//--------------------
// Addresses [4]
//--------------------

RVA <uintptr_t> ApplyImageSpaceShader_Check ({
    { RUNTIME_VERSION_1_10_64, 0x00BEBE43 },
    { RUNTIME_VERSION_1_10_50, 0x00BEBA23 },
    { RUNTIME_VERSION_1_10_40, 0x00BEB963 },
    { RUNTIME_VERSION_1_10_26, 0x00BEA013 },
}, "74 1A 8B 83 ? ? ? ? 85 C0");

RVA <uintptr_t> PowerArmorModMenu_StartHighlight_Check ({
    { RUNTIME_VERSION_1_10_64, 0x00BA34D1 },
    { RUNTIME_VERSION_1_10_50, 0x00BA30B1 },
    { RUNTIME_VERSION_1_10_40, 0x00BA30B1 },
    { RUNTIME_VERSION_1_10_26, 0x00BA1761 },
}, "FF 52 20 48 85 C0 74 17", 0x6);

RVA <uintptr_t> InventoryMenu_SetEffectType ({
    { RUNTIME_VERSION_1_10_64, 0x00B09D59 },
    { RUNTIME_VERSION_1_10_50, 0x00B09D59 },
    { RUNTIME_VERSION_1_10_40, 0x00B09D59 },
    { RUNTIME_VERSION_1_10_26, 0x00B09D49 },
}, "BA ? ? ? ? 48 8B CB E8 ? ? ? ? B2 01");    // mov edx, 5

RVA <float> ShaderBurstAmount ({
    { RUNTIME_VERSION_1_10_64, 0x0675A710 },
    { RUNTIME_VERSION_1_10_50, 0x06759710 },
    { RUNTIME_VERSION_1_10_40, 0x0675B710 },
    { RUNTIME_VERSION_1_10_26, 0x0673FA00 },
}, "F3 0F 10 15 ? ? ? ? 48 8D 05 ? ? ? ? 48 8D 3D ? ? ? ?", 0, 4, 8);   // movss xmm2, ShaderBurstAmount

// Member Functions
RVA_DEFINE_FUNCTION(GFxValue::ObjectInterface, SetMember,
    GFxValue_SetMember, "25 ? ? ? ? 4C 89 7D 97", 0x19, 1, 5);

RVA_DEFINE_FUNCTION(GFxValue::ObjectInterface, ReleaseManaged_Internal,
    GFxValue_ReleaseManaged_Internal, "25 ? ? ? ? 4C 89 7D 97", 0x34, 1, 5);

//----------------------------
// RVA
//----------------------------

// Address-independent wrapper for GFxValue
class GFxValueEx
{
public:
    GFxValueEx() { Construct(); }
    explicit GFxValueEx(GFxValue* value) : m_value(value) {};
    ~GFxValueEx() {
        if (m_constructed) {
            // We own the object, dealloc
            CleanManaged();
            ::operator delete(m_value);
        }
    }

    // We own the object, manually allocate memory
    // Use placement-new so that we can bypass the destructor
    void Construct() {
        m_constructed = true;
        void* mem = new char[sizeof(GFxValue)];
        m_value = new (mem) GFxValue();
    }

    void CleanManaged() {
        if (m_value->IsManaged()) {
            GFxValue_ReleaseManaged_Internal(m_value->objectInterface, m_value, m_value->data.obj);
            m_value->objectInterface = NULL;
            m_value->type = GFxValue::kType_Undefined;
        }
    }

    bool SetMember(const char * name, GFxValue * value) {
        return GFxValue_SetMember(m_value->objectInterface, m_value->data.obj, name, value, m_value->IsDisplayObject());
    }

    GFxValue* m_value;

private:
    bool m_constructed = false;
};

//----------------------------
// Crafting Highlight Fix
//----------------------------

// User Settings
static int iPowerArmorShaderMode, iRobotWorkbenchShaderMode;

class MenuOpenCloseHandler : public BSTEventSink<MenuOpenCloseEventEx> {
public:
    virtual EventResult ReceiveEvent(MenuOpenCloseEventEx * evn, void * dispatcher) override {
        if (evn->opening) {
            const char* menuName = evn->menuName.c_str();
            if (strcmp(menuName, "PowerArmorModMenu") == 0) {
                switch (iPowerArmorShaderMode) {
                    case 1:
                        *ShaderBurstAmount = 0.0;
                        break;
                    case 2:
                        *ShaderBurstAmount = -2.0;
                        break;
                }
            } else if (strcmp(menuName, "RobotModMenu") == 0) {
                switch (iRobotWorkbenchShaderMode) {
                    case 0:
                        *ShaderBurstAmount = FLT_MAX;
                        break;
                    case 1:
                        *ShaderBurstAmount = 0.0;
                        break;
                    case 2:
                        *ShaderBurstAmount = -2.0;
                        break;
                }
            }
        }
        return kEvent_Continue;
    }
    static void Register() {
        static MenuOpenCloseHandler eventSink;
        auto eventDispatcher = Utils::GetOffsetPtr<BSTEventDispatcher<MenuOpenCloseEventEx>>(*G::ui, 0x18);
        eventDispatcher->AddEventSink(&eventSink);
    }
};

void ApplyPatches() {
    if (Settings::GetBool("bDisableCraftingHighlights:Main", true)) {
        SafeWrite8(ApplyImageSpaceShader_Check.GetUIntPtr(), 0xEB);     // JMP
    } else {
        SafeWrite8(ApplyImageSpaceShader_Check.GetUIntPtr(), 0x74);     // JZ (original)
    }

    if (Settings::GetBool("bDisableInventoryMenuHighlights:Main", true)) {
        SafeWrite32(InventoryMenu_SetEffectType.GetUIntPtr() + 1, 0);   // mov edx, 0
    } else {
        SafeWrite32(InventoryMenu_SetEffectType.GetUIntPtr() + 1, 5);   // mov edx, 5 (original)
    }

    iPowerArmorShaderMode = Settings::GetInt("iPowerArmorShaderMode:Main", 0);
    iRobotWorkbenchShaderMode = Settings::GetInt("iRobotWorkbenchShaderMode:Main", 0);

    if (iPowerArmorShaderMode == 0) {
        SafeWrite8(PowerArmorModMenu_StartHighlight_Check.GetUIntPtr(), 0xEB);  // JMP
    } else {
        SafeWrite8(PowerArmorModMenu_StartHighlight_Check.GetUIntPtr(), 0x74);  // JZ (original)
    }
}

//-------------------------
// F4SE Event Handlers
//-------------------------

void OnF4SEMessage(F4SEMessagingInterface::Message* msg) {
    switch (msg->type) {
        case F4SEMessagingInterface::kMessage_GameLoaded:
            MenuOpenCloseHandler::Register();
            break;
    }
}

class OnMCMSettingUpdate : public GFxFunctionHandler {
public:
    void Invoke(Args* args) override {
        ApplyPatches();
    }
};

bool RegisterScaleform(GFxMovieView* view, GFxValue* f4se_root) {
    GFxValueEx fnValue;
    CreateFunction<OnMCMSettingUpdate>(fnValue.m_value, view->movieRoot);
    GFxValueEx(f4se_root).SetMember("OnMCMSettingUpdate", fnValue.m_value);
    return true;
}

//--------------------
// F4SE Init
//--------------------

extern "C"
{

bool F4SEPlugin_Query(const F4SEInterface * f4se, PluginInfo * info)
{
    char logPath[MAX_PATH];
    sprintf_s(logPath, sizeof(logPath), "\\My Games\\Fallout4\\F4SE\\%s.log", PLUGIN_NAME_SHORT);
    gLog.OpenRelative(CSIDL_MYDOCUMENTS, logPath);

    _MESSAGE("%s v%s", PLUGIN_NAME_SHORT, PLUGIN_VERSION_STRING);

    // Populate info structure
    info->infoVersion = PluginInfo::kInfoVersion;
    info->name    = PLUGIN_NAME_SHORT;
    info->version = PLUGIN_VERSION;

    // Store plugin handle
    g_pluginHandle = f4se->GetPluginHandle();

    // Check game version
    if (!COMPATIBLE(f4se->runtimeVersion)) {
        char str[512];
        sprintf_s(str, sizeof(str), "Your game version: v%d.%d.%d.%d\nExpected version: v%d.%d.%d.%d\n%s will be disabled.",
            GET_EXE_VERSION_MAJOR(f4se->runtimeVersion),
            GET_EXE_VERSION_MINOR(f4se->runtimeVersion),
            GET_EXE_VERSION_BUILD(f4se->runtimeVersion),
            GET_EXE_VERSION_SUB(f4se->runtimeVersion),
            GET_EXE_VERSION_MAJOR(SUPPORTED_RUNTIME_VERSION),
            GET_EXE_VERSION_MINOR(SUPPORTED_RUNTIME_VERSION),
            GET_EXE_VERSION_BUILD(SUPPORTED_RUNTIME_VERSION),
            GET_EXE_VERSION_SUB(SUPPORTED_RUNTIME_VERSION),
            PLUGIN_NAME_LONG
        );

        MessageBox(NULL, str, PLUGIN_NAME_LONG, MB_OK | MB_ICONEXCLAMATION);
        return false;
    }

    if (f4se->runtimeVersion != SUPPORTED_RUNTIME_VERSION) {
        _MESSAGE("INFO: Game version (%08X). Target (%08X).", f4se->runtimeVersion, SUPPORTED_RUNTIME_VERSION);
    }

    // Get the scaleform interface
    g_scaleform = (F4SEScaleformInterface *)f4se->QueryInterface(kInterface_Scaleform);
    if (!g_scaleform) {
        _MESSAGE("couldn't get scaleform interface");
        return false;
    }

    // Get the messaging interface
    g_messaging = (F4SEMessagingInterface *)f4se->QueryInterface(kInterface_Messaging);
    if (!g_messaging) {
        _MESSAGE("couldn't get messaging interface");
        return false;
    }

    return true;
}

bool F4SEPlugin_Load(const F4SEInterface *f4se)
{
    _MESSAGE("%s load", PLUGIN_NAME_SHORT);

    G::Init();
    RVAManager::UpdateAddresses(f4se->runtimeVersion);

    // Register Scaleform handlers
    g_scaleform->Register(PLUGIN_NAME_SHORT, RegisterScaleform);

    // Register for F4SE messages
    g_messaging->RegisterListener(g_pluginHandle, "F4SE", OnF4SEMessage);

    // Register for MCM messages
    //g_messaging->RegisterListener(g_pluginHandle, "MCM", OnMCMMessage);
	
    ApplyPatches();

    return true;
}

};
