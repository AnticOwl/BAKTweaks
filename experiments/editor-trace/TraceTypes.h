#pragma once
#include <cstdint>
#include <string>
namespace BakTrace {
struct Vec3 { float x{},y{},z{}; };
struct LightState {
    bool enabled{true};
    float color[3]{1.0f,1.0f,1.0f};
    float intensity{1.0f};
    float lightBulbRadius{};
    float radiosity{};
    float radiosityDiffuse{};
    float shadowUpdatePriority{};
    float cullAreaMultiplier{};
    float layer0Multiplier{};
    float layer1Multiplier{};
    float layer01Multiplier{};
    float signalSmoothingHigh{};
    float signalSmoothingLow{};
    float effectorIntensity{};
    float effectorColor[3]{1.0f,1.0f,1.0f};

    bool umbraDoNotCull{};
    bool radiosityOnly{};
    bool castShadows{};
    bool castStaticShadows{};
    bool castDynamicShadows{};
    bool castCompositeShadow{};
    bool affectCompositeShadowDirection{};
    bool nonModulatedSelfShadowing{};
    bool selfShadowOnly{};
    bool allowPreShadow{};
    bool forceDynamicLight{};
    bool useDirectLightMap{};
    bool affectDynamicOutsideChannel{};
    bool renderLightShafts{};
    bool drawAsVolume{};
    bool dontAffectLightVolume{};
    bool dontAffectForwardLitTransparencies{};
    bool forceDynamicShadows{};
    bool cheapLight{};
    bool ambientOnly{};
    bool xray{};
    bool alsoXray{};
    bool notInCinematics{};
    bool lod2Fade{};
    bool explicitlyAssigned{};
    bool allowCompositingIntoDLE{};
    bool cinematicLightType{};

    bool channelInitialized{};
    bool channelBSP{};
    bool channelStatic{};
    bool channelDynamic{};
    bool channelCompositeDynamic{};
    bool channelSkybox{};

    int lightAffectsClassification{};
    int shadowProjectionTechnique{};
    int shadowFilterQuality{};
    int depthPriorityGroup{};
    float coreSize{};
    float omniFadeRadiusFactor{};
    float occlusionDepthRange{};
    float bloomScale{};
    float bloomThreshold{};
    float bloomScreenBlendThreshold{};
    float bloomTint[3]{1.0f,1.0f,1.0f};
    float radialBlurPercent{};
    float occlusionMaskDarkness{};
    float shadowResolutionMultiplier{};

    float shadowRadiusMultiplier{};
    float radius{};
    float falloffExponent{};
    float shadowFalloffExponent{};
    float minShadowFalloffRadius{};
    bool quadraticFalloff{};
    Vec3 translation{};

    float innerConeAngle{};
    float outerConeAngle{};
    float lightShaftConeAngle{};
    float pitch{}, yaw{}, roll{};
    bool absoluteRotation{};
    bool nvidiaLightShafts{};
    bool castOpacityShadows{};
    int qualityLevel{};
    int phaseFunction{};
    float distanceScale{};
    float gelThreshold{};
    float scatter{};
    float absorption{};
    float phase{};
    float shaftIntensity{};
    float opacityShadowNearDepth{};
    float opacityShadowOpacityMultiplier{};
    float opacityShadowAmbientSelf{};
    float opacityShadowAmbientProjected{};
};

enum LiveLightChange : uint32_t {
    LiveEnabled = 1u << 0, LiveColor = 1u << 1, LiveIntensity = 1u << 2,
    LiveRadius = 1u << 3, LiveBulb = 1u << 4, LiveFalloff = 1u << 5,
    LiveQuadratic = 1u << 6, LivePosition = 1u << 7, LiveRotation = 1u << 8,
    LiveCone = 1u << 9, LiveShaftCone = 1u << 10, LiveAbsoluteRotation = 1u << 11
};

struct LightRecord {
    uint32_t id{};
    bool isSpot{};
    int ordinal{};
    void* actor{};
    void* component{};
    void* spawnWorld{};
    void* spawnWorldInfo{};
    std::string name;
    bool editOpen{};
    bool editorLoggedOpen{};
    bool gizmoVisible{true};
    bool valid{};
    bool statePending{};
    int stateRetryTicks{};
    uint32_t liveDirty{};
    bool liveEditFailed{};
    LightState state{};
    LightState defaults{};
};

}
