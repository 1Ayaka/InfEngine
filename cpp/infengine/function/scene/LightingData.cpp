#include "LightingData.h"
#include "Camera.h"
#include "GameObject.h"
#include "Light.h"
#include "Scene.h"
#include "SceneRenderer.h"
#include "Transform.h"
#include <algorithm>
#include <cmath>
#include <core/log/InfLog.h>

namespace infengine
{

void SceneLightCollector::CollectLights(Scene *scene, const glm::vec3 &cameraPosition)
{
    Clear();

    if (!scene) {
        return;
    }

    // Set camera position
    m_lightingUBO.worldSpaceCameraPos = glm::vec4(cameraPosition, 1.0f);

    // Find all GameObjects with Light components
    std::vector<GameObject *> allObjects = scene->GetAllObjects();

    static int debugCounter = 0;
    // if (debugCounter++ % 300 == 0) {
    //     // INFLOG_DEBUG("CollectLights: scene has ", allObjects.size(), " objects");
    // }

    int totalLightsFound = 0;
    for (GameObject *obj : allObjects) {
        if (!obj || !obj->IsActiveInHierarchy()) {
            continue;
        }

        Light *light = obj->GetComponent<Light>();
        if (!light || !light->IsEnabled()) {
            continue;
        }

        Transform *transform = obj->GetTransform();
        if (!transform) {
            continue;
        }

        totalLightsFound++;
        glm::vec3 worldPosition = transform->GetWorldPosition();
        glm::vec3 worldForward = transform->GetWorldForward();

        switch (light->GetLightType()) {
        case LightType::Directional:
            AddDirectionalLight(light);
            // INFLOG_DEBUG("Collected Directional Light: intensity=", light->GetIntensity(), ", color=(",
            //              light->GetColor().r, ",", light->GetColor().g, ",", light->GetColor().b, ")");
            break;
        case LightType::Point:
            AddPointLight(light, worldPosition);
            // INFLOG_DEBUG("Collected Point Light at (", worldPosition.x, ",", worldPosition.y, ",", worldPosition.z,
            //              "): range=", light->GetRange(), ", intensity=", light->GetIntensity());
            break;
        case LightType::Spot:
            AddSpotLight(light, worldPosition, worldForward);
            break;
        case LightType::Area:
            // Area lights are typically for baked lighting only
            break;
        }
    }

    // Sort point lights by importance
    SortPointLightsByImportance(cameraPosition);

    // Update light counts in UBO
    m_lightingUBO.lightCounts = glm::ivec4(static_cast<int>(m_directionalLightCount),
                                           static_cast<int>(m_pointLightCount), static_cast<int>(m_spotLightCount), 0);

    if (totalLightsFound > 0) {
        // INFLOG_INFO("CollectLights: found ", totalLightsFound, " lights (dir=", m_directionalLightCount,
        //             ", point=", m_pointLightCount, ", spot=", m_spotLightCount, ")");
    }

    // Prepare simplified UBO
    PrepareSimpleLightingUBO();
}

void SceneLightCollector::Clear()
{
    m_lightingUBO = LightingUBO{};
    m_simpleLightingUBO = SimpleLightingUBO{};
    m_directionalLightCount = 0;
    m_pointLightCount = 0;
    m_spotLightCount = 0;
    m_pointLightSortBuffer.clear();
    m_shadowEnabled = false;

    // Set default ambient
    m_lightingUBO.ambientSkyColor = glm::vec4(0.2f, 0.2f, 0.3f, 0.5f);
    m_lightingUBO.ambientGroundColor = glm::vec4(0.1f, 0.1f, 0.1f, 0.3f);
    m_lightingUBO.ambientEquatorColor = glm::vec4(0.15f, 0.15f, 0.2f, 0.0f); // mode = 0 (flat)
}

void SceneLightCollector::AddDirectionalLight(const Light *light)
{
    if (m_directionalLightCount >= MAX_DIRECTIONAL_LIGHTS) {
        INFLOG_WARN("Maximum directional lights (", MAX_DIRECTIONAL_LIGHTS, ") exceeded, ignoring light");
        return;
    }

    // Get direction from the light's transform (forward vector)
    // Convention: direction = light ray direction (the way light travels).
    // The shader computes L = normalize(-direction) to get toward-light vector.
    // GetForward() already returns the light ray direction, so NO negation here.
    Transform *transform = light->GetTransform();
    glm::vec3 direction = transform ? transform->GetWorldForward() : glm::vec3(0.0f, -1.0f, 0.0f);

    DirectionalLightData &data = m_lightingUBO.directionalLights[m_directionalLightCount];
    data.direction = glm::vec4(glm::normalize(direction), 0.0f);
    // Store raw color in rgb, intensity in w (shader does color.rgb * color.w)
    data.color = glm::vec4(light->GetColor(), light->GetIntensity());

    // Shadow parameters: x=strength, y=bias, z=normalBias, w=shadowType (0=off, 1=hard, 2=soft)
    float shadowType = 0.0f;
    if (light->GetShadows() == LightShadows::Hard)
        shadowType = 1.0f;
    else if (light->GetShadows() == LightShadows::Soft)
        shadowType = 2.0f;
    data.shadowParams =
        glm::vec4(light->GetShadowStrength(), light->GetShadowBias(), light->GetShadowNormalBias(), shadowType);

    m_directionalLightCount++;
}

void SceneLightCollector::AddPointLight(const Light *light, const glm::vec3 &worldPosition)
{
    // Don't add to main buffer yet, add to sort buffer
    PointLightSortData sortData;
    sortData.data.position = glm::vec4(worldPosition, light->GetRange());
    // Store raw color in rgb, intensity in w (shader does color.rgb * color.w)
    sortData.data.color = glm::vec4(light->GetColor(), light->GetIntensity());
    // Store range in x for URP-style smooth attenuation (yz unused, kept for compatibility)
    sortData.data.attenuation = glm::vec4(light->GetRange(), 0.0f, 0.0f, 0.0f);
    sortData.importance = 0.0f; // Will be calculated during sorting

    m_pointLightSortBuffer.push_back(sortData);
}

void SceneLightCollector::AddSpotLight(const Light *light, const glm::vec3 &worldPosition,
                                       const glm::vec3 &worldDirection)
{
    if (m_spotLightCount >= MAX_SPOT_LIGHTS) {
        INFLOG_WARN("Maximum spot lights (", MAX_SPOT_LIGHTS, ") exceeded, ignoring light");
        return;
    }

    SpotLightData &data = m_lightingUBO.spotLights[m_spotLightCount];
    data.position = glm::vec4(worldPosition, light->GetRange());
    data.direction = glm::vec4(glm::normalize(worldDirection), 0.0f);
    // Store raw color in rgb, intensity in w (shader does color.rgb * color.w)
    data.color = glm::vec4(light->GetColor(), light->GetIntensity());

    // Calculate cos of angles for spot falloff
    float innerAngleRad = glm::radians(light->GetSpotAngle() * 0.5f);
    float outerAngleRad = glm::radians(light->GetOuterSpotAngle() * 0.5f);
    data.spotParams = glm::vec4(std::cos(innerAngleRad), std::cos(outerAngleRad), 0.0f, 0.0f);
    // Store range in x for URP-style smooth attenuation
    data.attenuation = glm::vec4(light->GetRange(), 0.0f, 0.0f, 0.0f);

    m_spotLightCount++;
}

void SceneLightCollector::SortPointLightsByImportance(const glm::vec3 &cameraPosition)
{
    // Calculate importance for each point light
    for (auto &sortData : m_pointLightSortBuffer) {
        glm::vec3 lightPos = glm::vec3(sortData.data.position);
        float range = sortData.data.position.w;
        float intensity = sortData.data.color.a;
        float distance = glm::length(lightPos - cameraPosition);

        // Importance = intensity / (distance + 1)^2, capped at range
        if (distance > range * 2.0f) {
            sortData.importance = 0.0f;
        } else {
            sortData.importance = intensity / ((distance + 1.0f) * (distance + 1.0f));
        }
    }

    // Sort by importance (highest first)
    std::sort(m_pointLightSortBuffer.begin(), m_pointLightSortBuffer.end(),
              [](const PointLightSortData &a, const PointLightSortData &b) { return a.importance > b.importance; });

    // Copy to UBO (up to MAX_POINT_LIGHTS)
    m_pointLightCount = std::min(static_cast<uint32_t>(m_pointLightSortBuffer.size()), MAX_POINT_LIGHTS);
    for (uint32_t i = 0; i < m_pointLightCount; ++i) {
        m_lightingUBO.pointLights[i] = m_pointLightSortBuffer[i].data;
    }
}

glm::vec3 SceneLightCollector::CalculateAttenuation(float range)
{
    // Unity-style attenuation:
    // attenuation = 1.0 / (constant + linear * d + quadratic * d^2)
    // For a light with range R, we want attenuation ≈ 0 at d = R

    // Simple quadratic falloff that reaches ~0 at range
    float constant = 1.0f;
    float linear = 2.0f / range;
    float quadratic = 1.0f / (range * range);

    return glm::vec3(constant, linear, quadratic);
}

void SceneLightCollector::PrepareSimpleLightingUBO()
{
    // Copy ambient
    m_simpleLightingUBO.ambientColor = m_lightingUBO.ambientSkyColor;

    // Main directional light (first directional light)
    if (m_directionalLightCount > 0) {
        m_simpleLightingUBO.mainLightDirection = m_lightingUBO.directionalLights[0].direction;
        m_simpleLightingUBO.mainLightColor = m_lightingUBO.directionalLights[0].color;
    } else {
        m_simpleLightingUBO.mainLightDirection = glm::vec4(0.0f, -1.0f, 0.0f, 0.0f);
        m_simpleLightingUBO.mainLightColor = glm::vec4(0.0f);
    }

    // Camera position
    m_simpleLightingUBO.cameraPosition = m_lightingUBO.worldSpaceCameraPos;

    // Point lights (up to 16 for simple mode)
    m_simpleLightingUBO.pointLightCount = static_cast<int>(std::min(m_pointLightCount, 16u));
    for (int i = 0; i < m_simpleLightingUBO.pointLightCount; ++i) {
        m_simpleLightingUBO.pointLights[i] = m_lightingUBO.pointLights[i];
    }
}

void SceneLightCollector::SetAmbientColor(const glm::vec3 &color, float intensity)
{
    m_lightingUBO.ambientSkyColor = glm::vec4(color, intensity);
    m_lightingUBO.ambientEquatorColor.a = 0.0f; // Flat mode
}

void SceneLightCollector::SetAmbientGradient(const glm::vec3 &skyColor, const glm::vec3 &equatorColor,
                                             const glm::vec3 &groundColor)
{
    m_lightingUBO.ambientSkyColor = glm::vec4(skyColor, 1.0f);
    m_lightingUBO.ambientEquatorColor = glm::vec4(equatorColor, 1.0f); // Gradient mode
    m_lightingUBO.ambientGroundColor = glm::vec4(groundColor, 1.0f);
}

void SceneLightCollector::SetFog(bool enabled, const glm::vec3 &color, float density, float start, float end, int mode)
{
    m_lightingUBO.fogColor = glm::vec4(color, enabled ? 1.0f : 0.0f);
    m_lightingUBO.fogParams = glm::vec4(density, start, end, static_cast<float>(mode));
}

void SceneLightCollector::UpdateTime(float time, float deltaTime)
{
    m_lightingUBO.time = glm::vec4(time, std::sin(time), std::cos(time), deltaTime);
}

void SceneLightCollector::SetCameraPosition(const glm::vec3 &position)
{
    m_lightingUBO.worldSpaceCameraPos = glm::vec4(position, 1.0f);
    m_simpleLightingUBO.cameraPosition = glm::vec4(position, 1.0f);
}

void SceneLightCollector::SetShadowData(const glm::mat4 &lightVP, float resolution)
{
    m_shadowLightVP = lightVP;
    m_shadowMapResolution = resolution;
    m_shadowEnabled = true;
}

void SceneLightCollector::ComputeShadowVP(Scene *scene, const glm::vec3 &cameraPos, float shadowMapResolution)
{
    if (!scene)
        return;

    // Find the first shadow-casting directional light
    auto allObjects = scene->GetAllObjects();
    for (auto *obj : allObjects) {
        if (!obj || !obj->IsActiveInHierarchy())
            continue;
        Light *light = obj->GetComponent<Light>();
        if (!light || !light->IsEnabled())
            continue;
        if (light->GetLightType() != LightType::Directional)
            continue;
        if (light->GetShadows() == LightShadows::None)
            continue;

        // Compute shadow extent dynamically from camera frustum
        float shadowExtent = 30.0f; // default fallback
        float shadowNear = -100.0f; // Negative near plane to include casters behind the light
        float shadowFar = 100.0f;
        glm::vec3 shadowCenter = cameraPos;

        Camera *cam = SceneRenderBridge::Instance().GetActiveCamera();
        if (cam) {
            // Shadow distance: how far from camera shadows should be visible.
            // For a single shadow map (no CSM), 25 units is a good balance.
            float shadowDistance = 25.0f;
            float camFov = glm::radians(cam->GetFieldOfView());
            float camAspect = cam->GetAspectRatio();

            // Calculate a bounding sphere for the view frustum up to shadowDistance
            float zCenter = shadowDistance * 0.5f;

            // Get camera forward vector
            glm::vec3 camForward(0.0f, 0.0f, -1.0f);
            if (cam->GetGameObject() && cam->GetGameObject()->GetTransform()) {
                camForward = cam->GetGameObject()->GetTransform()->GetWorldForward();
            }

            shadowCenter = cameraPos + camForward * zCenter;

            // Calculate the radius of the bounding sphere
            float halfHeight = std::tan(camFov * 0.5f) * shadowDistance;
            float halfWidth = halfHeight * camAspect;

            // Distance from center to the far corner of the frustum
            float radius = std::sqrt(halfWidth * halfWidth + halfHeight * halfHeight + zCenter * zCenter);

            // To prevent shadow swimming when the camera rotates, the extent must be constant.
            // The bounding sphere radius is constant for a given FOV, aspect, and distance.
            shadowExtent = std::ceil(radius);
        }

        // Compute lightVP = proj * view, centered on the frustum bounding sphere
        glm::mat4 lightView = light->GetLightViewMatrix(shadowCenter);
        glm::mat4 lightProj = light->GetLightProjectionMatrix(shadowExtent, shadowNear, shadowFar);
        glm::mat4 lightVP = lightProj * lightView;

        // ── Texel snapping ──────────────────────────────────────────────
        // Snap the shadow frustum origin to shadow-map texel boundaries
        // to prevent sub-texel jitter that causes shadow swimming/flickering
        // when the camera moves.
        {
            float smSize = shadowMapResolution;

            // Project the world origin into NDC via lightVP (ortho → w=1)
            glm::vec4 originNDC = lightVP * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);

            // Convert NDC [-1,1] → texel coordinates [0, smSize]
            float texX = originNDC.x * smSize * 0.5f;
            float texY = originNDC.y * smSize * 0.5f;

            // Round to nearest integer texel
            float snappedTexX = std::round(texX);
            float snappedTexY = std::round(texY);

            // Convert the rounding offset back to NDC
            float offsetX = (snappedTexX - texX) / (smSize * 0.5f);
            float offsetY = (snappedTexY - texY) / (smSize * 0.5f);

            // Apply offset to the projection matrix's translation column
            lightProj[3][0] += offsetX;
            lightProj[3][1] += offsetY;
            lightVP = lightProj * lightView;
        }

        // Store shadow data for BuildShaderLightingUBO and DrawShadowCasters
        SetShadowData(lightVP, shadowMapResolution);

        static bool loggedShadowLight = false;
        if (!loggedShadowLight) {
            glm::vec3 fwd = obj->GetTransform()->GetWorldForward();
            INFLOG_INFO("Shadow light found: '", obj->GetName(), "', forward=(", fwd.x, ",", fwd.y, ",", fwd.z,
                        "), shadowType=", static_cast<int>(light->GetShadows()));
            loggedShadowLight = true;
        }
        return; // Only first shadow-casting directional light
    }

