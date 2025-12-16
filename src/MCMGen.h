#pragma once

namespace MCMGen
{
	// Resets the "New Widget" counter.
	void ResetSession();

	// Updates the JSON with current settings/cache.
	void Update(bool a_isRuntime = false);
}
