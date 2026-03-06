#include "InfShaderLoader.hpp"

#include <SPIRV/GlslangToSpv.h>
#include <glslang/Public/ShaderLang.h>

#include <core/log/InfLog.h>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>

namespace infengine
{
InfShaderLoader::InfShaderLoader(bool generateDebugInfo, bool stripDebugInfo, bool disableOptimizer, bool optimizeSize,
                                 bool disassemble, bool validate, bool emitNonSemanticShaderDebugInfo,
                                 bool emitNonSemanticShaderDebugSource, bool compileOnly,
                                 bool optimizerAllowExpandedIDBound)
{
    // Initialize the glslang library and set options
    glslang::InitializeProcess();
    m_options.generateDebugInfo = generateDebugInfo;
    m_options.stripDebugInfo = stripDebugInfo;
    m_options.disableOptimizer = disableOptimizer;
    m_options.optimizeSize = optimizeSize;
    m_options.disassemble = disassemble;
    m_options.validate = validate;
    m_options.emitNonSemanticShaderDebugInfo = emitNonSemanticShaderDebugInfo;
    m_options.emitNonSemanticShaderDebugSource = emitNonSemanticShaderDebugSource;
    m_options.compileOnly = compileOnly;
    m_options.optimizerAllowExpandedIDBound = optimizerAllowExpandedIDBound;

    // Initialize built-in resources
    InitGLSLBuiltResources();
}

void InfShaderLoader::SetShaderCompilerOptions(const std::string &prop, bool value)
{
    if (prop == "generateDebugInfo") {
        m_options.generateDebugInfo = value;
    } else if (prop == "stripDebugInfo") {
        m_options.stripDebugInfo = value;
    } else if (prop == "disableOptimizer") {
        m_options.disableOptimizer = value;
    } else if (prop == "optimizeSize") {
        m_options.optimizeSize = value;
    } else if (prop == "disassemble") {
        m_options.disassemble = value;
    } else if (prop == "validate") {
        m_options.validate = value;
    } else if (prop == "emitNonSemanticShaderDebugInfo") {
        m_options.emitNonSemanticShaderDebugInfo = value;
    } else if (prop == "emitNonSemanticShaderDebugSource") {
        m_options.emitNonSemanticShaderDebugSource = value;
    } else if (prop == "compileOnly") {
        m_options.compileOnly = value;
    } else if (prop == "optimizerAllowExpandedIDBound") {
        m_options.optimizerAllowExpandedIDBound = value;
    }
}

bool InfShaderLoader::LoadMeta(const char *content, const std::string &filePath, InfResourceMeta &metaData)
{
    INFLOG_DEBUG("Loading shader with metadata from file: ", filePath);
    // not implemented yet.
    return false;
}

void InfShaderLoader::CreateMeta(const char *content, size_t contentSize, const std::string &filePath,
                                 InfResourceMeta &metaData)
{
    if (!content) {
        INFLOG_ERROR("Invalid shader content for metadata creation");
        return;
    }
    metaData.Init(content, contentSize, filePath, ResourceType::Shader);

    // Determine shader type from file extension
    std::string type = "vertex"; // Default type
    std::filesystem::path path(filePath);
    std::string extension = path.extension().string();
    std::string filename = path.filename().string();

    if (extension == ".vert") {
        type = "vertex";
    } else if (extension == ".frag") {
        type = "fragment";
    } else if (extension == ".geom") {
        type = "geometry";
    } else if (extension == ".comp") {
        type = "compute";
    } else if (extension == ".tesc") {
        type = "tess_control";
    } else if (extension == ".tese") {
        type = "tess_evaluation";
    }

    metaData.AddMetadata("type", type);

    // Parse shader annotations from content
    // Format: @shader_id: <id>
    //         @property: <name>, <type>, <default_value>
    //         @cull: front/back/none (default: back)
    // Lines starting with @ are annotation lines (will be commented out for compilation)
    std::string contentStr(content, contentSize);
    std::istringstream stream(contentStr);
    std::string line;
    std::string shaderId = filename; // Default to filename (without extension)
    // Remove extension from default shaderId
    size_t dotPos = shaderId.find_last_of('.');
    if (dotPos != std::string::npos) {
        shaderId = shaderId.substr(0, dotPos);
    }
    std::string propertiesJson = "[]"; // JSON array of properties
    std::vector<std::string> properties;
    std::string shaderLightingType = "unlit"; // Default to unlit
    std::string shaderCullMode = "back";      // Default to back-face culling
    bool shaderHidden = false;                // Default to visible

    while (std::getline(stream, line)) {
        // Trim leading whitespace
        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos)
            continue;
        line = line.substr(start);

        // Check for @type annotation (with or without // prefix)
        if (line.rfind("@type:", 0) == 0 || line.rfind("// @type:", 0) == 0) {
            size_t colonPos = line.find(':');
            if (colonPos != std::string::npos) {
                std::string typePart = line.substr(colonPos + 1);
                // Trim whitespace
                size_t typeStart = typePart.find_first_not_of(" \t");
                size_t typeEnd = typePart.find_last_not_of(" \t\r\n");
                if (typeStart != std::string::npos && typeEnd != std::string::npos) {
                    shaderLightingType = typePart.substr(typeStart, typeEnd - typeStart + 1);
                }
            }
        }
        // Check for @cull annotation (with or without // prefix)
        else if (line.rfind("@cull:", 0) == 0 || line.rfind("// @cull:", 0) == 0) {
            size_t colonPos = line.find(':');
            if (colonPos != std::string::npos) {
                std::string cullPart = line.substr(colonPos + 1);
                // Trim whitespace
                size_t cullStart = cullPart.find_first_not_of(" \t");
                size_t cullEnd = cullPart.find_last_not_of(" \t\r\n");
                if (cullStart != std::string::npos && cullEnd != std::string::npos) {
                    shaderCullMode = cullPart.substr(cullStart, cullEnd - cullStart + 1);
                    // Normalize to lowercase
                    std::transform(shaderCullMode.begin(), shaderCullMode.end(), shaderCullMode.begin(), ::tolower);
                }
            }
        }
        // Check for @shader_id annotation (with or without // prefix)
        else if (line.rfind("@shader_id:", 0) == 0 || line.rfind("// @shader_id:", 0) == 0) {
            size_t colonPos = line.find(':');
            if (colonPos != std::string::npos) {
                std::string idPart = line.substr(colonPos + 1);
                // Trim whitespace
                size_t idStart = idPart.find_first_not_of(" \t");
                size_t idEnd = idPart.find_last_not_of(" \t\r\n");
                if (idStart != std::string::npos && idEnd != std::string::npos) {
                    shaderId = idPart.substr(idStart, idEnd - idStart + 1);
                }
            }
        }
        // Check for @hidden annotation (with or without // prefix)
        else if (line.rfind("@hidden", 0) == 0 || line.rfind("// @hidden", 0) == 0) {
            shaderHidden = true;
        }
        // Check for @property annotation (with or without // prefix)
        else if (line.rfind("@property:", 0) == 0 || line.rfind("// @property:", 0) == 0) {
            size_t colonPos = line.find(':');
            if (colonPos != std::string::npos) {
                std::string propPart = line.substr(colonPos + 1);
                // Format: name, type, default_value
                properties.push_back(propPart);
            }
        }
        // Skip #version and layout lines (annotations can appear before or after #version)
        else if (line.rfind("#version", 0) == 0 || line.rfind("layout", 0) == 0 || line.rfind("//", 0) == 0 ||
                 line.rfind("/*", 0) == 0 || line.rfind("void ", 0) == 0) {
            // Stop at actual code (function definitions), but keep scanning through
            // #version, layout, and comments since annotations may appear after them
            if (line.rfind("void ", 0) == 0) {
                break;
            }
            continue;
        }
    }

