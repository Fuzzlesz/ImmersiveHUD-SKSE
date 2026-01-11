#pragma once

namespace MCMGen
{
	// Updates the JSON with current settings/cache.
	// a_isRuntime: Controls status text (New Found vs Registered) to prevent stale messages.
	// a_widgetsPopulated: If false, skips pruning of widget-container elements.
	void Update(bool a_isRuntime = false, bool a_widgetsPopulated = false);

	// Resets the session modification flag (called when transitioning to runtime)
	void ResetSessionFlag();
}
