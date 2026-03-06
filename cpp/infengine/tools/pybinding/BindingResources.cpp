#include "InfFileLoader/InfTextureLoader.hpp"
#include "InfFileManager.h"
#include "InfResource/InfResourceMeta.h"
#include <function/resources/InfMaterial/InfMaterial.h>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;

namespace infengine
{

void RegisterResourceBindings(py::module_ &m)
{
    // ResourceType enum
    py::enum_<ResourceType>(m, "ResourceType")
        .value("Meta", ResourceType::Meta)
        .value("Shader", ResourceType::Shader)
        .value("Texture", ResourceType::Texture)
        .value("Mesh", ResourceType::Mesh)
        .value("Material", ResourceType::Material)
        .value("Script", ResourceType::Script)
        .value("Audio", ResourceType::Audio)
        .value("DefaultText", ResourceType::DefaultText)
        .value("DefaultBinary", ResourceType::DefaultBinary)
        .export_values();

    // InfResourceMeta - resource metadata
    py::class_<InfResourceMeta>(m, "ResourceMeta")
        .def(py::init<>())
        .def("get_resource_name", &InfResourceMeta::GetResourceName,
             "Get the resource name (filename without extension)")
        .def("get_hash_code", &InfResourceMeta::GetHashCode, "Get the hash code used internally")
        .def("get_guid", &InfResourceMeta::GetGuid, "Get the stable GUID for this resource")
        .def("get_resource_type", &InfResourceMeta::GetResourceType, "Get the resource type")
        .def("has_key", &InfResourceMeta::HasKey, py::arg("key"), "Check if metadata has a specific key")
        .def(
            "get_string",
            [](const InfResourceMeta &self, const std::string &key) {
                if (!self.HasKey(key))
                    return std::string("");
                return self.GetDataAs<std::string>(key);
            },
            py::arg("key"), "Get a string metadata value")
        .def(
            "get_int",
            [](const InfResourceMeta &self, const std::string &key) {
                if (!self.HasKey(key))
                    return 0;
                return self.GetDataAs<int>(key);
            },
            py::arg("key"), "Get an integer metadata value")
        .def(
            "get_float",
            [](const InfResourceMeta &self, const std::string &key) {
                if (!self.HasKey(key))
                    return 0.0f;
                return self.GetDataAs<float>(key);
            },
            py::arg("key"), "Get a float metadata value");

    // InfTextureData - raw texture data accessible from Python
    py::class_<InfTextureData>(m, "TextureData")
        .def(py::init<>())
        .def_readonly("width", &InfTextureData::width, "Texture width in pixels")
        .def_readonly("height", &InfTextureData::height, "Texture height in pixels")
        .def_readonly("channels", &InfTextureData::channels, "Number of color channels (always 4 for RGBA)")
        .def_readonly("name", &InfTextureData::name, "Texture name/identifier")
        .def_readonly("source_path", &InfTextureData::sourcePath, "Original file path")
        .def("is_valid", &InfTextureData::IsValid, "Check if texture data is valid")
        .def("get_size_bytes", &InfTextureData::GetSizeBytes, "Get total size in bytes")
        .def(
            "get_pixels",
            [](const InfTextureData &self) {
                // Return pixels as bytes for Python access
                return py::bytes(reinterpret_cast<const char *>(self.pixels.data()), self.pixels.size());
            },
            "Get raw pixel data as bytes (RGBA format)")
        .def(
            "get_pixels_list",
            [](const InfTextureData &self) {
                // Return pixels as list of unsigned char for passing to upload_texture_for_imgui
                return self.pixels;
            },
            "Get raw pixel data as list of unsigned char (for upload_texture_for_imgui)");

    // InfTextureLoader - static methods for loading textures
    py::class_<InfTextureLoader>(m, "TextureLoader")
        .def_static("load_from_file", &InfTextureLoader::LoadFromFile, py::arg("file_path"), py::arg("name") = "",
                    "Load texture from file")
        .def_static(
            "load_from_memory",
            [](py::bytes data, const std::string &name) {
                std::string str = data;
                return InfTextureLoader::LoadFromMemory(reinterpret_cast<const unsigned char *>(str.data()), str.size(),
                                                        name);
            },
            py::arg("data"), py::arg("name") = "", "Load texture from memory buffer")
        .def_static("create_solid_color", &InfTextureLoader::CreateSolidColor, py::arg("width"), py::arg("height"),
                    py::arg("r"), py::arg("g"), py::arg("b"), py::arg("a"), py::arg("name") = "solid_color",
                    "Create a solid color texture")
        .def_static("create_checkerboard", &InfTextureLoader::CreateCheckerboard, py::arg("width"), py::arg("height"),
                    py::arg("checker_size") = 8, py::arg("name") = "checkerboard",
                    "Create a checkerboard texture (for error indication)");

    // InfFileManager - resource management
    py::class_<InfFileManager>(m, "FileManager")
        .def(py::init<>())
        .def("register_resource", &InfFileManager::RegisterResource, py::arg("file_path"), py::arg("type"),
             "Register a resource file and get its UID")
        .def("load_resource", &InfFileManager::LoadResource, py::arg("uid"), "Load a resource by its UID")
        .def("unload_resource", &InfFileManager::UnloadResource, py::arg("uid"), "Unload a resource by its UID")
        .def("move_resource", &InfFileManager::MoveResource, py::arg("old_path"), py::arg("new_path"),
             "Move/rename a resource file")
        .def("delete_resource", &InfFileManager::DeleteResource, py::arg("file_path"), "Delete a resource file")
        .def("modify_resource", &InfFileManager::ModifyResource, py::arg("file_path"),
             "Notify that a resource has been modified")
        .def("get_resource_type", &InfFileManager::GetResourceTypeForPath, py::arg("file_path"),
             "Get the ResourceType for a file based on its path")
        .def("get_meta_by_guid", &InfFileManager::GetMetaByGuid, py::arg("guid"), py::return_value_policy::reference,
             "Get resource metadata by GUID, returns None if not found")
        .def("get_meta_by_path", &InfFileManager::GetMetaByPath, py::arg("file_path"),
             py::return_value_policy::reference, "Get resource metadata by file path, returns None if not found")
        .def("get_all_resource_guids", &InfFileManager::GetAllResourceGuids,
             "Get a list of all registered resource GUIDs");

    // InfMaterial - material definition (named InfMaterial to avoid conflict with ResourceType.Material)
    py::class_<InfMaterial, std::shared_ptr<InfMaterial>>(m, "InfMaterial")
        .def(py::init<>())
        .def(py::init<const std::string &>(), py::arg("name"))
        .def(py::init<const std::string &, const std::string &, const std::string &>(), py::arg("name"),
             py::arg("vert_shader_path"), py::arg("frag_shader_path"))
        .def_property("name", &InfMaterial::GetName, &InfMaterial::SetName, "Material name")
        .def_property("guid", &InfMaterial::GetGuid, &InfMaterial::SetGuid, "Material GUID")
        .def_property("file_path", &InfMaterial::GetFilePath, &InfMaterial::SetFilePath, "File path for saving")
        .def_property("is_builtin", &InfMaterial::IsBuiltin, &InfMaterial::SetBuiltin,
                      "Whether this is a built-in material (shader cannot be changed)")
        .def_property("vertex_shader_path", &InfMaterial::GetVertexShaderPath, &InfMaterial::SetVertexShaderPath,
                      "Path to the vertex shader file")
        .def_property("fragment_shader_path", &InfMaterial::GetFragmentShaderPath, &InfMaterial::SetFragmentShaderPath,
                      "Path to the fragment shader file")
        .def("set_shaders", &InfMaterial::SetShaders, py::arg("vert_path"), py::arg("frag_path"),
             "Set both vertex and fragment shader paths")
        .def("get_render_queue", &InfMaterial::GetRenderQueue, "Get the render queue value")
        .def("set_render_queue", &InfMaterial::SetRenderQueue, py::arg("queue"), "Set the render queue value")
        .def("save", py::overload_cast<>(&InfMaterial::SaveToFile, py::const_), "Save material to its file path")
        .def("save_to", py::overload_cast<const std::string &>(&InfMaterial::SaveToFile), py::arg("path"),
             "Save material to specified path")
        // Property setters (accept both tuple and individual args)
        .def("set_float", &InfMaterial::SetFloat, py::arg("name"), py::arg("value"), "Set a float property")
        .def(
            "set_vector2",
            [](InfMaterial &mat, const std::string &name, py::args args) {
                glm::vec2 v;
                if (args.size() == 1) {
                    py::object obj = args[0];
                    if (py::isinstance<py::tuple>(obj) || py::isinstance<py::list>(obj)) {
                        py::sequence seq = obj.cast<py::sequence>();
                        v = glm::vec2(seq[0].cast<float>(), seq[1].cast<float>());
                    } else {
                        v = obj.cast<glm::vec2>();
                    }
                } else if (args.size() >= 2) {
                    v = glm::vec2(args[0].cast<float>(), args[1].cast<float>());
                } else {
                    throw std::runtime_error("set_vector2: expected (name, x, y) or (name, vec2)");
                }
                mat.SetVector2(name, v);
            },
            py::arg("name"), "Set a vec2 property: set_vector2(name, x, y) or set_vector2(name, (x,y))")
        .def(
            "set_vector3",
            [](InfMaterial &mat, const std::string &name, py::args args) {
                glm::vec3 v;
                if (args.size() == 1) {
                    py::object obj = args[0];
                    if (py::isinstance<py::tuple>(obj) || py::isinstance<py::list>(obj)) {
                        py::sequence seq = obj.cast<py::sequence>();
                        v = glm::vec3(seq[0].cast<float>(), seq[1].cast<float>(), seq[2].cast<float>());
                    } else {
                        v = obj.cast<glm::vec3>();
                    }
                } else if (args.size() >= 3) {
                    v = glm::vec3(args[0].cast<float>(), args[1].cast<float>(), args[2].cast<float>());
                } else {
                    throw std::runtime_error("set_vector3: expected (name, x, y, z) or (name, vec3)");
                }
                mat.SetVector3(name, v);
            },
            py::arg("name"), "Set a vec3 property: set_vector3(name, x, y, z) or set_vector3(name, (x,y,z))")
        .def(
            "set_vector4",
            [](InfMaterial &mat, const std::string &name, py::args args) {
                glm::vec4 v;
                if (args.size() == 1) {
                    py::object obj = args[0];
                    if (py::isinstance<py::tuple>(obj) || py::isinstance<py::list>(obj)) {
                        py::sequence seq = obj.cast<py::sequence>();
                        v = glm::vec4(seq[0].cast<float>(), seq[1].cast<float>(), seq[2].cast<float>(),
                                      seq[3].cast<float>());
                    } else {
                        v = obj.cast<glm::vec4>();
                    }
                } else if (args.size() >= 4) {
                    v = glm::vec4(args[0].cast<float>(), args[1].cast<float>(), args[2].cast<float>(),
                                  args[3].cast<float>());
                } else {
                    throw std::runtime_error("set_vector4: expected (name, x, y, z, w) or (name, vec4)");
                }
                mat.SetVector4(name, v);
            },
            py::arg("name"), "Set a vec4 property: set_vector4(name, x, y, z, w) or set_vector4(name, (x,y,z,w))")
        .def(
            "set_color",
            [](InfMaterial &mat, const std::string &name, py::args args) {
                glm::vec4 color;
                if (args.size() == 1) {
                    py::object obj = args[0];
                    if (py::isinstance<py::tuple>(obj) || py::isinstance<py::list>(obj)) {
                        py::sequence seq = obj.cast<py::sequence>();
                        color = glm::vec4(seq[0].cast<float>(), seq[1].cast<float>(), seq[2].cast<float>(),
                                          py::len(seq) >= 4 ? seq[3].cast<float>() : 1.0f);
                    } else {
                        color = obj.cast<glm::vec4>();
                    }
                } else if (args.size() >= 3) {
                    float r = args[0].cast<float>();
                    float g = args[1].cast<float>();
                    float b = args[2].cast<float>();
                    float a = args.size() >= 4 ? args[3].cast<float>() : 1.0f;
                    color = glm::vec4(r, g, b, a);
                } else {
                    throw std::runtime_error("set_color: expected (name, r, g, b[, a]) or (name, color_tuple)");
                }
                mat.SetColor(name, color);
            },
            py::arg("name"), "Set a color property: set_color(name, r, g, b[, a]) or set_color(name, (r,g,b,a))")
        .def("set_int", &InfMaterial::SetInt, py::arg("name"), py::arg("value"), "Set an int property")
        .def("set_matrix", &InfMaterial::SetMatrix, py::arg("name"), py::arg("value"), "Set a mat4 property")
        .def("set_texture", &InfMaterial::SetTexture, py::arg("name"), py::arg("texture_path"),
             "Set a texture property")
        // Individual property getters (convenience wrappers over GetProperty)
        .def(
            "get_float",
            [](const InfMaterial &mat, const std::string &name, float defaultVal) -> float {
                const MaterialProperty *prop = mat.GetProperty(name);
                if (prop && prop->type == MaterialPropertyType::Float)
                    return std::get<float>(prop->value);
                return defaultVal;
            },
            py::arg("name"), py::arg("default_value") = 0.0f, "Get a float property")
        .def(
            "get_int",
            [](const InfMaterial &mat, const std::string &name, int defaultVal) -> int {
                const MaterialProperty *prop = mat.GetProperty(name);
                if (prop && prop->type == MaterialPropertyType::Int)
                    return std::get<int>(prop->value);
                return defaultVal;
            },
            py::arg("name"), py::arg("default_value") = 0, "Get an int property")
        .def(
            "get_color",
            [](const InfMaterial &mat, const std::string &name) -> glm::vec4 {
                const MaterialProperty *prop = mat.GetProperty(name);
                if (prop && prop->type == MaterialPropertyType::Float4) {
                    return std::get<glm::vec4>(prop->value);
                }
                return glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
            },
            py::arg("name"), "Get a color property as vec4f")
        .def(
            "get_vector2",
            [](const InfMaterial &mat, const std::string &name) -> glm::vec2 {
                const MaterialProperty *prop = mat.GetProperty(name);
                if (prop && prop->type == MaterialPropertyType::Float2) {
                    return std::get<glm::vec2>(prop->value);
                }
                return glm::vec2(0.0f);
            },
            py::arg("name"), "Get a vec2 property as vec2f")
        .def(
            "get_vector3",
            [](const InfMaterial &mat, const std::string &name) -> glm::vec3 {
                const MaterialProperty *prop = mat.GetProperty(name);
                if (prop && prop->type == MaterialPropertyType::Float3) {
                    return std::get<glm::vec3>(prop->value);
                }
                return glm::vec3(0.0f);
            },
            py::arg("name"), "Get a vec3 property as vec3f")
        .def(
            "get_vector4",
            [](const InfMaterial &mat, const std::string &name) -> glm::vec4 {
                const MaterialProperty *prop = mat.GetProperty(name);
                if (prop && prop->type == MaterialPropertyType::Float4) {
                    return std::get<glm::vec4>(prop->value);
                }
                return glm::vec4(0.0f);
            },
            py::arg("name"), "Get a vec4 property as vec4f")
        .def(
            "get_texture",
            [](const InfMaterial &mat, const std::string &name) -> py::object {
                const MaterialProperty *prop = mat.GetProperty(name);
                if (prop && prop->type == MaterialPropertyType::Texture2D)
                    return py::cast(std::get<std::string>(prop->value));
                return py::none();
            },
            py::arg("name"), "Get a texture property path")
        // Generic property access
        .def("has_property", &InfMaterial::HasProperty, py::arg("name"), "Check if material has a property")
        .def(
            "get_property",
            [](const InfMaterial &mat, const std::string &name) -> py::object {
                const MaterialProperty *prop = mat.GetProperty(name);
                if (!prop) {
                    return py::none();
                }
                // Return the property value as appropriate Python type
                switch (prop->type) {
                case MaterialPropertyType::Float:
                    return py::cast(std::get<float>(prop->value));
                case MaterialPropertyType::Float2:
                    return py::cast(std::get<glm::vec2>(prop->value));
                case MaterialPropertyType::Float3:
                    return py::cast(std::get<glm::vec3>(prop->value));
                case MaterialPropertyType::Float4:
                    return py::cast(std::get<glm::vec4>(prop->value));
                case MaterialPropertyType::Int:
                    return py::cast(std::get<int>(prop->value));
                case MaterialPropertyType::Mat4: {
                    // mat4 not registered as pybind11 type — return as list of 16 floats
                    auto &m = std::get<glm::mat4>(prop->value);
                    py::list result;
                    const float *data = &m[0][0];
                    for (int i = 0; i < 16; ++i)
                        result.append(data[i]);
                    return result;
                }
                case MaterialPropertyType::Texture2D:
                    return py::cast(std::get<std::string>(prop->value));
                }
                return py::none();
            },
            py::arg("name"), "Get a property value by name")
        .def(
            "get_all_properties",
            [](const InfMaterial &mat) -> py::dict {
                py::dict result;
                for (const auto &[name, prop] : mat.GetAllProperties()) {
                    switch (prop.type) {
                    case MaterialPropertyType::Float:
                        result[py::str(name)] = std::get<float>(prop.value);
                        break;
                    case MaterialPropertyType::Float2:
                        result[py::str(name)] = py::cast(std::get<glm::vec2>(prop.value));
                        break;
                    case MaterialPropertyType::Float3:
                        result[py::str(name)] = py::cast(std::get<glm::vec3>(prop.value));
                        break;
                    case MaterialPropertyType::Float4:
                        result[py::str(name)] = py::cast(std::get<glm::vec4>(prop.value));
                        break;
                    case MaterialPropertyType::Int:
                        result[py::str(name)] = std::get<int>(prop.value);
                        break;
                    case MaterialPropertyType::Mat4: {
                        // mat4 not registered as pybind11 type — return as list of 16 floats
                        auto &mat = std::get<glm::mat4>(prop.value);
                        py::list ml;
                        const float *data = &mat[0][0];
                        for (int i = 0; i < 16; ++i)
                            ml.append(data[i]);
                        result[py::str(name)] = ml;
                        break;
                    }
                    case MaterialPropertyType::Texture2D:
                        result[py::str(name)] = std::get<std::string>(prop.value);
                        break;
                    }
                }
                return result;
            },
            "Get all properties as a dictionary")
        // Pipeline state
        .def("is_pipeline_dirty", &InfMaterial::IsPipelineDirty, "Check if pipeline needs recreation")
        .def("clear_pipeline_dirty", &InfMaterial::ClearPipelineDirty, "Clear the pipeline dirty flag")
        .def("get_pipeline_hash", &InfMaterial::GetPipelineHash, "Get a hash of the pipeline configuration")
        // Serialization
        .def("serialize", &InfMaterial::Serialize, "Serialize material to JSON string")
        .def("deserialize", &InfMaterial::Deserialize, py::arg("json_str"), "Deserialize material from JSON string")
        .def_static("create_default_lit", &InfMaterial::CreateDefaultLit,
                    "Create the default lit opaque material (built-in)")
        .def_static("create_default_unlit", &InfMaterial::CreateDefaultUnlit, "Create a default unlit opaque material");

    // MaterialManager - material registry singleton (uses nodelete since it's a singleton with private destructor)
    py::class_<MaterialManager, std::unique_ptr<MaterialManager, py::nodelete>>(m, "MaterialManager")
        .def_static("instance", &MaterialManager::Instance, py::return_value_policy::reference,
                    "Get the MaterialManager singleton")
        .def("initialize", &MaterialManager::Initialize, "Initialize the material manager")
        .def("shutdown", &MaterialManager::Shutdown, "Shutdown and clean up materials")
        .def("get_default_material", &MaterialManager::GetDefaultMaterial, "Get the default unlit opaque material")
        .def("get_error_material", &MaterialManager::GetErrorMaterial,
             "Get the error material (purple-black checkerboard)")
        .def("register_material", &MaterialManager::RegisterMaterial, py::arg("name"), py::arg("material"),
             "Register a material by name")
        .def("get_material", &MaterialManager::GetMaterial, py::arg("name"), "Get a material by name")
        .def("has_material", &MaterialManager::HasMaterial, py::arg("name"), "Check if a material is registered")
        .def("load_material", &MaterialManager::LoadMaterial, py::arg("file_path"), "Load a material from a .mat file")
        .def("load_default_material_from_file", &MaterialManager::LoadDefaultMaterialFromFile, py::arg("mat_file_path"),
             "Load default material from a .mat file in project directory");
}

} // namespace infengine