    // Build properties JSON array
    if (!properties.empty()) {
        std::ostringstream jsonStream;
        jsonStream << "[";
        for (size_t i = 0; i < properties.size(); ++i) {
            if (i > 0)
                jsonStream << ",";
            // Parse property: name, type, default_value
            std::string prop = properties[i];
            size_t firstComma = prop.find(',');
            size_t secondComma = prop.find(',', firstComma + 1);

            std::string name, propType, defaultVal;
            if (firstComma != std::string::npos && secondComma != std::string::npos) {
                name = prop.substr(0, firstComma);
                propType = prop.substr(firstComma + 1, secondComma - firstComma - 1);
                defaultVal = prop.substr(secondComma + 1);

                // Trim whitespace from each part
                auto trim = [](std::string &s) {
                    size_t start = s.find_first_not_of(" \t");
                    size_t end = s.find_last_not_of(" \t\r\n");
                    if (start != std::string::npos && end != std::string::npos) {
                        s = s.substr(start, end - start + 1);
                    }
                };
                trim(name);
                trim(propType);
                trim(defaultVal);

                jsonStream << "{\"name\":\"" << name << "\",\"type\":\"" << propType << "\",\"default\":" << defaultVal
                           << "}";
            }
        }
        jsonStream << "]";
        propertiesJson = jsonStream.str();
    }

