#pragma once

#include <core/log/InfLog.h>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>

namespace infengine
{

// ----------------------------------
// Error Codes
// ----------------------------------

enum class ErrorCode
{
    Success = 0,

    // General errors (1-99)
    Unknown = 1,
    InvalidArgument = 2,
    NullPointer = 3,
    NotInitialized = 4,
    AlreadyInitialized = 5,
    NotFound = 6,
    AlreadyExists = 7,
    InvalidState = 8,
    OperationFailed = 9,

    // Resource errors (100-199)
    ResourceLoadFailed = 100,
    ResourceNotFound = 101,
    ResourceInvalid = 102,
    ResourceBusy = 103,
    FileNotFound = 104,
    FileReadFailed = 105,
    FileWriteFailed = 106,

    // Renderer errors (200-299)
    RendererNotInitialized = 200,
    VulkanError = 201,
    ShaderCompileFailed = 202,
    PipelineCreateFailed = 203,
    SwapchainError = 204,
    TextureUploadFailed = 205,

    // GUI errors (300-399)
    GUINotInitialized = 300,
    GUIRenderableNotFound = 301,

    // Engine errors (400-499)
    EngineNotInitialized = 400,
    EngineAlreadyRunning = 401,
    EngineCleanedUp = 402,
};

// ----------------------------------
// Error class
// ----------------------------------

class InfError
{
  public:
    InfError() : m_code(ErrorCode::Success)
    {
    }

    explicit InfError(ErrorCode code, const std::string &message = "")
        : m_code(code), m_message(message.empty() ? GetDefaultMessage(code) : message)
    {
    }

    [[nodiscard]] bool IsOk() const
    {
        return m_code == ErrorCode::Success;
    }
    [[nodiscard]] bool IsError() const
    {
        return m_code != ErrorCode::Success;
    }

    [[nodiscard]] ErrorCode GetCode() const
    {
        return m_code;
    }
    [[nodiscard]] const std::string &GetMessage() const
    {
        return m_message;
    }

    /// @brief Log the error if it's not success
    void LogIfError(const char *file = __FILE__, int line = __LINE__) const
    {
        if (IsError()) {
            InfLog::GetInstance().Log(LOG_ERROR, file, line, "[Error ", static_cast<int>(m_code), "] ", m_message);
        }
    }

    static InfError Ok()
    {
        return InfError();
    }

    static InfError Make(ErrorCode code, const std::string &message = "")
    {
        return InfError(code, message);
    }

  private:
    ErrorCode m_code;
    std::string m_message;