    // No shadow light found this frame — shadow stays disabled (cleared by Clear())
}

void SceneLightCollector::BuildShaderLightingUBO()
{
    // Build shader-compatible UBO from full UBO data
    // This structure exactly matches lit.frag layout

    // Light counts
    m_shaderLightingUBO.lightCounts = m_lightingUBO.lightCounts;

    // Ambient color (use sky color as the ambient)
    m_shaderLightingUBO.ambientColor = m_lightingUBO.ambientSkyColor;

    // Camera position
    m_shaderLightingUBO.cameraPos = m_lightingUBO.worldSpaceCameraPos;

    // Copy directional lights
    for (uint32_t i = 0; i < MAX_DIRECTIONAL_LIGHTS; ++i) {
        m_shaderLightingUBO.directionalLights[i] = m_lightingUBO.directionalLights[i];
    }

    // Copy point lights
    for (uint32_t i = 0; i < MAX_POINT_LIGHTS; ++i) {
        m_shaderLightingUBO.pointLights[i] = m_lightingUBO.pointLights[i];
    }

    // Copy spot lights
    for (uint32_t i = 0; i < MAX_SPOT_LIGHTS; ++i) {
        m_shaderLightingUBO.spotLights[i] = m_lightingUBO.spotLights[i];
    }

    // Shadow mapping data
    if (m_shadowEnabled) {
        m_shaderLightingUBO.lightVP[0] = m_shadowLightVP;
        m_shaderLightingUBO.shadowMapParams = glm::vec4(m_shadowMapResolution, 1.0f, 1.0f, 0.0f);
    } else {
        for (uint32_t i = 0; i < NUM_SHADOW_CASCADES; ++i) {
            m_shaderLightingUBO.lightVP[i] = glm::mat4(1.0f);
        }
        m_shaderLightingUBO.shadowMapParams = glm::vec4(0.0f);
    }
    m_shaderLightingUBO.shadowCascadeSplits = glm::vec4(0.0f);

    // Reset shadow state for next frame
    m_shadowEnabled = false;

    // Debug: log what we built
    if (m_shaderLightingUBO.lightCounts.x > 0) {
        auto &light = m_shaderLightingUBO.directionalLights[0];
        // INFLOG_DEBUG("BuildShaderLightingUBO: dir=", m_shaderLightingUBO.lightCounts.x, ", light0 dir=(",
        //              light.direction.x, ",", light.direction.y, ",", light.direction.z, "), color=(", light.color.x,
        //              ",", light.color.y, ",", light.color.z, "), intensity=", light.color.w);
    }
}

} // namespace infengine