    metaData.AddMetadata("shader_id", shaderId);
    metaData.AddMetadata("properties", propertiesJson);
    metaData.AddMetadata("shader_lighting_type", shaderLightingType);
    metaData.AddMetadata("shader_cull_mode", shaderCullMode);
    metaData.AddMetadata("shader_hidden", shaderHidden);

    INFLOG_DEBUG("Shader metadata created - type: ", type, ", shader_id: ", shaderId,
                 ", lighting_type: ", shaderLightingType, ", properties: ", propertiesJson, " for file: ", filePath);
}

std::string InfShaderLoader::PreprocessShaderSource(const std::string &source, const std::string &filePath)
{
    // Step 0: Resolve @import directives before any other preprocessing
    std::string resolvedSource = source;
    if (!filePath.empty()) {
        std::filesystem::path shaderPath(filePath);
        std::string baseDir = shaderPath.parent_path().string();

        // Build shader_id → file_path map by scanning all shader files in the directory
        auto shaderIdMap = BuildShaderIdMap(baseDir);

        std::set<std::string> includeStack;
        // Add current file's shader_id to prevent self-import
        // (extract shader_id from current source if present)
        std::regex shaderIdRegex(R"(^\s*@shader_id\s*:\s*(\S+))");
        std::istringstream scanStream(source);
        std::string scanLine;
        while (std::getline(scanStream, scanLine)) {
            std::smatch m;
            if (std::regex_search(scanLine, m, shaderIdRegex)) {
                includeStack.insert(m[1].str());
                break;
            }
        }

        resolvedSource = ResolveImports(resolvedSource, shaderIdMap, includeStack, 0);
    }

    // Preprocess shader source:
    // 1. Parse @type annotation (lit/unlit) and inject LightingUBO for lit shaders
    // 2. Parse @property annotations and generate MaterialProperties UBO
    // 3. Comment out lines starting with @ (annotation lines)
    // 4. Ensure #version directive is at the very beginning
    std::istringstream stream(resolvedSource);
    std::ostringstream result;
    std::string line;
    std::string versionLine;
    std::vector<std::string> annotationLines;
    std::vector<std::string> codeLines;
    std::string shaderLightingType = "unlit"; // Default to unlit

    // Collect properties for auto-generating MaterialProperties UBO
    struct PropertyInfo
    {
        std::string name;
        std::string glslType; // vec4, vec3, vec2, float, int, mat4
    };
    std::vector<PropertyInfo> properties;

    while (std::getline(stream, line)) {
        // Trim leading whitespace for checking
        size_t start = line.find_first_not_of(" \t");
        std::string trimmedLine = (start != std::string::npos) ? line.substr(start) : "";

        // Check if line starts with @
        if (!trimmedLine.empty() && trimmedLine[0] == '@') {
            // Parse @type annotation
            if (trimmedLine.rfind("@type:", 0) == 0) {
                size_t colonPos = trimmedLine.find(':');
                if (colonPos != std::string::npos) {
                    std::string typePart = trimmedLine.substr(colonPos + 1);
                    size_t typeStart = typePart.find_first_not_of(" \t");
                    size_t typeEnd = typePart.find_last_not_of(" \t\r\n");
                    if (typeStart != std::string::npos && typeEnd != std::string::npos) {
                        shaderLightingType = typePart.substr(typeStart, typeEnd - typeStart + 1);
                    }
                }
            }
            // Parse @property annotation
            else if (trimmedLine.rfind("@property:", 0) == 0) {
                size_t colonPos = trimmedLine.find(':');
                if (colonPos != std::string::npos) {
                    std::string propPart = trimmedLine.substr(colonPos + 1);
                    // Format: name, type, default_value
                    size_t firstComma = propPart.find(',');
                    size_t secondComma = propPart.find(',', firstComma + 1);
                    if (firstComma != std::string::npos && secondComma != std::string::npos) {
                        std::string name = propPart.substr(0, firstComma);
                        std::string propType = propPart.substr(firstComma + 1, secondComma - firstComma - 1);
                        // Trim whitespace
                        auto trim = [](std::string &s) {
                            size_t st = s.find_first_not_of(" \t");
                            size_t en = s.find_last_not_of(" \t\r\n");
                            if (st != std::string::npos && en != std::string::npos) {
                                s = s.substr(st, en - st + 1);
                            }
                        };
                        trim(name);
                        trim(propType);

                        // Convert property type to GLSL type
                        std::string glslType;
                        if (propType == "Float4" || propType == "Color") {
                            glslType = "vec4";
                        } else if (propType == "Float3") {
                            glslType = "vec3";
                        } else if (propType == "Float2") {
                            glslType = "vec2";
                        } else if (propType == "Float") {
                            glslType = "float";
                        } else if (propType == "Int") {
                            glslType = "int";
                        } else if (propType == "Mat4") {
                            glslType = "mat4";
                        }

                        if (!glslType.empty()) {
                            properties.push_back({name, glslType});
                        }
                    }
                }
            }
            // Comment out annotation line
            annotationLines.push_back("// " + line);
        } else if (!trimmedLine.empty() && trimmedLine.rfind("// @", 0) == 0) {
            // Already commented annotation - also parse it
            if (trimmedLine.find("@property:") != std::string::npos) {
                size_t colonPos = trimmedLine.find(':', trimmedLine.find("@property"));
                if (colonPos != std::string::npos) {
                    std::string propPart = trimmedLine.substr(colonPos + 1);
                    size_t firstComma = propPart.find(',');
                    size_t secondComma = propPart.find(',', firstComma + 1);
                    if (firstComma != std::string::npos && secondComma != std::string::npos) {
                        std::string name = propPart.substr(0, firstComma);
                        std::string propType = propPart.substr(firstComma + 1, secondComma - firstComma - 1);
                        auto trim = [](std::string &s) {
                            size_t st = s.find_first_not_of(" \t");
                            size_t en = s.find_last_not_of(" \t\r\n");
                            if (st != std::string::npos && en != std::string::npos) {
                                s = s.substr(st, en - st + 1);
                            }
                        };
                        trim(name);
                        trim(propType);

                        std::string glslType;
                        if (propType == "Float4" || propType == "Color") {
                            glslType = "vec4";
                        } else if (propType == "Float3") {
                            glslType = "vec3";
                        } else if (propType == "Float2") {
                            glslType = "vec2";
                        } else if (propType == "Float") {
                            glslType = "float";
                        } else if (propType == "Int") {
                            glslType = "int";
                        } else if (propType == "Mat4") {
                            glslType = "mat4";
                        }

                        if (!glslType.empty()) {
                            properties.push_back({name, glslType});
                        }
                    }
                }
            }
            annotationLines.push_back(line);
        } else if (trimmedLine.rfind("#version", 0) == 0) {
            // Capture #version directive
            versionLine = line;
        } else {
            codeLines.push_back(line);
        }
    }

    // Build final source - #version MUST be first
    if (!versionLine.empty()) {
        result << versionLine << "\n";
    } else {
        result << "#version 450\n";
    }

    // Add annotations as comments (after #version)
    for (const auto &ann : annotationLines) {
        result << ann << "\n";
    }

    // Auto-generate LightingUBO for lit shaders (matches ShaderLightingUBO in C++)
    if (shaderLightingType == "lit") {
        result << "\n// Auto-generated LightingUBO for @type: lit shaders\n";
        result << "#define MAX_DIRECTIONAL_LIGHTS 4\n";
        result << "#define MAX_POINT_LIGHTS 64\n";
        result << "#define MAX_SPOT_LIGHTS 32\n";
        result << "\n";
        result << "struct DirectionalLightData {\n";
        result << "    vec4 direction;      // xyz = direction, w = unused\n";
        result << "    vec4 color;          // xyz = color, w = intensity\n";
        result << "    vec4 shadowParams;   // x = shadow strength, y = shadow bias, zw = unused\n";
        result << "};\n";
        result << "\n";
        result << "struct PointLightData {\n";
        result << "    vec4 position;       // xyz = position, w = range\n";
        result << "    vec4 color;          // xyz = color, w = intensity\n";
        result << "    vec4 attenuation;    // x = constant, y = linear, z = quadratic, w = unused\n";
        result << "};\n";
        result << "\n";
        result << "struct SpotLightData {\n";
        result << "    vec4 position;       // xyz = position, w = range\n";
        result << "    vec4 direction;      // xyz = direction, w = unused\n";
        result << "    vec4 color;          // xyz = color, w = intensity\n";
        result << "    vec4 spotParams;     // x = inner angle cos, y = outer angle cos, zw = unused\n";
        result << "    vec4 attenuation;    // x = constant, y = linear, z = quadratic, w = unused\n";
        result << "};\n";
        result << "\n";
        result << "layout(std140, binding = 1) uniform LightingUBO {\n";
        result << "    ivec4 lightCounts;   // x = directional, y = point, z = spot, w = unused\n";
        result << "    vec4 ambientColor;   // xyz = ambient color, w = ambient intensity\n";
        result << "    vec4 cameraPos;      // xyz = camera world position, w = unused\n";
        result << "    DirectionalLightData directionalLights[MAX_DIRECTIONAL_LIGHTS];\n";
        result << "    PointLightData pointLights[MAX_POINT_LIGHTS];\n";
        result << "    SpotLightData spotLights[MAX_SPOT_LIGHTS];\n";
        result << "    mat4 lightVP[4];     // Light view-projection matrices per cascade\n";
        result << "    vec4 shadowCascadeSplits; // x,y,z,w = cascade split distances (view-space Z)\n";
        result << "    vec4 shadowMapParams;     // x = resolution, y = enabled(1/0), z = numCascades, w = unused\n";
        result << "} lighting;\n\n";

        // Auto-generate shadow map sampler in descriptor set 1 (per-view bindings).
        // Set 1 is owned per-render-graph, enabling multi-camera shadow isolation.
        result << "// Auto-generated shadow map sampler (per-view descriptor set 1)\n";
        result << "layout(set = 1, binding = 0) uniform sampler2D shadowMap;\n\n";
    }

    // Auto-generate MaterialProperties UBO if properties were found
    // For lit shaders: binding = 3 (because binding 1 = LightingUBO, binding 2 = texSampler)
    // For unlit shaders: binding = 2 (because binding 1 = texSampler)
    if (!properties.empty()) {
        int materialBinding = (shaderLightingType == "lit") ? 3 : 2;
        result << "\n// Auto-generated MaterialProperties UBO from @property annotations\n";
        result << "layout(std140, binding = " << materialBinding << ") uniform MaterialProperties {\n";

        // Write properties in std140-compatible order: vec4, vec3, vec2, float, int, mat4
        // This matches the order in UpdateMaterialUBO
        for (const auto &prop : properties) {
            if (prop.glslType == "vec4") {
                result << "    " << prop.glslType << " " << prop.name << ";\n";
            }
        }
        for (const auto &prop : properties) {
            if (prop.glslType == "vec3") {
                result << "    " << prop.glslType << " " << prop.name << ";\n";
            }
        }
        for (const auto &prop : properties) {
            if (prop.glslType == "vec2") {
                result << "    " << prop.glslType << " " << prop.name << ";\n";
            }
        }
        for (const auto &prop : properties) {
            if (prop.glslType == "float") {
                result << "    " << prop.glslType << " " << prop.name << ";\n";
            }
        }
        for (const auto &prop : properties) {
            if (prop.glslType == "int") {
                result << "    " << prop.glslType << " " << prop.name << ";\n";
            }
        }
        for (const auto &prop : properties) {
            if (prop.glslType == "mat4") {
                result << "    " << prop.glslType << " " << prop.name << ";\n";
            }
        }

        result << "} material;\n\n";
    }

    // Add rest of code
    for (const auto &codeLine : codeLines) {
        result << codeLine << "\n";
    }

    return result.str();
}