    static std::string GetDefaultMessage(ErrorCode code)
    {
        switch (code) {
        case ErrorCode::Success:
            return "Success";
        case ErrorCode::Unknown:
            return "Unknown error";
        case ErrorCode::InvalidArgument:
            return "Invalid argument";
        case ErrorCode::NullPointer:
            return "Null pointer";
        case ErrorCode::NotInitialized:
            return "Not initialized";
        case ErrorCode::AlreadyInitialized:
            return "Already initialized";
        case ErrorCode::NotFound:
            return "Not found";
        case ErrorCode::AlreadyExists:
            return "Already exists";
        case ErrorCode::InvalidState:
            return "Invalid state";
        case ErrorCode::OperationFailed:
            return "Operation failed";
        case ErrorCode::ResourceLoadFailed:
            return "Resource load failed";
        case ErrorCode::ResourceNotFound:
            return "Resource not found";
        case ErrorCode::ResourceInvalid:
            return "Resource invalid";
        case ErrorCode::ResourceBusy:
            return "Resource busy";
        case ErrorCode::FileNotFound:
            return "File not found";
        case ErrorCode::FileReadFailed:
            return "File read failed";
        case ErrorCode::FileWriteFailed:
            return "File write failed";
        case ErrorCode::RendererNotInitialized:
            return "Renderer not initialized";
        case ErrorCode::VulkanError:
            return "Vulkan error";
        case ErrorCode::ShaderCompileFailed:
            return "Shader compile failed";
        case ErrorCode::PipelineCreateFailed:
            return "Pipeline create failed";
        case ErrorCode::SwapchainError:
            return "Swapchain error";
        case ErrorCode::TextureUploadFailed:
            return "Texture upload failed";
        case ErrorCode::GUINotInitialized:
            return "GUI not initialized";
        case ErrorCode::GUIRenderableNotFound:
            return "GUI renderable not found";
        case ErrorCode::EngineNotInitialized:
            return "Engine not initialized";
        case ErrorCode::EngineAlreadyRunning:
            return "Engine already running";
        case ErrorCode::EngineCleanedUp:
            return "Engine has been cleaned up";
        default:
            return "Unknown error code";
        }
    }
};

// ----------------------------------
// Result template for returning value or error
// ----------------------------------

template <typename T> class Result
{
  public:
    Result(const T &value) : m_data(value)
    {
    }
    Result(T &&value) : m_data(std::move(value))
    {
    }
    Result(const InfError &error) : m_data(error)
    {
    }
    Result(InfError &&error) : m_data(std::move(error))
    {
    }

    [[nodiscard]] bool IsOk() const
    {
        return std::holds_alternative<T>(m_data);
    }
    [[nodiscard]] bool IsError() const
    {
        return std::holds_alternative<InfError>(m_data);
    }

    [[nodiscard]] const T &GetValue() const
    {
        return std::get<T>(m_data);
    }
    [[nodiscard]] T &GetValue()
    {
        return std::get<T>(m_data);
    }
    [[nodiscard]] T &&TakeValue()
    {
        return std::move(std::get<T>(m_data));
    }

    [[nodiscard]] const InfError &GetError() const
    {
        return std::get<InfError>(m_data);
    }

    /// @brief Get value or default if error
    [[nodiscard]] T ValueOr(const T &defaultValue) const
    {
        if (IsOk()) {
            return GetValue();
        }
        return defaultValue;
    }

    /// @brief Log error if present
    void LogIfError(const char *file = __FILE__, int line = __LINE__) const
    {
        if (IsError()) {
            GetError().LogIfError(file, line);
        }
    }

    static Result<T> Ok(const T &value)
    {
        return Result<T>(value);
    }
    static Result<T> Ok(T &&value)
    {
        return Result<T>(std::move(value));
    }
    static Result<T> Err(ErrorCode code, const std::string &message = "")
    {
        return Result<T>(InfError::Make(code, message));
    }

  private:
    std::variant<T, InfError> m_data;
};

// ----------------------------------
// Result<void> specialization
// ----------------------------------

template <> class Result<void>
{
  public:
    Result() : m_error(std::nullopt)
    {
    }
    Result(const InfError &error) : m_error(error)
    {
    }
    Result(InfError &&error) : m_error(std::move(error))
    {
    }

    [[nodiscard]] bool IsOk() const
    {
        return !m_error.has_value();
    }
    [[nodiscard]] bool IsError() const
    {
        return m_error.has_value();
    }

    [[nodiscard]] const InfError &GetError() const
    {
        return m_error.value();
    }

    void LogIfError(const char *file = __FILE__, int line = __LINE__) const
    {
        if (IsError()) {
            GetError().LogIfError(file, line);
        }
    }

    static Result<void> Ok()
    {
        return Result<void>();
    }
    static Result<void> Err(ErrorCode code, const std::string &message = "")
    {
        return Result<void>(InfError::Make(code, message));
    }

  private:
    std::optional<InfError> m_error;
};

// ----------------------------------
// Exception class (for Python binding)
// ----------------------------------

class InfException : public std::runtime_error
{
  public:
    explicit InfException(const InfError &error)
        : std::runtime_error(error.GetMessage()), m_code(error.GetCode()), m_message(error.GetMessage())
    {
    }

    explicit InfException(ErrorCode code, const std::string &message = "")
        : std::runtime_error(message.empty() ? "InfEngine Error" : message), m_code(code), m_message(message)
    {
    }

    [[nodiscard]] ErrorCode GetCode() const
    {
        return m_code;
    }
    [[nodiscard]] const std::string &GetErrorMessage() const
    {
        return m_message;
    }

  private:
    ErrorCode m_code;
    std::string m_message;
};

// ----------------------------------
// Macros for error handling
// ----------------------------------

/// @brief Check condition and return error if false
#define INF_CHECK(condition, code, message)                                                                            \
    do {                                                                                                               \
        if (!(condition)) {                                                                                            \
            return InfError::Make(code, message);                                                                      \
        }                                                                                                              \
    } while (false)

/// @brief Check condition and throw exception if false
#define INF_CHECK_THROW(condition, code, message)                                                                      \
    do {                                                                                                               \
        if (!(condition)) {                                                                                            \
            throw InfException(code, message);                                                                         \
        }                                                                                                              \
    } while (false)

/// @brief Propagate error from Result
#define INF_TRY(result)                                                                                                \
    do {                                                                                                               \
        auto _result = (result);                                                                                       \
        if (_result.IsError()) {                                                                                       \
            return _result.GetError();                                                                                 \
        }                                                                                                              \
    } while (false)

/// @brief Log and return error
#define INFLOG_ERROR_RETURN(code, message)                                                                             \
    do {                                                                                                               \
        auto _error = InfError::Make(code, message);                                                                   \
        _error.LogIfError(__FILE__, __LINE__);                                                                         \
        return _error;                                                                                                 \
    } while (false)

} // namespace infengine
