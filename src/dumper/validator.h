// ============================================================================
//  OFFSET VALIDATOR — Runtime sanity checks
//  
//  After loading offsets (from dumper or file), validate them by reading
//  known values from game memory. If health isn't 100 or position is all
//  zeros, something is wrong.
// ============================================================================

#pragma once

#include <string>
#include <vector>

// Forward declarations
namespace mem { class ProcessMemory; }

namespace validator {

// ============================================================================
//  Validation result for a single offset
// ============================================================================

struct ValidationResult {
    std::string offsetName;     // e.g., "AFortPlayerPawn.CurrentHealth"
    bool passed = false;
    std::string details;        // What we found vs. what we expected
};

// ============================================================================
//  Public API
// ============================================================================

/// Validate all critical offsets by reading values from live game memory.
/// Returns a list of validation results. If most pass, offsets are good.
///
/// Requires an active game session (player must be in a match or lobby).
std::vector<ValidationResult> ValidateOffsets(const mem::ProcessMemory& memory);

/// Print validation results to the debug log
void PrintResults(const std::vector<ValidationResult>& results);

/// Quick check: returns true if enough offsets pass validation
bool IsValid(const std::vector<ValidationResult>& results);

} // namespace validator