std::unique_ptr<InfResource> InfShaderLoader::Load(const char *content, size_t contentSize, InfResourceMeta &metaData)
{
    if (!content) {
        INFLOG_ERROR("Invalid shader content");
        return nullptr;
    }

    std::string filePath = metaData.GetDataAs<std::string>("file_path");
    INFLOG_DEBUG("InfShaderLoader::Load - Loading shader: ", filePath);

    // Preprocess: comment out @ lines and add #version if needed
    std::string shaderSource = PreprocessShaderSource(std::string(content), filePath);

    INFLOG_DEBUG("Preprocessed shader source for ", filePath, ":\n", shaderSource);

    size_t lastBracePos = shaderSource.find_last_of('}');
    if (lastBracePos != std::string::npos) {
        shaderSource = shaderSource.substr(0, lastBracePos + 1);
    }

    while (!shaderSource.empty() && std::isspace(shaderSource.back())) {
        shaderSource.pop_back();
    }

    size_t fileSize = shaderSource.size();
    auto rawData = std::make_unique<std::vector<char>>();
    rawData->resize(fileSize + 1); // +1 for null terminator
    std::memcpy(rawData->data(), shaderSource.c_str(), fileSize);
    (*rawData)[fileSize] = '\0'; // Ensure null termination
    INFLOG_DEBUG("Shader loaded successfully, size: ", fileSize, " bytes");

    std::string type = metaData.GetDataAs<std::string>("type");
    EShLanguage shaderType = GetShaderType(type);
    INFLOG_DEBUG("Shader type resolved to: ", shaderType, " for type: ", type);

    // Validate shader type
    if (shaderType == EShLangCount) {
        INFLOG_ERROR("Invalid shader type: ", type);
        return nullptr;
    }

    glslang::TShader shader(shaderType);
    const char *shaderStrings[1] = {rawData->data()};
    shader.setStrings(shaderStrings, 1);

    int clientInputSemanticVersion = 100;
    int clientInputSemanticsVersion = 100;
    glslang::EShTargetClientVersion vulkanClientVersion = glslang::EShTargetVulkan_1_2;
    glslang::EShTargetLanguageVersion targetVersion = glslang::EShTargetSpv_1_5;

    shader.setEnvInput(glslang::EShSourceGlsl, shaderType, glslang::EShClientVulkan, clientInputSemanticsVersion);
    shader.setEnvClient(glslang::EShClientVulkan, vulkanClientVersion);
    shader.setEnvTarget(glslang::EShTargetSpv, targetVersion);

    EShMessages messages = (EShMessages)(EShMsgSpvRules | EShMsgVulkanRules);
    if (!shader.parse(&m_builtInResources, 100, false, messages)) {
        INFLOG_ERROR("Shader parse failed:\n", shader.getInfoLog());
        INFLOG_ERROR("Shader content:\n",
                     std::string(rawData->data(), rawData->size() - 1)); // -1 to exclude null terminator
        INFLOG_ERROR("Shader file path: ", metaData.GetDataAs<std::string>("file_path"));
        return nullptr;
    }
    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(messages)) {
        INFLOG_ERROR("Shader link failed:\n", program.getInfoLog());
        return nullptr;
    }

    std::vector<unsigned int> compiledSpirv;
    glslang::GlslangToSpv(*program.getIntermediate(shaderType), compiledSpirv, &m_options);

    auto compiledData = std::make_unique<std::vector<char>>();
    compiledData->resize(compiledSpirv.size() * sizeof(unsigned int));
    const char *spirvPtr = reinterpret_cast<const char *>(compiledSpirv.data());
    std::memcpy(compiledData->data(), spirvPtr, compiledSpirv.size() * sizeof(unsigned int));

    // Release ownership to MakeResource which takes raw pointers and manages them via shared_ptr
    return MakeResource<ResourceType::Shader>(rawData.release(), compiledData.release());
}

