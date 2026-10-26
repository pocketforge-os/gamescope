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

void test_modifier_present_plans_remain_unchanged()
{
	const std::array kms = {
		KmsModifier{ kFormat, LinearModifier },
		KmsModifier{ kFormat, kTiledModifier },
	};
	const std::array combined = {
		VulkanModifier{ kFormat, kTiledModifier, kCompositionFeatures, true, false },
	};
	OutputPlan plan = chooseOutputPlan( { kFormat, kCompositionFeatures, kms, combined } );
	CHECK( plan.mode == OutputMode::Combined );
	CHECK( plan.modifier == kTiledModifier );
	CHECK( plan.rejection == Rejection::Accepted );

	const std::array staged = {
		VulkanModifier{ kFormat, LinearModifier, FeatureTransferDst, false, true },
	};
	plan = chooseOutputPlan( { kFormat, kCompositionFeatures, kms, staged } );
	CHECK( plan.mode == OutputMode::Staged );
	CHECK( plan.modifier == LinearModifier );
	CHECK( plan.rejection == Rejection::Accepted );

	const std::array invalidOnly = { KmsModifier{ kFormat, InvalidModifier } };
	plan = chooseOutputPlan( { kFormat, kCompositionFeatures, invalidOnly, staged } );
	CHECK( plan.mode == OutputMode::Unsupported );
	CHECK( plan.modifier == InvalidModifier );
	CHECK( plan.rejection == Rejection::NoKmsLinearModifier );
}

void test_no_modifier_xr24_accepts_sufficient_optimal_features()
{
	const std::array kms = { KmsModifier{ kFormat, InvalidModifier } };
	const OutputPlan plan = chooseNoModifierOutputPlan( {
		kFormat,
		kCompositionFeatures,
		kms,
		{},
	} );
	CHECK( plan.mode == OutputMode::Combined );
	CHECK( plan.modifier == InvalidModifier );
	CHECK( plan.rejection == Rejection::Accepted );
}

void test_no_modifier_rejects_explicit_only_kms_modifiers()
{
	const std::array kms = { KmsModifier{ kFormat, kTiledModifier } };
	const OutputPlan plan = chooseNoModifierOutputPlan( {
		kFormat,
		kCompositionFeatures,
		kms,
		{},
	} );
	CHECK( plan.mode == OutputMode::Unsupported );
}

void test_no_modifier_rejects_linear_only_kms_modifiers()
{
	const std::array kms = { KmsModifier{ kFormat, LinearModifier } };
	const OutputPlan plan = chooseNoModifierOutputPlan( {
		kFormat,
		kCompositionFeatures,
		kms,
		{},
	} );
	CHECK( plan.mode == OutputMode::Unsupported );
}

void test_no_modifier_accepts_invalid_modifier_alone_or_mixed()
{
	const std::array invalidOnly = { KmsModifier{ kFormat, InvalidModifier } };
	OutputPlan plan = chooseNoModifierOutputPlan( {
		kFormat,
		kCompositionFeatures,
		invalidOnly,
		{},
	} );
	CHECK( plan.mode == OutputMode::Combined );
	CHECK( plan.modifier == InvalidModifier );
	CHECK( plan.rejection == Rejection::Accepted );

	const std::array mixed = {
		KmsModifier{ kFormat, LinearModifier },
		KmsModifier{ kFormat, kTiledModifier },
		KmsModifier{ kFormat, InvalidModifier },
	};
	plan = chooseNoModifierOutputPlan( {
		kFormat,
		kCompositionFeatures,
		mixed,
		{},
	} );
	CHECK( plan.mode == OutputMode::Combined );
	CHECK( plan.modifier == InvalidModifier );
	CHECK( plan.rejection == Rejection::Accepted );
}

void test_no_modifier_rejects_invalid_modifier_for_different_format()
{
	const std::array kms = {
		KmsModifier{ kFormat, kTiledModifier },
		KmsModifier{ kOtherFormat, InvalidModifier },
	};
	const OutputPlan plan = chooseNoModifierOutputPlan( {
		kFormat,
		kCompositionFeatures,
		kms,
		{},
	} );
	CHECK( plan.mode == OutputMode::Unsupported );
}

void test_no_modifier_xr24_rejects_each_missing_optimal_feature()
{
	const std::array kms = { KmsModifier{ kFormat, InvalidModifier } };
	for ( const FeatureFlags missing : {
			FeatureSampled,
			FeatureStorage,
			FeatureTransferSrc,
		} )
	{
		const OutputPlan plan = chooseNoModifierOutputPlan( {
			kFormat,
			kCompositionFeatures & ~missing,
			kms,
			{},
		} );
		CHECK( plan.mode == OutputMode::Unsupported );
		CHECK( plan.modifier == InvalidModifier );
		CHECK( plan.rejection == Rejection::NoOptimalComposition );
	}
}

void test_no_modifier_undefined_or_unknown_format_is_unsupported()
{
	const std::array kms = { KmsModifier{ kFormat, InvalidModifier } };
	OutputPlan plan = chooseNoModifierOutputPlan( {
		kFormat,
		kCompositionFeatures,
		kms,
		{},
	}, false );
	CHECK( plan.mode == OutputMode::Unsupported );
	CHECK( plan.modifier == InvalidModifier );
	CHECK( plan.rejection == Rejection::NoKmsFormat );

	constexpr uint32_t kUnknownFormat = 0x12345678;
	const std::array unknownKms = { KmsModifier{ kUnknownFormat, InvalidModifier } };
	plan = chooseNoModifierOutputPlan( {
		kUnknownFormat,
		kCompositionFeatures,
		unknownKms,
		{},
	}, false );
	CHECK( plan.mode == OutputMode::Unsupported );
	CHECK( plan.modifier == InvalidModifier );
	CHECK( plan.rejection == Rejection::NoKmsFormat );
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

void test_output_allocation_respects_scanout_capability()
{
	const OutputAllocationPolicy scanout = outputAllocationPolicy( true );
	CHECK( scanout.flippable );

	const OutputAllocationPolicy headless = outputAllocationPolicy( false );
	CHECK( !headless.flippable );
}

} // namespace

int main()
{
	test_combined_is_preferred();
	test_staged_requires_exact_linear_intersection();
	test_staged_rejects_missing_capabilities();
	test_modifier_present_plans_remain_unchanged();
	test_no_modifier_xr24_accepts_sufficient_optimal_features();
	test_no_modifier_rejects_explicit_only_kms_modifiers();
	test_no_modifier_rejects_linear_only_kms_modifiers();
	test_no_modifier_accepts_invalid_modifier_alone_or_mixed();
	test_no_modifier_rejects_invalid_modifier_for_different_format();
	test_no_modifier_xr24_rejects_each_missing_optimal_feature();
	test_no_modifier_undefined_or_unknown_format_is_unsupported();
	test_frame_actions_preserve_bypass_screenshot_and_pipewire_source();
	test_ring_reuse_and_failed_submission();
	test_staged_barrier_and_ownership_contract();
	test_output_allocation_respects_scanout_capability();
	return g_failures == 0 ? 0 : 1;
}
