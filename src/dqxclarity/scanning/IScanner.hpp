#pragma once

namespace dqxclarity
{

/**
 * @brief Pure virtual interface for all scanner types
 * 
 * Scanners perform non-intrusive memory pattern detection and data extraction.
 * Unlike hooks (which inject code), scanners only read memory.
 * 
 * Lifecycle States:
 * 1. Constructed - Scanner created, not initialized
 * 2. Initialized - Ready to scan (pattern may be pre-located)
 * 3. Active - Actively scanning (for continuous scanners)
 * 4. Shutdown - Stopped scanning
 */
class IScanner
{
public:
    virtual ~IScanner() = default;

    /**
     * @brief Initialize scanner (find patterns, prepare for scanning)
     * @return true on success
     */
    virtual bool Initialize() = 0;

    /**
     * @brief Poll/scan for data (called periodically for continuous scanners)
     * @return true if new data was captured
     */
    virtual bool Poll() = 0;

    /**
     * @brief Check if scanner is active and operational
     * @return true if active
     */
    virtual bool IsActive() const = 0;

    /**
     * @brief Shutdown scanner and release resources
     */
    virtual void Shutdown() = 0;
};

} // namespace dqxclarity

