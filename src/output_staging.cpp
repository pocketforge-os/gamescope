#include "output_staging.hpp"

namespace gamescope::output_staging
{

namespace
{

constexpr FeatureFlags kCompositionFeatures =
	FeatureSampled | FeatureStorage | FeatureTransferSrc;

bool hasFeatures( FeatureFlags available, FeatureFlags required )
{
	return ( available & required ) == required;
}

} // namespace

OutputPlan chooseOutputPlan( const PlanInput &input )
{
	bool hasKmsFormat = false;
	for ( const KmsModifier &kms : input.kmsModifiers )
	{
		if ( kms.format != input.format )
			continue;

		hasKmsFormat = true;
		for ( const VulkanModifier &vulkan : input.vulkanModifiers )
		{
			if ( vulkan.format == input.format &&
				vulkan.modifier == kms.modifier &&
				vulkan.modifier != LinearModifier &&
				vulkan.combinedExportable &&
				hasFeatures( vulkan.features, kCompositionFeatures ) )
				return { OutputMode::Combined, kms.modifier, Rejection::Accepted };
		}
	}

	if ( !hasKmsFormat )
		return { OutputMode::Unsupported, InvalidModifier, Rejection::NoKmsFormat };

	if ( !hasFeatures( input.optimalFeatures, kCompositionFeatures ) )
		return { OutputMode::Unsupported, InvalidModifier, Rejection::NoOptimalComposition };

	bool hasKmsLinear = false;
	for ( const KmsModifier &kms : input.kmsModifiers )
	{
		if ( kms.format == input.format && kms.modifier == LinearModifier )
		{
			hasKmsLinear = true;
			break;
		}
	}
	if ( !hasKmsLinear )
		return { OutputMode::Unsupported, InvalidModifier, Rejection::NoKmsLinearModifier };

	for ( const VulkanModifier &vulkan : input.vulkanModifiers )
	{
		if ( vulkan.format == input.format &&
			vulkan.modifier == LinearModifier &&
			vulkan.transferDstExportable &&
			hasFeatures( vulkan.features, FeatureTransferDst ) )
			return { OutputMode::Staged, LinearModifier, Rejection::Accepted };
	}

	return { OutputMode::Unsupported, InvalidModifier,
		Rejection::NoExportableLinearTransferDst };
}

FrameActions frameActions( OutputMode mode, bool directScanout, bool outputOverride )
{
	if ( directScanout )
		return { ImageRole::Unused, ImageRole::Unused, ImageRole::Client, 0, 0 };

	if ( outputOverride )
		return { ImageRole::ExplicitOutput, ImageRole::ExplicitOutput, ImageRole::Unused, 0, 0 };

	switch ( mode )
	{
		case OutputMode::Combined:
			return { ImageRole::CombinedOutput, ImageRole::CombinedOutput,
				ImageRole::CombinedOutput, 1, 0 };
		case OutputMode::Staged:
			return { ImageRole::OptimalComposition, ImageRole::OptimalComposition,
				ImageRole::LinearScanout, 1, 1 };
		case OutputMode::Unsupported:
			return {};
	}

	return {};
}

uint32_t advanceRing( uint32_t current, bool submissionSucceeded, bool increment )
{
	return submissionSucceeded && increment ? ( current + 1 ) % 3 : current;
}

uint32_t lastSubmittedRing( uint32_t current, bool deferred )
{
	return ( current + ( deferred ? 1 : 2 ) ) % 3;
}

CopyTransitions stagedCopyTransitions( bool reusedScanoutImage )
{
	return {
		.sourceShaderWriteToTransferRead = true,
		.destinationOldContentsDiscarded = true,
		.destinationBefore = reusedScanoutImage ? QueueOwner::Foreign : QueueOwner::Undefined,
		.destinationDuring = QueueOwner::Vulkan,
		.destinationAfter = QueueOwner::Foreign,
		.transferWriteReleasedToForeign = true,
	};
}

} // namespace gamescope::output_staging
