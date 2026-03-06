#include "function/resources/AssetDatabase/AssetDatabase.h"
#include "function/resources/InfResource/InfResourceMeta.h"

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;

namespace infengine
{

void RegisterAssetDatabaseBindings(py::module_ &m)
{
    py::class_<AssetDatabase>(m, "AssetDatabase")
        .def(py::init<>())
        .def("initialize", &AssetDatabase::Initialize, py::arg("project_root"),
             "Initialize asset database with project root")
        .def("refresh", &AssetDatabase::Refresh, "Refresh assets by scanning Assets folder")
        .def("import_asset", &AssetDatabase::ImportAsset, py::arg("path"), "Import a single asset")
        .def("delete_asset", &AssetDatabase::DeleteAsset, py::arg("path"), "Delete asset and its meta")
        .def("move_asset", &AssetDatabase::MoveAsset, py::arg("old_path"), py::arg("new_path"),
             "Move/rename asset preserving GUID")
        .def("on_asset_created", &AssetDatabase::OnAssetCreated, py::arg("path"), "File watcher hook: asset created")
        .def("on_asset_modified", &AssetDatabase::OnAssetModified, py::arg("path"), "File watcher hook: asset modified")
        .def("on_asset_deleted", &AssetDatabase::OnAssetDeleted, py::arg("path"), "File watcher hook: asset deleted")
        .def("on_asset_moved", &AssetDatabase::OnAssetMoved, py::arg("old_path"), py::arg("new_path"),
             "File watcher hook: asset moved")
        .def("contains_guid", &AssetDatabase::ContainsGuid, py::arg("guid"), "Check if GUID exists")
        .def("contains_path", &AssetDatabase::ContainsPath, py::arg("path"), "Check if path exists")
        .def("get_guid_from_path", &AssetDatabase::GetGuidFromPath, py::arg("path"), "Get GUID from asset path")
        .def("get_path_from_guid", &AssetDatabase::GetPathFromGuid, py::arg("guid"), "Get asset path from GUID")
        .def("get_meta_by_guid", &AssetDatabase::GetMetaByGuid, py::arg("guid"), py::return_value_policy::reference,
             "Get meta by GUID")
        .def("get_meta_by_path", &AssetDatabase::GetMetaByPath, py::arg("path"), py::return_value_policy::reference,
             "Get meta by path")
        .def("get_all_guids", &AssetDatabase::GetAllGuids, "Get all GUIDs in database")
        .def("is_asset_path", &AssetDatabase::IsAssetPath, py::arg("path"), "Check if path is in Assets folder")
        .def_property_readonly("project_root", &AssetDatabase::GetProjectRoot, "Project root path")
        .def_property_readonly("assets_root", &AssetDatabase::GetAssetsRoot, "Assets root path");
}

} // namespace infengine
