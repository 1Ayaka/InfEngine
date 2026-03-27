#include "function/resources/AssetDatabase/AssetDatabase.h"
#include "function/resources/AssetDependencyGraph.h"
#include "function/resources/InfResource/InfResourceMeta.h"

#include <pybind11/functional.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;

namespace infengine
{

void RegisterAssetDatabaseBindings(py::module_ &m)
{
    // AssetEvent enum
    py::enum_<AssetEvent>(m, "AssetEvent")
        .value("Deleted", AssetEvent::Deleted)
        .value("Modified", AssetEvent::Modified)
        .value("Moved", AssetEvent::Moved);

    // AssetDependencyGraph singleton
    py::class_<AssetDependencyGraph, std::unique_ptr<AssetDependencyGraph, py::nodelete>>(m, "AssetDependencyGraph")
        .def_static("instance", &AssetDependencyGraph::Instance, py::return_value_policy::reference,
                    "Get the singleton instance")
        .def("add_dependency", &AssetDependencyGraph::AddDependency, py::arg("user_guid"), py::arg("dependency_guid"),
             "Register that user depends on dependency")
        .def("remove_dependency", &AssetDependencyGraph::RemoveDependency, py::arg("user_guid"),
             py::arg("dependency_guid"), "Remove a single dependency edge")
        .def("clear_dependencies_of", &AssetDependencyGraph::ClearDependenciesOf, py::arg("user_guid"),
             "Remove all dependencies declared by user")
        .def("remove_asset", &AssetDependencyGraph::RemoveAsset, py::arg("guid"), "Remove all records for an asset")
        .def("set_dependencies", &AssetDependencyGraph::SetDependencies, py::arg("user_guid"),
             py::arg("dependency_guids"), "Bulk-set dependencies (replaces previous)")
        .def("get_dependencies", &AssetDependencyGraph::GetDependencies, py::arg("guid"),
             "Get all GUIDs that this asset depends on")
        .def("get_dependents", &AssetDependencyGraph::GetDependents, py::arg("guid"),
             "Get all GUIDs that depend on this asset")
        .def("has_dependency", &AssetDependencyGraph::HasDependency, py::arg("user_guid"), py::arg("dependency_guid"),
             "Check if user depends on dependency")
        .def("get_edge_count", &AssetDependencyGraph::GetEdgeCount, "Total dependency edges")
        .def("get_node_count", &AssetDependencyGraph::GetNodeCount, "Total tracked assets")
        .def("clear", &AssetDependencyGraph::Clear, "Clear the entire graph");

    // AssetDatabase
    py::class_<AssetDatabase>(m, "AssetDatabase")
        .def(py::init<>())
        .def("initialize", &AssetDatabase::Initialize, py::arg("project_root"),
             "Initialize asset database with project root")
        .def("refresh", &AssetDatabase::Refresh, "Refresh assets by scanning Assets folder")
        .def("add_scan_root", &AssetDatabase::AddScanRoot, py::arg("path"),
             "Add an extra directory to scan during Refresh (e.g. Library/Resources)")
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
        .def_property_readonly("assets_root", &AssetDatabase::GetAssetsRoot, "Assets root path")
        // Resource management methods
        .def("register_resource", &AssetDatabase::RegisterResource, py::arg("file_path"), py::arg("type"),
             "Register a resource file and get its UID")
        .def("move_resource", &AssetDatabase::MoveResource, py::arg("old_path"), py::arg("new_path"),
             "Move/rename a resource file")
        .def("delete_resource", &AssetDatabase::DeleteResource, py::arg("file_path"),
             "Delete a resource from caches and its meta file")
        .def("modify_resource", &AssetDatabase::ModifyResource, py::arg("file_path"),
             "Notify that a resource has been modified, update meta")
        .def("get_resource_type", &AssetDatabase::GetResourceTypeForPath, py::arg("file_path"),
             "Get the ResourceType for a file based on its path")
        .def("get_all_resource_guids", &AssetDatabase::GetAllResourceGuids,
             "Get a list of all registered resource GUIDs");
}

} // namespace infengine
