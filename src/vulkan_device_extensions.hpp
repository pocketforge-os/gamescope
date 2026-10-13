#pragma once

namespace gamescope::vulkan_device_extensions
{
	enum class ExternalSemaphoreFdSelection
	{
		Disabled,
		Enabled,
		MissingRequired,
	};

	constexpr ExternalSemaphoreFdSelection selectExternalSemaphoreFd(
		bool supportsOutputImageScanout,
		bool supportsExplicitSync,
		bool extensionAvailable )
	{
		const bool required = supportsOutputImageScanout || supportsExplicitSync;

		if ( extensionAvailable )
			return ExternalSemaphoreFdSelection::Enabled;

		return required
			? ExternalSemaphoreFdSelection::MissingRequired
			: ExternalSemaphoreFdSelection::Disabled;
	}
}