void InfShaderLoader::InitGLSLBuiltResources()
{
    m_builtInResources.maxLights = 32;
    m_builtInResources.maxClipPlanes = 6;
    m_builtInResources.maxTextureUnits = 32;
    m_builtInResources.maxTextureCoords = 32;
    m_builtInResources.maxVertexAttribs = 64;
    m_builtInResources.maxVertexUniformComponents = 4096;
    m_builtInResources.maxVaryingFloats = 64;
    m_builtInResources.maxVertexTextureImageUnits = 32;
    m_builtInResources.maxCombinedTextureImageUnits = 80;
    m_builtInResources.maxTextureImageUnits = 32;
    m_builtInResources.maxFragmentUniformComponents = 4096;
    m_builtInResources.maxDrawBuffers = 32;
    m_builtInResources.maxVertexUniformVectors = 128;
    m_builtInResources.maxVaryingVectors = 8;
    m_builtInResources.maxFragmentUniformVectors = 16;
    m_builtInResources.maxVertexOutputVectors = 16;
    m_builtInResources.maxFragmentInputVectors = 15;
    m_builtInResources.minProgramTexelOffset = -8;
    m_builtInResources.maxProgramTexelOffset = 7;
    m_builtInResources.maxClipDistances = 8;
    m_builtInResources.maxComputeWorkGroupCountX = 65535;
    m_builtInResources.maxComputeWorkGroupCountY = 65535;
    m_builtInResources.maxComputeWorkGroupCountZ = 65535;
    m_builtInResources.maxComputeWorkGroupSizeX = 1024;
    m_builtInResources.maxComputeWorkGroupSizeY = 1024;
    m_builtInResources.maxComputeWorkGroupSizeZ = 64;
    m_builtInResources.maxComputeUniformComponents = 1024;
    m_builtInResources.maxComputeTextureImageUnits = 16;
    m_builtInResources.maxComputeImageUniforms = 8;
    m_builtInResources.maxComputeAtomicCounters = 8;
    m_builtInResources.maxComputeAtomicCounterBuffers = 1;
    m_builtInResources.maxVaryingComponents = 60;
    m_builtInResources.maxVertexOutputComponents = 64;
    m_builtInResources.maxGeometryInputComponents = 64;
    m_builtInResources.maxGeometryOutputComponents = 128;
    m_builtInResources.maxFragmentInputComponents = 128;
    m_builtInResources.maxImageUnits = 8;
    m_builtInResources.maxCombinedImageUnitsAndFragmentOutputs = 8;
    m_builtInResources.maxCombinedShaderOutputResources = 8;
    m_builtInResources.maxImageSamples = 0;
    m_builtInResources.maxVertexImageUniforms = 0;
    m_builtInResources.maxTessControlImageUniforms = 0;
    m_builtInResources.maxTessEvaluationImageUniforms = 0;
    m_builtInResources.maxGeometryImageUniforms = 0;
    m_builtInResources.maxFragmentImageUniforms = 8;
    m_builtInResources.maxCombinedImageUniforms = 8;
    m_builtInResources.maxGeometryTextureImageUnits = 16;
    m_builtInResources.maxGeometryOutputVertices = 256;
    m_builtInResources.maxGeometryTotalOutputComponents = 1024;
    m_builtInResources.maxGeometryUniformComponents = 1024;
    m_builtInResources.maxGeometryVaryingComponents = 64;
    m_builtInResources.maxTessControlInputComponents = 128;
    m_builtInResources.maxTessControlOutputComponents = 128;
    m_builtInResources.maxTessControlTextureImageUnits = 16;
    m_builtInResources.maxTessControlUniformComponents = 1024;
    m_builtInResources.maxTessControlTotalOutputComponents = 4096;
    m_builtInResources.maxTessEvaluationInputComponents = 128;
    m_builtInResources.maxTessEvaluationOutputComponents = 128;
    m_builtInResources.maxTessEvaluationTextureImageUnits = 16;
    m_builtInResources.maxTessEvaluationUniformComponents = 1024;
    m_builtInResources.maxTessPatchComponents = 120;
    m_builtInResources.maxPatchVertices = 32;
    m_builtInResources.maxTessGenLevel = 64;
    m_builtInResources.maxViewports = 16;
    m_builtInResources.maxVertexAtomicCounters = 0;
    m_builtInResources.maxTessControlAtomicCounters = 0;
    m_builtInResources.maxTessEvaluationAtomicCounters = 0;
    m_builtInResources.maxGeometryAtomicCounters = 0;
    m_builtInResources.maxFragmentAtomicCounters = 8;
    m_builtInResources.maxCombinedAtomicCounters = 8;
    m_builtInResources.maxAtomicCounterBindings = 1;
    m_builtInResources.maxVertexAtomicCounterBuffers = 0;
    m_builtInResources.maxTessControlAtomicCounterBuffers = 0;
    m_builtInResources.maxTessEvaluationAtomicCounterBuffers = 0;
    m_builtInResources.maxGeometryAtomicCounterBuffers = 0;
    m_builtInResources.maxFragmentAtomicCounterBuffers = 1;
    m_builtInResources.maxCombinedAtomicCounterBuffers = 1;
    m_builtInResources.maxAtomicCounterBufferSize = 16384;
    m_builtInResources.maxTransformFeedbackBuffers = 4;
    m_builtInResources.maxTransformFeedbackInterleavedComponents = 64;
    m_builtInResources.maxCullDistances = 8;
    m_builtInResources.maxCombinedClipAndCullDistances = 8;
    m_builtInResources.maxSamples = 4;
    m_builtInResources.maxMeshOutputVerticesNV = 256;
    m_builtInResources.maxMeshOutputPrimitivesNV = 512;
    m_builtInResources.maxMeshWorkGroupSizeX_NV = 32;
    m_builtInResources.maxMeshWorkGroupSizeY_NV = 1;
    m_builtInResources.maxMeshWorkGroupSizeZ_NV = 1;
    m_builtInResources.maxTaskWorkGroupSizeX_NV = 32;
    m_builtInResources.maxTaskWorkGroupSizeY_NV = 1;
    m_builtInResources.maxTaskWorkGroupSizeZ_NV = 1;
    m_builtInResources.maxMeshViewCountNV = 4;

    m_builtInResources.limits.nonInductiveForLoops = 1;
    m_builtInResources.limits.whileLoops = 1;
    m_builtInResources.limits.doWhileLoops = 1;
    m_builtInResources.limits.generalUniformIndexing = 1;
    m_builtInResources.limits.generalAttributeMatrixVectorIndexing = 1;
    m_builtInResources.limits.generalVaryingIndexing = 1;
    m_builtInResources.limits.generalSamplerIndexing = 1;
    m_builtInResources.limits.generalVariableIndexing = 1;
    m_builtInResources.limits.generalConstantMatrixVectorIndexing = 1;
}

