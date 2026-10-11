/// @file
/// @brief Error types raised by the library.

#pragma once

#include <stdexcept>
#include <string>

namespace OpenAnima {

/// Base class of every error raised by the library.
class OpenAnimaError : public std::runtime_error {
public:
    /// Creates an error with a human-readable message.
    /// @param message Description of the failure.
    explicit OpenAnimaError(const std::string& message) : std::runtime_error(message) {}
};

/// Configuration taken from the environment is missing or malformed.
class ConfigError : public OpenAnimaError {
public:
    /// @copydoc OpenAnimaError::OpenAnimaError
    explicit ConfigError(const std::string& message) : OpenAnimaError(message) {}
};

/// The request never reached the provider: DNS, TLS, refused connection or timeout.
class TransportError : public OpenAnimaError {
public:
    /// @copydoc OpenAnimaError::OpenAnimaError
    explicit TransportError(const std::string& message) : OpenAnimaError(message) {}
};

/// The provider answered with a non-success status or with an error payload.
class ProviderError : public OpenAnimaError {
public:
    /// Creates an error carrying the status reported by the provider.
    /// @param message Description of the failure, including the status when there is one.
    /// @param statusCode HTTP status, or 0 when the provider failed without reporting one.
    ProviderError(const std::string& message, long statusCode)
        : OpenAnimaError(message), m_statusCode(statusCode) {}

    /// @return The HTTP status reported by the provider, or 0 when there is none.
    long statusCode() const noexcept {
        return m_statusCode;
    }

private:
    long m_statusCode;
};

/// The provider answered with a body that is not a usable completion payload.
class ParseError : public OpenAnimaError {
public:
    /// @copydoc OpenAnimaError::OpenAnimaError
    explicit ParseError(const std::string& message) : OpenAnimaError(message) {}
};

}  // namespace OpenAnima
