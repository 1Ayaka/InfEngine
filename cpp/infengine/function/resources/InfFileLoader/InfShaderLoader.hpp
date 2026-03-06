#include "InfFileLoader.h"
#include <SPIRV/GlslangToSpv.h>
#include <glslang/Public/ShaderLang.h>
#include <set>
#include <unordered_map>

namespace infengine
{
class InfShaderLoader : public InfFileLoader
{
  public:
    InfShaderLoader::InfShaderLoader(bool generateDebugInfo, bool stripDebugInfo, bool disableOptimizer,
                                     bool optimizeSize, bool disassemble, bool validate,
                                     bool emitNonSemanticShaderDebugInfo, bool emitNonSemanticShaderDebugSource,
                                     bool compileOnly, bool optimizerAllowExpandedIDBound);
    void SetShaderCompilerOptions(const std::string &prop, bool value);

    bool LoadMeta(const char *content, const std::string &filePath, InfResourceMeta &metaData) override;
    void CreateMeta(const char *content, size_t contentSize, const std::string &filePath,
                    InfResourceMeta &metaData) override;
    std::unique_ptr<InfResource> Load(const char *content, size_t contentSize, InfResourceMeta &metaData) override;

  private:
    glslang::SpvOptions m_options;
    TBuiltInResource m_builtInResources;

    void InitGLSLBuiltResources();
    EShLanguage GetShaderType(const std::string &typeStr);
    std::string PreprocessShaderSource(const std::string &source, const std::string &filePath = "");

    /// @brief Build a mapping of shader_id → file_path by scanning all shader files in a directory
    /// @param dir Directory to scan for shader files (.vert, .frag, .glsl)
    /// @return Map from shader_id to the canonical file path
    std::unordered_map<std::string, std::string> BuildShaderIdMap(const std::string &dir);

    /// @brief Resolve @import directives by inlining referenced shader files (looked up by shader_id)
    /// @param source Shader source text
    /// @param shaderIdMap Map of shader_id → file_path (built by BuildShaderIdMap)
    /// @param includeStack Set of already-imported shader_ids (for circular import detection)
    /// @param depth Current recursion depth (to prevent runaway imports)
    /// @return Source with all @import directives resolved
    std::string ResolveImports(const std::string &source,
                               const std::unordered_map<std::string, std::string> &shaderIdMap,
                               std::set<std::string> &includeStack, int depth = 0);
};
} // namespace infengine