EShLanguage InfShaderLoader::GetShaderType(const std::string &typeStr)
{
    if (typeStr == "vertex") {
        return EShLangVertex;
    } else if (typeStr == "fragment") {
        return EShLangFragment;
    } else if (typeStr == "geometry") {
        return EShLangGeometry;
    } else if (typeStr == "compute") {
        return EShLangCompute;
    } else if (typeStr == "tess_control") {
        return EShLangTessControl;
    } else if (typeStr == "tess_evaluation") {
        return EShLangTessEvaluation;
    }
    return EShLangCount;
}

std::unordered_map<std::string, std::string> InfShaderLoader::BuildShaderIdMap(const std::string &dir)
{
    std::unordered_map<std::string, std::string> idMap;
    std::regex shaderIdRegex(R"(^\s*@shader_id\s*:\s*(\S.*\S|\S+))");

    std::error_code ec;
    for (const auto &entry : std::filesystem::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file())
            continue;

        auto ext = entry.path().extension().string();
        if (ext != ".vert" && ext != ".frag" && ext != ".glsl")
            continue;

        // Read the first 20 lines to find @shader_id annotation
        std::ifstream file(entry.path());
        if (!file.is_open())
            continue;

        std::string line;
        int lineCount = 0;
        while (std::getline(file, line) && lineCount < 20) {
            std::smatch m;
            if (std::regex_search(line, m, shaderIdRegex)) {
                std::string id = m[1].str();
                // Trim trailing whitespace
                while (!id.empty() && (id.back() == ' ' || id.back() == '\t'))
                    id.pop_back();

                std::string canonicalPath = std::filesystem::canonical(entry.path(), ec).string();
                if (!ec) {
                    idMap[id] = canonicalPath;
                }
                break;
            }
            ++lineCount;
        }
    }
    return idMap;
}

