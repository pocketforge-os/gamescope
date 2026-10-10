#include "output_staging.hpp"

#include <array>
#include <cstdio>

using namespace gamescope::output_staging;

namespace
{

int g_failures = 0;

#define CHECK( expression ) check( expression, #expression, __LINE__ )

void check( bool value, const char *expression, int line )
{
	if ( value )
		return;

	std::fprintf( stderr, "test_output_staging.cpp:%d: CHECK(%s) failed\n", line, expression );
	g_failures++;
}

constexpr uint32_t kFormat = 0x34325258; // DRM_FORMAT_XRGB8888
constexpr uint32_t kOtherFormat = 0x34325241; // DRM_FORMAT_ARGB8888
constexpr uint64_t kTiledModifier = 0x100000000000001;

constexpr FeatureFlags kCompositionFeatures =
	FeatureSampled | FeatureStorage | FeatureTransferSrc;

void test_combined_is_preferred()
{
	const std::array kms = {
		KmsModifier{ kFormat, LinearModifier },
		KmsModifier{ kFormat, kTiledModifier },
	};
	const std::array vulkan = {
		VulkanModifier{ kFormat, LinearModifier, FeatureTransferDst, false, true },
		VulkanModifier{ kFormat, kTiledModifier, kCompositionFeatures, true, false },
	};

	const OutputPlan plan = chooseOutputPlan( { kFormat, kCompositionFeatures, kms, vulkan } );
	CHECK( plan.mode == OutputMode::Combined );
	CHECK( plan.modifier == kTiledModifier );
	CHECK( plan.rejection == Rejection::Accepted );
}

void test_linear_scanout_never_uses_combined_compute()
{
	const std::array kms = { KmsModifier{ kFormat, LinearModifier } };
	const std::array vulkan = {
		VulkanModifier{
			kFormat,
			LinearModifier,
			kCompositionFeatures | FeatureTransferDst,
			true,
			true,
		},
	};

	OutputPlan plan = chooseOutputPlan( { kFormat, kCompositionFeatures, kms, vulkan } );
	CHECK( plan.mode == OutputMode::Staged );
	CHECK( plan.modifier == LinearModifier );

	const std::array noTransferDestination = {
		VulkanModifier{
			kFormat,
			LinearModifier,
			kCompositionFeatures,
			true,
			false,
		},
	};
	plan = chooseOutputPlan( { kFormat, kCompositionFeatures, kms, noTransferDestination } );
	CHECK( plan.mode == OutputMode::Unsupported );
	CHECK( plan.rejection == Rejection::NoExportableLinearTransferDst );
}

void test_staged_requires_exact_linear_intersection()
{
	const std::array kms = { KmsModifier{ kFormat, LinearModifier } };
	const std::array vulkan = {
		VulkanModifier{ kFormat, LinearModifier, FeatureTransferDst, false, true },
	};

	OutputPlan plan = chooseOutputPlan( { kFormat, kCompositionFeatures, kms, vulkan } );
	CHECK( plan.mode == OutputMode::Staged );
	CHECK( plan.modifier == LinearModifier );

	const std::array wrongKmsFormat = { KmsModifier{ kOtherFormat, LinearModifier } };
	plan = chooseOutputPlan( { kFormat, kCompositionFeatures, wrongKmsFormat, vulkan } );
	CHECK( plan.mode == OutputMode::Unsupported );
	CHECK( plan.rejection == Rejection::NoKmsFormat );

	const std::array wrongVkFormat = {
		VulkanModifier{ kOtherFormat, LinearModifier, FeatureTransferDst, false, true },
	};
	plan = chooseOutputPlan( { kFormat, kCompositionFeatures, kms, wrongVkFormat } );
	CHECK( plan.mode == OutputMode::Unsupported );
	CHECK( plan.rejection == Rejection::NoExportableLinearTransferDst );

	const std::array invalidOnly = { KmsModifier{ kFormat, InvalidModifier } };
	plan = chooseOutputPlan( { kFormat, kCompositionFeatures, invalidOnly, vulkan } );
	CHECK( plan.mode == OutputMode::Unsupported );
	CHECK( plan.rejection == Rejection::NoKmsLinearModifier );
}

void test_staged_rejects_missing_capabilities()
{
	const std::array kms = { KmsModifier{ kFormat, LinearModifier } };
	const std::array noTransferDst = {
		VulkanModifier{ kFormat, LinearModifier, FeatureTransferSrc, false, true },
	};
	OutputPlan plan = chooseOutputPlan( { kFormat, kCompositionFeatures, kms, noTransferDst } );
	CHECK( plan.mode == OutputMode::Unsupported );
	CHECK( plan.rejection == Rejection::NoExportableLinearTransferDst );

	const std::array notExportable = {
		VulkanModifier{ kFormat, LinearModifier, FeatureTransferDst, false, false },
	};
	plan = chooseOutputPlan( { kFormat, kCompositionFeatures, kms, notExportable } );
	CHECK( plan.mode == OutputMode::Unsupported );

	plan = chooseOutputPlan( { kFormat, FeatureStorage | FeatureTransferSrc, kms, notExportable } );
	CHECK( plan.mode == OutputMode::Unsupported );
	CHECK( plan.rejection == Rejection::NoOptimalComposition );
}

void test_frame_actions_preserve_bypass_screenshot_and_pipewire_source()
{
	FrameActions actions = frameActions( OutputMode::Staged, true, false );
	CHECK( actions.compositeTarget == ImageRole::Unused );
	CHECK( actions.presentImage == ImageRole::Client );
	CHECK( actions.compositionDispatches == 0 );
	CHECK( actions.stagingCopies == 0 );

	actions = frameActions( OutputMode::Staged, false, false );
	// PipeWire capture reads the composition target before the staging copy.
	CHECK( actions.compositeTarget == ImageRole::OptimalComposition );
	CHECK( actions.captureSource == ImageRole::OptimalComposition );
	CHECK( actions.presentImage == ImageRole::LinearScanout );
	CHECK( actions.compositionDispatches == 1 );
	CHECK( actions.stagingCopies == 1 );

	actions = frameActions( OutputMode::Combined, false, false );
	CHECK( actions.compositeTarget == ImageRole::CombinedOutput );
	CHECK( actions.captureSource == ImageRole::CombinedOutput );
	CHECK( actions.presentImage == ImageRole::CombinedOutput );
	CHECK( actions.compositionDispatches == 1 );
	CHECK( actions.stagingCopies == 0 );

	actions = frameActions( OutputMode::Staged, false, true );
	// Screenshot/full-composition calls with pOutputOverride bypass output staging.
	CHECK( actions.compositeTarget == ImageRole::ExplicitOutput );
	CHECK( actions.presentImage == ImageRole::Unused );
	CHECK( actions.compositionDispatches == 0 );
	CHECK( actions.stagingCopies == 0 );
}

void test_ring_reuse_and_failed_submission()
{
	CHECK( advanceRing( 0, true, true ) == 1 );
	CHECK( advanceRing( 2, true, true ) == 0 );
	CHECK( advanceRing( 1, false, true ) == 1 );
	CHECK( advanceRing( 1, true, false ) == 1 );
	CHECK( lastSubmittedRing( 0, false ) == 2 );
	CHECK( lastSubmittedRing( 0, true ) == 1 );
}

void test_staged_barrier_and_ownership_contract()
{
	const CopyTransitions firstUse = stagedCopyTransitions( false );
	CHECK( firstUse.sourceShaderWriteToTransferRead );
	CHECK( firstUse.destinationOldContentsDiscarded );
	CHECK( firstUse.destinationBefore == QueueOwner::Undefined );
	CHECK( firstUse.destinationDuring == QueueOwner::Vulkan );
	CHECK( firstUse.destinationAfter == QueueOwner::Foreign );
	CHECK( firstUse.transferWriteReleasedToForeign );

	const CopyTransitions reused = stagedCopyTransitions( true );
	CHECK( reused.destinationOldContentsDiscarded );
	CHECK( reused.destinationBefore == QueueOwner::Foreign );
	CHECK( reused.destinationDuring == QueueOwner::Vulkan );
	CHECK( reused.destinationAfter == QueueOwner::Foreign );
}

} // namespace

int main()
{
	test_combined_is_preferred();
	test_linear_scanout_never_uses_combined_compute();
	test_staged_requires_exact_linear_intersection();
	test_staged_rejects_missing_capabilities();
	test_frame_actions_preserve_bypass_screenshot_and_pipewire_source();
	test_ring_reuse_and_failed_submission();
	test_staged_barrier_and_ownership_contract();
	return g_failures == 0 ? 0 : 1;
}
