/**
 * @file BindingVector4f.cpp
 * @brief Python bindings for glm::vec4 as "vec4f" and glm::quat as "quatf".
 *
 * Replaces the former custom Vector4f bindings.  Now operates directly on
 * glm::vec4 so that no manual conversion is needed anywhere in the binding
 * layer.  Also adds glm::quat bindings ("quatf") that were previously
 * missing — quaternions were only exposed as raw (x,y,z,w) tuples before.
 */

#include <cmath>
#include <functional>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <pybind11/pybind11.h>

namespace py = pybind11;

namespace infengine
{

// ── helper free-functions (mirror the old Vector4f static API) ──────────────
namespace vec4_util
{

inline float Dot(const glm::vec4 &a, const glm::vec4 &b)
{
    return glm::dot(a, b);
}
inline float Magnitude(const glm::vec4 &v)
{
    return glm::length(v);
}
inline float SqrMagnitude(const glm::vec4 &v)
{
    return glm::dot(v, v);
}
inline glm::vec4 Project(const glm::vec4 &a, const glm::vec4 &b)
{
    float denom = Dot(b, b);
    if (denom < 1e-6f)
        return glm::vec4(0.f);
    return b * (Dot(a, b) / denom);
}
inline float Distance(const glm::vec4 &a, const glm::vec4 &b)
{
    return Magnitude(a - b);
}
inline glm::vec4 Lerp(const glm::vec4 &a, const glm::vec4 &b, float t)
{
    t = std::clamp(t, 0.f, 1.f);
    return a + (b - a) * t;
}
inline glm::vec4 LerpUnclamped(const glm::vec4 &a, const glm::vec4 &b, float t)
{
    return a + (b - a) * t;
}
inline glm::vec4 Max(const glm::vec4 &a, const glm::vec4 &b)
{
    return glm::max(a, b);
}
inline glm::vec4 Min(const glm::vec4 &a, const glm::vec4 &b)
{
    return glm::min(a, b);
}
inline glm::vec4 MoveTowards(const glm::vec4 &current, const glm::vec4 &target, float maxDelta)
{
    glm::vec4 to = target - current;
    float dist = Magnitude(to);
    if (dist <= maxDelta || dist < 1e-6f)
        return target;
    return current + to * (maxDelta / dist);
}
inline glm::vec4 Normalize(const glm::vec4 &v)
{
    float mag = Magnitude(v);
    if (mag < 1e-6f)
        return glm::vec4(0.f);
    return v / mag;
}
inline glm::vec4 SmoothDamp(glm::vec4 current, glm::vec4 target, glm::vec4 &currentVelocity, float smoothTime,
                            float maxSpeed, float deltaTime)
{
    if (smoothTime < 1e-6f)
        smoothTime = 1e-6f;
    glm::vec4 diff = current - target;
    float dist = Magnitude(diff);
    if (dist > maxSpeed * smoothTime)
        diff = diff * (maxSpeed * smoothTime / dist);
    glm::vec4 targetPos = current - diff;
    float omega = 2.f / smoothTime;
    float x = omega * deltaTime;
    float exp = 1.f / (1.f + x + 0.48f * x * x + 0.235f * x * x * x);
    glm::vec4 temp = (diff * omega) + currentVelocity;
    glm::vec4 change = temp * deltaTime;
    currentVelocity = (currentVelocity - temp * omega) * exp;
    return targetPos + (diff + change) * exp;
}

} // namespace vec4_util

void RegisterVec4fBindings(py::module_ &m)
{
    // ====================================================================
    // vec4f — glm::vec4
    // ====================================================================
    {
        using Vec = glm::vec4;
        py::class_<Vec>(m, "vec4f")
            .def(py::init<>())
            .def(py::init<float, float, float, float>(), "Construct vec4f")
            .def("__getitem__",
                 [](const Vec &v, int i) -> float {
                     if (i < 0 || i >= 4)
                         throw std::out_of_range("index out of range");
                     return v[i];
                 })
            .def("__setitem__",
                 [](Vec &v, int i, float value) {
                     if (i < 0 || i >= 4)
                         throw std::out_of_range("index out of range");
                     v[i] = value;
                 })
            .def("__add__", [](const Vec &a, const Vec &b) { return Vec(a + b); })
            .def("__add__",
                 [](const Vec &v, py::object s) {
                     float f = py::isinstance<py::int_>(s) ? static_cast<float>(s.cast<int>()) : s.cast<float>();
                     return Vec(v.x + f, v.y + f, v.z + f, v.w + f);
                 })
            .def("__radd__",
                 [](const Vec &v, py::object s) {
                     float f = py::isinstance<py::int_>(s) ? static_cast<float>(s.cast<int>()) : s.cast<float>();
                     return Vec(f + v.x, f + v.y, f + v.z, f + v.w);
                 })
            .def("__sub__", [](const Vec &a, const Vec &b) { return Vec(a - b); })
            .def("__sub__",
                 [](const Vec &v, py::object s) {
                     float f = py::isinstance<py::int_>(s) ? static_cast<float>(s.cast<int>()) : s.cast<float>();
                     return Vec(v.x - f, v.y - f, v.z - f, v.w - f);
                 })
            .def("__rsub__",
                 [](const Vec &v, py::object s) {
                     float f = py::isinstance<py::int_>(s) ? static_cast<float>(s.cast<int>()) : s.cast<float>();
                     return Vec(f - v.x, f - v.y, f - v.z, f - v.w);
                 })
            .def("__mul__", [](const Vec &a, const Vec &b) { return Vec(a * b); })
            .def("__mul__",
                 [](const Vec &v, py::object s) {
                     float f = py::isinstance<py::int_>(s) ? static_cast<float>(s.cast<int>()) : s.cast<float>();
                     return Vec(v * f);
                 })
            .def("__rmul__",
                 [](const Vec &v, py::object s) {
                     float f = py::isinstance<py::int_>(s) ? static_cast<float>(s.cast<int>()) : s.cast<float>();
                     return Vec(v * f);
                 })
            .def("__truediv__", [](const Vec &a, const Vec &b) { return Vec(a / b); })
            .def("__truediv__",
                 [](const Vec &v, py::object s) {
                     float f = py::isinstance<py::int_>(s) ? static_cast<float>(s.cast<int>()) : s.cast<float>();
                     return Vec(v / f);
                 })
            .def("__rtruediv__",
                 [](const Vec &v, py::object s) {
                     float f = py::isinstance<py::int_>(s) ? static_cast<float>(s.cast<int>()) : s.cast<float>();
                     return Vec(f / v.x, f / v.y, f / v.z, f / v.w);
                 })
            .def("__iadd__",
                 [](Vec &v, const Vec &o) {
                     v += o;
                     return v;
                 })
            .def("__iadd__",
                 [](Vec &v, py::object s) {
                     float f = py::isinstance<py::int_>(s) ? static_cast<float>(s.cast<int>()) : s.cast<float>();
                     v.x += f;
                     v.y += f;
                     v.z += f;
                     v.w += f;
                     return v;
                 })
            .def("__isub__",
                 [](Vec &v, const Vec &o) {
                     v -= o;
                     return v;
                 })
            .def("__isub__",
                 [](Vec &v, py::object s) {
                     float f = py::isinstance<py::int_>(s) ? static_cast<float>(s.cast<int>()) : s.cast<float>();
                     v.x -= f;
                     v.y -= f;
                     v.z -= f;
                     v.w -= f;
                     return v;
                 })
            .def("__imul__",
                 [](Vec &v, py::object s) {
                     float f = py::isinstance<py::int_>(s) ? static_cast<float>(s.cast<int>()) : s.cast<float>();
                     v *= f;
                     return v;
                 })
            .def("__itruediv__",
                 [](Vec &v, py::object s) {
                     float f = py::isinstance<py::int_>(s) ? static_cast<float>(s.cast<int>()) : s.cast<float>();
                     v /= f;
                     return v;
                 })
            .def("__eq__",
                 [](const Vec &a, const Vec &b) {
                     return std::fabs(a.x - b.x) <= 1e-6f && std::fabs(a.y - b.y) <= 1e-6f &&
                            std::fabs(a.z - b.z) <= 1e-6f && std::fabs(a.w - b.w) <= 1e-6f;
                 })
            .def("__ne__",
                 [](const Vec &a, const Vec &b) {
                     return std::fabs(a.x - b.x) > 1e-6f || std::fabs(a.y - b.y) > 1e-6f ||
                            std::fabs(a.z - b.z) > 1e-6f || std::fabs(a.w - b.w) > 1e-6f;
                 })
            .def_property(
                "x", [](const Vec &v) { return v.x; }, [](Vec &v, float val) { v.x = val; })
            .def_property(
                "y", [](const Vec &v) { return v.y; }, [](Vec &v, float val) { v.y = val; })
            .def_property(
                "z", [](const Vec &v) { return v.z; }, [](Vec &v, float val) { v.z = val; })
            .def_property(
                "w", [](const Vec &v) { return v.w; }, [](Vec &v, float val) { v.w = val; })
            .def_property(
                "r", [](const Vec &v) { return v.x; }, [](Vec &v, float val) { v.x = val; })
            .def_property(
                "g", [](const Vec &v) { return v.y; }, [](Vec &v, float val) { v.y = val; })
            .def_property(
                "b", [](const Vec &v) { return v.z; }, [](Vec &v, float val) { v.z = val; })
            .def_property(
                "a", [](const Vec &v) { return v.w; }, [](Vec &v, float val) { v.w = val; })
            .def("__repr__",
                 [](const Vec &v) {
                     return "Vector4(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ", " + std::to_string(v.z) +
                            ", " + std::to_string(v.w) + ")";
                 })
            .def("__str__",
                 [](const Vec &v) {
                     return "(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ", " + std::to_string(v.z) + ", " +
                            std::to_string(v.w) + ")";
                 })
            .def("__neg__", [](const Vec &v) { return Vec(-v.x, -v.y, -v.z, -v.w); })
            .def("__len__", [](const Vec &) { return 4; })
            .def(
                "__iter__", [](const Vec &v) { return py::make_iterator(&v.x, &v.x + 4); }, py::keep_alive<0, 1>())
            .def("__hash__",
                 [](const Vec &v) {
                     size_t h = std::hash<float>{}(v.x);
                     h ^= std::hash<float>{}(v.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
                     h ^= std::hash<float>{}(v.z) + 0x9e3779b9 + (h << 6) + (h >> 2);
                     h ^= std::hash<float>{}(v.w) + 0x9e3779b9 + (h << 6) + (h >> 2);
                     return h;
                 })
            .def("__bool__", [](const Vec &v) { return vec4_util::SqrMagnitude(v) > 1e-12f; })
            .def("__abs__",
                 [](const Vec &v) { return Vec(std::abs(v.x), std::abs(v.y), std::abs(v.z), std::abs(v.w)); })
            .def("__copy__", [](const Vec &v) { return Vec(v); })
            .def("__deepcopy__", [](const Vec &v, py::dict) { return Vec(v); })
            // ---- Instance properties ----
            .def_property_readonly(
                "magnitude", [](const Vec &v) { return vec4_util::Magnitude(v); },
                "Length of this vector. Unity: vector.magnitude")
            .def_property_readonly(
                "normalized", [](const Vec &v) { return vec4_util::Normalize(v); },
                "Normalized copy of this vector. Unity: vector.normalized")
            .def_property_readonly(
                "sqr_magnitude", [](const Vec &v) { return vec4_util::SqrMagnitude(v); },
                "Squared length of this vector. Unity: vector.sqrMagnitude")
            // ---- Instance methods ----
            .def(
                "set",
                [](Vec &v, float x, float y, float z, float w) {
                    v.x = x;
                    v.y = y;
                    v.z = z;
                    v.w = w;
                },
                py::arg("x"), py::arg("y"), py::arg("z"), py::arg("w"), "Set x, y, z, w components")
            .def("to_tuple",
                 [](const Vec &v) -> py::tuple {
                     return py::make_tuple(py::float_(v.x), py::float_(v.y), py::float_(v.z), py::float_(v.w));
                 })
            .def("to_list",
                 [](const Vec &v) {
                     py::list l;
                     l.append(v.x);
                     l.append(v.y);
                     l.append(v.z);
                     l.append(v.w);
                     return l;
                 })
            // ---- Static constants ----
            .def_static("zero", []() { return Vec(0.f); })
            .def_static("one", []() { return Vec(1.f); })
            .def_static("negative_infinity", []() { return Vec(-std::numeric_limits<float>::infinity()); })
            .def_static("positive_infinity", []() { return Vec(std::numeric_limits<float>::infinity()); })
            // ---- Static math operations ----
            .def_static("normalize", &vec4_util::Normalize)
            .def_static("distance", &vec4_util::Distance)
            .def_static("project", &vec4_util::Project)
            .def_static("dot", &vec4_util::Dot)
            .def_static("lerp", &vec4_util::Lerp)
            .def_static("lerp_unclamped", &vec4_util::LerpUnclamped)
            .def_static("max", &vec4_util::Max)
            .def_static("min", &vec4_util::Min)
            .def_static("move_towards", &vec4_util::MoveTowards)
            .def_static(
                "scale", [](const Vec &a, const Vec &b) { return Vec(a * b); },
                "Multiplies two vectors component-wise. Unity: Vector4.Scale(a, b)")
            .def_static("smooth_damp", [](const Vec &current, const Vec &target, Vec &currentVelocity, float smoothTime,
                                          float maxSpeed, float deltaTime) {
                return vec4_util::SmoothDamp(current, target, currentVelocity, smoothTime, maxSpeed, deltaTime);
            });
    }

    // ====================================================================
    // quatf — glm::quat  (NEW — previously only exposed as raw tuples)
    // ====================================================================
    {
        using Quat = glm::quat;
        py::class_<Quat>(m, "quatf")
            .def(py::init<>())
            .def(py::init([](float x, float y, float z, float w) {
                     return Quat(w, x, y, z); // glm ctor order: w, x, y, z
                 }),
                 py::arg("x"), py::arg("y"), py::arg("z"), py::arg("w"), "Construct from (x, y, z, w)")
            .def_property(
                "x", [](const Quat &q) { return q.x; }, [](Quat &q, float v) { q.x = v; })
            .def_property(
                "y", [](const Quat &q) { return q.y; }, [](Quat &q, float v) { q.y = v; })
            .def_property(
                "z", [](const Quat &q) { return q.z; }, [](Quat &q, float v) { q.z = v; })
            .def_property(
                "w", [](const Quat &q) { return q.w; }, [](Quat &q, float v) { q.w = v; })
            .def("__repr__",
                 [](const Quat &q) {
                     return "Quaternion(" + std::to_string(q.x) + ", " + std::to_string(q.y) + ", " +
                            std::to_string(q.z) + ", " + std::to_string(q.w) + ")";
                 })
            .def("__str__",
                 [](const Quat &q) {
                     return "(" + std::to_string(q.x) + ", " + std::to_string(q.y) + ", " + std::to_string(q.z) + ", " +
                            std::to_string(q.w) + ")";
                 })
            .def("__eq__", [](const Quat &a, const Quat &b) { return a == b; })
            .def("__ne__", [](const Quat &a, const Quat &b) { return a != b; })
            .def("__mul__", [](const Quat &a, const Quat &b) { return a * b; })
            .def("__getitem__",
                 [](const Quat &q, int i) -> float {
                     switch (i) {
                     case 0:
                         return q.x;
                     case 1:
                         return q.y;
                     case 2:
                         return q.z;
                     case 3:
                         return q.w;
                     default:
                         throw std::out_of_range("index out of range");
                     }
                 })
            .def("__len__", [](const Quat &) { return 4; })
            .def(
                "__iter__",
                [](const Quat &q) {
                    // x, y, z, w order
                    return py::make_iterator(&q.x, &q.x + 4);
                },
                py::keep_alive<0, 1>())
            .def("__copy__", [](const Quat &q) { return Quat(q); })
            .def("__deepcopy__", [](const Quat &q, py::dict) { return Quat(q); })
            .def("to_tuple",
                 [](const Quat &q) -> py::tuple {
                     return py::make_tuple(py::float_(q.x), py::float_(q.y), py::float_(q.z), py::float_(q.w));
                 })
            // ---- Instance properties ----
            .def_property_readonly(
                "euler_angles",
                [](const Quat &q) {
                    auto e = glm::degrees(glm::eulerAngles(q));
                    return glm::vec3(e.x, e.y, e.z);
                },
                "Convert to Euler angles (degrees)")
            .def_property_readonly(
                "normalized", [](const Quat &q) { return glm::normalize(q); }, "Normalized copy of this quaternion")
            // ---- Static constructors ----
            .def_static("identity", []() { return Quat(1.f, 0.f, 0.f, 0.f); })
            .def_static(
                "euler", [](float x, float y, float z) { return glm::quat(glm::radians(glm::vec3(x, y, z))); },
                py::arg("x"), py::arg("y"), py::arg("z"), "Create quaternion from Euler angles (degrees)")
            .def_static(
                "angle_axis",
                [](float angle, const glm::vec3 &axis) {
                    return glm::angleAxis(glm::radians(angle), glm::normalize(axis));
                },
                py::arg("angle"), py::arg("axis"), "Create quaternion from angle (degrees) and axis")
            .def_static(
                "look_rotation",
                [](const glm::vec3 &forward, const glm::vec3 &up) {
                    return glm::quatLookAt(glm::normalize(forward), up);
                },
                py::arg("forward"), py::arg("up") = glm::vec3(0.f, 1.f, 0.f),
                "Create rotation looking in forward direction")
            // ---- Static math ----
            .def_static("dot", [](const Quat &a, const Quat &b) { return glm::dot(a, b); })
            .def_static("angle",
                        [](const Quat &a, const Quat &b) { return glm::degrees(glm::angle(a * glm::inverse(b))); })
            .def_static("slerp",
                        [](const Quat &a, const Quat &b, float t) { return glm::slerp(a, b, std::clamp(t, 0.f, 1.f)); })
            .def_static("lerp", [](const Quat &a, const Quat &b,
                                   float t) { return glm::normalize(glm::lerp(a, b, std::clamp(t, 0.f, 1.f))); })
            .def_static("inverse", [](const Quat &q) { return glm::inverse(q); })
            .def_static(
                "rotate_towards",
                [](const Quat &from, const Quat &to, float maxDeg) {
                    float ang = glm::degrees(glm::angle(from * glm::inverse(to)));
                    if (ang < 1e-6f)
                        return to;
                    float t = std::min(1.f, maxDeg / ang);
                    return glm::slerp(from, to, t);
                },
                py::arg("from"), py::arg("to"), py::arg("max_degrees_delta"));
    }
}

} // namespace infengine