std::string InfShaderLoader::ResolveImports(const std::string &source,
                                            const std::unordered_map<std::string, std::string> &shaderIdMap,
                                            std::set<std::string> &includeStack, int depth)
{
    // Guard against excessive recursion (e.g., A imports B imports C imports D ...)
    constexpr int MAX_IMPORT_DEPTH = 16;
    if (depth >= MAX_IMPORT_DEPTH) {
        INFLOG_ERROR("Shader @import depth exceeded maximum of ", MAX_IMPORT_DEPTH);
        return source;
    }

    std::istringstream stream(source);
    std::ostringstream result;
    std::string line;

    // Regex: @import: shader_id  (with optional spaces around the colon and id)
    std::regex importRegex(R"(^\s*@import\s*:\s*(\S+))");

    while (std::getline(stream, line)) {
        std::smatch match;
        if (std::regex_search(line, match, importRegex)) {
            std::string importId = match[1].str();

            // Look up the shader_id in the map
            auto it = shaderIdMap.find(importId);
            if (it == shaderIdMap.end()) {
                INFLOG_ERROR("Shader @import: shader_id '", importId, "' not found in shaders directory");
                result << "// ERROR: @import shader_id not found: " << importId << "\n";
                continue;
            }

            const std::string &importPath = it->second;

            // Check for circular imports
            if (includeStack.count(importId) > 0) {
                INFLOG_WARN("Circular @import detected, skipping: ", importId);
                result << "// WARNING: circular @import skipped: " << importId << "\n";
                continue;
            }

            // Read the imported file
            std::ifstream importFile(importPath);
            if (!importFile.is_open()) {
                INFLOG_ERROR("Failed to open @import file: ", importPath);
                result << "// ERROR: failed to open @import: " << importId << "\n";
                continue;
            }

            std::ostringstream importContent;
            importContent << importFile.rdbuf();
            importFile.close();

            // Strip #version directive from imported content (the parent file's #version takes precedence)
            std::string content = importContent.str();
            std::regex versionRegex(R"(^\s*#version\s+\d+.*)");
            std::istringstream contentStream(content);
            std::ostringstream strippedContent;
            std::string contentLine;
            while (std::getline(contentStream, contentLine)) {
                if (!std::regex_match(contentLine, versionRegex)) {
                    strippedContent << contentLine << "\n";
                }
            }

            // Recursively resolve imports in the imported file
            includeStack.insert(importId);
            std::string resolvedContent = ResolveImports(strippedContent.str(), shaderIdMap, includeStack, depth + 1);
            includeStack.erase(importId);

            // Insert the resolved content (with markers for debugging)
            result << "// --- begin @import: " << importId << " ---\n";
            result << resolvedContent;
            // Ensure newline at end of imported content
            if (!resolvedContent.empty() && resolvedContent.back() != '\n') {
                result << "\n";
            }
            result << "// --- end @import: " << importId << " ---\n";
        } else {
            result << line << "\n";
        }
    }

    return result.str();
}
} // namespace infengine